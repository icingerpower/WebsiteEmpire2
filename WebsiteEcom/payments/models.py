"""
Payments app — ProcessorAccount, routing control-plane, and audit log (ADR-006-R, TICKET-017R).

Model inventory:
  ProcessorAccount      — credentials + health for one org/processor combination.
  OrgPool               — named group of Organizations used in stage-2 split rules.
  OrgPoolMember         — explicit through table for pool membership + split targets.
  OrganizationRule      — two-stage decision tree (stage1_geography / stage2_allocation).
  PaymentMethod         — checkout-visible family (card / paypal / crypto) with strategy.
  ProcessorOption       — ordered ProcessorAccount list within a PaymentMethod.
  ProcessorSplitCounter — running counters for percent_split and alternate_evenly (two scopes).
  DecisionLog           — immutable audit row per route_payment() call.

Design decisions:
- ProcessorAccount is NOT a StoreOwnedModel — owned by Organization (ADR-001 §3b, ADR-006).
- All new models are platform-global [G]; add them to EXEMPT_MODELS (compliance test).
- Credentials (api_key, api_secret, webhook_secret) are stored encrypted via EncryptedCharField.
- RoutingRule is DROPPED — OrganizationRule replaces it with a two-stage tree (ADR-006-R).
- DecisionLog is recreated with a richer schema (evaluated_rules_json, over_cap, parent_decision).
- Custom __repr__ / __str__ on ProcessorAccount never expose credential values.
"""

from __future__ import annotations

import logging

from django.core.exceptions import ValidationError
from django.db import models
from django.db.models.signals import pre_save
from django.dispatch import receiver

from stores.models import Organization

from .fields import EncryptedCharField

logger = logging.getLogger("payments.models")

# ---------------------------------------------------------------------------
# Frozen vocabulary constants used in OrganizationRule.clean() validation (§1.8)
# ---------------------------------------------------------------------------

#: EU member states (27 post-Brexit) — frozen so routing never diverges from save-time lint.
EU_COUNTRIES: frozenset[str] = frozenset(
    {
        "AT", "BE", "BG", "CY", "CZ", "DE", "DK", "EE", "ES", "FI",
        "FR", "GR", "HR", "HU", "IE", "IT", "LT", "LU", "LV", "MT",
        "NL", "PL", "PT", "RO", "SE", "SI", "SK",
    }
)

#: Allowed keys in conditions_json (§1.8).
_ALLOWED_CONDITION_KEYS: frozenset[str] = frozenset(
    {"countries", "areas", "currencies", "min_amount_cents", "max_amount_cents"}
)

#: Allowed area tokens in conditions_json (§1.8).
_ALLOWED_AREA_TOKENS: frozenset[str] = frozenset({"EU", "ROW", "ALL"})


# ---------------------------------------------------------------------------
# ProcessorAccount
# ---------------------------------------------------------------------------


class ProcessorType(models.TextChoices):
    """
    Supported payment processor types (ADR-006 §7).
    Phase 2+ additions: BRAINTREE, ADYEN, HIPAY, MOLLIE, BTCPAY, BITPAY.
    """

    STRIPE = "stripe", "Stripe"
    PAYPAL = "paypal", "PayPal"


