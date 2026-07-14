"""
Management command: check_csv_list_corruption

One-off detector for ADR-032 D5's CsvListWidget corruption bug: before this
fix, `forms.JSONField.prepare_value()` unconditionally `json.dumps()`'d an
empty `[]` into the literal textarea text "[]"; `CsvListWidget
.value_from_datadict()` then CSV-split ANY save of that unchanged textarea
(including a pure no-op save of an already-empty product) into the list
`['[]']`, silently persisting a corrupted value instead of the field's own
`[]` default. This never wrote arbitrary garbage — only ever the exact
literal string '[]' as one element of the list — so detection is a plain
membership check, not a JSON-shape heuristic.

Usage:
    python manage.py check_csv_list_corruption

Writes to stderr (non-zero-signal via style.ERROR, matching
analytics.check_purchase_mismatch's cron-monitoring convention) if any
corrupted row is found, naming the model and pk so an operator can decide
whether to hand-fix or write a targeted data migration (ADR-032 rollback
strategy: keep any real cleanup as its own commit, separate from the code
fix, so it survives a mixin revert). This command performs no writes itself
— detection only.
"""

from django.core.management.base import BaseCommand

from catalog.models import Product
from stores.models import Organization

CORRUPTION_TOKEN = "[]"


class Command(BaseCommand):
    help = "Detect CsvListWidget '[]'-corrupted rows in Product.tags / Organization.coverage_areas_json"

    def handle(self, *args, **options):
        corrupted_products = [
            product.pk
            for product in Product.objects.cross_store_unsafe().only("pk", "tags")
            if isinstance(product.tags, list) and CORRUPTION_TOKEN in product.tags
        ]
        corrupted_orgs = [
            org.pk
            for org in Organization.objects.all().only("pk", "coverage_areas_json")
            if isinstance(org.coverage_areas_json, list) and CORRUPTION_TOKEN in org.coverage_areas_json
        ]

        if not corrupted_products and not corrupted_orgs:
            self.stdout.write(self.style.SUCCESS("OK: no CsvListWidget '[]' corruption found."))
            return

        if corrupted_products:
            self.stderr.write(
                self.style.ERROR(
                    f"CORRUPTION: {len(corrupted_products)} catalog.Product row(s) "
                    f"with a literal '[]' entry in tags. pks: {corrupted_products[:20]}"
                )
            )
        if corrupted_orgs:
            self.stderr.write(
                self.style.ERROR(
                    f"CORRUPTION: {len(corrupted_orgs)} stores.Organization row(s) "
                    f"with a literal '[]' entry in coverage_areas_json. pks: {corrupted_orgs[:20]}"
                )
            )
