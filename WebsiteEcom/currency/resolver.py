"""
The single resolution function for currency display conversion (ADR-023 §2,
§XV-4 design-pattern-ideas: "exactly one function answers the question").

display_amount() is the ONLY place multiplication happens for display-currency
conversion. Every caller — the T031 template filter (currency/templatetags/
currency_tags.py), the future T033 per-country catalog feed converter
(FEED-C1), and any future report — calls this function. No second copy of the
cross-rate formula anywhere in the codebase.

apply_rounding() is the single deterministic rounding function (ADR-023 §6),
used by display_amount() and nowhere else directly (callers never round on
their own).
"""

from decimal import ROUND_CEILING, ROUND_HALF_UP, Decimal

from currency.models import (
    REFERENCE_CURRENCY_CODE,
    CurrencyConverterSettings,
    CurrencyDefinition,
    CurrencyRate,
    RoundingMode,
)


def _smallest_unit(decimal_places: int) -> Decimal:
    """Return the smallest displayable unit for a currency, e.g. 0.01 or 1."""
    return Decimal(1).scaleb(-decimal_places)


def apply_rounding(raw: Decimal, currency_def: CurrencyDefinition, rounding_mode: str) -> Decimal:
    """
    Deterministic rounding (ADR-023 §6). All arithmetic is Decimal, never float.

    rounding_mode='none': plain rounding to currency_def.decimal_places
    (ROUND_HALF_UP).

    rounding_mode='psychological_99' (default): let u = smallest unit
    (0.01 for 2-decimal currencies, 1 for 0-decimal currencies like JPY/KRW —
    DECIDED 2026-07-10: the SAME generic rule applies at whole-unit granularity,
    no separate per-locale convention). Then:
        if raw <= u: return raw quantized to u   (zero/near-zero guard below)
        whole = ceil(raw) to the nearest whole unit
        return whole - u

    Zero/near-zero guard: without the `raw <= u` branch, a $0.00 free line item
    (buy-X-get-X-free, or a 100%-discounted order bump) would ceiling to 0 and
    then go NEGATIVE (-u) — an invisible failure (§XV-1) that would only ever
    surface as a shopper support ticket about a negative price on a free gift.
    """
    u = _smallest_unit(currency_def.decimal_places)

    if rounding_mode == RoundingMode.NONE:
        return raw.quantize(u, rounding=ROUND_HALF_UP)

    # psychological_99 (default)
    if raw <= u:
        return raw.quantize(u, rounding=ROUND_HALF_UP)
    whole = raw.quantize(Decimal(1), rounding=ROUND_CEILING)
    return (whole - u).quantize(u, rounding=ROUND_HALF_UP)


def _rate_for(code: str) -> Decimal:
    """
    Look up the EUR-anchored rate for a currency code.

    Raises CurrencyRate.DoesNotExist if no rate row exists yet — this must
    never happen for a currency actually offered to a shopper: StoreCurrencySetting
    enablement and the admin form are the gate that keeps un-rated currencies
    out of the picker (loud failure here is a data/config bug, not a shopper-
    facing scenario — see currency/templatetags/currency_tags.py for the
    template-layer fallback that keeps a misconfiguration from 500ing a page).
    """
    return CurrencyRate.objects.select_related("currency").get(currency__code=code).rate_to_reference


def display_amount(base_amount: Decimal, store_currency: str, display_currency: str) -> Decimal:
    """
    Convert base_amount (in store_currency) to display_currency (ADR-023 §2, §XV-4).

    Identity path: if display_currency == store_currency, returns base_amount
    completely unchanged — no rate lookup, no rounding pass. This means a
    currency with zero CurrencyRate rows still lets a shopper see prices in
    their own store's currency (ADR-023 §2 "Tests required").
    """
    if display_currency == store_currency:
        return base_amount

    rate_store = Decimal("1") if store_currency == REFERENCE_CURRENCY_CODE else _rate_for(store_currency)
    rate_display = Decimal("1") if display_currency == REFERENCE_CURRENCY_CODE else _rate_for(display_currency)

    raw = base_amount * (rate_display / rate_store)

    currency_def = CurrencyDefinition.objects.get(code=display_currency)
    settings_row = CurrencyConverterSettings.get_solo()
    return apply_rounding(raw, currency_def, settings_row.rounding_mode)