class ProcessorAccount(models.Model):
    """
    Payment processor credentials for an Organization (ADR-006).

    NOT a StoreOwnedModel — owned by Organization, not Store (ADR-001 §3b).
    See EXEMPT_MODELS in core/tests/test_store_owned_model_compliance.py.

    Credentials are encrypted at rest using EncryptedCharField (Fernet).
    The is_test_mode flag distinguishes sandbox from live credentials;
    never mix test and live keys in the same account row.

    Health state machine (§1.2.1):
      UNKNOWN → GREEN | YELLOW | RED  (first probe or manual set)
      GREEN   → YELLOW (1 probe failure) → RED (2 consecutive failures)
      YELLOW  → GREEN (1 probe success)
      RED     → GREEN (1 probe success)
      any     → RED/GREEN/YELLOW (manual, sets health_source="manual")
    The checkout path NEVER calls a processor API — it reads health_status.

    is_healthy(): returns True when is_active AND health_status != RED.
    GREEN, YELLOW, and UNKNOWN are all routable (unprobed must not block checkout).

    clean(): forces may_be_primary=False when backup_only=True.
    """

    organization = models.ForeignKey(
        Organization,
        on_delete=models.CASCADE,
        related_name="processor_accounts",
        help_text="Organization (legal entity) that owns these credentials.",
    )
    processor_type = models.CharField(
        max_length=50,
        choices=ProcessorType.choices,
        help_text="Payment processor family (e.g. stripe, paypal).",
    )
    display_name = models.CharField(
        max_length=255,
        help_text="Human-readable label shown in /superadmin/ lists.",
    )
    is_active = models.BooleanField(
        default=True,
        help_text=(
            "Manual block flag.  False = excluded from routing unconditionally, "
            "regardless of health-probe status."
        ),
    )
    is_test_mode = models.BooleanField(
        default=True,
        help_text=(
            "True = sandbox/test credentials; False = live production credentials. "
            "Never mix test and live credentials in the same account row."
        ),
    )

    # --- Encrypted credentials (Fernet AES-128-CBC + HMAC) ---
    api_key = EncryptedCharField(
        blank=True,
        default="",
        help_text="Primary API key / secret key (encrypted at rest).",
    )
    api_secret = EncryptedCharField(
        blank=True,
        default="",
        help_text="API secret / client secret (encrypted at rest).",
    )
    webhook_secret = EncryptedCharField(
        blank=True,
        default="",
        help_text="Webhook signing secret for validating inbound events (encrypted at rest).",
    )

    # --- Non-secret routing identifiers ---
    account_id = models.CharField(
        max_length=255,
        blank=True,
        default="",
        help_text="Stripe Connect account ID (e.g. acct_xxxx) or equivalent.",
    )
    client_id = models.CharField(
        max_length=255,
        blank=True,
        default="",
        help_text="PayPal client ID or equivalent public identifier.",
    )

    # --- Method family and routing priority (ADR-006-R §1.2) ---
    method_family = models.CharField(
        max_length=20,
        choices=[("card", "card"), ("paypal", "paypal"), ("crypto", "crypto")],
        default="card",
        help_text=(
            "Which checkout method family this account can process. "
            "Stripe accounts = card; PayPal = paypal. "
            "PayPal-as-card capability: create a second row with method_family='card'."
        ),
    )
    supported_countries = models.JSONField(
        default=list,
        help_text="ISO country codes this account may charge buyers from. Empty = all countries.",
    )
    settlement_currency = models.CharField(
        max_length=3,
        blank=True,
        default="",
        help_text="ISO 4217 settlement currency.",
    )
    priority_within_family = models.SmallIntegerField(
        default=100,
        help_text="Tie-breaker ordering inside one org + family (lower = first).",
    )
    may_be_primary = models.BooleanField(
        default=True,
        help_text="False = never selected first in a backup chain.",
    )
    backup_only = models.BooleanField(
        default=False,
        help_text=(
            "True = selected only when every non-backup candidate is ineligible. "
            "Lint: backup_only=True forces may_be_primary=False (auto-set in clean())."
        ),
    )

    # --- PayPal-specific capture / vault configuration (ADR-011 Q3) ---
    # These fields are meaningful only when processor_type='paypal'.
    # For all other processor types the values are inert (defaults are safe).
    paypal_capture_mode = models.CharField(
        max_length=10,
        choices=[
            ("delayed", "Delayed (authorize, then capture — funnel-eligible)"),
            ("immediate", "Immediate capture (skip post-purchase funnel)"),
        ],
        default="delayed",
        help_text=(
            "PayPal only. "
            "Delayed = Orders API intent=AUTHORIZE; immediate = intent=CAPTURE. "
            "Immediate-capture orders are funnel-ineligible (no capture window, no upsell)."
        ),
    )
    paypal_vault_enabled = models.BooleanField(
        default=False,
        help_text=(
            "PayPal only. True once the merchant has completed PayPal Vault onboarding "
            "(reference-transaction / vaulting agreement). Off-session upsell charges — and "
            "therefore post-purchase funnel eligibility for PayPal-routed orders — require "
            "this. False = PayPal orders are funnel-ineligible regardless of capture mode "
            "(prevents a silent 100%-failure funnel, ADR-011 Q3 §XV-1)."
        ),
    )

    # --- Health state machine (§1.2.1) ---
    health_status = models.CharField(
        max_length=10,
        choices=[
            ("GREEN", "GREEN"),
            ("YELLOW", "YELLOW"),
            ("RED", "RED"),
            ("UNKNOWN", "UNKNOWN"),
        ],
        default="UNKNOWN",
        help_text="Health probe result. UNKNOWN = not yet probed; still routable.",
    )
    health_source = models.CharField(
        max_length=10,
        choices=[("probe", "probe"), ("manual", "manual")],
        default="probe",
        help_text="'manual' freezes health_status against probe overwrites.",
    )
    health_checked_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Timestamp of the last probe or manual status change.",
    )
    health_detail = models.CharField(
        max_length=255,
        blank=True,
        default="",
        help_text="Last probe error message, shown in the super-admin health screen.",
    )

    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "processor account"
        verbose_name_plural = "processor accounts"
        constraints = [
            models.UniqueConstraint(
                fields=["organization", "processor_type"],
                name="unique_org_processor_active",
                condition=models.Q(is_active=True),
            ),
        ]

    def is_healthy(self) -> bool:
        """
        True when this account is eligible for routing:
          - manually active (is_active=True), AND
          - health probe has not marked it RED.

        GREEN, YELLOW, and UNKNOWN are all routable:
        an unprobed account must not block checkout (§1.2.1).
        """
        return self.is_active and self.health_status != "RED"

    def clean(self) -> None:
        """Auto-force may_be_primary=False when backup_only=True."""
        if self.backup_only:
            self.may_be_primary = False

    def __str__(self) -> str:
        mode = "TEST" if self.is_test_mode else "LIVE"
        return f"{self.get_processor_type_display()} [{mode}] @ {self.organization}"

    def __repr__(self) -> str:
        # Never expose credential values in repr — they must not surface in logs.
        return (
            f"<ProcessorAccount id={self.pk} "
            f"processor={self.processor_type!r} "
            f"org={self.organization_id} "
            f"test_mode={self.is_test_mode}>"
        )


