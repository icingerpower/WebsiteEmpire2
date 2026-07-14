"""
Celery beat task: retention purge (ADR-024 Decision 10, AC-CHAT-14).

Registered in webecom/settings/base.py CELERY_BEAT_SCHEDULE as
"purge-expired-chat-sessions", daily.
"""

import logging

from celery import shared_task
from django.conf import settings
from django.utils import timezone

from chat.models import ChatSession

logger = logging.getLogger(__name__)


@shared_task(name="chat.tasks.purge_expired_chat_sessions")
def purge_expired_chat_sessions() -> int:
    """
    Delete ChatSession rows (and their ChatMessage rows, via CASCADE) whose
    created_at is older than settings.CHAT_RETENTION_DAYS (default 30).

    Cross-store by design (a platform-wide retention sweep, not a per-store
    query) — uses cross_store_unsafe(), the deliberately-ugly escape hatch for
    exactly this kind of maintenance task (core/managers.py).

    Returns the number of sessions deleted.
    """
    cutoff = timezone.now() - timezone.timedelta(days=settings.CHAT_RETENTION_DAYS)
    queryset = ChatSession.objects.cross_store_unsafe().filter(created_at__lt=cutoff)
    count = queryset.count()
    queryset.delete()
    if count:
        logger.info("Purged %d expired chat session(s) older than %s.", count, cutoff)
    return count
