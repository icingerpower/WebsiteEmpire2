"""
Management command: check_purchase_mismatch

Nightly purchase-vs-order mismatch detector (spec §XV-1, ADR-004).

Finds orders with payment_status=PAID that have no corresponding analytics
'purchase' event within the same time window. Writes to stderr on mismatch
so that cron monitoring tools (Sentry, Cronitor, etc.) can alert on it.

Usage:
    python manage.py check_purchase_mismatch
    python manage.py check_purchase_mismatch --hours 48
"""

from datetime import timedelta

from django.core.management.base import BaseCommand
from django.db import connections
from django.utils import timezone

from orders.models import Order, PaymentStatus


class Command(BaseCommand):
    help = 'Detect paid orders that are missing a server-side purchase analytics event'

    def add_arguments(self, parser):
        parser.add_argument(
            '--hours',
            type=int,
            default=24,
            help='Look-back window in hours (default: 24).',
        )

    def handle(self, *args, **options):
        hours = options['hours']
        since = timezone.now() - timedelta(hours=hours)

        # Paid order IDs in the look-back window (across all stores).
        paid_order_ids = set(
            Order.objects.cross_store_unsafe()
            .filter(
                payment_status=PaymentStatus.PAID,
                created_at__gte=since,
            )
            .values_list('pk', flat=True)
        )

        if not paid_order_ids:
            self.stdout.write(
                self.style.SUCCESS(
                    f'OK: no paid orders in the last {hours}h.'
                )
            )
            return

        # Order IDs that have a purchase event in the analytics DB.
        conn = connections['analytics']
        ph = '%s' if conn.vendor == 'postgresql' else '?'
        with conn.cursor() as cursor:
            cursor.execute(
                f"SELECT DISTINCT order_id FROM analytics_event "
                f"WHERE event_type = 'purchase' AND created_at >= {ph}",
                [since.isoformat()],
            )
            analytics_order_ids = {row[0] for row in cursor.fetchall() if row[0] is not None}

        missing = sorted(paid_order_ids - analytics_order_ids)
        if missing:
            self.stderr.write(
                self.style.ERROR(
                    f'MISMATCH: {len(missing)} paid order(s) missing a purchase event '
                    f'(last {hours}h). First 10: {missing[:10]}'
                )
            )
        else:
            self.stdout.write(
                self.style.SUCCESS(
                    f'OK: all {len(paid_order_ids)} paid order(s) have purchase events '
                    f'(last {hours}h).'
                )
            )
