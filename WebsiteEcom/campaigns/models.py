"""
Campaigns app: Campaign, CampaignStep, CampaignSession, CampaignSessionToken,
CampaignIssuedCode, CampaignStepTranslation, AbandonedCheckoutEmailStep,
AbandonedCheckoutEmailSend.

Design decisions (ADR-007, ADR-009, ADR-010, design-pattern-ideas.txt):
- Campaign is the top-level funnel definition (store-scoped).
- Campaign.owner_scope discriminates store-created vs platform (super-admin) campaigns.
  Precedence is resolved in campaigns/service.py — the single resolution function (§XV-4).
- Campaign.entry_step is the explicit FK entry point — never derived from position (§XV-2).
- CampaignStep is an ordered node in the funnel graph. Ordering uses stable FK
  references (accept_next_step / decline_next_step) — never positional indexes
  (§VII: deleting a step must not renumber others).
- CampaignSession is the explicit per-(order or cart, campaign) state machine. Every
  state transition is written; absence of a row means "not evaluated", never "declined"
  (§VIII: never infer state from absence of data).
- CampaignSession.order is nullable (ADR-010): abandoned-checkout sessions anchor on
  the cart, not an order. The anchor invariant (order OR cart OR customer_email <> '')
  is enforced by a CheckConstraint.
- CampaignSession.campaign_type is denormalized from campaign.campaign_type at session
  creation. The UniqueConstraint on (order, campaign_type) enforces ADR-007 §1 at the
  DB layer (one session per campaign-type per order). A second conditional UniqueConstraint
  on (cart, campaign_type) WHERE cart IS NOT NULL enforces the same for abandoned-checkout
  sessions.
- CampaignSessionToken stores only the SHA-256 hash of each issued token; the raw
  value is returned once at creation time and never persisted (ADR-007 §5).
  Abandoned-checkout resume tokens use django.core.signing (stateless, ADR-010 Q3).
- CampaignIssuedCode is an audit/idempotency link between a session+step and the
  real DiscountCode row (ADR-009 §3). step is nullable so abandoned-checkout coupons
  can link to AbandonedCheckoutEmailStep instead (ADR-010 Q2). Exactly one of
  (step, email_step) must be non-null — enforced by CheckConstraint.
- AbandonedCheckoutEmailStep holds the per-email wizard fields for the timed email
  sequence (ADR-010 Q2). One record per email in the sequence.
- AbandonedCheckoutEmailSend is the per-step send record and dedup key. Created BEFORE
  enqueueing the worker; status='scheduled' → 'sent'/'cancelled'/'failed' (§XV-3).
- CampaignStepTranslation holds translated customer-visible content for each step,
  produced by the AiJob CLI runner system (mirrors catalog.ProductTranslation).
- OrderCharge (one row per processor charge on an order) is defined in orders.models
  — it was implemented as part of the order data layer (TICKET-009/T010) and is
  imported here for admin registration only.
- All models inherit StoreOwnedModel (ADR-001 §4); each carries a direct store FK.
  CampaignStep.store is intentionally redundant with campaign.store — it satisfies
  the compliance test requirement without breaking the graph structure.
"""

from django.core.exceptions import ValidationError
from django.core.validators import MaxValueValidator, MinValueValidator
from django.db import models

from core.models import StoreOwnedModel


class CampaignType(models.TextChoices):
    ABANDONED_CHECKOUT = "abandoned_checkout", "Abandoned checkout"
    RELATED_PRODUCTS = "related_products", "Related products"
    BUY_X_GET_X = "buy_x_get_x", "Buy X get X"
    STOREWIDE_DISCOUNT = "storewide_discount", "Storewide discount"
    ONE_CLICK_FUNNEL = "one_click_funnel", "One-click funnel"
    ORDER_BUMP = "order_bump", "Order bump"


class StepOfferType(models.TextChoices):
    """
    Offer types available to individual funnel steps.

    Excludes ABANDONED_CHECKOUT: that type drives the campaign-level email
    sequence scheduler, not individual step offers. Individual step offers
    are rendered in response to user interaction within the funnel.
    """

    RELATED_PRODUCTS = "related_products", "Related products"
    BUY_X_GET_X = "buy_x_get_x", "Buy X get X"
    STOREWIDE_DISCOUNT = "storewide_discount", "Storewide discount"
    ONE_CLICK_FUNNEL = "one_click_funnel", "One-click funnel"
    ORDER_BUMP = "order_bump", "Order bump"


