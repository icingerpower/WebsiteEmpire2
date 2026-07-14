"""
Stores app — Store, Organization, Theme, StoreEmployee models.

These are infrastructure/auth models (ADR-001) — they must NOT extend StoreOwnedModel.
StoreOwnedModel is for per-store business data (products, orders, customers).

Key design decisions:
- Organization is payment-routing only with NO config inheritance (ADR-001 §3b).
  Do not add settings, theme, or any inherited config to Organization.
- Theme is platform-global and owned by super-admin (ADR-001 §3b).
  Do not add an organization FK to Theme.
- StoreEmployee is the User × Store mapping — not store-owned data.
- Store.organization FK is nullable, loose grouping for payment routing only.
  A store's orders can still route to any eligible Organization (ADR-006).
"""

import re

from django.conf import settings
from django.core.exceptions import ValidationError
from django.core.validators import RegexValidator
from django.db import models, transaction
from django.db.models import Q
from django.utils import timezone

from core.models import StoreOwnedModel
from core.slugs import make_slug


# ---------------------------------------------------------------------------
# Theme — colour-token validation helpers
# ---------------------------------------------------------------------------

_HEX_RE = re.compile(r"^#[0-9a-fA-F]{6}$")

# Whitelisted color slot names accepted in StoreThemeCustomization.customization_json.
_ALLOWED_COLOR_SLOTS = frozenset(
    {"primary", "accent", "background", "surface", "text", "muted_text", "badge", "announcement"}
)


def validate_customization(theme, data: dict) -> list[str]:
    """
    Validate a StoreThemeCustomization.customization_json payload (ADR-012 D2, §XV-5).

    Returns a list of human-readable error messages (empty = valid).  Used by both the
    admin form and StoreThemeCustomization.clean() so there is exactly one validation path.

    Validated keys:
      preset          — string; must be a key in theme.tokens_json["presets"] (Phase 4).
      font_pair       — string; must be a key in theme.tokens_json["font_pairs"] (Phase 4).
      color_overrides — dict; slot names whitelisted, values must be 6-digit hex.
      structural      — dict; all values must be booleans.
      (unknown top-level keys are rejected.)

    Phase 1 note: preset/font_pair cross-validation against tokens.json is deferred to
    Phase 4 when the actual theme packages ship.
    """
    errors: list[str] = []

    allowed_keys = {"preset", "font_pair", "color_overrides", "structural"}
    unknown_keys = set(data.keys()) - allowed_keys
    for key in sorted(unknown_keys):
        errors.append(f"Unknown customization key: {key!r}.")

    # color_overrides
    color_overrides = data.get("color_overrides")
    if color_overrides is not None:
        if not isinstance(color_overrides, dict):
            errors.append("color_overrides must be an object.")
        else:
            for slot, value in color_overrides.items():
                if slot not in _ALLOWED_COLOR_SLOTS:
                    errors.append(
                        f"Unknown color slot {slot!r}. "
                        f"Allowed: {sorted(_ALLOWED_COLOR_SLOTS)}."
                    )
                elif not isinstance(value, str) or not _HEX_RE.match(value):
                    errors.append(
                        f"Color slot {slot!r} must be a 6-digit hex string (e.g. '#1a2b3c'); "
                        f"got {value!r}."
                    )

    # preset — string; Phase 4 will cross-validate against tokens.json
    preset = data.get("preset")
    if preset is not None and not isinstance(preset, str):
        errors.append("preset must be a string.")

    # font_pair — string; Phase 4 will cross-validate against tokens.json
    font_pair = data.get("font_pair")
    if font_pair is not None and not isinstance(font_pair, str):
        errors.append("font_pair must be a string.")

    # structural — dict of booleans
    structural = data.get("structural")
    if structural is not None:
        if not isinstance(structural, dict):
            errors.append("structural must be an object.")
        else:
            for toggle, val in structural.items():
                if not isinstance(val, bool):
                    errors.append(
                        f"Structural toggle {toggle!r} must be a boolean; got {type(val).__name__}."
                    )

    return errors


PERMISSION_LEVELS = ("full", "limited", "none")
PERMISSION_HIERARCHY = {level: rank for rank, level in enumerate(PERMISSION_LEVELS)}


class StoreManager(models.Manager):
    """
    Manager for Store.  Provides .active() to exclude soft-deleted stores.
    Not a StoreScopedManager — Store itself is not store-owned.
    """

    def active(self):
        """Returns only stores that have not been soft-deleted."""
        return self.get_queryset().filter(deleted_at__isnull=True)


