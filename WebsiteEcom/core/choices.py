"""
Shared choice lists for admin widgets with fixed vocabularies.

Used by JsonMultipleChoiceField in admin forms where the underlying JSONField
stores a list of strings (e.g. settlement_currencies, supported_countries).

Format: (code, 'CODE — Human name') so admins see the code inline.

Zone tokens (ZONE_EU, ZONE_NA, ZONE_APAC, ZONE_GCC) can be stored alongside
individual country codes in supported_countries JSONFields.  At routing time
expand_country_list() expands them to their member ISO-3166 codes.
"""


# ---------------------------------------------------------------------------
# Zone definitions
# ---------------------------------------------------------------------------

#: Zone token → set of ISO-3166 country codes.
#: Add a zone here when a new geographic grouping is needed for routing rules.
ZONE_COUNTRIES: dict[str, frozenset[str]] = {
    "ZONE_EU": frozenset({
        "AT", "BE", "BG", "CY", "CZ", "DE", "DK", "EE", "ES", "FI",
        "FR", "GR", "HR", "HU", "IE", "IT", "LT", "LU", "LV", "MT",
        "NL", "PL", "PT", "RO", "SE", "SI", "SK",
    }),
    "ZONE_NA": frozenset({"US", "CA", "MX"}),
    "ZONE_APAC": frozenset({
        "AU", "CN", "HK", "ID", "IN", "JP", "KR", "MY", "NZ", "PH",
        "SG", "TH", "VN",
    }),
    "ZONE_GCC": frozenset({"AE", "KW", "QA", "SA"}),
}

#: Choices shown at the top of the supported_countries widget (one entry per zone).
ZONE_CHOICES: list[tuple[str, str]] = [
    ("ZONE_EU", "ZONE: European Union (AT BE BG CY CZ DE DK EE ES FI FR GR HR HU IE IT LT LU LV MT NL PL PT RO SE SI SK)"),
    ("ZONE_NA", "ZONE: North America (CA MX US)"),
    ("ZONE_APAC", "ZONE: Asia-Pacific (AU CN HK ID IN JP KR MY NZ PH SG TH VN)"),
    ("ZONE_GCC", "ZONE: Gulf States (AE KW QA SA)"),
]


def expand_country_list(codes: list) -> frozenset:
    """
    Expand a list of country codes and zone tokens to a flat set of ISO-3166 codes.

    Called by routing.select_account to evaluate supported_countries against
    a buyer's country.  Unknown tokens are kept as-is (harmless — no real
    country code equals a zone token).
    """
    result: set = set()
    for code in codes:
        if code in ZONE_COUNTRIES:
            result |= ZONE_COUNTRIES[code]
        else:
            result.add(code)
    return frozenset(result)

CURRENCY_CHOICES = [
    ("AED", "AED — UAE Dirham"),
    ("ARS", "ARS — Argentine Peso"),
    ("AUD", "AUD — Australian Dollar"),
    ("BGN", "BGN — Bulgarian Lev"),
    ("BRL", "BRL — Brazilian Real"),
    ("CAD", "CAD — Canadian Dollar"),
    ("CHF", "CHF — Swiss Franc"),
    ("CLP", "CLP — Chilean Peso"),
    ("CNY", "CNY — Chinese Yuan"),
    ("COP", "COP — Colombian Peso"),
    ("CZK", "CZK — Czech Koruna"),
    ("DKK", "DKK — Danish Krone"),
    ("EUR", "EUR — Euro"),
    ("GBP", "GBP — British Pound"),
    ("HKD", "HKD — Hong Kong Dollar"),
    ("HRK", "HRK — Croatian Kuna"),
    ("HUF", "HUF — Hungarian Forint"),
    ("IDR", "IDR — Indonesian Rupiah"),
    ("INR", "INR — Indian Rupee"),
    ("JPY", "JPY — Japanese Yen"),
    ("KRW", "KRW — South Korean Won"),
    ("KWD", "KWD — Kuwaiti Dinar"),
    ("MAD", "MAD — Moroccan Dirham"),
    ("MXN", "MXN — Mexican Peso"),
    ("MYR", "MYR — Malaysian Ringgit"),
    ("NOK", "NOK — Norwegian Krone"),
    ("NZD", "NZD — New Zealand Dollar"),
    ("PHP", "PHP — Philippine Peso"),
    ("PLN", "PLN — Polish Zloty"),
    ("QAR", "QAR — Qatari Riyal"),
    ("RON", "RON — Romanian Leu"),
    ("RSD", "RSD — Serbian Dinar"),
    ("SAR", "SAR — Saudi Riyal"),
    ("SEK", "SEK — Swedish Krona"),
    ("SGD", "SGD — Singapore Dollar"),
    ("THB", "THB — Thai Baht"),
    ("TRY", "TRY — Turkish Lira"),
    ("USD", "USD — US Dollar"),
    ("VND", "VND — Vietnamese Dong"),
    ("ZAR", "ZAR — South African Rand"),
]

COUNTRY_CHOICES = ZONE_CHOICES + [
    ("AE", "AE — United Arab Emirates"),
    ("AT", "AT — Austria"),
    ("AU", "AU — Australia"),
    ("BE", "BE — Belgium"),
    ("BG", "BG — Bulgaria"),
    ("BR", "BR — Brazil"),
    ("CA", "CA — Canada"),
    ("CH", "CH — Switzerland"),
    ("CN", "CN — China"),
    ("CY", "CY — Cyprus"),
    ("CZ", "CZ — Czech Republic"),
    ("DE", "DE — Germany"),
    ("DK", "DK — Denmark"),
    ("EE", "EE — Estonia"),
    ("ES", "ES — Spain"),
    ("FI", "FI — Finland"),
    ("FR", "FR — France"),
    ("GB", "GB — United Kingdom"),
    ("GR", "GR — Greece"),
    ("HK", "HK — Hong Kong"),
    ("HR", "HR — Croatia"),
    ("HU", "HU — Hungary"),
    ("ID", "ID — Indonesia"),
    ("IE", "IE — Ireland"),
    ("IN", "IN — India"),
    ("IS", "IS — Iceland"),
    ("IT", "IT — Italy"),
    ("JP", "JP — Japan"),
    ("KR", "KR — South Korea"),
    ("KW", "KW — Kuwait"),
    ("LT", "LT — Lithuania"),
    ("LU", "LU — Luxembourg"),
    ("LV", "LV — Latvia"),
    ("MA", "MA — Morocco"),
    ("MT", "MT — Malta"),
    ("MX", "MX — Mexico"),
    ("MY", "MY — Malaysia"),
    ("NL", "NL — Netherlands"),
    ("NO", "NO — Norway"),
    ("NZ", "NZ — New Zealand"),
    ("PH", "PH — Philippines"),
    ("PL", "PL — Poland"),
    ("PT", "PT — Portugal"),
    ("QA", "QA — Qatar"),
    ("RO", "RO — Romania"),
    ("RU", "RU — Russia"),
    ("SA", "SA — Saudi Arabia"),
    ("SE", "SE — Sweden"),
    ("SG", "SG — Singapore"),
    ("SI", "SI — Slovenia"),
    ("SK", "SK — Slovakia"),
    ("TH", "TH — Thailand"),
    ("TR", "TR — Turkey"),
    ("UA", "UA — Ukraine"),
    ("US", "US — United States"),
    ("VN", "VN — Vietnam"),
    ("ZA", "ZA — South Africa"),
]