# ---------------------------------------------------------------------------
# OrgPool + OrgPoolMember
# ---------------------------------------------------------------------------


class OrgPool(models.Model):
    """
    Named group of Organizations used in stage-2 allocation rules (ADR-006-R §1.4).

    Stage-1 rules can point to a pool (outcome_org_pool); the pool's active stage-2
    rule governs how orders are split among pool members (percent_split or threshold_switch).
    """

    name = models.CharField(max_length=255)
    is_active = models.BooleanField(default=True)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "org pool"
        verbose_name_plural = "org pools"

    def __str__(self) -> str:
        return self.name


class OrgPoolMember(models.Model):
    """
    Explicit through table for OrgPool membership (ADR-006-R §1.4).

    target_percent is used by percent_split rules; position is used by threshold_switch.
    When the pool has an active percent_split rule, Σ target_percent across all members
    must equal 100.00 — enforced by a pre_save signal (see below).

    unique_together: each Organization may appear in a pool at most once.
    """

    pool = models.ForeignKey(
        OrgPool,
        on_delete=models.CASCADE,
        related_name="members",
    )
    organization = models.ForeignKey(
        Organization,
        on_delete=models.PROTECT,
        related_name="pool_memberships",
    )
    target_percent = models.DecimalField(
        max_digits=5,
        decimal_places=2,
        default=0,
        help_text="Target share for percent_split rules (0–100).",
    )
    position = models.SmallIntegerField(
        default=0,
        help_text="Ordering for threshold_switch rules (first-under-cap wins).",
    )

    class Meta:
        verbose_name = "org pool member"
        verbose_name_plural = "org pool members"
        unique_together = [("pool", "organization")]
        ordering = ["position"]

    def __str__(self) -> str:
        return f"{self.organization} in {self.pool} ({self.target_percent}%)"


# ---------------------------------------------------------------------------
# OrganizationRule
# ---------------------------------------------------------------------------


def _validate_conditions_json(conditions: object) -> None:
    """
    Validate a conditions_json dict against the closed §1.8 vocabulary.

    Raises ValidationError on:
    - non-dict value
    - unknown keys
    - invalid country codes (not 2-char uppercase)
    - unknown area tokens (not EU / ROW / ALL)
    - invalid currency codes (not 3-char uppercase)
    - non-integer min/max_amount_cents
    """
    if not isinstance(conditions, dict):
        raise ValidationError("conditions_json must be a JSON object (dict).")

    unknown = set(conditions) - _ALLOWED_CONDITION_KEYS
    if unknown:
        raise ValidationError(
            f"Unknown keys in conditions_json: {', '.join(sorted(unknown))}. "
            f"Allowed: {', '.join(sorted(_ALLOWED_CONDITION_KEYS))}."
        )

    for code in conditions.get("countries", []):
        if not isinstance(code, str) or len(code) != 2 or not code.isupper():
            raise ValidationError(
                f"Invalid country code {code!r} in conditions_json['countries']. "
                "Must be a 2-character uppercase ISO alpha-2 code."
            )

    for token in conditions.get("areas", []):
        if token not in _ALLOWED_AREA_TOKENS:
            raise ValidationError(
                f"Unknown area token {token!r} in conditions_json['areas']. "
                f"Allowed: {', '.join(sorted(_ALLOWED_AREA_TOKENS))}."
            )

    for code in conditions.get("currencies", []):
        if not isinstance(code, str) or len(code) != 3 or not code.isupper():
            raise ValidationError(
                f"Invalid currency code {code!r} in conditions_json['currencies']. "
                "Must be a 3-character uppercase ISO 4217 code."
            )

    for key in ("min_amount_cents", "max_amount_cents"):
        val = conditions.get(key)
        if val is not None and not isinstance(val, int):
            raise ValidationError(f"conditions_json['{key}'] must be an integer.")


def _expand_countries(conditions: dict) -> frozenset[str] | None:
    """
    Expand conditions_json to an effective country set.

    Returns None if the rule is a catch-all (matches any country),
    which is the case when: conditions is empty, "ALL" is an area token,
    or "ROW" is an area token (ROW = "everything else", effectively unbounded).

    Otherwise returns the union of explicit country codes and named-area expansions.
    """
    if not conditions:
        return None

    explicit: set[str] = set(conditions.get("countries", []))
    areas: set[str] = set(conditions.get("areas", []))

    if "ALL" in areas or "ROW" in areas:
        return None  # catch-all or unbounded remainder

    if not explicit and not areas:
        return None  # empty conditions = catch-all

    result = set(explicit)
    if "EU" in areas:
        result |= EU_COUNTRIES
    return frozenset(result)


