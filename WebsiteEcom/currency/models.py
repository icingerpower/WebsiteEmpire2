"""
Currency display converter models (ADR-023, TICKET-031/TICKET-037).

Four entities, replacing the single placeholder `Currency [G]` row sketched in
`05_database_schema.md` §12 (ADR-023 §2/§7):

  CurrencyDefinition        [G] platform-global master data (super-admin owned,
                                 same tier as Theme/ShippingCarrier/ShippingZone —
                                 ADR-001 §3b). code/name/symbol/decimal_places.
  CurrencyRate              [G] platform-global, 1:1 with CurrencyDefinition.
                                 All rates anchored to EUR (rate_to_reference).
                                 EUR's own row is the anchor: rate=1.00000000,
                                 source='manual', never touched by the refresh task.
  StoreCurrencySetting      [S] store-scoped (StoreOwnedModel) — per-store display
                                 currency enablement checklist. The store's own
                                 `Store.default_currency` needs NO row here (ADR-023
                                 §2) — it is implicitly always available.
  CurrencyConverterSettings [G] platform-global singleton — auto-refresh toggle +
                                 rounding mode + staleness threshold.

Why platform-global rates, not per-store (ADR-023 §2): a EUR/JPY rate is a fact
about the world, not about a store. Per-store control is expressed only through
StoreCurrencySetting (enablement) and Store.default_currency (untouched by this
feature).

Why split CurrencyDefinition/CurrencyRate into two tables (ADR-023 §2): symbol/
placement/decimals change essentially never; the rate changes daily under
auto-refresh. Splitting means the refresh task only ever touches the narrow,
hot CurrencyRate table.
"""

from datetime import timedelta

from django.db import models, transaction
from django.utils import timezone

from core.models import StoreOwnedModel


class SymbolPlacement(models.TextChoices):
    PREFIX = "prefix", "Prefix (e.g. $45.99)"
    SUFFIX = "suffix", "Suffix (e.g. 45.99 kr)"


class CurrencyDefinition(models.Model):
    """
    Platform-global currency master data (ADR-023 §2, §3). Super-admin CRUD at
    `/superadmin/` only — never editable from a store's `/admin/`.

    decimal_places is REQUIRED for correct rounding (ADR-023 §2/§6): 2 for most
    currencies, 0 for JPY/KRW-style currencies. This is an ISO 4217 fact about
    the currency, not a per-store or per-rounding-mode choice.

    show_code/code_placement (ADR-023 §2 addendum, human decision 2026-07-11)
    restore the admin-011 screenshot's "Code visibility" control, independent
    of symbol_placement: a currency can show its symbol as a prefix and its
    ISO code as a suffix at the same time (e.g. "$45.99 CAD").

    Deactivating (is_active=False) removes a currency from every store's enable
    list without deleting history — StoreCurrencySetting rows referencing it are
    left in place (ADR-023 §3 "Risks").
    """

    code = models.CharField(
        max_length=3,
        unique=True,
        help_text="ISO 4217 currency code, e.g. 'USD', 'JPY'.",
    )
    name = models.CharField(max_length=64)
    symbol = models.CharField(max_length=8, blank=True, default="")
    symbol_placement = models.CharField(
        max_length=8,
        choices=SymbolPlacement.choices,
        default=SymbolPlacement.PREFIX,
    )
    decimal_places = models.PositiveSmallIntegerField(
        default=2,
        help_text=(
            "ISO 4217 minor-unit count. 2 for most currencies, 0 for JPY/KRW-style "
            "currencies (ADR-023 §2/§6, DECIDED 2026-07-10 — 0-decimal currencies "
            "round to whole units using the same psychological rule)."
        ),
    )
    show_code = models.BooleanField(
        default=False,
        help_text=(
            "Show the ISO code alongside the symbol, e.g. '45.99 $ CAD' "
            "(ADR-023 §2 addendum, human decision 2026-07-11 — restores the "
            "admin-011 screenshot's 'Code visibility' control)."
        ),
    )
    code_placement = models.CharField(
        max_length=8,
        choices=SymbolPlacement.choices,
        default=SymbolPlacement.SUFFIX,
        help_text="Where the ISO code renders relative to the symbol+amount, when show_code is on.",
    )
    is_active = models.BooleanField(default=True)

    class Meta:
        ordering = ["code"]
        verbose_name = "currency"
        verbose_name_plural = "currencies"

    def __str__(self):
        return self.code


class RateSourceChoices(models.TextChoices):
    MANUAL = "manual", "Manual entry"
    ECB = "api:ecb", "ECB daily reference-rate feed"


