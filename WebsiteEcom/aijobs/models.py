"""
AI Job System models (ADR-003, TICKET-014).

Entities:
  AiJob            — one unit of intent (store-scoped, StoreOwnedModel)
  AiJobRun         — one execution attempt per job (store-scoped)
  AiJobOutput      — field_id-keyed output rows per run (store-scoped, ADR-003 §VII)
  AiJobValidation  — automated validation check rows per run (store-scoped, ADR-003 §XV-1)
  AiJobDependency  — DAG edges between jobs (NOT store-scoped — cross-store graph)
  AiJobMetric      — longitudinal cost/quality metrics (NOT store-scoped — soft store_id)
  StoreAiQuota     — monthly USD budget per store (store-scoped)

Design rules (ADR-003):
  - Status enum is explicitly NOT_STARTED/IN_PROGRESS/DONE/ERROR/SKIPPED (§XV-3,
    ADR-003 §2). Never infer state from absence of a row.
  - AiJobRun carries `last_heartbeat` for the scheduler's heartbeat-reclaim mechanism
    (dead > 3× interval => NOT_STARTED re-queue, ADR-003 §3). See service.reclaim_stale_runs.
  - AiJobOutput has one row per (run, field_id). Persisted ONLY when all ERROR validation
    checks pass (ADR-003 §6). On duplicate (run, field_id): UPDATE content (§I continuation).
  - AiJobValidation has one row per check per run. Written regardless of pass/fail so
    the dashboard shows why something failed (§XV-1). Human-review decisions belong to a
    separate model (phase-2 ticket), not here.
  - AiJobDependency and AiJobMetric do NOT extend StoreOwnedModel.
    They are added to EXEMPT_MODELS in core/tests/test_store_owned_model_compliance.py.
"""

from django.db import models

from core.models import StoreOwnedModel


class AiJobType(models.TextChoices):
    """Closed enum of supported AI job types (pluggable registry ADR-003 §7)."""

    PRODUCT_DESCRIPTION = "product_description", "Product Description"
    PRODUCT_SEO_TITLE = "product_seo_title", "Product SEO Title"
    PRODUCT_SEO_DESCRIPTION = "product_seo_description", "Product SEO Description"
    COLLECTION_DESCRIPTION = "collection_description", "Collection Description"
    IMAGE_ALT_TEXT = "image_alt_text", "Image Alt Text"
    REVIEW_RESPONSE = "review_response", "Review Response"
    TRANSLATION = "translation", "Translation"


class AiJobStatus(models.TextChoices):
    """
    Explicit status enum for AiJob and AiJobRun (ADR-003 §2, §XV-3).

    NOT_STARTED  — queued, no attempt made yet.
    IN_PROGRESS  — a runner holds this job; heartbeat must be updated regularly.
    DONE         — completed successfully (validated output persisted if applicable).
    ERROR        — failed (either runner error or validation failure); may be retried.
    SKIPPED      — idempotent short-circuit: output already exists for this target.
    """

    NOT_STARTED = "not_started", "Not Started"
    IN_PROGRESS = "in_progress", "In Progress"
    DONE = "done", "Done"
    ERROR = "error", "Error"
    SKIPPED = "skipped", "Skipped"


