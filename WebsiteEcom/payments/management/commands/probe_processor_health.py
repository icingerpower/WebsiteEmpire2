"""
Management command: probe all active processor accounts and update health status.

Calls probe_all_processor_accounts() synchronously — suitable for cron jobs,
manual operations, and testing without a running Celery worker.

Usage:
    python manage.py probe_processor_health
    python manage.py probe_processor_health --verbosity=2
"""

from django.core.management.base import BaseCommand

from payments.tasks import probe_all_processor_accounts


class Command(BaseCommand):
    help = "Probe all active processor accounts and update their health status."

    def handle(self, *args, **options):
        verbosity = options.get("verbosity", 1)

        if verbosity >= 1:
            self.stdout.write("Probing processor account health…")

        result = probe_all_processor_accounts()

        if verbosity >= 1:
            self.stdout.write(
                self.style.SUCCESS(
                    f"Done. probed={result['probed']} skipped={result['skipped']} "
                    f"errors={result['errors']} stale={result['stale']}"
                )
            )

        if result["errors"] > 0:
            self.stderr.write(
                self.style.WARNING(
                    f"{result['errors']} processor account(s) failed the health probe. "
                    "Check logs for details."
                )
            )