class CampaignSessionState(models.TextChoices):
    NOT_STARTED = "not_started", "Not started"
    IMPRESSION = "impression", "Impression"
    INTERACTION = "interaction", "Interaction"
    CONVERTED = "converted", "Converted"
    DISMISSED = "dismissed", "Dismissed"
    EXPIRED = "expired", "Expired"


TERMINAL_STATES = {
    CampaignSessionState.CONVERTED,
    CampaignSessionState.DISMISSED,
    CampaignSessionState.EXPIRED,
}

# Derived from TERMINAL_STATES; used by service and tasks to filter non-terminal sessions.
NON_TERMINAL_STATES = frozenset(
    s for s in CampaignSessionState.values if s not in TERMINAL_STATES
)


class TokenPurpose(models.TextChoices):
    THANKYOU_VIEW = "thankyou-view", "Thank-you view"
    UPSELL_ACT = "upsell-act", "Upsell act"


class AbandonedCheckoutEmailType(models.TextChoices):
    WARNING = "warning", "Warning"
    REMINDER = "reminder", "Reminder"
    INCENTIVE = "incentive", "Incentive"


class AbandonedCheckoutEmailSendStatus(models.TextChoices):
    SCHEDULED = "scheduled", "Scheduled"
    SENT = "sent", "Sent"
    CANCELLED = "cancelled", "Cancelled"
    FAILED = "failed", "Failed"