class Organization(models.Model):
    """
    Legal entity for payment routing (ADR-001 §3b, ADR-006).

    Organization owns exactly two resource families:
    - ProcessorAccount(s)
    - Payment RoutingRule(s) (OrganizationRule, pools, split counters)

    Nothing else hangs off Organization.  Themes, shipping zones, email templates,
    and campaign templates are platform-global, owned by super-admin — never by Org.

    Exactly one Organization may be the platform default, enforced via partial unique
    constraint at the DB level.  The default org receives orders when all routing options
    are exhausted (ADR-006 §1: never silently reject a paying customer).
    """

    name = models.CharField(max_length=255)
    is_default = models.BooleanField(
        default=False,
        help_text=(
            "Exactly one Organization may be the platform default. "
            "Enforced by a partial unique index."
        ),
    )

    # --- Routing identity fields (ADR-006-R §1.1) ---
    legal_name = models.CharField(
        max_length=255,
        blank=True,
        default="",
        help_text="Legal entity name. 'name' remains the internal admin label.",
    )
    display_name = models.CharField(
        max_length=255,
        blank=True,
        default="",
        help_text="Buyer-facing name shown on receipts and emails.",
    )
    registration_country = models.CharField(
        max_length=2,
        blank=True,
        default="",
        help_text="ISO 3166-1 alpha-2 registration country code.",
    )
    settlement_currencies = models.JSONField(
        default=list,
        help_text="List of ISO 4217 codes this org settles in, e.g. ['EUR', 'USD'].",
    )
    coverage_areas_json = models.JSONField(
        default=list,
        blank=True,
        help_text=(
            "Informational area tokens (EU, ROW, country codes) for the super-admin UI. "
            "Routing reads rule conditions_json, not this field."
        ),
    )
    statement_descriptor = models.CharField(
        max_length=22,
        blank=True,
        default="",
        help_text="Card-statement text. Processor maximum is 22 characters.",
    )

    # --- Monthly cap fields (ADR-006-R §1.1) ---
    # monthly_threshold: NULL = no cap. Default org MUST have NULL (DB CheckConstraint
    # 'organization_default_no_cap').  current_month_volume is incremented via F()
    # expression at webhook confirmation — never read-modify-write.
    monthly_threshold = models.DecimalField(
        max_digits=12,
        decimal_places=2,
        null=True,
        blank=True,
        help_text=(
            "Monthly volume cap. NULL = no cap. "
            "The default org must always be NULL (enforced by DB constraint)."
        ),
    )
    current_month_volume = models.DecimalField(
        max_digits=12,
        decimal_places=2,
        default=0,
        help_text="Running volume for volume_month. Incremented via F() at auth confirmation.",
    )
    volume_month = models.DateField(
        null=True,
        blank=True,
        help_text="First day of the month covered by current_month_volume.",
    )

    # --- Lifecycle status (ADR-006-R §1.1) ---
    status = models.CharField(
        max_length=10,
        choices=[("active", "active"), ("draft", "draft")],
        default="draft",
        help_text="Draft orgs are excluded from routing entirely.",
    )

    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "organization"
        verbose_name_plural = "organizations"
        constraints = [
            models.UniqueConstraint(
                fields=["is_default"],
                condition=Q(is_default=True),
                name="unique_default_organization",
            ),
            # The default org is the uncapped catch-all (AF-C6).
            # Enforcing NULL monthly_threshold at the DB level prevents silent cap misconfig.
            models.CheckConstraint(
                condition=Q(is_default=False) | Q(monthly_threshold__isnull=True),
                name="organization_default_no_cap",
            ),
        ]

    def __str__(self):
        return self.display_name or self.name


class PhoneMode(models.TextChoices):
    """
    Controls the phone field at checkout (CO-003).

    HIDDEN   — field not shown (no phone collected).
    OPTIONAL — field shown, not required (default).
    REQUIRED — field shown and must be filled.
    """

    HIDDEN = "hidden", "Hidden"
    OPTIONAL = "optional", "Optional"
    REQUIRED = "required", "Required"


class ThemeEngine(models.TextChoices):
    """
    Theme rendering engine (ADR-012 D2).

    CODE           — Django Template Language (DTL) themes under themes/.
    VISUAL_BUILDER — Planned P3 visual editor; no Phase 1 rows use this.
    """

    CODE = "code", "Code theme (DTL)"
    VISUAL_BUILDER = "visual_builder", "Visual builder"