def _is_country_superset(broad: frozenset[str] | None, narrow: frozenset[str] | None) -> bool:
    """Return True if 'broad' country set is a superset of 'narrow'."""
    if broad is None:
        return True  # catch-all is a superset of everything
    if narrow is None:
        return False  # catch-all is not a subset of a finite set
    return broad >= narrow


def _is_currency_superset(
    broad_currencies: list[str], narrow_currencies: list[str]
) -> bool:
    """Return True if 'broad' currency list is a superset of 'narrow' (empty = any)."""
    if not broad_currencies:
        return True  # match-any is a superset of anything
    if not narrow_currencies:
        return False  # match-any is not a subset of a finite set
    return set(broad_currencies) >= set(narrow_currencies)


def _is_amount_superset(
    broad_min: int | None,
    broad_max: int | None,
    narrow_min: int | None,
    narrow_max: int | None,
) -> bool:
    """Return True if [broad_min, broad_max] fully contains [narrow_min, narrow_max]."""
    # Broad lower bound must be <= narrow lower bound (None = -∞)
    if broad_min is not None:
        if narrow_min is None or broad_min > narrow_min:
            return False
    # Broad upper bound must be >= narrow upper bound (None = +∞)
    if broad_max is not None:
        if narrow_max is None or broad_max < narrow_max:
            return False
    return True