class Campaign(StoreOwnedModel):
    """
    A marketing campaign definition (store-scoped).

    A campaign is a named funnel type that can be activated or deactivated.
    targeting_rules_json holds eligibility and exclusion rules (e.g. specific
    product IDs, collection IDs, minimum cart total, country codes) evaluated
    at funnel entry. The full rules vocabulary is incremental (ADR-007 §9);
    Phase 1 returns all active campaigns of the type (see campaigns/service.py).

    owner_scope discriminates store-created ('store') from platform / super-admin
    ('platform') campaigns. Precedence is resolved in campaigns.service.evaluate_eligibility
    — the single resolution function (ADR-009 §2, §XV-4). Store-scoped campaigns shadow
    platform campaigns of the same campaign_type. Platform rows are visible in the store
    admin but read-only; owner_scope is forced to 'store' on all store-admin saves.

    entry_step is the explicit funnel entry point. Never derived from position (§XV-2).
    Required to activate a funnel-type campaign (ADR-009 §1); validated in clean().

    Invariants:
    - is_active controls whether the campaign is eligible for evaluation.
      Deactivating a campaign stops it from appearing to new sessions; it does
      not affect in-progress CampaignSession rows.
    - campaign_type determines which eligibility path and step offer types apply.
    - name is for internal use only; it is never shown to shoppers.
    - entry_step (when set) must belong to this campaign. Validated in clean() via
      validate_campaign_activation; the FK is nullable so SET_NULL on step deletion
      naturally deactivates the entry point without silently picking another step.
    """

    OWNER_SCOPE_STORE = "store"
    OWNER_SCOPE_PLATFORM = "platform"

    _OWNER_SCOPE_CHOICES = [
        (OWNER_SCOPE_STORE, "Store admin"),
        (OWNER_SCOPE_PLATFORM, "Platform (super-admin)"),
    ]

    name = models.CharField(max_length=255)
    campaign_type = models.CharField(
        max_length=30,
        choices=CampaignType.choices,
    )
    is_active = models.BooleanField(default=False)
    owner_scope = models.CharField(
        max_length=10,
        choices=_OWNER_SCOPE_CHOICES,
        default=OWNER_SCOPE_STORE,
        help_text=(
            "Scope of the campaign creator. 'store' = store admin owns this campaign; "
            "'platform' = super-admin created it. Precedence: store-owned shadows platform "
            "campaigns of the same type for the same store."
        ),
    )
    entry_step = models.ForeignKey(
        "CampaignStep",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="+",
        help_text=(
            "First step shown at funnel entry. Required to activate a funnel campaign. "
            "Must belong to this campaign. Never derived from position (§XV-2 / ADR-009 §1)."
        ),
    )
    targeting_rules_json = models.JSONField(
        default=dict,
        help_text=(
            "Eligibility and exclusion rules evaluated at funnel entry. "
            "Keys vary by rule type; Phase 1 ignores this field and returns "
            "all active campaigns of the type (see campaigns/service.py)."
        ),
    )
    # Capture-window duration for funnel campaigns (ADR-011 Q5).
    # Set at checkout: Order.capture_window_expires_at = authorized_at + minutes.
    # Meaningful only for funnel-type campaigns; validated by validate_campaign_activation
    # when is_active=True (1–60 minutes allowed).
    # Not per-step: the window is a funnel-level fact — one window per order (ADR-007 §2).
    capture_window_minutes = models.PositiveSmallIntegerField(
        default=10,
        validators=[MinValueValidator(1), MaxValueValidator(60)],
        help_text=(
            "Minutes from authorization to capture-window expiry (ADR-011 Q5). "
            "Applied at checkout: Order.capture_window_expires_at = authorized_at + minutes. "
            "Meaningful only for funnel-type campaigns. Range: 1–60 minutes."
        ),
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def clean(self):
        """
        Persistence-boundary validation (§XV-5 / ADR-009 §1).

        Delegates to validate_campaign_activation so that all save paths (admin,
        API, management commands) go through the same validation function.
        """
        from campaigns.validators import validate_campaign_activation
        validate_campaign_activation(self)

    def __str__(self):
        return f"{self.name} ({self.get_campaign_type_display()})"

    class Meta:
        verbose_name = "campaign"
        verbose_name_plural = "campaigns"
        indexes = [
            models.Index(fields=["store", "campaign_type", "is_active"]),
            models.Index(fields=["store", "created_at"]),
        ]


class CampaignStep(StoreOwnedModel):
    """
    One step (node) in a campaign funnel graph.

    Steps are ordered by position (ascending), but branching is expressed via
    stable FK references to sibling steps (accept_next_step / decline_next_step)
    — never by positional arithmetic. Deleting a step does not renumber others
    or invalidate any stored foreign key (§VII: stable IDs, never positions).

    offer_config_json holds archetype-specific configuration. Examples:
      RELATED_PRODUCTS:  {"product_ids": [1, 2, 3]}
      BUY_X_GET_X:       {"buy_quantity": 2, "get_quantity": 1, "product_id": 7}
      STOREWIDE_DISCOUNT:{"discount_pct": 10, "expires_in_days": 30}
      ONE_CLICK_FUNNEL:  {"product_id": 4}
      ORDER_BUMP:        {"product_id": 9}

    Invariants:
    - position is a display-order hint only; it can have gaps and is never
      renumbered. Do not use position as a unique key.
    - store is deliberately redundant with campaign.store. It satisfies the
      StoreOwnedModel compliance requirement so queryset scoping (.for_store())
      works without following the campaign FK.
    - accept_next_step and decline_next_step may be null for terminal steps.
    """

    campaign = models.ForeignKey(
        Campaign,
        on_delete=models.CASCADE,
        related_name="steps",
    )
    position = models.PositiveIntegerField(
        help_text=(
            "Display order within the campaign. Gaps are allowed; "
            "this field is never renumbered (§VII)."
        ),
    )
    offer_type = models.CharField(
        max_length=30,
        choices=StepOfferType.choices,
    )
    offer_config_json = models.JSONField(
        default=dict,
        help_text="Archetype-specific offer configuration (product IDs, discount %, etc.).",
    )
    accept_next_step = models.ForeignKey(
        "self",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="+",
        help_text="Step shown after the buyer accepts this offer. Null = terminal (converted).",
    )
    decline_next_step = models.ForeignKey(
        "self",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="+",
        help_text="Step shown after the buyer declines this offer. Null = terminal (dismissed).",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    def clean(self):
        """
        When saving a step that belongs to an active campaign, re-validate the
        full campaign graph (ADR-009 §1 / §XV-5).

        Prevents invalid configs, foreign-FK branches, and cycles from being
        introduced into live campaigns via the standalone CampaignStepAdmin.
        Called during form validation by ModelForm._post_clean() → full_clean().

        FR-S7 (T039): also performs in-memory cycle detection for the proposed
        accept_next_step / decline_next_step values BEFORE they are saved to DB.
        This closes the documented gap: the old implementation validated the
        pre-edit graph (DB state), so a cycle introduced by the edit was not
        caught until the campaign was activated. The in-memory check catches it
        immediately on save.

        Algorithm: load all campaign steps (one query); override the current
        step's branch FKs with the in-memory proposed values; DFS from each
        proposed branch — if DFS reaches self.pk, a cycle would be created.
        """
        if not self.campaign_id:
            return
        from campaigns.validators import validate_campaign_activation
        campaign = self.campaign
        if campaign.is_active:
            validate_campaign_activation(campaign)

        # FR-S7: In-memory cycle detection for proposed branch changes.
        # Runs for both active and inactive campaigns so cycles are caught on
        # save, not only at activation.
        if self.pk:
            # Load all steps in this campaign (single query for efficiency).
            all_step_branches = {
                s.pk: (s.accept_next_step_id, s.decline_next_step_id)
                for s in CampaignStep.objects.cross_store_unsafe()
                    .filter(campaign_id=self.campaign_id)
                    .only('pk', 'accept_next_step_id', 'decline_next_step_id')
            }
            # Override with in-memory proposed values (not yet committed to DB).
            all_step_branches[self.pk] = (
                self.accept_next_step_id,
                self.decline_next_step_id,
            )

            for branch_id in (self.accept_next_step_id, self.decline_next_step_id):
                if branch_id is None:
                    continue
                visited: set = set()
                frontier = [branch_id]
                while frontier:
                    curr_id = frontier.pop()
                    if curr_id == self.pk:
                        raise ValidationError(
                            f"The proposed change would create a cycle in the "
                            f"funnel graph involving step #{self.pk}."
                        )
                    if curr_id in visited:
                        continue
                    visited.add(curr_id)
                    next_accept, next_decline = all_step_branches.get(curr_id, (None, None))
                    for next_id in (next_accept, next_decline):
                        if next_id is not None and next_id not in visited:
                            frontier.append(next_id)

    def __str__(self):
        return f"Step {self.position} ({self.get_offer_type_display()}) — {self.campaign}"

    class Meta:
        verbose_name = "campaign step"
        verbose_name_plural = "campaign steps"
        ordering = ["position"]
        indexes = [
            models.Index(fields=["campaign", "position"]),
        ]


class CampaignSession(StoreOwnedModel):
    """
    Explicit state machine tracking one buyer's journey through a Campaign.

    For funnel-type campaigns: one row per (order, campaign) pair. For
    abandoned-checkout campaigns: one row per (cart, campaign), with order=NULL.

    The anchor invariant — at least one of (order, cart, customer_email) must be
    set — is enforced by a CheckConstraint. This survives cart deletion: when
    cart is SET_NULL, customer_email still provides the anchor (and cross-session
    suppression key).

    campaign_type is denormalized from campaign.campaign_type at session creation.
    UniqueConstraint (order, campaign_type) for funnel sessions; conditional
    UniqueConstraint (cart, campaign_type) WHERE cart IS NOT NULL for abandonment
    sessions — both enforce one session per campaign-type anchor (ADR-007 §1).

    Invariants:
    - State only advances; terminal states (CONVERTED, DISMISSED, EXPIRED) are
      never overwritten. TERMINAL_STATES is enforced by the service layer.
    - recovery_revenue records the Decimal revenue attributed to this session.
      Set on CONVERTED transition; non-zero only for attributed recoveries
      (INTERACTION reached, or order redeemed a coupon linked to this session).
    - abandoned_at is the frozen reference time for all send-delay calculations.
      Set at session creation from cart.updated_at (ADR-010 Q4).
    - customer_email is denormalized at session creation; survives cart deletion
      and enables cross-session suppression matching (AC-182, ADR-010 Q5).
    - started_at is set when a shopper clicks the resume link (INTERACTION).
    - converted_at is set on the CONVERTED transition.
    """

    order = models.ForeignKey(
        "orders.Order",
        null=True,
        blank=True,
        on_delete=models.CASCADE,
        related_name="campaign_sessions",
        help_text=(
            "Order anchor for funnel-type sessions. Null for abandoned-checkout sessions "
            "which use the cart FK instead (ADR-010 Q1)."
        ),
    )
    cart = models.ForeignKey(
        "cart.Cart",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="campaign_sessions",
        help_text=(
            "Cart anchor for abandoned-checkout sessions. SET_NULL on cart deletion so "
            "the campaign audit trail survives cart purge (ADR-010 Q1)."
        ),
    )
    customer_email = models.EmailField(
        blank=True,
        default="",
        help_text=(
            "Denormalized from cart.customer_email at session creation. "
            "Survives cart deletion; used for cross-session suppression (AC-182)."
        ),
    )
    abandoned_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text=(
            "Frozen reference time for all send-delay calculations. "
            "Set to cart.updated_at at detection time (ADR-010 Q4)."
        ),
    )
    campaign = models.ForeignKey(
        Campaign,
        on_delete=models.PROTECT,
        related_name="sessions",
    )
    campaign_type = models.CharField(
        max_length=32,
        blank=True,
        help_text=(
            "Denormalized from campaign.campaign_type at session creation. "
            "Enforces the (order, campaign_type) uniqueness invariant at DB level."
        ),
    )
    state = models.CharField(
        max_length=20,
        choices=CampaignSessionState.choices,
        default=CampaignSessionState.NOT_STARTED,
    )
    current_step = models.ForeignKey(
        CampaignStep,
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="active_sessions",
    )
    recovery_revenue = models.DecimalField(
        max_digits=12,
        decimal_places=2,
        default=0,
        help_text=(
            "Revenue attributed to this session's conversion. "
            "Non-zero only when INTERACTION was reached or an attributed coupon was redeemed."
        ),
    )
    # Rate-limit counter for accept actions (ADR-011 / upsell accept flow).
    # Incremented atomically with F('accept_attempts') + 1 inside the locked
    # accept transaction. DB-backed: survives process restarts and is visible
    # in admin. When >= 5, UpsellRateLimitError is raised (HTTP 403) with no
    # processor call. Never decremented.
    accept_attempts = models.PositiveSmallIntegerField(
        default=0,
        help_text=(
            "Count of upsell accept attempts for this session. "
            "Incremented on every accept call; capped at 5 (UpsellRateLimitError on 6th+). "
            "DB-backed for process-restart safety (ADR-011)."
        ),
    )
    started_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Set when a shopper clicks the resume link (IMPRESSION → INTERACTION).",
    )
    converted_at = models.DateTimeField(null=True, blank=True)
    data_json = models.JSONField(
        default=list,
        blank=True,
        help_text=(
            "Audit log of notable events for this session "
            "(e.g. upsell_charge_failed entries). Each entry is a dict with "
            "'event' and 'ts' keys appended by the service layer."
        ),
    )
    expires_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text=(
            "Denormalized copy of Order.capture_window_expires_at. "
            "Set at session creation; used to evaluate eligibility without a JOIN. "
            "Not used for abandoned-checkout sessions (those use abandoned_at + 30d)."
        ),
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def __str__(self):
        return (
            f"Session {self.pk} — {self.campaign} / {self.get_state_display()}"
        )

    class Meta:
        verbose_name = "campaign session"
        verbose_name_plural = "campaign sessions"
        unique_together = [("order", "campaign")]
        constraints = [
            models.UniqueConstraint(
                fields=["order", "campaign_type"],
                name="uniq_session_per_order_type",
            ),
            # Abandoned-checkout: one session per (cart, campaign_type). Conditional
            # so that NULL carts (after SET_NULL purge) don't conflict.
            models.UniqueConstraint(
                fields=["cart", "campaign_type"],
                condition=models.Q(cart__isnull=False),
                name="uniq_session_per_cart_type",
            ),
            # Every session must be anchored: either an order, a cart, or a known email.
            # This survives cart purge: after cart → NULL, customer_email provides the anchor.
            models.CheckConstraint(
                condition=(
                    models.Q(order__isnull=False)
                    | models.Q(cart__isnull=False)
                    | ~models.Q(customer_email="")
                ),
                name="session_has_anchor",
            ),
        ]
        indexes = [
            models.Index(fields=["store", "state"]),
            models.Index(fields=["store", "created_at"]),
            models.Index(fields=["campaign", "state"]),
            # Beat scan performance: find non-terminal abandoned-checkout sessions per store.
            # Explicit name matches migration 0003 (idx_session_store_type_state).
            models.Index(fields=["store", "campaign_type", "state"], name="idx_session_store_type_state"),
        ]


