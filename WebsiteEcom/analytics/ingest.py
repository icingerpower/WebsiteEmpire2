"""
Analytics ingest — ORM-free raw SQL batch insert (ADR-004).

Uses Django's 'analytics' database connection alias directly to avoid ORM
overhead on the hot ingest path.

SQL placeholder adapts to the database engine:
  - SQLite uses  ?
  - PostgreSQL uses  %s

On failure: logs a warning and increments _dropped_batch_count (in-memory;
reset on process restart). Production should replace this with a Redis counter
or dead-letter queue.

The beacon view calls ingest_events() synchronously. Async offload is deferred
to TICKET-014 (Celery workers). The function MUST NOT raise — the beacon
endpoint must always return 200.

W3-2 (WAVE3_AUDIT.md / ADR-028's own safety-gate item): the beacon body is
unauthenticated public input. `properties` is a JSONField and was previously
stored verbatim — `properties.page_version_id` was only coerced to int at
*read* time (aggregate_metrics.py's SQL CAST), which raises on PostgreSQL for
any non-integer value ("abc", true, 1.5, a huge int) and would kill the daily
aggregation query for every store for the row's whole retention window. Fix:
coerce/validate at ingest — `_coerce_int_or_none` below is applied to the
top-level soft-id columns (product_id, collection_id, order_id — also plain
attacker-controlled input) AND to `properties['page_version_id']` (dropped
from the dict when non-coercible, never stored as-is). This is defense in
depth alongside the now-non-throwing aggregation query (see
aggregate_metrics.py::_json_int_expr).
"""

import json
import logging

from django.db import connections
from django.utils import timezone

from analytics.models import EventType

logger = logging.getLogger('analytics.ingest')

# Closed vocabulary of permitted event_type values (ADR-004 §2).
# Events with an unknown type are dropped at ingest time and logged.
VALID_EVENT_TYPES: frozenset = frozenset(EventType.values)

# In-memory dropped-batch counter — informational only; not persisted.
_dropped_batch_count = 0

# Signed 32-bit range: matches the IntegerField width of Event.product_id /
# collection_id / order_id and of the page-version primary key. A value
# outside this range cannot be a real id — only exists to abuse the column —
# so it is treated the same as "not an integer".
_INT32_MIN = -2147483648
_INT32_MAX = 2147483647


def _coerce_int_or_none(value):
    """
    Coerce a raw, possibly attacker-controlled value to a plain int, or None
    if it cannot be safely interpreted as one.

    Public beacon input arrives as decoded JSON, so `value` may be an int,
    float, str, bool, None, list, or dict. Rejects:
    - bool: JSON `true`/`false` are not valid ids even though `bool` is an
      `int` subclass in Python (`isinstance(True, int)` is True).
    - non-integer float (e.g. 1.5): silently truncating would invent an id
      that was never sent.
    - non-digit strings ("abc") and anything else that isn't int/float/str.
    - values outside the signed 32-bit range (a 2^70 int): would not fit the
      target column and exists only to probe/DoS the cast.
    """
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        candidate = value
    elif isinstance(value, float):
        if not value.is_integer():
            return None
        candidate = int(value)
    elif isinstance(value, str):
        stripped = value.strip()
        body = stripped[1:] if stripped[:1] == '-' else stripped
        if not body or not body.isdigit():
            return None
        candidate = int(stripped)
    else:
        return None

    if not (_INT32_MIN <= candidate <= _INT32_MAX):
        return None
    return candidate


def _placeholder(connection) -> str:
    """Return the SQL parameter placeholder for the given connection's vendor."""
    return '%s' if connection.vendor == 'postgresql' else '?'


def ingest_events(store_id: int, session_id: str, events: list) -> int:
    """
    Insert a batch of events into the analytics DB.

    Returns the number of rows inserted (0 on failure).
    Never raises — beacon endpoint must always return 200 even if ingest fails.

    Each event dict may contain:
        event_type, product_id, collection_id, order_id,
        utm_source, utm_medium, utm_campaign, utm_term, utm_content,
        referrer, landing_page, customer_local_hour,
        url, device_type, country, value,
        properties (dict), created_at (ISO string).
    Missing keys fall back to safe defaults.
    """
    global _dropped_batch_count

    if not events:
        return 0

    now = timezone.now().isoformat()
    conn = connections['analytics']
    ph = _placeholder(conn)

    rows = []
    for e in events:
        # Reject unknown event types before any DB work (ADR-004 §2).
        event_type = e.get('event_type', '')
        if event_type not in VALID_EVENT_TYPES:
            logger.warning('Dropping beacon event with unknown event_type %r', event_type)
            continue

        properties_raw = e.get('properties', {})
        if isinstance(properties_raw, dict):
            # W3-2: page_version_id feeds a raw-SQL CAST at aggregation time
            # (aggregate_metrics.py::_json_int_expr). Coerce here so a
            # non-integer value is dropped from the stored JSON entirely,
            # never persisted, and never reaches that CAST.
            if 'page_version_id' in properties_raw:
                coerced_pv_id = _coerce_int_or_none(properties_raw['page_version_id'])
                if coerced_pv_id is None:
                    properties_raw = {
                        k: v for k, v in properties_raw.items() if k != 'page_version_id'
                    }
                elif coerced_pv_id != properties_raw['page_version_id']:
                    properties_raw = {**properties_raw, 'page_version_id': coerced_pv_id}
        # Ensure properties is always stored as a JSON string for raw SQL.
        properties_str = (
            json.dumps(properties_raw)
            if isinstance(properties_raw, dict)
            else str(properties_raw)
        )
        # value is a nullable decimal — pass None when absent or not numeric.
        raw_value = e.get('value')
        if raw_value is not None:
            try:
                stored_value = float(raw_value)
            except (TypeError, ValueError):
                stored_value = None
        else:
            stored_value = None

        rows.append((
            event_type,
            store_id,
            session_id,
            _coerce_int_or_none(e.get('product_id')),
            _coerce_int_or_none(e.get('collection_id')),
            _coerce_int_or_none(e.get('order_id')),
            e.get('utm_source', ''),
            e.get('utm_medium', ''),
            e.get('utm_campaign', ''),
            e.get('utm_term', ''),
            e.get('utm_content', ''),
            e.get('referrer', ''),
            e.get('landing_page', ''),
            e.get('customer_local_hour'),
            e.get('url', ''),
            e.get('device_type', ''),
            e.get('country', ''),
            stored_value,
            properties_str,
            e.get('created_at', now),
        ))

    sql = f"""
        INSERT INTO analytics_event
            (event_type, store_id, session_id, product_id, collection_id, order_id,
             utm_source, utm_medium, utm_campaign, utm_term, utm_content,
             referrer, landing_page, customer_local_hour,
             url, device_type, country, value,
             properties, created_at)
        VALUES ({ph}, {ph}, {ph}, {ph}, {ph}, {ph},
                {ph}, {ph}, {ph}, {ph}, {ph},
                {ph}, {ph}, {ph},
                {ph}, {ph}, {ph}, {ph},
                {ph}, {ph})
    """

    try:
        with conn.cursor() as cursor:
            cursor.executemany(sql, rows)
        return len(rows)
    except Exception:
        _dropped_batch_count += 1
        logger.warning(
            'analytics ingest: dropped batch of %d events '
            '(store_id=%s, dropped_total=%d)',
            len(rows),
            store_id,
            _dropped_batch_count,
            exc_info=True,
        )
        return 0
