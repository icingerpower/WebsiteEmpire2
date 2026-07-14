"""
Management command: purge_events

Purges raw analytics events older than ANALYTICS_EVENT_RETENTION_DAYS (default: 90).
AggregatedMetric rows are never touched by this command.

TICKET-044 / ADR-035: this command runs one of two mechanisms, chosen at
runtime by analytics.timescale.is_event_hypertable():

  - TimescaleDB hypertable present -> `drop_chunks` (partition drop).
  - otherwise (plain Postgres, or SQLite as used by the test DB) -> the
    original table-scan DELETE, unchanged.

Both paths purge the exact same rows (created_at older than the retention
cutoff) -- the mechanism differs, the retention semantics do not. Both paths
always log which mode ran, via both stdout and the logger, so an operator
watching the cron log can tell "chunk-drop" from "table-scan delete" without
reading source (§XV-1). See docs/adr/ADR-035-timescaledb-migration.md.

Usage:
    python manage.py purge_events
"""

import logging
from datetime import timedelta

from django.conf import settings
from django.core.management.base import BaseCommand
from django.db import connections
from django.utils import timezone

from analytics.timescale import is_event_hypertable

logger = logging.getLogger('analytics.purge_events')


class Command(BaseCommand):
    help = 'Purge analytics_event rows older than ANALYTICS_EVENT_RETENTION_DAYS'

    def handle(self, *args, **options):
        retention_days = getattr(settings, 'ANALYTICS_EVENT_RETENTION_DAYS', 90)
        cutoff = timezone.now() - timedelta(days=retention_days)

        conn = connections['analytics']

        if is_event_hypertable(conn):
            self._purge_via_drop_chunks(conn, retention_days, cutoff)
        else:
            self._purge_via_delete(conn, retention_days, cutoff)

    def _purge_via_drop_chunks(self, conn, retention_days, cutoff):
        with conn.cursor() as cursor:
            cursor.execute(
                "SELECT drop_chunks('analytics_event', older_than => %s::interval)",
                [f'{retention_days} days'],
            )
            dropped = cursor.fetchall()
        chunk_count = len(dropped)

        logger.info(
            "purge_events: mode=drop_chunks (TimescaleDB hypertable); "
            "chunks_dropped=%s; retention_days=%s; cutoff=%s",
            chunk_count, retention_days, cutoff.date(),
        )
        self.stdout.write(
            self.style.SUCCESS(
                f'[TimescaleDB] mode=drop_chunks: dropped {chunk_count} chunk(s) '
                f'older than {retention_days} days (cutoff: {cutoff.date()}).'
            )
        )

    def _purge_via_delete(self, conn, retention_days, cutoff):
        ph = '%s' if conn.vendor == 'postgresql' else '?'
        with conn.cursor() as cursor:
            cursor.execute(
                f'DELETE FROM analytics_event WHERE created_at < {ph}',
                [cutoff.isoformat()],
            )
            count = cursor.rowcount

        logger.info(
            "purge_events: mode=DELETE (plain table, vendor=%s); rows_deleted=%s; "
            "retention_days=%s; cutoff=%s",
            conn.vendor, count, retention_days, cutoff.date(),
        )
        self.stdout.write(
            self.style.SUCCESS(
                f'[plain table] mode=DELETE: purged {count} event(s) older than '
                f'{retention_days} days (cutoff: {cutoff.date()}).'
            )
        )