class CampaignSessionToken(StoreOwnedModel):
    """
    Single-use or long-lived token granting access to upsell and thank-you flows.

    The raw token value is generated once (in the service layer) and returned to
    the caller; it is NEVER stored here. Only the SHA-256 hash is persisted so
    a database compromise does not expose live tokens (ADR-007 §5).

    Two purposes:
    - thankyou-view: long-lived (~72 h). Sent in the confirmation email.
      Grants GET access to the order thank-you page.
    - upsell-act: short-lived (lifetime = capture window, ~10 min).
      Single-use: used_at is set on first consumption; subsequent uses are
      rejected by the accept/decline endpoint.

    Note: abandoned-checkout resume tokens use django.core.signing (stateless,
    ADR-010 Q3) and are NOT stored here.

    Invariants:
    - token_hash is unique across all stores (global SHA-256 uniqueness).
    - used_at is null until the token is consumed; non-null means consumed.
    - expires_at is always set; tokens past their expiry are rejected regardless
      of used_at.
    """

    campaign_session = models.ForeignKey(
        CampaignSession,
        on_delete=models.CASCADE,
        related_name="tokens",
    )
    purpose = models.CharField(
        max_length=20,
        choices=TokenPurpose.choices,
    )
    token_hash = models.CharField(
        max_length=64,
        unique=True,
        help_text="SHA-256 hex digest of the raw token. Raw value is never stored.",
    )
    expires_at = models.DateTimeField()
    used_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Set when the token is consumed. Null = not yet used.",
    )
    # M3 guard: snapshot the charge amount presented to the customer at token-issue time.
    # accept_upsell checks this against the current step amount before Phase A begins —
    # if an admin edit changed the price between impression and accept, the customer is
    # shown an error and must refresh to see the updated offer (no silent overcharge).
    # Null = storewide_discount / no charge (M3 guard skipped for these tokens).
    offered_amount_cents = models.PositiveIntegerField(
        null=True,
        blank=True,
        default=None,
        help_text=(
            "Amount shown to the customer at token-issue time (M3 guard). "
            "Checked at accept to prevent post-issue admin price edits from "
            "charging an amount the customer never saw. "
            "Null = storewide_discount step or no charge (guard skipped)."
        ),
    )

    def __str__(self):
        return f"Token({self.get_purpose_display()}) for session {self.campaign_session_id}"

    class Meta:
        verbose_name = "campaign session token"
        verbose_name_plural = "campaign session tokens"
        indexes = [
            models.Index(fields=["store", "expires_at"]),
        ]


