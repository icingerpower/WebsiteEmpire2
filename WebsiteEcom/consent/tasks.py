"""
Celery beat task: ConsentRecord retention purge (ADR-025 D2/P4).

Registered in webecom/settings/base.py CELERY_BEAT_SCHEDULE as
"purge-consent-records", daily — same shape as chat's
"purge-expired-chat-sessions" (ADR-024 Decision 10). Delegates to
consent.services.purge_consent_records() so this beat task and the operator
CLI command (consent/management/commands/purge_consent_records.py) share one
DELETE code path (ADR-025 D7).
"""

from celery import shared_task

from consent.services import purge_consent_records as _purge_consent_records


@shared_task(name="consent.tasks.purge_consent_records")
def purge_consent_records() -> int:
    return _purge_consent_records()
