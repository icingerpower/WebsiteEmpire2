"""
Analytics views — beacon endpoint for frontend JS event ingest.

Security (Safety Audit MEDIUM finding):
  store_id is derived from request.store (set by HostResolutionMiddleware from the
  Host header), not from the request body. An attacker cannot inject events for a
  different store by supplying a forged store_id. The body's store_id field is ignored.
  If request.store is None (unknown host), the request is rejected with 400.

Async dispatch:
  Events are dispatched to the ingest_beacon_event Celery task via .delay() so
  that the HTTP response is returned immediately without waiting for DB writes.
  If the Celery broker is unavailable, the view falls back to synchronous ingest
  (via ingest_events) so that events are not silently dropped.
"""

import logging
import json

from django.http import HttpResponseBadRequest, JsonResponse
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_POST

from .ingest import ingest_events
from .tasks import ingest_beacon_event

logger = logging.getLogger('analytics.views')

MAX_EVENTS_PER_BEACON = 50


@csrf_exempt
@require_POST
def beacon(request):
    """
    Receives batched events from the frontend beacon JS.

    store_id is resolved from request.store (set by HostResolutionMiddleware).
    The body's store_id field is ignored to prevent cross-store event injection.

    Events are dispatched asynchronously via Celery.  If the broker is down,
    the view falls back to synchronous ingest so no events are lost.

    Expected body (JSON):
        {
            "session_id": "<opaque string>",
            "events": [
                {
                    "event_type": "page_view",
                    "properties": {},
                    "utm_source": "",
                    ...
                }
            ]
        }
    """
    store = getattr(request, 'store', None)
    if store is None:
        return JsonResponse({'ok': False, 'error': 'unknown store'}, status=400)

    try:
        data = json.loads(request.body)
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        return JsonResponse({'ok': False, 'error': f'invalid JSON: {exc}'}, status=400)

    session_id = data.get('session_id', '')
    events = data.get('events', [])

    if not events:
        return JsonResponse({'ok': False, 'error': 'missing events'}, status=400)
    if not isinstance(events, list):
        return JsonResponse({'ok': False, 'error': 'events must be a list'}, status=400)
    if len(events) > MAX_EVENTS_PER_BEACON:
        logger.warning(
            'Beacon rejected: %d events exceeds cap of %d',
            len(events),
            MAX_EVENTS_PER_BEACON,
        )
        return HttpResponseBadRequest(
            f'Too many events: max {MAX_EVENTS_PER_BEACON} per request'
        )

    payload = {
        'store_id': store.pk,
        'session_id': str(session_id),
        'events': events,
    }

    try:
        ingest_beacon_event.delay(payload)
    except Exception:
        # Broker unavailable — fall back to synchronous ingest so events are not lost.
        # ingest_events never raises; errors are logged internally.
        logger.warning(
            'Celery broker unavailable; falling back to synchronous ingest (store_id=%s)',
            store.pk,
            exc_info=True,
        )
        ingest_events(store_id=store.pk, session_id=str(session_id), events=events)

    return JsonResponse({'ok': True})