class Theme(models.Model):
    """
    Platform-level theme registry row (ADR-012 D2, ADR-001 §3b).

    Themes are owned by the super-admin, never by an Organization.

    source_ref is the directory key under themes/ (e.g. "b2b" → themes/b2b/).
    It is unique and used as the CSS/template namespace — it must never change once
    theme data exists.

    is_default: exactly one Theme may be the platform default (enforced by partial
    unique constraint).  Stores without an active published theme fall back here.
    The default is the "general" theme seeded by the 0010 data migration.

    Theme defaults (palette presets, font pairs, token values) live in
    themes/<source_ref>/tokens.json, NOT in a DB column (ADR-012 D2: DB copy drifts
    against deployed code — invisible-failure class).

    status=draft rows are not activatable by stores (TH-007).
    """

    name = models.CharField(max_length=255)
    engine = models.CharField(
        max_length=20,
        choices=ThemeEngine.choices,
        default=ThemeEngine.CODE,
    )
    source_ref = models.CharField(
        max_length=64,
        unique=True,
        help_text="Theme package directory key, e.g. 'b2b' → themes/b2b/.",
    )
    is_default = models.BooleanField(
        default=False,
        help_text=(
            "Exactly one Theme may be the platform default. "
            "Enforced by a partial unique index."
        ),
    )
    status = models.CharField(
        max_length=20,
        choices=[("draft", "Draft"), ("published", "Published")],
        default="draft",
        help_text="Draft themes are not activatable by stores.",
    )
    thumbnail = models.ImageField(
        upload_to="themes/thumbnails/",
        null=True,
        blank=True,
        help_text="Library card image shown in the theme picker.",
    )
    is_active = models.BooleanField(default=True)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "theme"
        verbose_name_plural = "themes"
        constraints = [
            models.UniqueConstraint(
                fields=["is_default"],
                condition=Q(is_default=True),
                name="one_default_theme",
            ),
        ]

    def __str__(self):
        return self.name


class StoreThemeCustomization(StoreOwnedModel):
    """
    Per-store customization of a specific theme (ADR-012 D2, TH-021).

    Extends StoreOwnedModel (store FK + StoreScopedManager).  The theme FK is
    added here, giving the compound (store, theme) identity.

    Keyed on (store, theme) — one row per theme a store has ever activated.
    Switching back to a previously used theme restores this record automatically
    (never deleted on theme switch — TH-021).

    customization_json schema (validated at the persistence boundary by
    validate_customization() — §XV-5):
      preset          — key in the theme's tokens.json["presets"]
      font_pair       — key in the theme's tokens.json["font_pairs"]
      color_overrides — dict of whitelisted slot→hex overrides
      structural      — dict of boolean toggles (sticky_header, breadcrumbs, etc.)

    Media fields (logo, favicon, hero_images) live in customization_json as T003
    media refs — not dedicated DB columns — so the T003 pipeline handles resizing,
    CDN URLs, and per-language variants without model changes.

    Query pattern: always use .for_store(store).filter(theme=theme).first() — the
    StoreScopedManager inherited from StoreOwnedModel enforces tenant isolation.
    """

    theme = models.ForeignKey(
        Theme,
        on_delete=models.CASCADE,
        related_name="customizations",
    )
    customization_json = models.JSONField(
        default=dict,
        help_text=(
            "Validated by validate_customization() on save. "
            "Schema: {preset, font_pair, color_overrides, structural}."
        ),
    )
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "store theme customization"
        verbose_name_plural = "store theme customizations"
        constraints = [
            models.UniqueConstraint(
                fields=["store", "theme"],
                name="uniq_customization_per_store_theme",
            ),
        ]

    def __str__(self):
        return f"customization for store={self.store_id} theme={self.theme_id}"

    def clean(self):
        """
        Validate customization_json at the persistence boundary (§XV-5).
        Errors are raised as a ValidationError so they surface in admin forms.
        """
        errors = validate_customization(self.theme, self.customization_json or {})
        if errors:
            raise ValidationError({"customization_json": errors})


