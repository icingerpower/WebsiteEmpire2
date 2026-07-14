"""
Shared ConsentRecord retention-purge logic (ADR-025 D2/P4).

Both consent/tasks.py (Celery beat, daily) and
consent/management/commands/purge_consent_records.py (operator CLI) call
purge_consent_records() so the DELETE logic exists in exactly one place —
mirrors chat.tasks.purge_expired_chat_sessions's shape (ADR-024 Decision 10),
with the addition of a management command per ADR-025 D7's explicit
requirement.
"""

import logging

from django.utils import timezone

from consent.models import ConsentRecord
from consent.policy import CONSENT_RECORD_RETENTION_DAYS

logger = logging.getLogger(__name__)


def purge_consent_records() -> int:
    """
    Delete ConsentRecord rows whose created_at is older than
    CONSENT_RECORD_RETENTION_DAYS (13 months rolling purge, P4).

    Cross-store by design (a platform-wide retention sweep, not a per-store
    query) — uses cross_store_unsafe(), the deliberately-ugly escape hatch
    reserved for exactly this kind of maintenance task (core/managers.py).

    Returns the number of rows deleted.
    """
    cutoff = timezone.now() - timezone.timedelta(days=CONSENT_RECORD_RETENTION_DAYS)
    queryset = ConsentRecord.objects.cross_store_unsafe().filter(created_at__lt=cutoff)
    count = queryset.count()
    queryset.delete()
    if count:
        logger.info("Purged %d expired ConsentRecord row(s) older than %s.", count, cutoff)
    return count