class AiJob(StoreOwnedModel):
    """
    One unit of AI work intent (ADR-003 §1).

    Each job targets one object (target_model + target_id) and one job type.
    A job may have many AiJobRun attempts; the scheduler picks the next pending
    job ordered by (-priority, created_at).

    Index on (store_id, status, priority) for efficient queue polling.

    Fields from the spec (ADR-003 §1):
      lang                   — for translation jobs, the target language (ISO 639-1).
      requires_human_review  — if True, DONE output waits in a review queue before
                               persist_output() is called (ADR-003 §8).
      review_verdict         — accept/reject/request_revision once reviewed.
      chunk_index/total_chunks — for proactive chunking of large inputs (ADR-003 §5).
      last_chunk_received_at — used to detect incomplete multi-chunk responses.
      created_by             — 'rule', 'trigger', or 'human' — the initiating source.
    """

    job_type = models.CharField(
        max_length=50,
        # No choices constraint — the plugin registry (aijobs/registry.py) is the
        # authority for valid job types; AiJobType enum kept for DB compat with existing rows.
    )
    status = models.CharField(
        max_length=20,
        choices=AiJobStatus.choices,
        default=AiJobStatus.NOT_STARTED,
    )
    target_model = models.CharField(
        max_length=100,
        help_text="Django app_label.ModelName of the target object, e.g. 'catalog.Product'.",
    )
    target_id = models.BigIntegerField(
        help_text="PK of the target object in target_model.",
    )
    lang = models.CharField(
        max_length=10,
        blank=True,
        default="",
        help_text="Target language for translation jobs (ISO 639-1). Empty for non-translation jobs.",
    )
    input_payload = models.JSONField(
        default=dict,
        help_text="Job input data (source text, context, instructions, etc.).",
    )
    requires_human_review = models.BooleanField(
        default=False,
        help_text=(
            "If True, a validated-DONE job enters the review queue before output "
            "is persisted. Translations default False; reviews/promos default True."
        ),
    )
    review_verdict = models.CharField(
        max_length=30,
        blank=True,
        default="",
        help_text="Set by human reviewer: accept, reject, or request_revision.",
    )
    priority = models.SmallIntegerField(
        default=0,
        help_text="Higher value = higher priority. Translations run at highest priority.",
    )
    max_retries = models.PositiveSmallIntegerField(default=3)
    retry_count = models.PositiveSmallIntegerField(default=0)
    chunk_index = models.IntegerField(
        default=0,
        help_text="Index of this chunk when a large input is split proactively (ADR-003 §5).",
    )
    total_chunks = models.IntegerField(
        default=1,
        help_text="Total number of chunks for this job. 1 means no chunking.",
    )
    last_chunk_received_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Timestamp of the last received chunk (multi-chunk jobs only).",
    )
    created_by = models.CharField(
        max_length=100,
        blank=True,
        default="",
        help_text="Source that initiated this job: 'rule', 'trigger', or 'human'.",
    )
    scheduled_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="If set, do not process before this datetime (deferred jobs).",
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        indexes = [
            models.Index(
                fields=["store", "status", "priority"],
                name="aijob_store_status_pri_idx",
            ),
        ]
        verbose_name = "AI job"
        verbose_name_plural = "AI jobs"

    def __str__(self):
        return f"AiJob {self.pk} [{self.job_type}] {self.status}"


class AiJobRun(StoreOwnedModel):
    """
    One execution attempt for an AiJob (ADR-003 §1).

    A job has many runs; each retry creates a new AiJobRun. The scheduler
    reclaims runs whose last_heartbeat is older than 3x the heartbeat interval,
    marking them ERROR(reason=dead) and re-queuing the parent job (ADR-003 §3).

    `runner` matches the runner types from ADR-003 §7:
      claude_code / codex / gemini_terminal / api (fallback).
    `model_id` is the specific model used (e.g. 'claude-sonnet-4-5').
    """

    job = models.ForeignKey(
        AiJob,
        on_delete=models.CASCADE,
        related_name="runs",
    )
    runner = models.CharField(
        max_length=50,
        blank=True,
        default="",
        help_text="Runner type: claude_code, codex, gemini_terminal, or api (fallback).",
    )
    model_id = models.CharField(
        max_length=100,
        blank=True,
        default="",
        help_text="Specific model used, e.g. 'claude-sonnet-4-5'.",
    )
    prompt_tokens = models.IntegerField(default=0)
    completion_tokens = models.IntegerField(default=0)
    total_cost_usd = models.DecimalField(
        max_digits=12,
        decimal_places=6,
        default=0,
    )
    status = models.CharField(
        max_length=20,
        choices=AiJobStatus.choices,
        default=AiJobStatus.NOT_STARTED,
    )
    error_message = models.TextField(blank=True, default="")
    started_at = models.DateTimeField(auto_now_add=True)
    last_heartbeat = models.DateTimeField(
        null=True,
        blank=True,
        help_text=(
            "Updated by the runner every N seconds. "
            "Scheduler reclaims runs dead > 3x heartbeat interval (ADR-003 §3)."
        ),
    )
    completed_at = models.DateTimeField(null=True, blank=True)

    class Meta:
        verbose_name = "AI job run"
        verbose_name_plural = "AI job runs"

    def __str__(self):
        return f"AiJobRun {self.pk} [job={self.job_id}] {self.status}"


