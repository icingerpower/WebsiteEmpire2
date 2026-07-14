"""
Analytics Celery tasks — async beacon ingest (TICKET-014).

Architecture note
-----------------
The beacon view dispatches payload dicts via ingest_beacon_event.delay() rather
than writing to the analytics DB inline. This keeps HTTP response latency under
control and decouples the beacon endpoint from DB availability.

Payload shape (passed as a single dict to keep the task signature simple and
JSON-serialisable):

    {
        "store_id": 42,
        "session_id": "<opaque string>",
        "events": [
            {"event_type": "page_view", "url": "...", "properties": {}, ...},
            ...
        ]
    }

Retry / DLQ contract
--------------------
- max_retries=3, delay=30 s between attempts.
- After exhausting retries, the payload is written to BeaconDlq (analytics DB).
- Operator replays DLQ via: python manage.py replay_beacon_dlq

_write_beacon_batch() is intentionally separate from ingest_events() so that
it raises on failure rather than swallowing the exception.  The task needs
exceptions to trigger retries; the synchronous ingest path must never raise
(beacon always returns 200).
"""

import logging

from celery import shared_task

logger = logging.getLogger('analytics.tasks')


def _write_beacon_batch(payload: dict) -> None:
    """
    Write a batch of beacon events to the analytics DB, raising on failure.

    Unlike ingest_events() (which swallows exceptions so the beacon view
    always returns 200), this helper raises so that the Celery task can retry
    and ultimately park the payload in BeaconDlq.

    Used by both the Celery task and the replay management command.
    """
    from .ingest import ingest_events

    store_id = payload.get('store_id')
    session_id = payload.get('session_id', '')
    events = payload.get('events', [])

    if not events:
        return

    count = ingest_events(store_id=store_id, session_id=session_id, events=events)
    if count == 0:
        # ingest_events returns 0 only when an exception was caught internally.
        # The original exception is already logged by ingest_events; we raise a
        # descriptive error here so the task retry / DLQ path fires.
        raise RuntimeError(
            f'ingest_events returned 0 rows for {len(events)} events '
            f'(store_id={store_id}); see analytics.ingest log for the root cause'
        )


@shared_task(
    name='analytics.tasks.ingest_beacon_event',
    bind=True,
    max_retries=3,
    default_retry_delay=30,
)
def ingest_beacon_event(self, payload: dict) -> None:
    """
    Async beacon ingest task.

    Processes a batch of analytics events (store_id, session_id, events list)
    that the beacon view received from the frontend JS.

    On any exception: retries up to 3 times (30 s apart).
    After max retries: writes the payload to BeaconDlq for operator replay.
    """
    from .models import BeaconDlq

    try:
        _write_beacon_batch(payload)
    except Exception as exc:
        attempt = self.request.retries + 1
        logger.warning('ingest_beacon_event failed (attempt %d): %s', attempt, exc)
        try:
            raise self.retry(exc=exc)
        except self.MaxRetriesExceededError:
            BeaconDlq.objects.using('analytics').create(
                payload_json=payload,
                error_message=str(exc),
            )
            logger.error(
                'Beacon batch moved to DLQ after %d attempts (store_id=%s)',
                self.max_retries + 1,
                payload.get('store_id'),
            )
