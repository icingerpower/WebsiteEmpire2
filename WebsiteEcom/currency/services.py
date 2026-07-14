"""
Applying a RateFetchResult to CurrencyRate rows (ADR-023 §1).

Kept separate from currency/sources.py so EcbRateSource.parse() stays a pure,
network-free, database-free function (unit-testable against a fixture), while
this module owns the money-adjacent, platform-wide-blast-radius part: the
sanity-bound guard before ANY fetched value is written.

Sanity bound (ADR-023 §1): a fetched rate is applied only if
0.01 * previous_rate <= new_rate <= 100 * previous_rate (guards against a
decimal-shift or garbled-feed value) AND new_rate > 0. A rejected value keeps
the last-known-good rate, records CurrencyRate.last_refresh_error, and is
counted in the run's per-currency outcome map — visible on the admin
dashboard (§XV-1), never silently substituted.

The sanity bound only applies when a PREVIOUS CurrencyRate row already exists
for that currency — a brand-new row (first-ever fetch, or a currency that was
only ever a bare CurrencyDefinition with no rate yet) has no trustworthy
baseline to compare against, so it is written directly. This also means a
seeded CurrencyDefinition with no CurrencyRate row is deliberately left with
NO rate until either a super-admin enters one manually or the first
auto-refresh run succeeds (ADR-023 §1 "explicit, visible state... not a
silent gap") — never a fabricated placeholder value that could later reject a
legitimate first real rate as "out of bounds" against itself.

Idempotent (ADR-023 §1 "safe to re-run"): re-running with unchanged upstream
data reuses the same CurrencyRate row (get-then-update, never a second
INSERT) — no duplicate rows are ever created for a currency.
"""

from decimal import Decimal

from currency.models import CurrencyDefinition, CurrencyRate
from currency.sources import FEED_LEVEL_ERROR_KEY, RateFetchResult

SANITY_MIN_MULTIPLIER = Decimal("0.01")
SANITY_MAX_MULTIPLIER = Decimal("100")


def apply_fetched_rates(fetch_result: RateFetchResult, source_label: str = "api:ecb") -> dict[str, str]:
    """
    Apply a RateFetchResult to CurrencyRate rows with the sanity-bound guard.

    Returns a per-currency-code -> outcome string map ('ok' or 'error: <msg>').
    Currencies present in the feed but with no matching (seeded) CurrencyDefinition
    are silently skipped — they are not part of our currency catalog, not an error.

    The FEED_LEVEL_ERROR_KEY sentinel (a whole-feed parse failure) is passed
    through as an outcome entry but never looked up as a CurrencyDefinition.
    """
    outcomes: dict[str, str] = {}

    for code, result in fetch_result.results.items():
        if code == FEED_LEVEL_ERROR_KEY:
            outcomes[code] = f"error: {result.error}"
            continue

        try:
            currency_def = CurrencyDefinition.objects.get(code=code)
        except CurrencyDefinition.DoesNotExist:
            continue  # Not one of our seeded currencies — ignore, not an error.

        try:
            rate_row = CurrencyRate.objects.get(currency=currency_def)
        except CurrencyRate.DoesNotExist:
            rate_row = None

        if not result.ok:
            if rate_row is not None:
                rate_row.last_refresh_error = result.error
                rate_row.save(update_fields=["last_refresh_error"])
            outcomes[code] = f"error: {result.error}"
            continue

        new_rate = result.rate

        if rate_row is None:
            # No trustworthy baseline yet — write directly, no sanity check.
            CurrencyRate.objects.create(
                currency=currency_def,
                rate_to_reference=new_rate,
                source=source_label,
                previous_rate=None,
                last_refresh_error="",
            )
            outcomes[code] = "ok"
            continue

        previous = rate_row.rate_to_reference
        lower = previous * SANITY_MIN_MULTIPLIER
        upper = previous * SANITY_MAX_MULTIPLIER
        if new_rate <= 0 or not (lower <= new_rate <= upper):
            msg = f"Rejected out-of-bounds rate {new_rate} (previous {previous})"
            rate_row.last_refresh_error = msg
            rate_row.save(update_fields=["last_refresh_error"])
            outcomes[code] = f"error: {msg}"
            continue

        rate_row.previous_rate = previous
        rate_row.rate_to_reference = new_rate
        rate_row.source = source_label
        rate_row.last_refresh_error = ""
        rate_row.save(
            update_fields=["previous_rate", "rate_to_reference", "source", "last_refresh_error", "updated_at"]
        )
        outcomes[code] = "ok"

    return outcomes