class AiJobOutput(StoreOwnedModel):
    """
    One field-keyed output row from a completed AiJobRun (ADR-003 §1, §VII).

    Multiple rows per run, one per field_id. Persisted ONLY after all ERROR
    validation checks pass (ADR-003 §6). On duplicate (run, field_id):
    UPDATE content — never silently drop (§I continuation rule).

    field_id — stable identifier e.g. 'title', 'description', 'bullet_0',
               or 'item_42_label'. Never positions or display values (§VII).
    is_accepted — True once accepted and applied to the target object.
                  Updated externally by the human-review flow when
                  job.requires_human_review is True; defaults to True for
                  auto-publish jobs.
    """

    run = models.ForeignKey(
        AiJobRun,
        on_delete=models.CASCADE,
        related_name="outputs",
    )
    field_id = models.CharField(
        max_length=100,
        help_text="Stable field identifier, e.g. 'title', 'description', 'bullet_0'.",
    )
    content = models.TextField(
        help_text="AI-generated content for this field.",
    )
    is_accepted = models.BooleanField(
        default=True,
        help_text="True=accepted and applied to the target; False=rejected.",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "AI job output"
        verbose_name_plural = "AI job outputs"
        constraints = [
            models.UniqueConstraint(
                fields=["run", "field_id"],
                name="unique_aijoboutput_run_field_id",
            ),
        ]

    def __str__(self):
        return f"AiJobOutput {self.pk} [run={self.run_id}] {self.field_id}"


class AiJobValidation(StoreOwnedModel):
    """
    Automated validation check result for a completed AiJobRun (ADR-003 §6, §XV-1).

    One row per check per run, written regardless of pass/fail so the dashboard
    shows exactly why something failed. Human-review accept/reject decisions belong
    to a separate model (phase-2 ticket) keyed on AiJobOutput, not here.

    check_name   — e.g. 'min_length', 'max_length', 'bracket_balance',
                   'length_ratio', 'field_id_dedup'.
    passed       — True if the check passed, False if it failed.
    details_json — check-specific context, e.g. {'actual': 42, 'minimum': 50}.
    severity     — ERROR: if failed, blocks AiJobOutput commit and marks run ERROR.
                   WARNING: if failed, recorded but does NOT block commit.
    """

    SEVERITY_ERROR = "ERROR"
    SEVERITY_WARNING = "WARNING"

    run = models.ForeignKey(
        AiJobRun,
        on_delete=models.CASCADE,
        related_name="validations",
    )
    check_name = models.CharField(
        max_length=100,
        help_text="Identifier for the check, e.g. 'min_length', 'bracket_balance'.",
    )
    passed = models.BooleanField(
        help_text="True if this check passed; False if it failed.",
    )
    details_json = models.JSONField(
        default=dict,
        help_text="Check-specific details, e.g. {'actual': 42, 'minimum': 50}.",
    )
    severity = models.CharField(
        max_length=10,
        choices=[
            (SEVERITY_ERROR, "Error — blocks commit"),
            (SEVERITY_WARNING, "Warning — recorded only"),
        ],
        default=SEVERITY_ERROR,
    )
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "AI job validation"
        verbose_name_plural = "AI job validations"

    def __str__(self):
        status = "PASS" if self.passed else "FAIL"
        return f"AiJobValidation {self.pk} [{self.check_name}] {status} ({self.severity})"


class AiJobDependency(models.Model):
    """
    DAG edge between two AiJobs (ADR-003 §1, phase-2 pipeline design).

    NOT a StoreOwnedModel: the job graph is scheduler-global; jobs from the
    same store form dependencies, and the scheduler evaluates them without
    per-store scoping. Listed in EXEMPT_MODELS.

    upstream_job must reach DONE before the scheduler starts downstream_job.
    Loop creation is rejected by the scheduler before inserting a new edge.
    """

    upstream_job = models.ForeignKey(
        AiJob,
        on_delete=models.CASCADE,
        related_name="downstream_deps",
    )
    downstream_job = models.ForeignKey(
        AiJob,
        on_delete=models.CASCADE,
        related_name="upstream_deps",
    )

    class Meta:
        verbose_name = "AI job dependency"
        verbose_name_plural = "AI job dependencies"
        constraints = [
            models.UniqueConstraint(
                fields=["upstream_job", "downstream_job"],
                name="unique_aijob_dependency",
            ),
        ]

    def __str__(self):
        return f"AiJobDependency {self.upstream_job_id} -> {self.downstream_job_id}"


class AiJobMetric(models.Model):
    """
    Daily cost/quality metrics aggregated per store x job_type x model (ADR-003 §1).

    NOT a StoreOwnedModel: metrics are platform-global aggregates used for
    cross-store reporting and quota dashboards. Uses soft store_id (integer)
    rather than a FK into the main DB, so metrics survive store deletion.
    Listed in EXEMPT_MODELS.

    Written by record_cost() after each AiJobRun completes, using F() expressions
    for atomic counter updates (no race conditions under concurrent runners).
    """

    period_date = models.DateField(
        help_text="The date (YYYY-MM-DD) this metric row covers.",
    )
    store_id = models.IntegerField(
        help_text="Soft reference to the store (no FK — metrics survive store deletion).",
    )
    job_type = models.CharField(max_length=100)
    model_id = models.CharField(max_length=100)
    run_count = models.IntegerField(default=0)
    prompt_tokens = models.IntegerField(default=0)
    completion_tokens = models.IntegerField(default=0)
    total_cost_usd = models.DecimalField(
        max_digits=12,
        decimal_places=6,
        default=0,
    )

    class Meta:
        verbose_name = "AI job metric"
        verbose_name_plural = "AI job metrics"
        constraints = [
            models.UniqueConstraint(
                fields=["period_date", "store_id", "job_type", "model_id"],
                name="unique_aijobmetric_period",
            ),
        ]

    def __str__(self):
        return f"AiJobMetric [{self.period_date}] store={self.store_id} {self.job_type}"


class StoreAiQuota(StoreOwnedModel):
    """
    Monthly USD budget per store for AI job usage (ADR-003 §7, FM-C2 DECIDED 2026-07-02).

    Exactly one row per store (enforced via UniqueConstraint on store).
    When the current month rolls over, check_quota() resets current_month_spent_usd
    atomically before evaluating the budget.

    is_budget_enforced=False allows unlimited AI usage for that store (useful for
    trial stores or stores with a negotiated flat rate). Spending is still tracked.
    """

    monthly_budget_usd = models.DecimalField(
        max_digits=10,
        decimal_places=2,
        default=0,
        help_text="Maximum USD spend per calendar month. 0 with enforced=False means unlimited.",
    )
    current_month_spent_usd = models.DecimalField(
        max_digits=10,
        decimal_places=2,
        default=0,
        help_text="Running USD total for the current calendar month.",
    )
    current_month = models.DateField(
        help_text="The first day of the month this counter applies to (YYYY-MM-01).",
    )
    is_budget_enforced = models.BooleanField(
        default=True,
        help_text=(
            "If False, jobs run regardless of monthly_budget_usd. "
            "Spending is still tracked for reporting."
        ),
    )

    class Meta:
        verbose_name = "store AI quota"
        verbose_name_plural = "store AI quotas"
        constraints = [
            models.UniqueConstraint(
                fields=["store"],
                name="unique_store_ai_quota",
            ),
        ]

    def __str__(self):
        return f"StoreAiQuota [store={self.store_id}] {self.current_month}"
