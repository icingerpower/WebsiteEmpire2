"""
Management command: run_ai_jobs

Processes NOT_STARTED AiJobs ordered by priority DESC, then created_at ASC
(highest priority first — ADR-003 §1).

For each job the command:
  1. Claims it atomically (status → IN_PROGRESS, started_at = now).
  2. Calls build_prompt(job) via the aijobs registry.
  3. Writes the prompt to stdout (delimited) for the external CLI runner to read.
  4. Reads the AI response for this job from stdin (one response per job).
  5. Calls persist_output_from_text(job, response_text) to validate and store
     the result.
  6. On success: marks the job DONE.
  7. On error: records the error; if retries remain, requeues as NOT_STARTED
     (retry_count + 1); otherwise marks FAILED permanently.

--dry-run mode:
  Builds and prints the prompt for each eligible job without claiming, running
  the AI runner, or persisting any output.  Job status is never changed.

Stdin requirement:
  In non-dry-run mode the command MUST be invoked with the external CLI runner
  piping AI responses through stdin.  If stdin is a terminal (not piped), the
  command prints an error message and exits immediately without processing any jobs.

  Usage:
      external-cli-runner | python3 manage.py run_ai_jobs [--store-id N] \\
          [--job-type translation] [--limit 50]

  Each prompt is written to stdout wrapped in delimiters:
      --- JOB {pk} PROMPT START ---
      {prompt text}
      --- JOB {pk} PROMPT END ---

  The external runner reads the prompt block, sends it to the AI model, and
  writes the response as a single JSON line (no embedded newlines) to the
  command's stdin, terminated by '\\n'.  The command reads exactly one response
  line per job.

  The mock runner is intentionally removed: fabricating AI responses and
  publishing them to production records would corrupt live storefronts.
  Use --dry-run to preview prompts without risking production data.
"""

import sys

from django.core.management.base import BaseCommand
from django.db import transaction
from django.utils import timezone

from aijobs.models import AiJob, AiJobStatus
from aijobs.registry import get_job_type
from catalog.ai_jobs import (
    TRANSLATION_JOB_TYPE,
    TranslationPersistError,
    persist_output_from_text,
)

# Delimiter used to frame each prompt block on stdout so the external runner
# can parse individual prompts reliably.
_PROMPT_START = "--- JOB {pk} PROMPT START ---"
_PROMPT_END = "--- JOB {pk} PROMPT END ---"