class OrganizationRule(models.Model):
    """
    Two-stage payment routing rule (ADR-006-R §1.3).

    Stage 1 (stage1_geography): maps buyer geography / currency / amount to an
    Organization or OrgPool. Exactly one of outcome_organization/outcome_org_pool
    must be set; conditions_json holds the matching criteria.

    Stage 2 (stage2_allocation): governs how orders are split among pool members.
    Must reference an org_pool and have a non-blank allocation_type; conditions_json
    is always {} (pool-bound, not order-attribute-bound).

    DB CheckConstraints enforce the shape; clean() provides human-readable errors
    plus the fallback-cycle guard and shadow warnings (non-blocking).

    _shadow_warnings: list of warning strings set by clean(); the admin surfaces these
    as WARNING messages after save_model. Not persisted.
    """

    STAGE_CHOICES = [
        ("stage1_geography", "Stage 1 — Geography"),
        ("stage2_allocation", "Stage 2 — Allocation"),
    ]
    ALLOCATION_TYPE_CHOICES = [
        ("percent_split", "percent_split"),
        ("threshold_switch", "threshold_switch"),
    ]

    stage = models.CharField(max_length=20, choices=STAGE_CHOICES)
    name = models.CharField(max_length=255, help_text="Super-admin label, e.g. 'EU → Org-FR'.")
    conditions_json = models.JSONField(
        default=dict,
        help_text=(
            "Stage-1 only. Allowed keys: countries, areas, currencies, "
            "min_amount_cents, max_amount_cents. Stage-2 rules have {}."
        ),
    )

    # Stage-1 outcome: exactly one must be set (DB CheckConstraint + clean() lint)
    outcome_organization = models.ForeignKey(
        Organization,
        null=True,
        blank=True,
        on_delete=models.PROTECT,
        related_name="stage1_rules",
        help_text="Stage-1 outcome: direct Organization target (XOR with outcome_org_pool).",
    )
    outcome_org_pool = models.ForeignKey(
        "payments.OrgPool",
        null=True,
        blank=True,
        on_delete=models.PROTECT,
        related_name="stage1_rules",
        help_text="Stage-1 outcome: OrgPool target (XOR with outcome_organization).",
    )

    # Stage-2 allocation fields
    allocation_type = models.CharField(
        max_length=20,
        blank=True,
        choices=ALLOCATION_TYPE_CHOICES,
        help_text="Stage-2 only: how orders are distributed among pool members.",
    )
    org_pool = models.ForeignKey(
        "payments.OrgPool",
        null=True,
        blank=True,
        on_delete=models.CASCADE,
        related_name="allocation_rules",
        help_text="Stage-2 only: the pool this rule governs.",
    )

    # Fallback chain (§IX — explicit next node when this rule's outcome is ineligible)
    fallback_rule = models.ForeignKey(
        "self",
        null=True,
        blank=True,
        on_delete=models.PROTECT,
        related_name="fallback_sources",
        help_text=(
            "Next rule to evaluate when this rule's outcome is ineligible. "
            "NULL = fall through to the default org. Must not form a cycle."
        ),
    )

    priority_within_stage = models.SmallIntegerField(
        default=100,
        help_text="Lower = evaluated first. Ties broken by pk ascending.",
    )
    is_active = models.BooleanField(default=True)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "organization rule"
        verbose_name_plural = "organization rules"
        ordering = ["priority_within_stage", "pk"]
        constraints = [
            # Stage-1 rules must target exactly one of outcome_organization/outcome_org_pool,
            # must have blank allocation_type, and must have NULL org_pool.
            models.CheckConstraint(
                condition=(
                    ~models.Q(stage="stage1_geography")
                    | (
                        (
                            (
                                models.Q(outcome_organization__isnull=False)
                                & models.Q(outcome_org_pool__isnull=True)
                            )
                            | (
                                models.Q(outcome_organization__isnull=True)
                                & models.Q(outcome_org_pool__isnull=False)
                            )
                        )
                        & models.Q(allocation_type="")
                        & models.Q(org_pool__isnull=True)
                    )
                ),
                name="org_rule_stage1_shape",
            ),
            # Stage-2 rules must reference a pool, have a non-blank allocation_type,
            # and must NOT set either outcome FK.
            models.CheckConstraint(
                condition=(
                    ~models.Q(stage="stage2_allocation")
                    | (
                        models.Q(org_pool__isnull=False)
                        & ~models.Q(allocation_type="")
                        & models.Q(outcome_organization__isnull=True)
                        & models.Q(outcome_org_pool__isnull=True)
                    )
                ),
                name="org_rule_stage2_shape",
            ),
        ]

    def _detect_fallback_cycle(self) -> None:
        """
        Walk the fallback_rule chain and raise ValidationError if self is revisited.

        Called from clean() as a blocking check. Self-referential cycle is impossible
        for new (unsaved) instances since they have no pk yet.

        The depth guard (max 20 hops) prevents stack overflow on pathologically
        long chains and provides a defense-in-depth behind the save-time check.
        """
        if self.pk is None or self.fallback_rule_id is None:
            return

        visited: set[int] = set()
        current_id: int | None = self.fallback_rule_id
        depth = 0
        while current_id is not None and depth < 50:
            if current_id == self.pk:
                raise ValidationError(
                    "Fallback chain creates a cycle: this rule eventually points back to itself."
                )
            if current_id in visited:
                # Internal cycle that does not include self — not our concern here.
                break
            visited.add(current_id)
            depth += 1
            try:
                row = OrganizationRule.objects.values("fallback_rule_id").get(pk=current_id)
                current_id = row["fallback_rule_id"]
            except OrganizationRule.DoesNotExist:
                break

    def _validate_stage_shape(self) -> None:
        """
        Blocking: mirror the DB CheckConstraints with human-readable ValidationErrors.
        """
        if self.stage == "stage1_geography":
            has_org = self.outcome_organization_id is not None
            has_pool = self.outcome_org_pool_id is not None
            if has_org and has_pool:
                raise ValidationError(
                    "Stage-1 rules must set exactly one of outcome_organization or "
                    "outcome_org_pool — not both."
                )
            if not has_org and not has_pool:
                raise ValidationError(
                    "Stage-1 rules must set either outcome_organization or outcome_org_pool."
                )
            if self.allocation_type:
                raise ValidationError("Stage-1 rules must have allocation_type blank.")
            if self.org_pool_id is not None:
                raise ValidationError("Stage-1 rules must have org_pool null.")

        elif self.stage == "stage2_allocation":
            if self.org_pool_id is None:
                raise ValidationError("Stage-2 rules must have org_pool set.")
            if not self.allocation_type:
                raise ValidationError("Stage-2 rules must have allocation_type set.")
            if self.outcome_organization_id is not None or self.outcome_org_pool_id is not None:
                raise ValidationError(
                    "Stage-2 rules must have both outcome_organization and outcome_org_pool null."
                )
            # Stage-2 fallback_rule must also be stage-2 or NULL.
            if self.fallback_rule_id is not None:
                try:
                    fb = OrganizationRule.objects.values("stage").get(pk=self.fallback_rule_id)
                    if fb["stage"] != "stage2_allocation":
                        raise ValidationError(
                            "Stage-2 fallback_rule must also be a stage-2 rule."
                        )
                except OrganizationRule.DoesNotExist:
                    pass  # FK constraint will catch the dangling reference

    def _compute_shadow_warnings(self) -> list[str]:
        """
        Non-blocking: check if this stage-1 rule is unreachable (shadowed by a broader
        earlier rule) or itself shadows later narrower rules.

        Returns a list of warning strings. Empty list = no shadow issues found.
        Also warns when:
        - stage-1 outcome pool has no active stage-2 rule
        - rule targets an org whose every account is RED or blocked

        This method is called from clean() and its output is stored in _shadow_warnings.
        It performs DB queries, so it is intentionally only run for active stage-1 rules.
        """
        if self.stage != "stage1_geography":
            return []

        warnings: list[str] = []

        my_countries = _expand_countries(self.conditions_json)
        my_currencies: list[str] = self.conditions_json.get("currencies", [])
        my_min: int | None = self.conditions_json.get("min_amount_cents")
        my_max: int | None = self.conditions_json.get("max_amount_cents")

        # Build the comparison key for "earlier" rules (lower priority or lower pk).
        my_priority = self.priority_within_stage
        my_pk = self.pk  # may be None for new objects

        earlier_qs = OrganizationRule.objects.filter(
            stage="stage1_geography",
            is_active=True,
        )
        if my_pk is not None:
            earlier_qs = earlier_qs.filter(
                models.Q(priority_within_stage__lt=my_priority)
                | (
                    models.Q(priority_within_stage=my_priority)
                    & models.Q(pk__lt=my_pk)
                )
            )
        else:
            earlier_qs = earlier_qs.filter(priority_within_stage__lt=my_priority)

        for rule in earlier_qs.select_related("outcome_organization", "outcome_org_pool"):
            e_conditions = rule.conditions_json
            e_countries = _expand_countries(e_conditions)
            e_currencies: list[str] = e_conditions.get("currencies", [])
            e_min: int | None = e_conditions.get("min_amount_cents")
            e_max: int | None = e_conditions.get("max_amount_cents")

            if (
                _is_country_superset(e_countries, my_countries)
                and _is_currency_superset(e_currencies, my_currencies)
                and _is_amount_superset(e_min, e_max, my_min, my_max)
            ):
                warnings.append(
                    f"This rule may be unreachable: rule '{rule.name}' "
                    f"(priority={rule.priority_within_stage}, pk={rule.pk}) "
                    "has a broader or equal geographic/currency/amount envelope and "
                    "is evaluated first."
                )

        # Warn if outcome pool has no active stage-2 rule.
        if self.outcome_org_pool_id is not None:
            has_stage2 = OrganizationRule.objects.filter(
                stage="stage2_allocation",
                org_pool_id=self.outcome_org_pool_id,
                is_active=True,
            ).exists()
            if not has_stage2:
                warnings.append(
                    f"Outcome pool '{self.outcome_org_pool}' has no active stage-2 "
                    "allocation rule. Orders matched by this rule will have no candidates "
                    "and fall through to the default org."
                )

        # Warn if outcome org has every account RED or blocked.
        if self.outcome_organization_id is not None:
            from payments.models import ProcessorAccount  # local import avoids model ordering issues
            has_any = ProcessorAccount.objects.filter(
                organization_id=self.outcome_organization_id
            ).exists()
            if has_any:
                all_healthy = ProcessorAccount.objects.filter(
                    organization_id=self.outcome_organization_id,
                    is_active=True,
                ).exclude(health_status="RED").exists()
                if not all_healthy:
                    warnings.append(
                        f"Outcome org '{self.outcome_organization}' has no healthy "
                        "ProcessorAccounts (all are inactive or RED). This rule will "
                        "always fall through."
                    )

        return warnings

    def clean(self) -> None:
        """
        Blocking and non-blocking validation for OrganizationRule (ADR-006-R §3).

        Blocking (raises ValidationError):
        1. Stage shape validation (mirrors DB CheckConstraints with readable messages).
        2. Fallback-chain cycle detection.
        3. Stage-1 conditions_json vocabulary (§1.8).

        Non-blocking (populates self._shadow_warnings):
        - Broad-before-narrow shadow detection.
        - Pool without active stage-2 rule.
        - Org with all accounts RED/blocked.
        """
        self._shadow_warnings: list[str] = []

        self._validate_stage_shape()
        self._detect_fallback_cycle()

        if self.stage == "stage1_geography" and self.conditions_json:
            _validate_conditions_json(self.conditions_json)

        self._shadow_warnings = self._compute_shadow_warnings()

    def __str__(self) -> str:
        outcome = (
            str(self.outcome_organization or self.outcome_org_pool or self.org_pool or "?")
        )
        return f"[{self.get_stage_display()}] {self.name} → {outcome} (p={self.priority_within_stage})"


