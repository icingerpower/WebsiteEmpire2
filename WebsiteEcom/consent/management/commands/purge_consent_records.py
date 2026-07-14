"""
Operator CLI command: ConsentRecord retention purge (ADR-025 D2/D7/P4).

Shares consent.services.purge_consent_records() with the Celery beat task
(consent/tasks.py) — no DELETE logic is duplicated between the two entry
points.

Usage:
    python3 manage.py purge_consent_records
"""

from django.core.management.base import BaseCommand

from consent.services import purge_consent_records


class Command(BaseCommand):
    help = "Delete ConsentRecord rows older than the 13-month retention window (ADR-025 D2/P4)."

    def handle(self, *args, **options):
        count = purge_consent_records()
        self.stdout.write(self.style.SUCCESS(f"Purged {count} ConsentRecord row(s)."))