class CampaignIssuedCode(StoreOwnedModel):
    """
    Attribution + idempotency link: which session/step issued which DiscountCode.

    The code string itself lives only in DiscountCode (ADR-009 §3). This table
    provides campaign-side attribution and retry idempotency without duplicating
    the redemption path.

    Exactly one of (step, email_step) must be set — enforced by CheckConstraint:
    - step (funnel CampaignStep): used for post-purchase upsell coupons (T028).
    - email_step (AbandonedCheckoutEmailStep): used for abandonment-recovery coupons.

    Idempotency targets:
    - Funnel: unique (campaign_session, step) — unchanged from T027.
    - Abandonment: unique (campaign_session, email_step) WHERE email_step IS NOT NULL.

    Invariants:
    - This table is audit-only. Code issuance happens in the send task (T021) or
      accept-transaction hook (T028).
    - discount_code FK is PROTECT: deleting a DiscountCode rejects delete of the code.
    - step / email_step FKs are PROTECT: a step with issued codes cannot be deleted.
    - No delete permission on any admin surface (audit table).
    """

    campaign_session = models.ForeignKey(
        CampaignSession,
        on_delete=models.CASCADE,
        related_name="issued_codes",
    )
    step = models.ForeignKey(
        CampaignStep,
        null=True,
        blank=True,
        on_delete=models.PROTECT,
        related_name="issued_codes",
        help_text="Funnel step that issued this code. Null for abandoned-checkout coupons.",
    )
    email_step = models.ForeignKey(
        "AbandonedCheckoutEmailStep",
        null=True,
        blank=True,
        on_delete=models.PROTECT,
        related_name="issued_codes",
        help_text="Abandoned-checkout email step that issued this code. Null for funnel coupons.",
    )
    discount_code = models.ForeignKey(
        "discounts.DiscountCode",
        on_delete=models.PROTECT,
        related_name="+",
    )
    issued_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        step_ref = f"step={self.step_id}" if self.step_id else f"email_step={self.email_step_id}"
        return (
            f"IssuedCode session={self.campaign_session_id} "
            f"{step_ref} code={self.discount_code_id}"
        )

    class Meta:
        verbose_name = "campaign issued code"
        verbose_name_plural = "campaign issued codes"
        constraints = [
            models.UniqueConstraint(
                fields=["campaign_session", "step"],
                name="uniq_issued_code_per_session_step",
            ),
            # Exactly one of (step, email_step) must be non-null (ADR-010 Q2).
            models.CheckConstraint(
                condition=(
                    (
                        models.Q(step__isnull=False)
                        & models.Q(email_step__isnull=True)
                    )
                    | (
                        models.Q(step__isnull=True)
                        & models.Q(email_step__isnull=False)
                    )
                ),
                name="issued_code_exactly_one_step",
            ),
            # Abandonment coupon idempotency: one code per (session, email_step).
            models.UniqueConstraint(
                fields=["campaign_session", "email_step"],
                condition=models.Q(email_step__isnull=False),
                name="uniq_issued_code_per_session_email_step",
            ),
        ]


