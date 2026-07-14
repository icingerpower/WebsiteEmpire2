"""
import_tracking — bulk-import tracking numbers from a CSV file (T010, AC-062).

Usage:
    python manage.py import_tracking <csv_file_path>

CSV format (header row required, case-sensitive column names):
    order_number, tracking_number, carrier

Behaviour per row:
- Look up Order by order_number across all stores (cross_store_unsafe).
  If multiple stores share the same order_number, the row is skipped with
  an error message — use a store-scoped import for disambiguation.
- Create or update an OrderFulfillment for (order, tracking_number).
  On create: carrier and status='shipped' are set; store is copied from the order.
  On update: carrier and status are refreshed; store is not changed.
- Print success/skip/error per row.
- Print a summary line at the end: N succeeded, M failed.

Design notes:
- Uses cross_store_unsafe() intentionally — this is a super-admin / ops tool.
  Store-admin staff should use the admin inline instead.
- tracking_number is the natural key for get-or-create; empty tracking numbers
  are rejected immediately.
- Rows with unknown order_number produce an error line but do not abort the run.
"""

import csv
import os

from django.core.management.base import BaseCommand, CommandError

from orders.models import FulfillmentRecordStatus, Order, OrderFulfillment


class Command(BaseCommand):
    help = (
        "Import tracking numbers from a CSV and mark matching orders as shipped. "
        "CSV columns (header required): order_number, tracking_number, carrier"
    )

    def add_arguments(self, parser):
        parser.add_argument(
            'csv_file',
            type=str,
            help='Absolute path to the CSV file to import.',
        )

    def handle(self, *args, **options):
        csv_path = options['csv_file']
        if not os.path.isfile(csv_path):
            raise CommandError(f"File not found: {csv_path}")

        succeeded = 0
        failed = 0

        with open(csv_path, newline='', encoding='utf-8') as fh:
            reader = csv.DictReader(fh)
            required_headers = {'order_number', 'tracking_number', 'carrier'}
            actual_headers = set(reader.fieldnames or [])
            if not required_headers.issubset(actual_headers):
                missing = required_headers - actual_headers
                raise CommandError(
                    f"CSV is missing required columns: {', '.join(sorted(missing))}. "
                    f"Found: {', '.join(sorted(actual_headers)) or '(none)'}"
                )

            for row_num, row in enumerate(reader, start=2):  # row 1 = header
                order_number = (row.get('order_number') or '').strip()
                tracking_number = (row.get('tracking_number') or '').strip()
                carrier = (row.get('carrier') or '').strip()

                if not order_number:
                    self.stderr.write(f"Row {row_num}: empty order_number — skipped.")
                    failed += 1
                    continue

                if not tracking_number:
                    self.stderr.write(
                        f"Row {row_num}: empty tracking_number for order {order_number!r} — skipped."
                    )
                    failed += 1
                    continue

                # Resolve the order — cross-store since this is an ops command.
                try:
                    order = (
                        Order.objects.cross_store_unsafe()
                        .get(order_number=order_number)
                    )
                except Order.DoesNotExist:
                    self.stderr.write(
                        f"Row {row_num}: order {order_number!r} not found — skipped."
                    )
                    failed += 1
                    continue
                except Order.MultipleObjectsReturned:
                    self.stderr.write(
                        f"Row {row_num}: multiple orders found for {order_number!r} — skipped. "
                        "Use a store-scoped import for disambiguation."
                    )
                    failed += 1
                    continue

                # Create or update the fulfillment record for this tracking number.
                qs = OrderFulfillment.objects.cross_store_unsafe()
                try:
                    fulfillment = qs.get(order=order, tracking_number=tracking_number)
                    fulfillment.carrier = carrier
                    fulfillment.status = FulfillmentRecordStatus.SHIPPED
                    fulfillment.save(update_fields=['carrier', 'status'])
                    action = "updated"
                except OrderFulfillment.DoesNotExist:
                    OrderFulfillment.objects.create(
                        store=order.store,
                        order=order,
                        tracking_number=tracking_number,
                        carrier=carrier,
                        status=FulfillmentRecordStatus.SHIPPED,
                    )
                    action = "created"

                self.stdout.write(
                    f"Row {row_num}: order {order_number} — fulfillment {action} "
                    f"(tracking: {tracking_number}, carrier: {carrier})."
                )
                succeeded += 1

        summary = f"Import complete: {succeeded} succeeded, {failed} failed."
        if failed:
            self.stdout.write(self.style.WARNING(summary))
        else:
            self.stdout.write(self.style.SUCCESS(summary))
