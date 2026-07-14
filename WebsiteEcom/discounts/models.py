"""
Discount engine models: DiscountCode, GiftCardTransaction, DiscountCodeEmailUse,
CampaignReward.

Design decisions (ADR-002, §XIII, design-pattern-ideas.txt):
- One DiscountCode model handles coupons AND gift cards (type-discriminated).
- Gift card balance = initial_balance − Σ GiftCardTransaction.amount (positive = spend).
- Atomic increment of times_used via a single conditional UPDATE (ADR-002 §4) —
  never check-then-save (§XIII timing attack surface).
- coupon applied first, gift card second — enforced by DiscountService, not the model.
- DiscountCode.code is always stored UPPERCASE (stripped in save()).
- DiscountCodeEmailUse is NOT a StoreOwnedModel — it is a lookup table keyed by
  (discount_code, email). The store FK is reachable via discount_code.store.
- GiftCardTransaction.order is a lazy string FK ('orders.Order') — Order model ships
  in TICKET-009 and does not exist yet.
- CampaignReward enforces idempotency for campaign-issued rewards (ADR-002 §5):
  unique_together (store, campaign_id, recipient_email) prevents double-issuing.
- DiscountCode.provenance is the audit field for the "765×$5 audit" use-case.
- DiscountCode.stackable controls whether this code may stack with other discounts.
- DiscountCode.free_product pins the specific Product given free for free_product type.
- DiscountCode.currency is ISO 4217; empty means the code applies to all currencies.
- DiscountCode.gift_card_campaign (ADR-029 D1) links a gift_card_auto code back to the
  GiftCardCampaign that issued it — SET_NULL so deleting a campaign never deletes issued
  codes (audit trail; CampaignReward.discount_code is PROTECT anyway). Indexed via the
  related_name reverse FK — used for the D8 Issued/Used/Outstanding counters instead of
  an un-indexed LIKE query on CampaignReward.campaign_id prefixes.
- DiscountCode.refund_flagged / refund_flagged_reason (ADR-029 D9, human-approved
  2026-07-11): set when a campaign-issued gift card had already been partially/fully
  spent at the time its triggering order was refunded. The balance is NEVER clawed
  back automatically — this is a manual-review flag only, set by
  discounts.service.void_or_flag_campaign_gift_cards_for_refund. An unspent card in
  the same situation is deactivated instead (is_active=False) — no flag needed.

GiftCardCampaign (ADR-029 D1): a store-scoped automated gift-card campaign
configuration. When published, discounts.tasks.process_gift_card_campaigns issues one
gift_card_auto DiscountCode per qualifying PAID order and emails it to the buyer.
This is the ADR-027 D2 "campaign-shaped model outside the funnel machinery" precedent
(engagement.LeadCaptureCampaign) applied to gift cards — no funnel, no session, no
step graph: a server-side order-event reactor whose entire output is a DiscountCode
plus one email, idempotent via CampaignReward (ADR-002 §5).
"""

from django.db import models

from core.models import StoreOwnedModel


class DiscountType(models.TextChoices):
    COUPON = 'coupon', 'Coupon'
    GIFT_CARD_MANUAL = 'gift_card_manual', 'Gift card (manual)'
    GIFT_CARD_AUTO = 'gift_card_auto', 'Gift card (auto-issued)'


class ValueType(models.TextChoices):
    PERCENTAGE = 'percentage', 'Percentage'
    FIXED_AMOUNT = 'fixed_amount', 'Fixed amount'
    FREE_PRODUCT = 'free_product', 'Free product'
    FREE_SHIPPING = 'free_shipping', 'Free shipping'


class ProvenanceType(models.TextChoices):
    MERCHANT = 'merchant', 'Merchant'
    LEAD_CAPTURE = 'lead_capture', 'Lead capture'
    CAMPAIGN = 'campaign', 'Campaign'
    SOLD = 'sold', 'Sold'


