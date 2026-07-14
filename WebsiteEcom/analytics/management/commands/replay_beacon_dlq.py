"""
Management command: replay_beacon_dlq

Re-drives unresolved BeaconDlq rows into the analytics DB.

Usage:
    python manage.py replay_beacon_dlq
    python manage.py replay_beacon_dlq --limit 200

On success: row.resolved is set to True.
On failure: row.attempt_count is incremented and error_message is updated.
            last_attempted_at is refreshed automatically (auto_now field).

Exit code is always 0 — individual row failures are logged, not raised, so
that a cron job processes all remaining rows rather than aborting on the first
bad one.
"""

import logging

from django.core.management.base import BaseCommand

from analytics.models import BeaconDlq
from analytics.tasks import _write_beacon_batch

logger = logging.getLogger('analytics.management')


class Command(BaseCommand):
    help = 'Re-drive unresolved BeaconDlq rows into the analytics DB.'

    def add_arguments(self, parser):
        parser.add_argument(
            '--limit',
            type=int,
            default=500,
            help='Maximum number of rows to process per run (default: 500).',
        )

    def handle(self, *args, **options):
        limit = options['limit']
        qs = (
            BeaconDlq.objects.using('analytics')
            .filter(resolved=False)
            .order_by('created_at')[:limit]
        )

        ok = 0
        fail = 0

        for row in qs:
            try:
                _write_beacon_batch(row.payload_json)
                row.resolved = True
                row.save(using='analytics', update_fields=['resolved', 'last_attempted_at'])
                ok += 1
            except Exception as exc:
                row.attempt_count += 1
                row.error_message = str(exc)
                row.save(
                    using='analytics',
                    update_fields=['attempt_count', 'error_message', 'last_attempted_at'],
                )
                logger.warning('DLQ replay failed for row %d: %s', row.pk, exc)
                fail += 1

        msg = f'DLQ replay: {ok} resolved, {fail} failed'
        self.stdout.write(msg)
        logger.info(msg)