class Command(BaseCommand):
    help = (
        "Process NOT_STARTED AiJobs: build prompts, read AI responses from stdin, "
        "and persist translation results.  Use --dry-run to preview prompts only."
    )

    def add_arguments(self, parser):
        parser.add_argument(
            "--store-id",
            type=int,
            default=None,
            metavar="STORE_PK",
            help="Limit processing to a single store.",
        )
        parser.add_argument(
            "--job-type",
            type=str,
            default=None,
            metavar="JOB_TYPE",
            help=(
                "Limit processing to a specific job type "
                f"(default: all registered types; use '{TRANSLATION_JOB_TYPE}' for translations)."
            ),
        )
        parser.add_argument(
            "--limit",
            type=int,
            default=50,
            metavar="N",
            help="Maximum number of jobs to process in this run (default: 50).",
        )
        parser.add_argument(
            "--dry-run",
            action="store_true",
            default=False,
            help=(
                "Print the prompt for each job without claiming it, reading stdin, "
                "or persisting any output. Job status is not changed."
            ),
        )

    def handle(self, *args, **options):
        store_id = options["store_id"]
        job_type_filter = options["job_type"]
        limit = options["limit"]
        dry_run = options["dry_run"]

        # Non-dry-run requires a piped stdin (the external CLI runner).
        # If stdin is a terminal, refuse to proceed — publishing mock AI output
        # to production records would corrupt the storefront.
        if not dry_run and sys.stdin.isatty():
            self.stderr.write(
                self.style.ERROR(
                    "run_ai_jobs requires a CLI runner piping responses via stdin. "
                    "Use --dry-run to preview prompts."
                )
            )
            return

        qs = (
            AiJob.objects
            .cross_store_unsafe()
            .filter(status=AiJobStatus.NOT_STARTED)
            .select_related("store")
            .order_by("-priority", "created_at")
        )
        if store_id is not None:
            qs = qs.filter(store_id=store_id)
        if job_type_filter is not None:
            qs = qs.filter(job_type=job_type_filter)

        jobs = list(qs[:limit])

        if not jobs:
            self.stdout.write("No NOT_STARTED jobs to process.")
            return

        processed = 0
        skipped = 0
        failed = 0

        for job in jobs:
            self.stdout.write(
                f"Processing job #{job.pk} "
                f"(type: {job.job_type}, target: {job.target_model}#{job.target_id}, "
                f"lang: {job.lang or 'n/a'})..."
            )

            if dry_run:
                self._dry_run_job(job)
                skipped += 1
                continue

            result = self._process_job(job)
            if result:
                processed += 1
            else:
                failed += 1

        suffix = f" (store_id={store_id})" if store_id is not None else ""
        if dry_run:
            self.stdout.write(
                self.style.SUCCESS(
                    f"Dry run: inspected {skipped} job(s){suffix}. No changes made."
                )
            )
        else:
            self.stdout.write(
                self.style.SUCCESS(
                    f"Processed {processed} job(s), {failed} failed{suffix}."
                )
            )

    # ------------------------------------------------------------------
    # Dry-run
    # ------------------------------------------------------------------

    def _dry_run_job(self, job):
        """Build the prompt and print it; do not claim the job or read stdin."""
        try:
            defn = get_job_type(job.job_type)
        except KeyError:
            self.stdout.write(
                self.style.WARNING(
                    f"  [DRY-RUN] Job #{job.pk}: unknown job type {job.job_type!r}, skipping."
                )
            )
            return

        try:
            prompt = defn.build_prompt(job)
        except Exception as exc:
            self.stdout.write(
                self.style.WARNING(f"  [DRY-RUN] Job #{job.pk}: build_prompt failed — {exc}")
            )
            return

        self.stdout.write(f"  [DRY-RUN] Prompt for job #{job.pk}:\n{prompt}\n")

    # ------------------------------------------------------------------
    # Normal processing (stdin-driven)
    # ------------------------------------------------------------------

    def _process_job(self, job):
        """
        Claim the job, write prompt to stdout, read response from stdin, persist.

        Returns True on success, False on failure.

        The external CLI runner is responsible for reading the prompt block from
        stdout and writing a single JSON-line response to stdin.  This command
        reads exactly one line per job (terminated by '\\n').
        """
        # Step 1: Claim atomically.  select_for_update prevents two concurrent
        # command instances from double-claiming the same job.
        with transaction.atomic():
            try:
                locked_job = (
                    AiJob.objects
                    .cross_store_unsafe()
                    .select_for_update(nowait=True)
                    .get(pk=job.pk, status=AiJobStatus.NOT_STARTED)
                )
            except AiJob.DoesNotExist:
                # Another runner claimed it between our queryset and now — skip.
                self.stdout.write(
                    self.style.WARNING(f"  Job #{job.pk}: already claimed by another runner, skipping.")
                )
                return False

            locked_job.status = AiJobStatus.IN_PROGRESS
            locked_job.save(update_fields=["status", "updated_at"])

        # Step 2: Build prompt.
        try:
            defn = get_job_type(job.job_type)
        except KeyError:
            self._fail_job(job, f"Unknown job type: {job.job_type!r}")
            return False

        try:
            prompt = defn.build_prompt(job)
        except Exception as exc:
            self._fail_job(job, f"build_prompt error: {exc}")
            return False

        # Step 3: Write prompt to stdout (delimited) for the external CLI runner.
        self.stdout.write(_PROMPT_START.format(pk=job.pk))
        self.stdout.write(prompt)
        self.stdout.write(_PROMPT_END.format(pk=job.pk))
        # Flush immediately so the external runner can read the prompt without waiting.
        self.stdout.flush()

        # Step 4: Read the AI response from stdin (one JSON line per job).
        try:
            response_text = sys.stdin.readline()
        except Exception as exc:
            self._fail_job(job, f"stdin read error: {exc}")
            return False

        if not response_text.strip():
            self._fail_job(job, "Empty response received from stdin.")
            return False

        # Step 5: Persist output.
        try:
            persist_output_from_text(job, response_text.strip())
        except TranslationPersistError as exc:
            self._fail_job(job, f"persist_output error: {exc}")
            return False
        except Exception as exc:
            self._fail_job(job, f"unexpected persist error: {exc}")
            return False

        # Step 6: Mark DONE.
        AiJob.objects.cross_store_unsafe().filter(pk=job.pk).update(
            status=AiJobStatus.DONE,
            updated_at=timezone.now(),
        )
        self.stdout.write(self.style.SUCCESS(f"  Job #{job.pk}: DONE."))
        return True

    def _fail_job(self, job, reason):
        """
        Record a failure on the job.

        If retries remain, reset to NOT_STARTED with incremented retry_count
        (the scheduler will pick it up again).  If retries exhausted, set
        status=ERROR permanently.
        """
        self.stdout.write(self.style.ERROR(f"  Job #{job.pk}: FAILED — {reason}"))

        if job.retry_count < job.max_retries:
            AiJob.objects.cross_store_unsafe().filter(pk=job.pk).update(
                status=AiJobStatus.NOT_STARTED,
                retry_count=job.retry_count + 1,
                updated_at=timezone.now(),
            )
            self.stdout.write(
                f"  Job #{job.pk}: requeued (retry {job.retry_count + 1}/{job.max_retries})."
            )
        else:
            AiJob.objects.cross_store_unsafe().filter(pk=job.pk).update(
                status=AiJobStatus.ERROR,
                updated_at=timezone.now(),
            )
            self.stdout.write(
                self.style.ERROR(f"  Job #{job.pk}: max retries exhausted — permanently FAILED.")
            )