# ---------------------------------------------------------------------------
# PaymentMethod + ProcessorOption
# ---------------------------------------------------------------------------


class PaymentMethod(models.Model):
    """
    Checkout-visible payment family (ADR-006-R §1.6).

    One row per method_family (unique). The checkout renders one button per
    enabled PaymentMethod; the buyer never sees individual processor accounts.

    strategy:
      backup_chain    — try accounts in position order; skip unhealthy ones.
      alternate_evenly — round-robin across eligible accounts (uses ProcessorSplitCounter).

    hide_when_no_route (AC-134): if True, the checkout hides this method when
    preview_route() finds no eligible account.
    """

    METHOD_FAMILY_CHOICES = [
        ("card", "card"),
        ("paypal", "paypal"),
        ("crypto", "crypto"),
    ]
    STRATEGY_CHOICES = [
        ("backup_chain", "backup_chain"),
        ("alternate_evenly", "alternate_evenly"),
    ]

    method_family = models.CharField(
        max_length=20,
        choices=METHOD_FAMILY_CHOICES,
        unique=True,
        help_text="Checkout-visible method family. Unique per platform.",
    )
    displayed_label = models.CharField(max_length=100)
    is_enabled = models.BooleanField(default=True)
    strategy = models.CharField(
        max_length=20,
        choices=STRATEGY_CHOICES,
        default="backup_chain",
    )
    hide_when_no_route = models.BooleanField(
        default=True,
        help_text="AC-134: hide this method when preview_route() finds no eligible account.",
    )
    allow_store_local_disable = models.BooleanField(
        default=False,
        help_text="Allow individual stores to disable this method locally.",
    )

    class Meta:
        verbose_name = "payment method"
        verbose_name_plural = "payment methods"

    def __str__(self) -> str:
        return f"{self.displayed_label} ({self.method_family})"