class Store(models.Model):
    """
    One storefront: one primary language, one domain or subdomain, one catalog (ADR-001 §1).

    Subdomains are always lower-case ASCII; populated via core.slugs.make_slug.
    The organization FK is nullable and purely a grouping/filter dimension — no config
    is inherited from Organization to Store (ADR-001 §3b).

    Soft-delete: call soft_delete() instead of delete() to retain historical data.
    Hard delete is blocked by on_delete=PROTECT on child StoreOwnedModel rows.
    """

    name = models.CharField(max_length=255)
    subdomain = models.SlugField(
        max_length=255,
        unique=True,
        help_text=(
            "Always lower-case ASCII. Populated via core.slugs.make_slug. "
            "Deprecated: use StoreDomain/StoreLanguage instead. "
            "Will be removed after migration is complete."
        ),
    )
    custom_domain = models.CharField(
        max_length=255,
        blank=True,
        default="",
        help_text=(
            "Deprecated: use StoreDomain/StoreLanguage instead. "
            "Will be removed after migration is complete."
        ),
    )
    primary_language = models.CharField(
        max_length=10,
        default="en",
        help_text=(
            "ISO 639-1 language code. "
            "Deprecated: use StoreDomain/StoreLanguage instead. "
            "Will be removed after migration is complete."
        ),
    )
    timezone = models.CharField(max_length=64, default="UTC")
    default_currency = models.CharField(
        max_length=3,
        default="USD",
        help_text="ISO 4217 currency code.",
    )
    theme = models.ForeignKey(
        Theme,
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="stores",
    )
    organization = models.ForeignKey(
        Organization,
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="stores",
        help_text=(
            "Optional grouping under an Organization for payment routing filters. "
            "Carries no config inheritance — orders can still route to any eligible Org."
        ),
    )
    checkout_phone_mode = models.CharField(
        max_length=10,
        choices=PhoneMode.choices,
        default=PhoneMode.OPTIONAL,
        help_text=(
            "Controls the phone field at checkout (CO-003). "
            "hidden = not shown; optional = shown but not required (default); "
            "required = shown and must be filled."
        ),
    )
    review_request_delay_days = models.PositiveSmallIntegerField(
        default=7,
        help_text="Days after shipment before sending a review-request email.",
    )
    password_protection = models.BooleanField(
        default=False,
        help_text=(
            "When True, the storefront requires a password before visitors "
            "can browse.  Used for pre-launch or private storefronts."
        ),
    )
    is_active = models.BooleanField(default=True)
    deleted_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Set by soft_delete(); null means not deleted.",
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    objects = StoreManager()

    class Meta:
        verbose_name = "store"
        verbose_name_plural = "stores"

    def __str__(self):
        return f"{self.name} ({self.subdomain})"

    def soft_delete(self):
        """
        Soft-delete this store.  Sets deleted_at and deactivates the store.
        Does NOT hard-delete; historical data is retained.
        Use StoreManager.active() to exclude soft-deleted stores from queries.

        Also deactivates all StoreDomain rows for this store so that request routing
        stops immediately — defense-in-depth alongside the store__deleted_at__isnull
        filter in resolve_locale() (ADR-008 §2a step 2).
        """
        self.deleted_at = timezone.now()
        self.is_active = False
        self.save(update_fields=["deleted_at", "is_active", "updated_at"])
        # ADR-008: cascade to domains to stop routing immediately.
        self.domains.update(is_active=False)


