"""
manage.py seed_static_pages [--store <id>] [--all] (ADR-018 D5).

Idempotent backfill of the 7 draft StaticPages for existing stores, and repair
of accidental deletions. New stores are seeded automatically at creation time by
pages/signals.py `seed_static_pages_for_new_store` — this command exists for
stores that already existed before this feature shipped, or whose seed pages
were deleted.

Safe to run any number of times: get_or_create() on (store, slug) never creates
duplicates.
"""

from django.core.management.base import BaseCommand, CommandError

from pages.seeding import seed_pages_for_store
from stores.models import Store


class Command(BaseCommand):
    help = "Seed the 7 draft static pages (about/contact/quotation/policies) for stores."

    def add_arguments(self, parser):
        parser.add_argument(
            "--store", type=int, default=None,
            help="Seed only the store with this PK.",
        )
        parser.add_argument(
            "--all", action="store_true",
            help="Seed all stores (default when neither --store nor --all is given).",
        )

    def handle(self, *args, **options):
        store_id = options.get("store")
        if store_id is not None:
            try:
                stores = [Store.objects.get(pk=store_id)]
            except Store.DoesNotExist:
                raise CommandError(f"No store with pk={store_id!r}.")
        else:
            stores = list(Store.objects.all())

        total_created = 0
        for store in stores:
            created = seed_pages_for_store(store)
            total_created += created
            self.stdout.write(
                f"Store {store.pk} ({store.name}): {created} page(s) created."
            )

        self.stdout.write(self.style.SUCCESS(
            f"Done. {total_created} page(s) created across {len(stores)} store(s)."
        ))