class ProcessorOption(models.Model):
    """
    Ordered ProcessorAccount list within a PaymentMethod (ADR-006-R §1.6).

    Maps a checkout method to its eligible processor accounts, in the order
    the backup_chain strategy tries them.

    clean(): rejects method_family mismatches — prevents misconfiguration where,
    e.g., a PayPal account is wired to the 'card' checkout button.
    """

    payment_method = models.ForeignKey(
        PaymentMethod,
        on_delete=models.CASCADE,
        related_name="options",
    )
    processor_account = models.ForeignKey(
        ProcessorAccount,
        on_delete=models.CASCADE,
        related_name="processor_options",
    )
    position = models.SmallIntegerField(default=0, help_text="Order within backup_chain.")

    class Meta:
        verbose_name = "processor option"
        verbose_name_plural = "processor options"
        unique_together = [("payment_method", "processor_account")]
        ordering = ["position"]

    def clean(self) -> None:
        """Reject mismatched method_family between ProcessorOption and PaymentMethod."""
        if (
            self.processor_account_id is not None
            and self.payment_method_id is not None
        ):
            # Avoid extra queries if already loaded via select_related
            try:
                pa_family = self.processor_account.method_family
                pm_family = self.payment_method.method_family
            except (ProcessorAccount.DoesNotExist, PaymentMethod.DoesNotExist):
                return
            if pa_family != pm_family:
                raise ValidationError(
                    f"ProcessorAccount method_family ({pa_family!r}) does not match "
                    f"PaymentMethod method_family ({pm_family!r}). "
                    "A PayPal account cannot be wired to the 'card' method unless it "
                    "has method_family='card' (create a separate row for that capability)."
                )

    def __str__(self) -> str:
        return f"{self.payment_method} → {self.processor_account} (pos={self.position})"


# ---------------------------------------------------------------------------
# ProcessorSplitCounter
# ---------------------------------------------------------------------------


class ProcessorSplitCounter(models.Model):
    """
    Running counters for percent_split (org-level) and alternate_evenly (method-level).

    Two mutually exclusive scopes (ADR-006-R §1.5):
      Scope A (org-level):    (org_pool, organization, period)
      Scope B (method-level): (payment_method, processor_account, period)

    period = "YYYY-MM". Counters reset by starting a new period row — never by UPDATE-to-zero.
    This design ensures counters survive restarts and are consistent across concurrent writers.

    target_percent is NOT stored here (it lives on OrgPoolMember) to avoid stale copies
    after live-on-save edits to pool configuration.

    Rows are get_or_created lazily at first routing of a period.
    Counter increments use SELECT FOR UPDATE within the routing transaction (AC-131).
    """

    # Scope A fields
    org_pool = models.ForeignKey(
        OrgPool,
        null=True,
        blank=True,
        on_delete=models.CASCADE,
        related_name="split_counters",
    )
    organization = models.ForeignKey(
        Organization,
        null=True,
        blank=True,
        on_delete=models.CASCADE,
        related_name="split_counters",
    )

    # Scope B fields
    payment_method = models.ForeignKey(
        PaymentMethod,
        null=True,
        blank=True,
        on_delete=models.CASCADE,
        related_name="split_counters",
    )
    processor_account = models.ForeignKey(
        ProcessorAccount,
        null=True,
        blank=True,
        on_delete=models.CASCADE,
        related_name="split_counters",
    )

    period = models.CharField(max_length=7, help_text="'YYYY-MM' — new row per period.")
    count = models.PositiveIntegerField(default=0)

    class Meta:
        verbose_name = "processor split counter"
        verbose_name_plural = "processor split counters"
        constraints = [
            # Exactly one scope must be fully populated; the other must be fully NULL.
            models.CheckConstraint(
                condition=(
                    (
                        models.Q(org_pool__isnull=False)
                        & models.Q(organization__isnull=False)
                        & models.Q(payment_method__isnull=True)
                        & models.Q(processor_account__isnull=True)
                    )
                    | (
                        models.Q(org_pool__isnull=True)
                        & models.Q(organization__isnull=True)
                        & models.Q(payment_method__isnull=False)
                        & models.Q(processor_account__isnull=False)
                    )
                ),
                name="split_counter_scope",
            ),
            # Unique per scope A (org_pool, organization, period)
            models.UniqueConstraint(
                fields=["org_pool", "organization", "period"],
                condition=models.Q(org_pool__isnull=False),
                name="split_counter_org_unique",
            ),
            # Unique per scope B (payment_method, processor_account, period)
            models.UniqueConstraint(
                fields=["payment_method", "processor_account", "period"],
                condition=models.Q(payment_method__isnull=False),
                name="split_counter_method_unique",
            ),
        ]

    def __str__(self) -> str:
        if self.org_pool_id is not None:
            return (
                f"SplitCounter[org] pool={self.org_pool_id} "
                f"org={self.organization_id} period={self.period} count={self.count}"
            )
        return (
            f"SplitCounter[method] method={self.payment_method_id} "
            f"account={self.processor_account_id} period={self.period} count={self.count}"
        )


# ---------------------------------------------------------------------------
# DecisionLog (revised — ADR-006-R §1.7)
# ---------------------------------------------------------------------------