class CurrencyRate(models.Model):
    """
    Platform-global, 1:1 with CurrencyDefinition (ADR-023 §2). rate_to_reference
    is always expressed against the EUR anchor (matches the ECB feed's native
    anchor — zero cross-rate math for the auto path).

    previous_rate + last_refresh_error exist specifically to support the sanity-
    bound guard and the admin staleness/error dashboard (ADR-023 §1) — a rejected
    fetched value keeps the last-known-good rate and records why.
    """

    currency = models.OneToOneField(
        CurrencyDefinition,
        on_delete=models.CASCADE,
        related_name="rate",
    )
    rate_to_reference = models.DecimalField(
        max_digits=18,
        decimal_places=8,
        help_text="Rate vs the EUR anchor. EUR's own row is always 1.00000000.",
    )
    source = models.CharField(
        max_length=16,
        choices=RateSourceChoices.choices,
        default=RateSourceChoices.MANUAL,
    )
    updated_at = models.DateTimeField(auto_now=True)
    previous_rate = models.DecimalField(
        max_digits=18,
        decimal_places=8,
        null=True,
        blank=True,
        help_text="The rate this row held immediately before the last successful write.",
    )
    last_refresh_error = models.CharField(max_length=255, blank=True, default="")

    class Meta:
        ordering = ["currency__code"]
        verbose_name = "currency rate"
        verbose_name_plural = "currency rates"

    def __str__(self):
        return f"{self.currency.code} = {self.rate_to_reference}"

    @property
    def is_stale(self) -> bool:
        """
        True when this is an auto-fetched rate whose updated_at is older than
        CurrencyConverterSettings.stale_hours (ADR-023 §1).

        Manual rates NEVER auto-expire — the admin who entered them owns their
        freshness (ADR-023 §1: "only meaningful for source='api' rows").
        """
        if self.source == RateSourceChoices.MANUAL:
            return False
        stale_hours = CurrencyConverterSettings.get_solo().stale_hours
        return timezone.now() - self.updated_at > timedelta(hours=stale_hours)


class StoreCurrencySetting(StoreOwnedModel):
    """
    Per-store display-currency enablement checklist (ADR-023 §2/§3). Written
    only from a store's `/admin/` — never touches CurrencyDefinition/CurrencyRate.

    The store's own transactional currency (Store.default_currency) needs NO
    row here — it is implicitly always available and never itself "converted"
    (ADR-023 §2).
    """

    currency = models.ForeignKey(
        CurrencyDefinition,
        on_delete=models.PROTECT,
        related_name="store_settings",
    )
    is_enabled = models.BooleanField(default=True)

    class Meta:
        constraints = [
            models.UniqueConstraint(fields=["store", "currency"], name="unique_store_currency"),
        ]
        indexes = [models.Index(fields=["store", "currency"])]

    def __str__(self):
        return f"{self.store_id} / {self.currency_id}"


class RoundingMode(models.TextChoices):
    NONE = "none", "No rounding adjustment"
    PSYCHOLOGICAL_99 = "psychological_99", 'Psychological ("...99") rounding'


class CurrencyConverterSettings(models.Model):
    """
    Platform-global singleton (ADR-023 §2) — first singleton model in this
    codebase, established deliberately here rather than smuggling these fields
    onto CurrencyDefinition (per-row, not global) or into Django settings.py
    (which must be admin-editable per T037).

    Enforced singleton: pk is always pinned to 1; get_solo() is the only
    sanctioned way to fetch/create the row, guarded by select_for_update so
    concurrent callers never create a second row.
    """

    auto_refresh_enabled = models.BooleanField(
        default=False,
        help_text="ECB daily-feed auto-refresh. Off by default (ADR-023 §1).",
    )
    rounding_mode = models.CharField(
        max_length=20,
        choices=RoundingMode.choices,
        default=RoundingMode.PSYCHOLOGICAL_99,
    )
    stale_hours = models.PositiveIntegerField(
        default=48,
        help_text="Only meaningful for auto-fetched (source='api:ecb') rates.",
    )

    class Meta:
        verbose_name = "currency converter settings"
        verbose_name_plural = "currency converter settings"

    def __str__(self):
        return "Currency converter settings"

    def save(self, *args, **kwargs):
        # Enforced singleton (ADR-023 §2) — every save pins the row to pk=1.
        self.pk = 1
        super().save(*args, **kwargs)

    def delete(self, *args, **kwargs):
        # The singleton is never deleted.
        return

    @classmethod
    def get_solo(cls) -> "CurrencyConverterSettings":
        """
        Return the single CurrencyConverterSettings row, creating it with
        defaults on first call.

        select_for_update() inside the atomic block means two concurrent
        callers racing to create the row are serialized by the DB row lock —
        neither can create a second row (ADR-023 §2 "Tests required").
        """
        with transaction.atomic():
            obj, _created = cls.objects.select_for_update().get_or_create(pk=1)
        return obj


# EUR is always the reference/anchor currency (ADR-023 §2) — kept here as a
# named constant so callers never hardcode the literal string in more than
# one place.
REFERENCE_CURRENCY_CODE = "EUR"