class DiscountCode(StoreOwnedModel):
    """
    Unified discount code model: covers coupons, manually issued gift cards, and
    auto-issued gift cards (e.g. loyalty rewards).

    Invariants:
    - code is always UPPERCASE; save() strips whitespace and uppercases before writing.
    - unique_together ('store', 'code') ensures codes are unique per store.
    - value_type is only meaningful for coupon discount_type; ignored for gift cards.
    - initial_balance / current_balance are only meaningful for gift card types.
    - times_used is ALWAYS incremented atomically via a single conditional UPDATE
      in DiscountService (ADR-002 §4) — never obj.times_used += 1; obj.save().
    - is_valid_now() combines is_active, date window, and usage_limit; re-validated
      inside the checkout transaction (§XV-5 — cart-apply check is advisory only).
    - product_conditions / collection_conditions: if both are empty the discount
      applies to all products. If non-empty, at least one matching product or
      collection must be in the order.
    - provenance tracks the origin of the code (merchant/lead_capture/campaign/sold)
      for the "765×$5 audit" use-case (how many codes of each origin were redeemed).
    - stackable=False means this code cannot be combined with other discounts;
      DiscountService enforces this — the model is a data carrier only.
    - free_product is only meaningful when value_type=FREE_PRODUCT; it pins the exact
      catalog.Product to give free. Nullable because the field was added in 0003.
    - currency is ISO 4217 (e.g. 'EUR', 'USD'). Empty string means all currencies.
    """

    code = models.CharField(max_length=100)
    discount_type = models.CharField(
        max_length=20,
        choices=DiscountType.choices,
        default=DiscountType.COUPON,
    )
    value_type = models.CharField(
        max_length=20,
        choices=ValueType.choices,
        default=ValueType.PERCENTAGE,
        help_text='Ignored for gift card types.',
    )
    value = models.DecimalField(
        max_digits=10,
        decimal_places=2,
        default=0,
        help_text='Percent (0–100) for percentage type; fixed amount for fixed_amount type.',
    )
    minimum_order_amount = models.DecimalField(
        max_digits=10,
        decimal_places=2,
        default=0,
    )
    starts_at = models.DateTimeField(null=True, blank=True)
    ends_at = models.DateTimeField(null=True, blank=True)
    usage_limit = models.PositiveIntegerField(
        null=True,
        blank=True,
        help_text='None means unlimited.',
    )
    usage_limit_per_customer = models.PositiveIntegerField(
        default=1,
        help_text='Per customer account usage limit.',
    )
    per_email_limit = models.PositiveIntegerField(
        default=1,
        help_text='Per-email limit for guest checkout (ADR-002 §4).',
    )
    times_used = models.PositiveIntegerField(
        default=0,
        help_text=(
            'Atomically incremented on redemption via a single conditional UPDATE '
            '(ADR-002 §4). Never increment Python-side or with an unconditional UPDATE.'
        ),
    )
    initial_balance = models.DecimalField(
        max_digits=10,
        decimal_places=2,
        default=0,
        help_text='Gift card face value. Only meaningful for gift_card_manual / gift_card_auto.',
    )
    is_active = models.BooleanField(default=True)
    provenance = models.CharField(
        max_length=20,
        choices=ProvenanceType.choices,
        default=ProvenanceType.MERCHANT,
        help_text=(
            'Origin of this code: merchant (manually created), lead_capture '
            '(sign-up flow), campaign (external campaign reward), sold (paid for). '
            'Used for the "765×$5 audit" — how many codes of each origin were redeemed.'
        ),
    )
    stackable = models.BooleanField(
        default=False,
        help_text=(
            'If True, this code may be combined with a gift card at checkout. '
            'If False, DiscountService will reject it when another discount is '
            'already present in the cart (coupon+gift-card stacking opt-out).'
        ),
    )
    free_product = models.ForeignKey(
        'catalog.Product',
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name='free_product_coupons',
        help_text=(
            'Only meaningful when value_type=FREE_PRODUCT. '
            'The specific product the customer receives for free.'
        ),
    )
    currency = models.CharField(
        max_length=3,
        blank=True,
        default='',
        help_text='ISO 4217 currency code (e.g. EUR, USD). Empty = all currencies.',
    )
    gift_card_campaign = models.ForeignKey(
        'GiftCardCampaign',
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name='issued_codes',
        help_text=(
            'Set for gift_card_auto codes issued by an automated gift-card campaign '
            '(ADR-029 D1). Never set for merchant/lead_capture/sold codes.'
        ),
    )
    refund_flagged = models.BooleanField(
        default=False,
        help_text=(
            'ADR-029 D9: set when this campaign-issued gift card had already been '
            'partially/fully spent when its triggering order was refunded. The '
            'balance is never clawed back automatically — manual review only.'
        ),
    )
    refund_flagged_reason = models.CharField(
        max_length=255,
        blank=True,
        default='',
        help_text='Audit note explaining why refund_flagged was set.',
    )
    product_conditions = models.ManyToManyField(
        'catalog.Product',
        blank=True,
        related_name='discount_codes',
        help_text='Restrict to specific products. Empty = applies to all.',
    )
    collection_conditions = models.ManyToManyField(
        'catalog.Collection',
        blank=True,
        related_name='discount_codes',
        help_text='Restrict to specific collections. Empty = applies to all.',
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def save(self, *args, **kwargs):
        """Uppercase and strip the code before every save."""
        self.code = self.code.strip().upper()
        super().save(*args, **kwargs)

    @property
    def current_balance(self):
        """
        Only meaningful for gift card types.

        Returns initial_balance minus the sum of all GiftCardTransaction.amount rows.
        Positive transaction amount = spend; negative = refund reversal (ADR-002 §2).

        ADR-031 addendum audit (TICKET-051): `self.transactions` is a
        reverse-FK related manager (GiftCardTransaction.gift_card ->
        DiscountCode) — ADR-031 deliberately leaves those raising (only
        relation-bound M2M managers were relaxed). `.aggregate()` on it used
        to silently bypass the raise entirely; scoped explicitly now that it
        raises unconditionally.
        """
        spent = (
            GiftCardTransaction.objects.for_store(self.store)
            .filter(gift_card=self)
            .aggregate(total=models.Sum('amount'))['total']
            or 0
        )
        return self.initial_balance - spent

    def is_valid_now(self):
        """
        Returns True if this discount code may currently be applied.

        Checks: is_active, date window (starts_at / ends_at), and usage_limit.
        NOTE: this check is advisory. The usage_limit race is handled by the
        _increment_times_used conditional UPDATE in DiscountService (ADR-002 §4).
        """
        from django.utils import timezone

        now = timezone.now()
        if not self.is_active:
            return False
        if self.starts_at and now < self.starts_at:
            return False
        if self.ends_at and now > self.ends_at:
            return False
        if self.usage_limit is not None and self.times_used >= self.usage_limit:
            return False
        return True

    def __str__(self):
        return f"{self.code} ({self.get_discount_type_display()})"

    class Meta:
        verbose_name = 'discount code'
        verbose_name_plural = 'discount codes'
        unique_together = [('store', 'code')]
        indexes = [
            models.Index(fields=['store', 'code']),
            models.Index(fields=['store', 'is_active', 'ends_at']),
        ]


class GiftCardTransaction(StoreOwnedModel):
    """
    Append-only stored-value ledger for gift card balance movements.

    Invariants:
    - amount is positive for a spend (customer redeems value) and negative for a
      refund reversal (balance restored — ADR-002 §2 refund order: card refunded first,
      remainder restores gift card via a negative entry).
    - Every row is written inside a SELECT FOR UPDATE transaction on the parent
      DiscountCode row (§XIII — gift card balance decrement is atomic with the order
      payment record write; rollback restores the balance automatically).
    - order FK is a lazy string reference ('orders.Order') — Order ships in TICKET-009.
    - gift_card.discount_type must be gift_card_manual or gift_card_auto; enforced by
      DiscountService, not at the DB layer (no check constraint for now).
    """

    gift_card = models.ForeignKey(
        DiscountCode,
        on_delete=models.CASCADE,
        related_name='transactions',
        help_text='Must be a gift_card_manual or gift_card_auto DiscountCode.',
    )
    order = models.ForeignKey(
        'orders.Order',
        on_delete=models.SET_NULL,
        null=True,
        blank=True,
        related_name='gift_card_transactions',
    )
    amount = models.DecimalField(
        max_digits=10,
        decimal_places=2,
        help_text='Positive = spend; negative = refund reversal.',
    )
    note = models.CharField(max_length=255, blank=True, default='')
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        sign = '+' if self.amount >= 0 else ''
        return f"{self.gift_card.code} {sign}{self.amount}"

    class Meta:
        verbose_name = 'gift card transaction'
        verbose_name_plural = 'gift card transactions'
        indexes = [
            models.Index(fields=['store', 'gift_card']),
            models.Index(fields=['store', 'created_at']),
        ]


class DiscountCodeEmailUse(models.Model):
    """
    Per-email coupon usage counter for guest checkout (ADR-002 §4).

    NOT a StoreOwnedModel — this is a lookup table keyed by (discount_code, email).
    The store is reachable via discount_code.store; no separate store FK is needed.

    Invariants:
    - use_count is incremented atomically via F() expression + get_or_create in
      DiscountService — never obj.use_count += 1; obj.save().
    - email is stored lowercase (normalized in DiscountService before lookup).
    - The unique_together constraint (discount_code, email) is the ON-CONFLICT target
      for the atomic per-email limit check (ADR-002 §4).
    """

    discount_code = models.ForeignKey(
        DiscountCode,
        on_delete=models.CASCADE,
        related_name='email_uses',
    )
    email = models.EmailField()
    use_count = models.PositiveIntegerField(default=0)

    def __str__(self):
        return f"{self.email} used {self.discount_code.code} x{self.use_count}"

    class Meta:
        verbose_name = 'discount code email use'
        verbose_name_plural = 'discount code email uses'
        unique_together = [('discount_code', 'email')]


class CampaignReward(StoreOwnedModel):
    """
    Idempotency guard for campaign-issued rewards (ADR-002 §5).

    Prevents the same external campaign from issuing a reward to the same recipient
    twice. One row is written atomically with the DiscountCode creation (or retrieval);
    the unique_together constraint on (store, campaign_id, recipient_email) is the
    ON-CONFLICT target — a duplicate INSERT raises IntegrityError, which the caller
    must catch to detect and return the already-issued code.

    Invariants:
    - campaign_id is an opaque external reference (e.g. a marketing platform's campaign
      UUID). It is the caller's responsibility to keep it stable across retries.
    - recipient_email is stored as provided; callers should normalise to lowercase
      before lookup/create to avoid case-sensitive duplicates.
    - discount_code is PROTECT to prevent deleting the code while the reward row
      exists — the reward is the audit trail for the code issuance.
    - issued_at is set automatically at creation (auto_now_add).
    """

    campaign_id = models.CharField(
        max_length=100,
        help_text='Opaque external campaign reference (e.g. marketing platform UUID).',
    )
    recipient_email = models.EmailField(
        help_text='Normalise to lowercase before create/lookup to avoid duplicates.',
    )
    discount_code = models.ForeignKey(
        DiscountCode,
        on_delete=models.PROTECT,
        related_name='campaign_rewards',
        help_text='The code issued to this recipient for this campaign.',
    )
    issued_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return (
            f"Campaign {self.campaign_id} → {self.recipient_email} "
            f"({self.discount_code.code})"
        )

    class Meta:
        verbose_name = 'campaign reward'
        verbose_name_plural = 'campaign rewards'
        unique_together = [('store', 'campaign_id', 'recipient_email')]
        indexes = [
            models.Index(fields=['store', 'campaign_id'], name='disc_camp_reward_camp_idx'),
            models.Index(fields=['store', 'recipient_email'], name='disc_camp_reward_email_idx'),
        ]


# ---------------------------------------------------------------------------
# GiftCardCampaign (ADR-029, TICKET-045)
# ---------------------------------------------------------------------------


class GiftCardCampaignStatus(models.TextChoices):
    DRAFT = 'draft', 'Draft'
    PUBLISHED = 'published', 'Published'
    ARCHIVED = 'archived', 'Archived'


class GiftCardCampaignTrigger(models.TextChoices):
    """
    ADR-029 D2: order_paid is the only trigger dispatched in v1. The remaining
    values are reserved vocabulary (documented future triggers) so a later
    increment extends this choice list instead of requiring a migration to add
    the concept. discounts/tasks.py only ever queries trigger_event=ORDER_PAID.
    """
    ORDER_PAID = 'order_paid', 'Order paid'
    NTH_ORDER = 'nth_order', 'Nth order (reserved)'
    SPEND_THRESHOLD = 'spend_threshold', 'Spend threshold (reserved)'
    WIN_BACK = 'win_back', 'Win-back (reserved)'


class GiftCardCampaignTriggerScope(models.TextChoices):
    ALL_PRODUCTS = 'all_products', 'All products'
    MANUAL_PRODUCTS = 'manual_products', 'Manually add products'
    # Hidden in the v1 admin form (D2) — same PENDING rules vocabulary as
    # campaigns.Campaign.targeting_rules_json. discounts.service.campaign_matches_order
    # never matches a CONDITIONS-scoped campaign in Phase 1.
    CONDITIONS = 'conditions', 'Products based on conditions'


class GiftCardCampaignValueMode(models.TextChoices):
    """
    ADR-029 D3: FIXED is the only value mode implemented in v1 (matches the
    only observed production usage — 765 uniform $5 cards). The two percentage
    modes are reserved enum values so the admin form's radio group can render
    them (disabled, "pending decision") without a migration; issuing against
    either PENDING mode must never happen until a future ADR resolves D3.
    """
    FIXED = 'fixed', 'Set value'
    PERCENT_OF_ORDER = 'percent_of_order', 'Percentage of order total (pending decision)'
    PERCENT_DISCOUNT_COUPON = 'percent_discount_coupon', 'Set % discount off order (pending decision)'


class GiftCardCampaignExpiryMode(models.TextChoices):
    RELATIVE_DAYS = 'relative_days', 'Set number of days'
    ABSOLUTE_DATE = 'absolute_date', 'Specific date'


class GiftCardCampaignCapWindowUnit(models.TextChoices):
    MINUTES = 'minutes', 'Minutes'
    HOURS = 'hours', 'Hours'
    DAYS = 'days', 'Days'


class GiftCardCampaign(StoreOwnedModel):
    """
    An automated gift-card campaign (ADR-029, TICKET-045, super-admin-12 screens).

    Publishing (status=PUBLISHED) makes the campaign eligible for
    discounts.tasks.process_gift_card_campaigns, enqueued via transaction.on_commit
    from the Stripe/PayPal PAID webhook handlers. Issuance is idempotent via
    CampaignReward with campaign_id=f"gift_card_campaign:{pk}:order:{order.pk}"
    (ADR-002 §5 — the "765×$5 incident guard").

    Invariants:
    - owner_scope mirrors campaigns.Campaign's vocabulary. v1 UI (super-admin) only
      ever creates owner_scope=PLATFORM rows; the field is kept open for a future
      store-admin variant without a migration (D1).
    - clean() delegates to discounts.validators.validate_gift_card_campaign() — the
      single validation function for all save paths (§XV-5). Publishing enforces:
      value > 0 for FIXED, a coherent expiry config (incl. the FR 5-year floor, D6 —
      also re-checked at issuance against the store's CURRENT markets, see
      discounts.service.issue_campaign_reward_for_order), cap_count/cap_window_value
      > 0 when cap_enabled, and non-empty trigger_products when
      trigger_scope=MANUAL_PRODUCTS. Reserved/PENDING choices (value_mode's percent
      modes, D3; trigger_scope=CONDITIONS, D2) are rejected regardless of
      draft/published — never just at publish — so no campaign can ever be left
      configured with a choice that would silently never fire or never issue
      (§XV-1).
    - max_issued_cards=None means unlimited (D9 budget cap, ASSUMPTION beyond screens).
    - email_subject/email_body: when non-blank, override the platform
      'gift_card_campaign' email template (D7). The editor's token pills
      "[DISCOUNT]" / "[GENERATED GIFT CARD CODE]" are stored as literal bracket
      text and mapped at send time (discounts.service._substitute_gift_card_pills)
      to Django template variables {{ gift_value }} / {{ gift_code }} — the same
      context names the manual-issue dialog uses (admin-020-02).
    - issued_count / used_count / outstanding_count (D8) are ALWAYS computed
      queries against DiscountCode.gift_card_campaign — never denormalized
      counters (no new race surface; these are low-traffic admin list pages).
    """

    OWNER_SCOPE_STORE = 'store'
    OWNER_SCOPE_PLATFORM = 'platform'
    _OWNER_SCOPE_CHOICES = [
        (OWNER_SCOPE_STORE, 'Store admin'),
        (OWNER_SCOPE_PLATFORM, 'Platform (super-admin)'),
    ]

    name = models.CharField(max_length=255)
    status = models.CharField(
        max_length=10,
        choices=GiftCardCampaignStatus.choices,
        default=GiftCardCampaignStatus.DRAFT,
    )
    owner_scope = models.CharField(
        max_length=10,
        choices=_OWNER_SCOPE_CHOICES,
        default=OWNER_SCOPE_PLATFORM,
        help_text=(
            "Scope of the campaign creator. The v1 UI (super-admin) only creates "
            "'platform' rows; kept open for a future store-admin variant (D1)."
        ),
    )

    trigger_event = models.CharField(
        max_length=20,
        choices=GiftCardCampaignTrigger.choices,
        default=GiftCardCampaignTrigger.ORDER_PAID,
    )
    trigger_scope = models.CharField(
        max_length=20,
        choices=GiftCardCampaignTriggerScope.choices,
        default=GiftCardCampaignTriggerScope.ALL_PRODUCTS,
    )
    trigger_products = models.ManyToManyField(
        'catalog.Product',
        blank=True,
        related_name='gift_card_campaigns',
        help_text="Used only when trigger_scope='manual_products'.",
    )
    trigger_rules_json = models.JSONField(
        default=dict,
        blank=True,
        help_text=(
            "Reserved for trigger_scope='conditions' (hidden in v1 UI — D2). "
            "Phase 1 never evaluates this field."
        ),
    )

    value_mode = models.CharField(
        max_length=30,
        choices=GiftCardCampaignValueMode.choices,
        default=GiftCardCampaignValueMode.FIXED,
    )
    value = models.DecimalField(
        max_digits=10,
        decimal_places=2,
        default=0,
        help_text='Fixed face value of the issued gift card, in the store currency.',
    )

    expiry_mode = models.CharField(
        max_length=20,
        choices=GiftCardCampaignExpiryMode.choices,
        default=GiftCardCampaignExpiryMode.RELATIVE_DAYS,
    )
    expiry_days = models.PositiveIntegerField(
        default=30,
        help_text="Used when expiry_mode='relative_days'. Default matches the legacy platform's admin-020 behavior.",
    )
    expiry_date = models.DateField(
        null=True,
        blank=True,
        help_text="Used when expiry_mode='absolute_date'.",
    )

    cap_enabled = models.BooleanField(default=False)
    cap_count = models.PositiveIntegerField(
        default=1,
        help_text='Max gift cards issued to the same recipient email within the window (D5).',
    )
    cap_window_value = models.PositiveIntegerField(default=30)
    cap_window_unit = models.CharField(
        max_length=10,
        choices=GiftCardCampaignCapWindowUnit.choices,
        default=GiftCardCampaignCapWindowUnit.DAYS,
    )

    max_issued_cards = models.PositiveIntegerField(
        null=True,
        blank=True,
        help_text='Budget cap across all recipients. Null = unlimited (D9).',
    )

    email_subject = models.CharField(
        max_length=255,
        blank=True,
        default='',
        help_text="Blank = use the platform 'gift_card_campaign' template (D7).",
    )
    email_body = models.TextField(
        blank=True,
        default='',
        help_text=(
            "Blank = use the platform 'gift_card_campaign' template (D7). Supports "
            "the '[DISCOUNT]' and '[GENERATED GIFT CARD CODE]' token pills."
        ),
    )

    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = 'gift card campaign'
        verbose_name_plural = 'gift card campaigns'
        indexes = [
            models.Index(fields=['store', 'status'], name='disc_gcc_store_status_idx'),
        ]

    def __str__(self):
        return self.name

    def clean(self):
        from .validators import validate_gift_card_campaign

        validate_gift_card_campaign(self)

    # ADR-031 addendum audit (TICKET-051): issued_count/used_count/
    # outstanding_count all previously ran on `self.issued_codes` — a
    # reverse-FK related manager (DiscountCode.gift_card_campaign ->
    # GiftCardCampaign) that ADR-031 deliberately leaves raising (only
    # relation-bound M2M managers were relaxed). `.count()` used to silently
    # bypass that raise; all three now go through
    # DiscountCode.objects.for_store(self.store) explicitly.

    @property
    def issued_count(self):
        """D8 'Issued' counter — total codes this campaign has ever issued."""
        return DiscountCode.objects.for_store(self.store).filter(gift_card_campaign=self).count()

    @property
    def used_count(self):
        """D8 'Used' counter — issued codes with at least one spend transaction (amount > 0). Partial redemption counts as used."""
        return (
            DiscountCode.objects.for_store(self.store)
            .filter(gift_card_campaign=self, transactions__amount__gt=0)
            .distinct()
            .count()
        )

    @property
    def outstanding_count(self):
        """
        D8 'Outstanding' counter — issued codes that are active, unexpired, and
        still carry a positive balance (initial_balance > Σ spend). A single
        annotated aggregate query — never a per-row Python loop over
        current_balance (this campaign can have hundreds of issued codes).
        """
        from django.db.models import F, Q, Sum
        from django.utils import timezone

        now = timezone.now()
        return (
            DiscountCode.objects.for_store(self.store)
            .filter(gift_card_campaign=self, is_active=True)
            .filter(Q(ends_at__isnull=True) | Q(ends_at__gt=now))
            .annotate(spent=Sum('transactions__amount'))
            .filter(Q(spent__isnull=True) | Q(initial_balance__gt=F('spent')))
            .filter(initial_balance__gt=0)
            .count()
        )