class AbandonedCheckoutEmailStep(StoreOwnedModel):
    """
    One email in an abandoned-checkout campaign's timed sequence (ADR-010 Q2).

    A campaign may have N steps. Each step specifies when to send (send_delay_hours
    from abandoned_at), what to send (subject/body_template as Django template strings),
    and optionally a single-use coupon to include (coupon_config_json).

    Invariants:
    - position is a display-order hint only; gaps are allowed and it is never
      renumbered (§VII: stable IDs, never positions). Sequence order is determined
      by send_delay_hours; identity is the PK.
    - send_delay_hours is measured from session.abandoned_at (= cart.updated_at at
      detection time), consistently for step 1 and steps 2..N (ADR-010 ASSUMPTION).
      The wizard may warn when a later step's delay is <= an earlier step's.
    - subject and body_template are Django template strings (rendered by emails app).
      Customer-visible translations are NOT stored here; they live in a future
      AbandonedCheckoutEmailStepTranslation table (T024 scope).
    - coupon_config_json: {} means no coupon for this step. Non-empty schema:
        {'value_type': 'percent'|'fixed', 'value': number > 0,
         'expires_in_days': int >= 1 (default 30)}
      Validated by campaigns/validators.py:validate_email_step().
    - campaign FK is limited to abandoned_checkout type campaigns (limit_choices_to).
    """

    campaign = models.ForeignKey(
        Campaign,
        on_delete=models.CASCADE,
        related_name="email_steps",
        limit_choices_to={"campaign_type": CampaignType.ABANDONED_CHECKOUT},
        help_text="Parent campaign — must be of type 'abandoned_checkout'.",
    )
    position = models.PositiveIntegerField(
        help_text=(
            "Display order within the campaign. Gaps allowed; never renumbered (§VII). "
            "Sequence order is defined by send_delay_hours, not position."
        ),
    )
    send_delay_hours = models.PositiveIntegerField(
        default=1,
        validators=[MinValueValidator(1)],
        help_text=(
            "Hours from session.abandoned_at before this email is sent. "
            "Must be >= 1. Days-unit form input is stored as hours × 24. "
            "Measured from abandoned_at for all steps (ADR-010 ASSUMPTION). "
            "Delays are checked every 5 minutes — actual send time may be up to "
            "5 minutes later than configured."
        ),
    )
    email_type = models.CharField(
        max_length=20,
        choices=AbandonedCheckoutEmailType.choices,
        help_text="Semantic category of this email step.",
    )
    email_style = models.CharField(
        max_length=30,
        help_text="Wizard style key for the rendering wrapper (template wrapper choice).",
    )
    subject = models.CharField(
        max_length=255,
        help_text="Django template string for the email subject (may include {{ store.name }}, etc.).",
    )
    body_template = models.TextField(
        help_text=(
            "Django template string for the email body HTML. "
            "Context: cart_items, resume_url, coupon_code, store."
        ),
    )
    coupon_config_json = models.JSONField(
        default=dict,
        help_text=(
            "Coupon to issue with this email. {} = no coupon. "
            "Schema: {value_type: 'percent'|'fixed', value: number > 0, "
            "expires_in_days: int >= 1 (default 30)}."
        ),
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def clean(self):
        """Persistence-boundary validation (§XV-5)."""
        from campaigns.validators import validate_email_step
        validate_email_step(self)

    def __str__(self):
        return (
            f"EmailStep pos={self.position} delay={self.send_delay_hours}h "
            f"[{self.email_type}] — {self.campaign}"
        )

    class Meta:
        verbose_name = "abandoned checkout email step"
        verbose_name_plural = "abandoned checkout email steps"
        ordering = ["send_delay_hours", "position"]
        indexes = [
            # Explicit name matches migration 0003 (idx_email_step_campaign_delay).
            models.Index(fields=["campaign", "send_delay_hours"], name="idx_email_step_campaign_delay"),
        ]


class AbandonedCheckoutEmailSend(StoreOwnedModel):
    """
    Per-step send record for an abandoned-checkout session (ADR-010 Q4).

    One row is created (status=SCHEDULED) BEFORE enqueueing the worker task.
    This is the dedup key: get_or_create prevents duplicate enqueues across
    repeated beat runs. The worker claims via select_for_update + status check;
    any subsequent worker or retry that finds status != SCHEDULED exits.

    UniqueConstraint(session, email_step) is THE dedup key (AC-071).

    Invariants:
    - A row is created before enqueueing (§XV-3: never infer state from absence).
    - status: SCHEDULED → SENT | CANCELLED | FAILED (terminal; never rolled back).
    - scheduled_at is the due time (session.abandoned_at + send_delay_hours).
    - sent_at is set when the email is successfully delivered to the email backend.
    - cancelled_at is set when suppression or a race guard cancels the send.
    - No add/change/delete permission on admin surface (audit table).
    """

    session = models.ForeignKey(
        CampaignSession,
        on_delete=models.CASCADE,
        related_name="email_sends",
    )
    email_step = models.ForeignKey(
        AbandonedCheckoutEmailStep,
        on_delete=models.PROTECT,
        related_name="sends",
    )
    status = models.CharField(
        max_length=20,
        choices=AbandonedCheckoutEmailSendStatus.choices,
        default=AbandonedCheckoutEmailSendStatus.SCHEDULED,
        help_text="Lifecycle state of this send attempt (§XV-3 — always written explicitly).",
    )
    scheduled_at = models.DateTimeField(
        help_text="Due time: session.abandoned_at + step.send_delay_hours.",
    )
    sent_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Set when the email was accepted by the email backend.",
    )
    cancelled_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Set when suppression or a race guard cancelled this send.",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return (
            f"EmailSend(session={self.session_id} step={self.email_step_id} "
            f"status={self.status})"
        )

    class Meta:
        verbose_name = "abandoned checkout email send"
        verbose_name_plural = "abandoned checkout email sends"
        constraints = [
            models.UniqueConstraint(
                fields=["session", "email_step"],
                name="uniq_send_per_session_step",
            ),
        ]
        indexes = [
            # Explicit name matches migration 0003 (idx_email_send_session_status).
            models.Index(fields=["session", "status"], name="idx_email_send_session_status"),
        ]


