"""
Data migration: seed CurrencyDefinition rows for the ECB daily reference-rate
feed's currency set, plus the EUR anchor's CurrencyRate row (ADR-023 §1/§2/§7).

DECIDED (human, 2026-07-10, per specs/ecommerce_engine/11_uncertainties_to_validate.md):
seed ONLY the ~30 ECB feed currencies (+ EUR). No extra non-ECB currencies are
seeded now — a super-admin can add any additional CurrencyDefinition (with a
manual CurrencyRate) later at /superadmin/, exactly like the always-available
manual path already supports (ADR-023 §1).

decimal_places: 0 for JPY and KRW (DECIDED 2026-07-10 — round to whole units
using the SAME generic psychological rule as 2-decimal currencies, ADR-023 §6).
Every other seeded currency defaults to 2.

Deliberately NOT seeded here: a CurrencyRate row for any currency other than
EUR. See currency/services.py's module docstring for why — a fabricated
placeholder rate could later reject a legitimate first real rate as
"out of bounds" against itself. A currency exists as a definition with no
CurrencyRate row until a super-admin enters one manually or the first
auto-refresh run succeeds — an explicit, visible gap (ADR-023 §1), not a
silently wrong price.

symbol/symbol_placement are presentation-only and correctable anytime in
/superadmin/ — reasonable defaults, not verified against every locale's
typographic convention (ASSUMPTION, low risk).
"""

from decimal import Decimal

from django.db import migrations


# (code, name, symbol, symbol_placement, decimal_places)
ECB_CURRENCIES = [
    ("EUR", "Euro", "€", "suffix", 2),
    ("USD", "US Dollar", "$", "prefix", 2),
    ("JPY", "Japanese Yen", "¥", "prefix", 0),
    ("BGN", "Bulgarian Lev", "лв", "suffix", 2),
    ("CZK", "Czech Koruna", "Kč", "suffix", 2),
    ("DKK", "Danish Krone", "kr", "suffix", 2),
    ("GBP", "British Pound", "£", "prefix", 2),
    ("HUF", "Hungarian Forint", "Ft", "suffix", 2),
    ("PLN", "Polish Zloty", "zł", "suffix", 2),
    ("RON", "Romanian Leu", "lei", "suffix", 2),
    ("SEK", "Swedish Krona", "kr", "suffix", 2),
    ("CHF", "Swiss Franc", "CHF", "prefix", 2),
    ("ISK", "Icelandic Krona", "kr", "suffix", 2),
    ("NOK", "Norwegian Krone", "kr", "suffix", 2),
    ("TRY", "Turkish Lira", "₺", "prefix", 2),
    ("AUD", "Australian Dollar", "$", "prefix", 2),
    ("BRL", "Brazilian Real", "R$", "prefix", 2),
    ("CAD", "Canadian Dollar", "$", "prefix", 2),
    ("CNY", "Chinese Yuan", "¥", "prefix", 2),
    ("HKD", "Hong Kong Dollar", "$", "prefix", 2),
    ("IDR", "Indonesian Rupiah", "Rp", "prefix", 2),
    ("ILS", "Israeli New Shekel", "₪", "prefix", 2),
    ("INR", "Indian Rupee", "₹", "prefix", 2),
    ("KRW", "South Korean Won", "₩", "prefix", 0),
    ("MXN", "Mexican Peso", "$", "prefix", 2),
    ("MYR", "Malaysian Ringgit", "RM", "prefix", 2),
    ("NZD", "New Zealand Dollar", "$", "prefix", 2),
    ("PHP", "Philippine Peso", "₱", "prefix", 2),
    ("SGD", "Singapore Dollar", "$", "prefix", 2),
    ("THB", "Thai Baht", "฿", "prefix", 2),
    ("ZAR", "South African Rand", "R", "prefix", 2),
]

EUR_RATE = Decimal("1.00000000")


def seed_currencies(apps, schema_editor):
    CurrencyDefinition = apps.get_model("currency", "CurrencyDefinition")
    CurrencyRate = apps.get_model("currency", "CurrencyRate")

    for code, name, symbol, symbol_placement, decimal_places in ECB_CURRENCIES:
        CurrencyDefinition.objects.get_or_create(
            code=code,
            defaults={
                "name": name,
                "symbol": symbol,
                "symbol_placement": symbol_placement,
                "decimal_places": decimal_places,
                "is_active": True,
            },
        )

    # EUR anchor row: rate_to_reference is always 1.00000000, source='manual',
    # never touched by the refresh task (ADR-023 §2).
    eur = CurrencyDefinition.objects.get(code="EUR")
    CurrencyRate.objects.get_or_create(
        currency=eur,
        defaults={"rate_to_reference": EUR_RATE, "source": "manual"},
    )


def reverse_seed(apps, schema_editor):
    CurrencyDefinition = apps.get_model("currency", "CurrencyDefinition")

    codes = [code for code, *_ in ECB_CURRENCIES]
    # CurrencyRate rows cascade-delete with their CurrencyDefinition (on_delete=CASCADE).
    CurrencyDefinition.objects.filter(code__in=codes).delete()


class Migration(migrations.Migration):

    dependencies = [
        ("currency", "0001_initial"),
    ]

    operations = [
        migrations.RunPython(seed_currencies, reverse_code=reverse_seed),
    ]
