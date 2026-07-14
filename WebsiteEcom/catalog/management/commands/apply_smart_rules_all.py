"""
Management command: re-evaluate smart collection rules for all active products.

Run nightly via cron to catch cases where variant prices change without triggering
the parent product's post_save signal (e.g. bulk price updates via the ORM with
update_fields that does not include the relevant variant-change fields, or direct
DB writes).  This is the nightly sweep complementing the per-save signals.

Usage:
    python manage.py apply_smart_rules_all
    python manage.py apply_smart_rules_all --store-id 42
"""

from django.core.management.base import BaseCommand


class Command(BaseCommand):
    help = (
        "Re-evaluate smart collection rules for all active products. "
        "Run nightly via cron."
    )

    def add_arguments(self, parser):
        parser.add_argument(
            "--store-id",
            type=int,
            default=None,
            metavar="STORE_PK",
            help="Limit re-evaluation to a single store (useful for testing).",
        )

    def handle(self, *args, **options):
        # Deferred imports: management commands load before apps are fully ready
        # in some test configurations; deferred imports avoid circular issues.
        from catalog.models import Product
        from catalog.signals import apply_smart_rules_for_product

        store_id = options["store_id"]

        # cross_store_unsafe() is required: there is no request.store context in a
        # management command, and StoreScopedManager raises IsolationError on
        # unscoped access (ADR-001 §4).
        qs = Product.objects.cross_store_unsafe().filter(status="active")
        if store_id is not None:
            qs = qs.filter(store_id=store_id)

        # select_related('store') avoids N+1 queries when apply_smart_rules_for_product
        # accesses product.store to scope collection lookups.
        qs = qs.select_related("store")

        evaluated = 0
        for product in qs:
            apply_smart_rules_for_product(product)
            evaluated += 1

        suffix = f" (store_id={store_id})" if store_id is not None else ""
        self.stdout.write(
            self.style.SUCCESS(
                f"Re-evaluated smart rules for {evaluated} active products{suffix}."
            )
        )