class StoreEmployee(models.Model):
    """
    User × Store membership with per-module permission matrix (ADR-001 §3,
    frozen vocabulary + structural enforcement in ADR-033).

    A user can be admin of multiple stores.  Each (user, store) pair has exactly one
    StoreEmployee row holding the 19-module permission matrix (stores.modules.MODULES)
    in permissions_json.

    permissions_json structure:
        {"orders": "full"|"limited"|"none", "products": "full"|"none", ...}
    Keys/levels are validated against stores.modules.MODULES at the persistence
    boundary (clean()/save(), ADR-033 D1d) — unknown keys and illegal levels
    for a given module are rejected loudly.

    full_access = True is a master override that bypasses all permissions_json checks.

    "Orders limited access" defaults to view-only (no refunds, no status changes).
    Full permission matrix grid UI ships in TICKET-047 (stores/admin.py's
    StoreEmployeeAdminForm).

    Invite flow (AF-108/ADR-033 D4b): invited_at is set on creation; accepted_at
    is set when the invitee completes the set-password link. invited_name/
    invited_phone are informational, stored here (never on User — see their
    field docstrings). last_login_at is updated on each /admin/ login for this store.
    """

    user = models.ForeignKey(
        settings.AUTH_USER_MODEL,
        on_delete=models.CASCADE,
        related_name="store_memberships",
    )
    store = models.ForeignKey(
        Store,
        on_delete=models.CASCADE,
        related_name="employees",
    )
    full_access = models.BooleanField(
        default=False,
        help_text="Master override: bypasses all permissions_json module checks.",
    )
    permissions_json = models.JSONField(
        default=dict,
        help_text=(
            "19-module permission matrix (stores.modules.MODULES). "
            'Keys are module names; values are "full", "limited", or "none" '
            "(only Orders allows \"limited\" — ADR-033 D1a). "
            "Ignored when full_access is True."
        ),
    )
    invited_at = models.DateTimeField(auto_now_add=True)
    accepted_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Set when the user accepts the store invitation.",
    )
    last_login_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Updated on each /admin/ login for this store.",
    )
    is_active = models.BooleanField(default=True)

    # ADR-033 D4b/D5 (TICKET-047 invite flow): stored on StoreEmployee, NEVER
    # on User — a store admin must not gain a write surface onto a shared
    # User row that other stores' memberships also read (same isolation
    # instinct as ADR-001 §4). Informational only; phone 2FA is out of scope
    # (AF-002 step 4) — DECIDED, human 2026-07-11, per ADR-033 D7 item 5.
    invited_name = models.CharField(max_length=255, blank=True, default="")
    invited_phone = models.CharField(max_length=32, blank=True, default="")

    class Meta:
        verbose_name = "store employee"
        verbose_name_plural = "store employees"
        constraints = [
            models.UniqueConstraint(
                fields=["user", "store"],
                name="unique_store_employee",
            ),
        ]

    #: Transient (non-persisted) actor context, threaded in by the store-site
    #: admin form BEFORE full_clean() runs (see stores/admin.py's
    #: _ActingUserFormMixin) so clean() can enforce ADR-033 D5a's no-self-
    #: escalation guardrail. None for every non-admin write path (shell,
    #: migrations, signals, the super-admin form) — those are either trusted
    #: or exempt (super-admins bypass the guard entirely; see
    #: _validate_no_self_escalation).
    _acting_user = None

    #: Transient escape hatch for the super-admin site ONLY (ADR-033 D5c):
    #: "the super site ALLOWS these operations with a messages.warning — a
    #: super-admin can always re-grant; hard-blocking the control plane
    #: creates unfixable states." Set by
    #: stores.admin.StoreEmployeeSuperAdminAdmin.save_model() before saving;
    #: never set anywhere on the store site, so the lockout guard stays
    #: strict there.
    _allow_lockout_transition = False

    def __str__(self):
        return f"{self.user} @ {self.store}"

    def clean(self):
        super().clean()
        self._validate_permissions_json()
        self._validate_no_self_escalation()
        self._validate_lockout_guard()

    def save(self, *args, **kwargs):
        """
        ADR-033 D1d: a model-level guard for non-form writes (shell,
        migrations, signals) that never go through StoreEmployeeAdminForm's
        full_clean() call. Re-runs the same three checks clean() runs so
        that unknown/illegal permissions_json content, self-escalation, and
        the last-full-access lockout are rejected loudly (§XV-1) regardless
        of write path — not just when writing through the admin.

        The whole method runs inside transaction.atomic() so that, when
        _validate_lockout_guard() escalates to select_for_update() (see its
        docstring), the resulting row lock is held all the way through the
        actual write below (super().save()) rather than being released the
        instant the check function returns — required for the guard to be
        race-free (§XIII) rather than just a same-request sanity check.
        """
        self._validate_permissions_json()
        self._validate_no_self_escalation()
        with transaction.atomic():
            self._validate_lockout_guard()
            super().save(*args, **kwargs)

    def _validate_permissions_json(self):
        """
        ADR-033 D1d: reject unknown module keys and illegal levels in
        permissions_json at the persistence boundary (§XV-5) — loud failure,
        not silent key-dropping (§XV-1).
        """
        from stores.modules import MODULES_BY_KEY

        errors = []
        for key, value in (self.permissions_json or {}).items():
            module = MODULES_BY_KEY.get(key)
            if module is None:
                errors.append(f"Unknown permission module key: {key!r}.")
                continue
            if value not in module.levels:
                errors.append(
                    f"Module {key!r} does not allow level {value!r} "
                    f"(allowed: {', '.join(module.levels)})."
                )
        if errors:
            raise ValidationError({"permissions_json": errors})

    def _validate_no_self_escalation(self):
        """
        ADR-033 D5a: an employee can never change full_access,
        permissions_json, or is_active on their OWN StoreEmployee row via the
        store site. Super-admins are exempt (they bypass the matrix anyway
        and the super site is the recovery path). No-op when no actor is
        known (self._acting_user is None) or on creation (self.pk is None —
        there is no prior row to have escalated FROM).
        """
        if self._acting_user is None:
            return
        if getattr(self._acting_user, "is_super_admin", False):
            return
        if not self.pk or self.user_id != getattr(self._acting_user, "pk", None):
            return
        try:
            original = StoreEmployee.objects.get(pk=self.pk)
        except StoreEmployee.DoesNotExist:
            return
        changed = [
            field
            for field in ("full_access", "permissions_json", "is_active")
            if getattr(original, field) != getattr(self, field)
        ]
        if changed:
            raise ValidationError(
                "You cannot change your own "
                f"{', '.join(changed)} on this store's admin site — ask "
                "another admin or a super-admin to make this change."
            )

    def _validate_lockout_guard(self):
        """
        ADR-033 D5b: a store must never transition from >=1 to 0 ACTIVE,
        full_access employees through store-site actions. Blocks demote
        (full_access True->False) and deactivate (is_active True->False) on
        the last such row.

        Deletion is guarded separately (would_be_last_active_full_access(),
        used by StoreEmployeeStoreAdminAdmin.delete_model/delete_queryset)
        because Model.delete() does not call clean()/save().

        Two-phase check: a cheap, unlocked read first decides whether THIS
        save is even a candidate transition (was qualifying, would stop
        qualifying) — the overwhelmingly common case (bumping last_login_at,
        editing an unrelated field, promoting someone) returns here without
        ever taking a lock, so normal traffic on a store's employee rows
        does not serialize against this guard. Only a genuine demote/
        deactivate escalates to select_for_update() for the race-free
        recheck (§XIII): that SELECT ... FOR UPDATE locks the WHOLE
        qualifying set in the store — INCLUDING self's own row, which in the
        database still holds its pre-save full_access=True/is_active=True
        values at the moment of the SELECT. This is deliberate: locking only
        the *other* qualifying rows (excluding self from the lock itself)
        would let two concurrent demotes of the last two full-access
        employees each lock the row the OTHER transaction is demoting —
        disjoint row sets never contend, select_for_update never serializes
        them, and both saves observe "another qualifying row exists" and
        commit, leaving zero full-access employees (this was finding F-1 in
        docs/security/WAVE4_AUDIT.md). Including self in the locked set
        means both transactions always overlap on at least one common row,
        so one blocks until the other commits (or vice versa) — whichever
        commits first determines which save sees zero remaining *other*
        qualifying rows (self excluded only from that count, not the lock)
        and is rejected. save() wraps this whole method in
        transaction.atomic(), so the lock survives through the actual
        write, not just this check.

        On backends without row-level locking (e.g. SQLite, used in this
        project's test settings) select_for_update() is a documented no-op —
        the check is still correct for a single request, just not fully
        race-proof there; production uses a backend that supports it.

        _allow_lockout_transition (D5c) skips the raise entirely — set only
        by the super-admin site, which allows the transition with a warning
        instead of blocking it.
        """
        if self._allow_lockout_transition:
            return
        if not self._is_candidate_lockout_transition():
            return
        with transaction.atomic():
            # Lock the FULL qualifying set, including self: a concurrent
            # demote of a different row in the same "last two" must contend
            # on a shared locked row set to serialize (see docstring above
            # and the F-1 finding in docs/security/WAVE4_AUDIT.md). Locking
            # only "other" rows (an .exclude(pk=self.pk) on the locking query
            # itself) lets two concurrent demotes of the last two full-access
            # employees each lock the row the other is demoting — disjoint
            # row sets never serialize, and both saves see "another
            # qualifying row exists", so both commit and the store ends with
            # zero full-access employees. self.pk is included in this
            # locked queryset (it is still full_access=True/is_active=True
            # in the DB at this point — the in-memory self has the new,
            # not-yet-saved values), so both transactions lock overlapping
            # rows and one blocks until the other commits.
            locked_qualifying = list(
                StoreEmployee.objects.select_for_update().filter(
                    store_id=self.store_id, full_access=True, is_active=True
                )
            )
            other_qualifying = any(row.pk != self.pk for row in locked_qualifying)
        if not other_qualifying:
            raise ValidationError(
                "This store must always keep at least one active, "
                "full-access employee. Promote or activate another "
                "employee before changing this one."
            )

    def _is_candidate_lockout_transition(self):
        """
        Cheap, unlocked pre-check: True if saving self right now would flip
        it from active-full_access to not (the only transition
        _validate_lockout_guard ever blocks). False for every other save —
        including creation, promotions, and unrelated field edits — so those
        never pay for a lock (ADR-033 D5b).
        """
        if not self.pk or self.store_id is None:
            return False
        try:
            original = StoreEmployee.objects.get(pk=self.pk)
        except StoreEmployee.DoesNotExist:
            return False
        was_qualifying = original.full_access and original.is_active
        still_qualifying = self.full_access and self.is_active
        return was_qualifying and not still_qualifying

    def is_last_qualifying_transition(self):
        """
        True if saving self right now would flip it from active-full_access
        to not, AND no other active full_access row exists for the store —
        i.e. the transition _validate_lockout_guard would normally block.

        Used by StoreEmployeeSuperAdminAdmin (ADR-033 D5c) to decide whether
        to show the "you just zeroed out full-access employees" warning —
        the super site sets _allow_lockout_transition=True so the guard
        itself never raises, but the warning should still fire. Not used for
        enforcement (see _validate_lockout_guard); this is a plain, unlocked
        read, adequate for a UX message.
        """
        if not self._is_candidate_lockout_transition():
            return False
        return not (
            StoreEmployee.objects.filter(
                store_id=self.store_id, full_access=True, is_active=True
            )
            .exclude(pk=self.pk)
            .exists()
        )

    def would_be_last_active_full_access(self):
        """
        True if this row is CURRENTLY the only active, full_access=True
        StoreEmployee for its store — i.e. deleting it would leave zero
        (ADR-033 D5b). Used by StoreEmployeeStoreAdminAdmin.delete_model/
        delete_queryset, which acquire their own select_for_update() lock on
        the store's employee rows before calling this (same §XIII pattern as
        _validate_lockout_guard).
        """
        if self.store_id is None or not (self.full_access and self.is_active):
            return False
        return not (
            StoreEmployee.objects.filter(
                store_id=self.store_id, full_access=True, is_active=True
            )
            .exclude(pk=self.pk)
            .exists()
        )

    def has_module_access(self, module: str, level: str = "limited") -> bool:

        """
        Returns True if this employee can access the given module at the given level.

        Hierarchy: full > limited > none.
        "Orders limited access" defaults to view-only (no refunds, no status changes).

        Args:
            module: Module name matching a key in permissions_json (e.g. "orders").
            level:  Minimum required level — "full", "limited", or "none".

        Returns True when:
            - full_access is True (master override), OR
            - The stored level for the module is >= the required level in the hierarchy.
        """
        if self.full_access:
            return True
        stored_level = self.permissions_json.get(module, "none")
        stored_rank = PERMISSION_HIERARCHY.get(stored_level, PERMISSION_HIERARCHY["none"])
        required_rank = PERMISSION_HIERARCHY.get(level, PERMISSION_HIERARCHY["none"])
        return stored_rank <= required_rank


