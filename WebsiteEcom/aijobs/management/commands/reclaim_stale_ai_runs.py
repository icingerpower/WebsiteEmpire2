"""
Management command: reclaim_stale_ai_runs

Finds IN_PROGRESS AiJobRun rows whose last_heartbeat is older than
STALE_MINUTES and resets them to NOT_STARTED (ADR-003 §3, AC-223).

Intended to be run periodically by a cron job or scheduler
(e.g. every 5 minutes) to recover from dead runners.

Usage:
    python manage.py reclaim_stale_ai_runs
    python manage.py reclaim_stale_ai_runs --stale-minutes 15
"""

from django.core.management.base import BaseCommand

from aijobs.service import reclaim_stale_runs


class Command(BaseCommand):
    help = (
        "Reclaim IN_PROGRESS AI job runs whose heartbeat is older than "
        "STALE_MINUTES (default 10) and reset them to NOT_STARTED."
    )

    def add_arguments(self, parser):
        parser.add_argument(
            "--stale-minutes",
            type=int,
            default=10,
            metavar="MINUTES",
            help=(
                "Minutes without a heartbeat before a run is considered dead. "
                "Default: 10 (≈ 3× a 3-minute heartbeat interval)."
            ),
        )

    def handle(self, *args, **options):
        stale_minutes = options["stale_minutes"]
        count = reclaim_stale_runs(stale_minutes=stale_minutes)
        if count == 0:
            self.stdout.write("No stale AI job runs to reclaim.")
        else:
            self.stdout.write(
                self.style.SUCCESS(
                    f"Reclaimed {count} stale AI job run(s) "
                    f"(heartbeat older than {stale_minutes} minutes)."
                )
            )
