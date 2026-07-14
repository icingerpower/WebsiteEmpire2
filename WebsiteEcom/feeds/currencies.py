"""
Country -> display-currency resolution (ADR-026 D4a, FEED-008).

feed_display_currency(store, country_code) is the single function computing
which ISO 4217 currency a catalog feed for (store, country_code) is priced
in. Callers (feeds/items.py) never compute this any other way (§XV-4 "single
resolver").

COUNTRY_TO_CURRENCY is platform-global static data — like the pixel event
maps (ADR-022): reviewed in PR, no admin surface, stdlib only.

Loud failure (FeedCurrencyError) is deliberate: FEED-008 requires each feed to
be priced in the target country's OWN display currency (Google Merchant
Center rejects a CA feed priced in USD), so silently falling back to the
store's own currency would ship an invalid feed that only fails, invisibly,
on the platform's side (§XV-1). See ADR-026 D4a.
"""

from currency.models import CurrencyDefinition


class FeedCurrencyError(Exception):
    """
    Raised by feed_display_currency() when the target country's mapped
    currency has no usable CurrencyRate. Callers (feeds/tasks.py) catch this
    and set the whole FeedTarget to ERROR with the message carried here —
    never silently substitute the store's own currency.
    """


# ISO 3166-1 alpha-2 -> ISO 4217 currency code. Countries sharing a currency
# union (EUR, XOF, XAF, XCD, ...) all point to the shared code. Territories
# that use another sovereign's currency (Ecuador/El Salvador -> USD, Kosovo/
# Montenegro -> EUR, etc.) are mapped to the currency actually in circulation,
# not to a currency of their own that doesn't exist.
COUNTRY_TO_CURRENCY: dict[str, str] = {
    # --- Eurozone + euro-using territories -----------------------------
    "AT": "EUR", "BE": "EUR", "CY": "EUR", "EE": "EUR", "FI": "EUR",
    "FR": "EUR", "DE": "EUR", "GR": "EUR", "IE": "EUR", "IT": "EUR",
    "LV": "EUR", "LT": "EUR", "LU": "EUR", "MT": "EUR", "NL": "EUR",
    "PT": "EUR", "SK": "EUR", "SI": "EUR", "ES": "EUR", "HR": "EUR",
    "AD": "EUR", "MC": "EUR", "SM": "EUR", "VA": "EUR", "ME": "EUR",
    "XK": "EUR",

    # --- North America ---------------------------------------------------
    "US": "USD", "CA": "CAD", "MX": "MXN",
    "PR": "USD", "GU": "USD", "VI": "USD", "AS": "USD",

    # --- Central America / Caribbean --------------------------------------
    "GT": "GTQ", "HN": "HNL", "NI": "NIO", "CR": "CRC", "PA": "PAB",
    "SV": "USD", "BZ": "BZD", "CU": "CUP", "DO": "DOP", "HT": "HTG",
    "JM": "JMD", "TT": "TTD", "BB": "BBD", "BS": "BSD", "GY": "GYD",
    "SR": "SRD",
    # East Caribbean dollar union.
    "AG": "XCD", "DM": "XCD", "GD": "XCD", "KN": "XCD", "LC": "XCD",
    "VC": "XCD", "AI": "XCD", "MS": "XCD",

    # --- South America -----------------------------------------------------
    "BR": "BRL", "AR": "ARS", "CL": "CLP", "CO": "COP", "PE": "PEN",
    "UY": "UYU", "PY": "PYG", "BO": "BOB", "EC": "USD", "VE": "VES",

    # --- United Kingdom / Northern Europe ------------------------------------
    "GB": "GBP", "IS": "ISK", "NO": "NOK", "CH": "CHF", "LI": "CHF",
    "SE": "SEK", "DK": "DKK",

    # --- Eastern Europe / Balkans --------------------------------------------
    "PL": "PLN", "CZ": "CZK", "HU": "HUF", "RO": "RON", "BG": "BGN",
    "RS": "RSD", "AL": "ALL", "MK": "MKD", "BA": "BAM", "MD": "MDL",
    "UA": "UAH", "BY": "BYN", "RU": "RUB",

    # --- Caucasus / Central Asia -----------------------------------------
    "GE": "GEL", "AM": "AMD", "AZ": "AZN", "KZ": "KZT", "UZ": "UZS",
    "KG": "KGS", "TJ": "TJS", "TM": "TMT",

    # --- Middle East ----------------------------------------------------
    "TR": "TRY", "IL": "ILS", "PS": "ILS", "SA": "SAR", "AE": "AED",
    "QA": "QAR", "KW": "KWD", "BH": "BHD", "OM": "OMR", "JO": "JOD",
    "LB": "LBP", "SY": "SYP", "IQ": "IQD", "IR": "IRR", "YE": "YER",

    # --- Africa ------------------------------------------------------------
    "EG": "EGP", "ZA": "ZAR", "NG": "NGN", "KE": "KES", "GH": "GHS",
    "MA": "MAD", "DZ": "DZD", "TN": "TND", "LY": "LYD", "ET": "ETB",
    "TZ": "TZS", "UG": "UGX", "RW": "RWF", "ZM": "ZMW", "ZW": "ZWL",
    "MZ": "MZN", "AO": "AOA", "NA": "NAD", "BW": "BWP", "SZ": "SZL",
    "LS": "LSL", "MW": "MWK", "MG": "MGA", "MU": "MUR", "SC": "SCR",
    "SD": "SDG", "SS": "SSP", "ER": "ERN", "DJ": "DJF", "SO": "SOS",
    "CV": "CVE", "GM": "GMD", "GN": "GNF", "LR": "LRD", "SL": "SLE",
    "ST": "STN", "KM": "KMF",
    # West African CFA franc union.
    "BJ": "XOF", "BF": "XOF", "CI": "XOF", "GW": "XOF", "ML": "XOF",
    "NE": "XOF", "SN": "XOF", "TG": "XOF",
    # Central African CFA franc union.
    "CM": "XAF", "CF": "XAF", "TD": "XAF", "CG": "XAF", "GQ": "XAF",
    "GA": "XAF",

    # --- South / Southeast Asia --------------------------------------------
    "IN": "INR", "PK": "PKR", "BD": "BDT", "LK": "LKR", "NP": "NPR",
    "BT": "BTN", "MV": "MVR", "AF": "AFN",
    "TH": "THB", "VN": "VND", "ID": "IDR", "PH": "PHP", "MY": "MYR",
    "SG": "SGD", "BN": "BND", "KH": "KHR", "LA": "LAK", "MM": "MMK",
    "MN": "MNT",

    # --- East Asia / Oceania -------------------------------------------------
    "JP": "JPY", "CN": "CNY", "KR": "KRW", "TW": "TWD", "HK": "HKD",
    "AU": "AUD", "NZ": "NZD",
    "FJ": "FJD", "PG": "PGK", "WS": "WST", "TO": "TOP", "VU": "VUV",
    "SB": "SBD", "KI": "AUD", "TV": "AUD", "NR": "AUD",
    "PW": "USD", "FM": "USD", "MH": "USD",
}