class StoreDomain(models.Model):
    """
    A hostname registered for a store (ML-001/ML-002, ADR-008 §1).

    host is globally unique — request routing maps Host header → exactly one store.
    Always stored lowercase, no scheme, no port, no trailing dot (normalised in save()).
    A store's platform subdomain (e.g. "mystore.webecom.local") is ALSO a StoreDomain
    row, created by the ADR-008 data migration, so that request resolution has ONE mechanism.

    is_primary: the store's canonical domain. Exactly one per store (partial unique constraint).
    Used as the hreflang x-default host when the default language lives on it, and as
    the target of parked-domain redirects (SlugRedirect trigger=domain_park).

    The domain's default language (ML-002) is NOT a column here — it is the unique
    StoreLanguage on this domain with use_path_prefix=False (see ADR-008 Options B).
    """

    store = models.ForeignKey(
        "stores.Store",
        on_delete=models.CASCADE,
        related_name="domains",
    )
    host = models.CharField(
        max_length=253,  # RFC 1035 FQDN limit
        unique=True,
        help_text="Fully qualified hostname served by this store (e.g. shop.example.com).",
    )
    is_primary = models.BooleanField(
        default=False,
        help_text="The store's canonical domain. Exactly one per store.",
    )
    is_active = models.BooleanField(default=True)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "store domain"
        verbose_name_plural = "store domains"
        constraints = [
            models.UniqueConstraint(
                fields=["store"],
                condition=Q(is_primary=True),
                name="unique_primary_domain_per_store",
            ),
        ]

    def __str__(self):
        return self.host

    def save(self, *args, **kwargs):
        # Normalise host: lowercase, strip scheme if accidentally included,
        # strip port suffix (e.g. ":8001"), strip trailing dot.
        host = self.host or ""
        # Drop scheme — defensive (host field must never include "http://")
        if "://" in host:
            host = host.split("://", 1)[1]
        # Strip port
        host = host.split(":")[0]
        # Lowercase and strip trailing dot
        self.host = host.lower().rstrip(".")
        super().save(*args, **kwargs)

    def default_language(self):
        """
        The StoreLanguage served at this domain's root (ML-002), or None if misconfigured.

        None is a launch-blocking misconfiguration (settings validation), never silent.
        """
        return self.languages.filter(use_path_prefix=False).first()


