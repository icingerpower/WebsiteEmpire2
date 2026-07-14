"""
Celery task for the currency app (ADR-023 §1).

refresh_currency_rates — optional daily beat task (registered in
webecom/settings/base.py CELERY_BEAT_SCHEDULE, matching the existing pattern
for payments/reviews/sitemaps/campaigns/cart tasks). OFF by default: the task
itself checks CurrencyConverterSettings.auto_refresh_enabled and no-ops when
disabled, so it is always safe to have it wired into beat.

Idempotent and safe to re-run (§XV-6) — re-fetching and re-applying via
currency.services.apply_fetched_rates is safe regardless of how many times or
how recently it last ran; writes are per-row, not a single counter, so no
distributed lock is needed.

Network failures are caught, logged at ERROR, and leave ALL rates untouched
(never partially applied) — ADR-023 §1 "Risks".
"""

import logging
import urllib.error

from celery import shared_task

from currency.services import apply_fetched_rates
from currency.sources import EcbRateSource

logger = logging.getLogger(__name__)


@shared_task(name="currency.tasks.refresh_currency_rates")
def refresh_currency_rates() -> dict[str, str]:
    """
    Fetch the ECB daily feed and apply sanity-checked rate updates.

    No-ops (returns {}) when auto-refresh is disabled (default) — this makes
    it always safe to have this task registered in CELERY_BEAT_SCHEDULE.

    Returns the per-currency outcome map from apply_fetched_rates(), or {} if
    auto-refresh is off or the fetch itself failed (network/timeout error).
    """
    from currency.models import CurrencyConverterSettings

    settings_row = CurrencyConverterSettings.get_solo()
    if not settings_row.auto_refresh_enabled:
        logger.info("Currency auto-refresh is disabled; skipping refresh_currency_rates run.")
        return {}

    source = EcbRateSource()
    try:
        fetch_result = source.run()
    except (urllib.error.URLError, OSError, TimeoutError) as exc:
        logger.error("ECB currency-rate fetch failed: %s", exc)
        return {}

    return apply_fetched_rates(fetch_result, source_label="api:ecb")
