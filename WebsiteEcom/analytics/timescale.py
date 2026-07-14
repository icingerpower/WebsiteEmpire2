"""
TimescaleDB detection helpers (ADR-035, TICKET-044).

Single source of truth for "is TimescaleDB active on this connection?",
shared by:
  - analytics/migrations/0004_conditional_hypertable.py (gates create_hypertable)
  - analytics/management/commands/purge_events.py (gates drop_chunks vs DELETE)

Two independent checks are exposed, because they answer different questions:

  is_timescale_active(connection)
      "Is the timescaledb extension installed on this Postgres database?"
      (pg_extension catalog lookup). This is the gate for whether it is even
      safe to *attempt* create_hypertable().

  is_event_hypertable(connection)
      "Has analytics_event actually been converted to a hypertable?"
      (timescaledb_information.hypertables catalog lookup). This is the gate
      purge_events uses to decide drop_chunks vs DELETE — the extension can be
      installed before the conversion migration has run, so this must not be
      inferred from is_timescale_active() alone.

Both never let a query error propagate as "yes": any failure (missing
privilege, connection drop, unexpected catalog shape) resolves to False, the
plain-Postgres/DELETE path, which ADR-035 documents as the always-safe
failure mode. Failures are always logged loudly (§XV-1) — never a silent
except/pass.

Continuous aggregates (Timescale's incrementally-maintained materialized
views, replacing the nightly aggregate_metrics cron) are explicitly OUT OF
SCOPE for v1 per ADR-035 point 4 — a documented future path only, not
designed or implemented here. See
docs/adr/ADR-035-timescaledb-migration.md.

Caching: results are memoized per connection alias for the lifetime of the
process (a catalog fact that doesn't change mid-connection). Callers that
need a guaranteed fresh check (e.g. a migration, which should never trust a
cache warmed by unrelated earlier code) pass use_cache=False. Tests should
call clear_cache() between cases that mock different outcomes on the same
alias.
"""

from __future__ import annotations

import logging

logger = logging.getLogger('analytics.timescale')

_extension_cache: dict[str, bool] = {}
_hypertable_cache: dict[str, bool] = {}


def is_timescale_active(connection, *, use_cache: bool = True) -> bool:
    """
    True iff `connection` is a PostgreSQL connection with the `timescaledb`
    extension installed (regardless of whether any table has been converted
    to a hypertable yet).
    """
    alias = connection.alias
    if use_cache and alias in _extension_cache:
        return _extension_cache[alias]

    active = _detect_extension(connection)
    _extension_cache[alias] = active
    return active


def is_event_hypertable(connection, *, use_cache: bool = True) -> bool:
    """
    True iff analytics_event has actually been converted to a TimescaleDB
    hypertable on `connection`. Implies is_timescale_active(), but is checked
    independently since the extension can be installed before the conversion
    migration has run in a given environment.
    """
    alias = connection.alias
    if use_cache and alias in _hypertable_cache:
        return _hypertable_cache[alias]

    if not is_timescale_active(connection, use_cache=use_cache):
        _hypertable_cache[alias] = False
        return False

    is_hypertable = _detect_hypertable(connection)
    _hypertable_cache[alias] = is_hypertable
    return is_hypertable


def _detect_extension(connection) -> bool:
    if connection.vendor != 'postgresql':
        logger.info(
            "TimescaleDB detection: connection '%s' vendor=%s (not postgresql) "
            "-> extension inactive.",
            connection.alias, connection.vendor,
        )
        return False

    try:
        with connection.cursor() as cursor:
            cursor.execute(
                "SELECT 1 FROM pg_extension WHERE extname = %s", ['timescaledb']
            )
            found = cursor.fetchone() is not None
    except Exception:
        logger.exception(
            "TimescaleDB detection: querying pg_extension failed on connection "
            "'%s' -> treating extension as inactive (safe fallback per ADR-035).",
            connection.alias,
        )
        return False

    logger.info(
        "TimescaleDB detection: connection '%s' vendor=postgresql, "
        "timescaledb extension present=%s.",
        connection.alias, found,
    )
    return found


def _detect_hypertable(connection) -> bool:
    try:
        with connection.cursor() as cursor:
            cursor.execute(
                "SELECT hypertable_name FROM timescaledb_information.hypertables "
                "WHERE hypertable_name = %s",
                ['analytics_event'],
            )
            found = cursor.fetchone() is not None
    except Exception:
        logger.exception(
            "TimescaleDB detection: querying timescaledb_information.hypertables "
            "failed on connection '%s' -> treating analytics_event as a plain "
            "table (safe fallback per ADR-035).",
            connection.alias,
        )
        return False

    logger.info(
        "TimescaleDB detection: connection '%s' analytics_event is_hypertable=%s.",
        connection.alias, found,
    )
    return found


def clear_cache(alias: str | None = None) -> None:
    """
    Test-only helper: clear memoized detection results.

    With no alias, clears every cached result; with an alias, clears only
    that connection's cache entries.
    """
    if alias is None:
        _extension_cache.clear()
        _hypertable_cache.clear()
    else:
        _extension_cache.pop(alias, None)
        _hypertable_cache.pop(alias, None)