class StoreLanguage(models.Model):
    """
    One (store, language) pair with its routing configuration (ML-005, ADR-008 §1).

    Because (store, lang_code) is unique and each row binds to exactly one domain,
    a StoreLanguage row IS the (domain, language) pair of ML-003 — target countries
    (ShippingCountry) and feed matrices hang off this row.

    use_path_prefix:
      False → served at the domain root ('/'): this IS the domain's default language
              (ML-002). At most one per domain (DB partial unique constraint).
      True  → served under '/<lang_code>/' on the domain (e.g. '/fr/').

    is_default: the store's default language — hreflang x-default target (CAN-006).
    Exactly one per store (partial unique + launch-readiness check for existence).

    is_enabled: disabling (never deleting) a published language serves 410 Gone for
    its whole URL namespace and removes it from sitemap/hreflang/feeds (ML-012).
    Redirect entries are kept.

    Routability (ML-011) is NOT this model's job: a language being enabled exposes
    nothing by itself — only 'published' translation records make URLs resolve.
    """

    LANG_CODE_VALIDATOR = RegexValidator(
        r"^[a-z]{2}(-[a-z]{2})?$",
        "Lowercase ISO 639-1 code, optionally with a region suffix — e.g. 'en', 'pt-br'.",
    )

    store = models.ForeignKey(
        "stores.Store",
        on_delete=models.CASCADE,
        related_name="languages",
    )
    domain = models.ForeignKey(
        StoreDomain,
        on_delete=models.PROTECT,
        related_name="languages",
        help_text="The domain this language edition is served from.",
    )
    lang_code = models.CharField(
        max_length=10,
        validators=[LANG_CODE_VALIDATOR],
        help_text="ISO 639-1 code — doubles as the URL path prefix when use_path_prefix=True.",
    )
    use_path_prefix = models.BooleanField(
        help_text=(
            "False = served at the domain root (this is the domain's default language, ML-002). "
            "True = served under /<lang_code>/ on the domain."
        ),
    )
    is_default = models.BooleanField(
        default=False,
        help_text="The store's default language (hreflang x-default). Exactly one per store.",
    )
    is_enabled = models.BooleanField(
        default=True,
        help_text="Disabled languages serve 410 Gone (ML-012); rows are never deleted.",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "store language"
        verbose_name_plural = "store languages"
        constraints = [
            models.UniqueConstraint(
                fields=["store", "lang_code"],
                name="unique_lang_per_store",
            ),
            models.UniqueConstraint(
                fields=["domain"],
                condition=Q(use_path_prefix=False),
                name="one_root_language_per_domain",
            ),
            models.UniqueConstraint(
                fields=["store"],
                condition=Q(is_default=True),
                name="one_default_language_per_store",
            ),
        ]

    def __str__(self):
        return f"{self.lang_code} @ {self.domain_id}"

    def clean(self):
        # Cross-tenant guard: the domain must belong to the same store.
        # Not expressible as a DB constraint (requires a join) — enforced here and
        # covered by a dedicated test (invisible-failure class, §XV-1).
        if self.domain_id and self.store_id:
            # Access domain.store_id directly if domain is already fetched,
            # otherwise query to avoid an extra attribute access on a potentially
            # unsaved object.
            try:
                domain_store_id = self.domain.store_id
            except StoreDomain.DoesNotExist:
                domain_store_id = None
            if domain_store_id is not None and domain_store_id != self.store_id:
                raise ValidationError("domain must belong to the same store.")


class ShippingCountry(models.Model):
    """
    A target country for a (store, language) pair — ML-003, ADR-008 §1.

    Drives: shopping feed matrix (one feed per provider × country × language,
    FEED-001/ML-030) and hreflang availability (CAN-004: hreflang emitted only for
    translated ∩ targeted). Countries NEVER appear in URLs (ML-003).

    NOT the shipping-rates model: operational shippability/rates come from shipping
    zones (TICKET-032). This is the admin's declared target-market list (ML-010c).
    A settings-validation check warns when a target country has no shipping-zone
    coverage (divergence is visible, never silent — ADR-008 Risks).
    """

    store_language = models.ForeignKey(
        StoreLanguage,
        on_delete=models.CASCADE,
        related_name="shipping_countries",
    )
    country_code = models.CharField(
        max_length=2,
        validators=[RegexValidator(r"^[A-Z]{2}$", "ISO 3166-1 alpha-2, uppercase.")],
        help_text="ISO 3166-1 alpha-2 — e.g. 'US', 'FR'.",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "shipping country"
        verbose_name_plural = "shipping countries"
        constraints = [
            models.UniqueConstraint(
                fields=["store_language", "country_code"],
                name="unique_country_per_store_language",
            ),
        ]

    def __str__(self):
        return f"{self.country_code} ({self.store_language_id})"
