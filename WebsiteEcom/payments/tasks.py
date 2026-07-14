"""
Payment processor health-probe task (ADR-006-R §1.2.1, TICKET-020R).

Probes all active ProcessorAccount instances whose health_source == 'probe'
every 5 minutes (Celery beat — not yet wired in Phase 1).

Health state machine:
  UNKNOWN → GREEN (first success) | YELLOW (first failure)
  GREEN   → YELLOW (one failure)
  YELLOW  → GREEN (one success) | RED (one more failure)
  RED     → GREEN (one success)
  manual  → untouched by probe (health_source == 'manual')

Staleness alert: if health_checked_at is older than 30 minutes after the probe
attempt, a WARNING is logged (§XV-1 — failures must be visible before an order
hits them).

The task is invokable synchronously via the management command
payments/management/commands/probe_processor_health.py for testing and manual ops.
"""

import logging
from datetime import timedelta

from django.utils import timezone

logger = logging.getLogger("payments.tasks")

# Staleness threshold: alert when last probe is older than this.
_STALENESS_THRESHOLD = timedelta(minutes=30)


def _next_health_status(current: str, success: bool) -> str:
    """
    Apply the health state machine transition.

    Transitions (§1.2.1):
      UNKNOWN + success → GREEN
      UNKNOWN + failure → YELLOW
      GREEN   + success → GREEN  (no change)
      GREEN   + failure → YELLOW
      YELLOW  + success → GREEN
      YELLOW  + failure → RED
      RED     + success → GREEN  (immediate recovery)
      RED     + failure → RED    (no change)
    """
    if success:
        return "GREEN"
    # Failure transitions.
    if current in ("GREEN", "UNKNOWN"):
        return "YELLOW"
    if current == "YELLOW":
        return "RED"
    # RED + failure → stays RED.
    return "RED"


def probe_all_processor_accounts() -> dict:
    """
    Probe all active ProcessorAccount instances with health_source == 'probe'.

    Returns a summary dict:
      {
        'probed': int,       -- number of accounts probed
        'skipped': int,      -- accounts with health_source == 'manual'
        'errors': int,       -- probe calls that returned healthy=False
        'stale': int,        -- accounts whose health_checked_at was stale post-probe
      }
    """
    from payments.factory import get_connector
    from payments.models import ProcessorAccount

    accounts = ProcessorAccount.objects.filter(is_active=True)
    probed = skipped = errors = stale = 0

    for account in accounts:
        if account.health_source == "manual":
            skipped += 1
            continue

        try:
            connector = get_connector(account)
            result = connector.health_check()
        except Exception as exc:
            logger.error(
                "probe_all_processor_accounts: connector error for account pk=%s: %s",
                account.pk, exc,
            )
            # Treat connector instantiation failure as a probe failure.
            result_healthy = False
            result_error = str(exc)
        else:
            result_healthy = result.healthy
            result_error = result.error_message if not result.healthy else ""

        now = timezone.now()
        new_status = _next_health_status(account.health_status, result_healthy)

        if not result_healthy:
            errors += 1
            logger.warning(
                "Processor account pk=%s (%s) probe failed: %s → health %s → %s",
                account.pk, account.display_name, result_error,
                account.health_status, new_status,
            )
        elif new_status != account.health_status:
            logger.info(
                "Processor account pk=%s (%s) health transition: %s → %s",
                account.pk, account.display_name,
                account.health_status, new_status,
            )

        ProcessorAccount.objects.filter(pk=account.pk).update(
            health_status=new_status,
            health_checked_at=now,
            health_detail=result_error,
        )
        probed += 1

        # Staleness check: alert if health_checked_at is still stale after the update.
        # This covers the case where another process set health_checked_at very recently
        # but the probe just ran — in practice the update above ensures freshness.
        # We check the original health_checked_at to detect if the account was stale
        # BEFORE this probe run (i.e. this probe run was overdue).
        if account.health_checked_at and (now - account.health_checked_at) > _STALENESS_THRESHOLD:
            stale += 1
            logger.warning(
                "Processor account pk=%s (%s) health was stale: last checked %s ago",
                account.pk, account.display_name,
                now - account.health_checked_at,
            )

    logger.info(
        "probe_all_processor_accounts complete: probed=%s skipped=%s errors=%s stale=%s",
        probed, skipped, errors, stale,
    )
    return {
        "probed": probed,
        "skipped": skipped,
        "errors": errors,
        "stale": stale,
    }


# ---------------------------------------------------------------------------
# Celery task registration (Phase 2 — not yet wired to beat schedule)
# ---------------------------------------------------------------------------

try:
    from celery import shared_task  # type: ignore[import-untyped]

    @shared_task(name="payments.probe_processor_health", bind=False)
    def probe_processor_health_task():
        """Celery beat task: probe all processor accounts and update health status."""
        return probe_all_processor_accounts()

except ImportError:
    # Celery is not installed (tests, dev without worker) — task is unavailable.
    # The management command probe_processor_health calls probe_all_processor_accounts()
    # directly and does not require Celery.
    pass