class CampaignStepTranslation(StoreOwnedModel):
    """
    Translated customer-visible content for a CampaignStep (ADR-009, TICKET-027).

    Mirrors catalog.ProductTranslation: translation records are produced by the
    AiJob CLI runner system (NOT direct API calls). The ai_job FK links the job
    that produced this translation. Status lifecycle: draft → published.

    Customer-visible fields: title, description, cta_label (all in offer_config_json
    on the source step; this model stores their translated counterparts).

    Invariants:
    - (store, step, lang_code) is unique: one translation record per step per language.
    - store must match step.campaign.store — enforced in clean().
    - cta_label is the translated call-to-action button label (e.g. "Yes, add it!").
    - status='published' makes the translated content live; 'draft' suppresses it.
    - ai_job FK is SET_NULL: deleting an AiJob does not delete the translation.
    """

    _STATUS_DRAFT = "draft"
    _STATUS_PUBLISHED = "published"
    _STATUS_CHOICES = [
        (_STATUS_DRAFT, "Draft"),
        (_STATUS_PUBLISHED, "Published"),
    ]

    step = models.ForeignKey(
        CampaignStep,
        on_delete=models.CASCADE,
        related_name="translations",
    )
    lang_code = models.CharField(
        max_length=10,
        help_text="ISO 639-1 language code — e.g. 'fr', 'pt-br'.",
    )
    title = models.CharField(max_length=255, blank=True)
    description = models.TextField(blank=True)
    cta_label = models.CharField(
        max_length=100,
        blank=True,
        help_text="Translated call-to-action button label shown to the shopper.",
    )
    status = models.CharField(
        max_length=20,
        choices=_STATUS_CHOICES,
        default=_STATUS_DRAFT,
    )
    ai_job = models.ForeignKey(
        "aijobs.AiJob",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="campaign_step_translations",
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def clean(self):
        if self.store_id and self.step_id and self.store_id != self.step.campaign.store_id:
            raise ValidationError(
                "Translation store must match the step's campaign store."
            )

    def __str__(self):
        return f"Step {self.step_id} translation [{self.lang_code}]"

    class Meta:
        verbose_name = "campaign step translation"
        verbose_name_plural = "campaign step translations"
        unique_together = [("store", "step", "lang_code")]