class DecisionLog(models.Model):
    """
    Immutable audit log for each routing decision (ADR-006-R §1.7).

    One row is written per route_payment() call, regardless of outcome.
    Immutable once written — no update/delete in normal operation.

    order_id and store_id are soft references (plain integers) rather than FKs:
    the audit log must survive order/store deletion.

    evaluated_rules_json: full walk audit trail — rule IDs + verdicts, never buyer PII.
    over_cap: True when the chosen outcome was displaced by a monthly cap.
    parent_decision: for upsell child charges (ADR-007 §4).

    Indexes: order_id (db_index), decided_at (db_index), (reason, decided_at) composite.
    """

    REASON_CHOICES = [
        ("rule_match", "rule_match"),
        ("fallback_chain", "fallback_chain"),
        ("fallback_default_org", "fallback_default_org"),
        ("all_orgs_at_cap", "all_orgs_at_cap"),
        ("routing_failure", "routing_failure"),
        ("upsell_charge", "upsell_charge"),
    ]

    order_id = models.BigIntegerField(
        db_index=True,
        help_text="Soft reference to orders.Order.pk — not a FK so the log survives order deletion.",
    )
    store_id = models.IntegerField(
        db_index=True,
        help_text="Soft reference to stores.Store.pk — not a FK.",
    )
    decided_at = models.DateTimeField(
        auto_now_add=True,
        db_index=True,
    )
    evaluated_rules_json = models.JSONField(
        default=list,
        help_text=(
            "Full audit trail of rule evaluation: "
            "[{rule, stage, org, account, verdict}, ...]. No buyer PII."
        ),
    )
    chosen_organization = models.ForeignKey(
        Organization,
        null=True,
        on_delete=models.SET_NULL,
        related_name="routing_decisions",
        help_text="Organization whose account was selected. Null on routing failure.",
    )
    chosen_processor_account = models.ForeignKey(
        ProcessorAccount,
        null=True,
        on_delete=models.SET_NULL,
        related_name="routing_decisions",
        help_text="ProcessorAccount selected. Null on routing failure.",
    )
    matched_rule = models.ForeignKey(
        OrganizationRule,
        null=True,
        on_delete=models.SET_NULL,
        related_name="routing_decisions",
        help_text="Final stage-1 rule whose branch produced the outcome (after fallback walking).",
    )
    reason = models.CharField(
        max_length=40,
        choices=REASON_CHOICES,
        help_text="Closed vocabulary outcome reason.",
    )
    over_cap = models.BooleanField(
        default=False,
        help_text="True when the outcome was displaced by a monthly cap.",
    )
    buyer_country = models.CharField(
        max_length=2,
        blank=True,
        default="",
        help_text="Country granularity only — required to debug geography rules; not buyer PII.",
    )
    method_family = models.CharField(
        max_length=20,
        blank=True,
        default="",
    )
    parent_decision = models.ForeignKey(
        "self",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="child_decisions",
        help_text="Upsell child entries reference the original decision (ADR-007 §4).",
    )

    class Meta:
        verbose_name = "decision log"
        verbose_name_plural = "decision logs"
        ordering = ["-decided_at"]
        indexes = [
            models.Index(
                fields=["reason", "decided_at"],
                name="decisionlog_reason_decided_idx",
            ),
        ]

    def __str__(self) -> str:
        return f"DecisionLog(order={self.order_id}, reason={self.reason!r}, at={self.decided_at})"


# ---------------------------------------------------------------------------
# pre_save signal: OrgPoolMember percent-sum lint
# ---------------------------------------------------------------------------


@receiver(pre_save, sender=OrgPoolMember)
def _check_pool_percent_sum(sender, instance: OrgPoolMember, **kwargs) -> None:
    """
    Blocking pre_save signal: when the pool has an active percent_split rule,
    the sum of all member target_percent values (including this one) must equal 100.00.

    This enforces that an active percent_split pool is always fully allocated.
    Pool members must be configured (via bulk_create or before the rule is activated)
    so that the total is exactly 100% before any individual save is attempted with an
    active rule in place.

    Correct workflow:
      1. Create pool + members (no rule yet — signal skips the check).
      2. Activate the percent_split rule.
      3. Any subsequent member save that would break the 100% total is rejected.

    Runs for both create and update to prevent drift after editing individual members.
    """
    pool = instance.pool
    has_percent_rule = OrganizationRule.objects.filter(
        stage="stage2_allocation",
        allocation_type="percent_split",
        org_pool=pool,
        is_active=True,
    ).exists()
    if not has_percent_rule:
        return

    # Sum all existing members excluding the current instance (if updating).
    existing_qs = OrgPoolMember.objects.filter(pool=pool)
    if instance.pk is not None:
        existing_qs = existing_qs.exclude(pk=instance.pk)

    from django.db.models import Sum
    existing_total = existing_qs.aggregate(total=Sum("target_percent"))["total"] or 0
    new_total = existing_total + (instance.target_percent or 0)

    if round(float(new_total), 2) != 100.00:
        raise ValidationError(
            f"Pool '{pool}' has an active percent_split rule. "
            f"Sum of target_percent across all members must equal 100.00 "
            f"(current total after this save would be {new_total:.2f})."
        )