def feed_display_currency(store, country_code: str) -> str:
    """
    Resolve the display currency a feed for (store, country_code) must be
    priced in (ADR-026 D4a).

    Algorithm:
      1. mapped = COUNTRY_TO_CURRENCY.get(country_code); unknown country ->
         FeedCurrencyError (nothing to fall back to).
      2. mapped == store.default_currency -> identity, always works (no rate
         lookup needed — matches currency.resolver.display_amount()'s own
         identity path).
      3. mapped has an active CurrencyDefinition AND a CurrencyRate -> return
         mapped (feeds/items.py converts store currency -> mapped per item
         via display_amount()).
      4. else -> raise FeedCurrencyError with the actionable message from D4a.

    Note: StoreCurrencySetting enablement is deliberately NOT consulted here
    — it gates the storefront's shopper-facing currency picker, not feed
    pricing (ADR-026 D4a: "the shopper-facing picker is not a prerequisite").
    """
    mapped = COUNTRY_TO_CURRENCY.get(country_code)
    if not mapped:
        raise FeedCurrencyError(
            f"No known currency mapping for country {country_code!r}."
        )

    if mapped == store.default_currency:
        return mapped

    has_rate = (
        CurrencyDefinition.objects.filter(code=mapped, is_active=True)
        .filter(rate__isnull=False)
        .exists()
    )
    if not has_rate:
        raise FeedCurrencyError(
            f"No exchange rate for {mapped} ({country_code}); add a "
            f"CurrencyRate at /superadmin/ or remove {country_code} from "
            "this language's target countries."
        )
    return mapped
