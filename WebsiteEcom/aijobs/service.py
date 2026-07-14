"""
AI Job System service functions (ADR-003, TICKET-014).

Scheduler and stale-run reclaim are implemented here (T014/T015).
Celery-based async dispatch is a separate infrastructure concern deferred to production setup.

Public API:
  create_job(store, job_type, target_model, target_id, input_payload, ...) -> AiJob
  check_quota(store) -> bool
  record_cost(job_run) -> None
  reclaim_stale_runs(stale_minutes=10) -> int
"""

from datetime import timedelta

from django.db import transaction
from django.db.models import F
from django.utils import timezone

from aijobs.models import AiJob, AiJobMetric, AiJobRun, AiJobStatus, StoreAiQuota
from aijobs.registry import get_job_type


@transaction.atomic
def create_job(
    store,
    job_type,
    target_model,
    target_id,
    input_payload,
    priority=None,
    scheduled_at=None,
    lang="",
    requires_human_review=False,
    created_by="",
):
    """
    Create and persist a new AiJob for the given store.

    Uses AiJob(...).save() directly — bypasses StoreScopedManager since save()
    does not go through the manager's queryset. This is the correct creation
    pattern for StoreOwnedModel subclasses.

    Args:
        store:                The Store instance this job belongs to.
        job_type:             An AiJobType value (e.g. AiJobType.TRANSLATION).
        target_model:         app_label.ModelName of the target, e.g. 'catalog.Product'.
        target_id:            PK of the target object.
        input_payload:        dict — source text, context, instructions, etc.
        priority:             Higher = higher priority. Default None — falls back to the
                              registered job type's default_priority (e.g. 100 for
                              translations, per ADR-003 §1). Pass an explicit integer to
                              override the registry default.
        scheduled_at:         Optional datetime; do not process before this time.
        lang:                 Target language for translation jobs (ISO 639-1).
        requires_human_review: If True, validated output waits in the review queue.
        created_by:           Source: 'rule', 'trigger', or 'human'.

    Returns:
        The newly created AiJob instance.
    """
    # Resolve priority: when None, use the registered job type's default_priority.
    # This enforces per-type defaults in code rather than relying on call sites.
    # §XV-1: an unregistered job_type is always a programming error (typo or
    # missing import in apps.py). Fail loudly so it is caught immediately rather
    # than silently creating a job at priority=0 that no runner will recognise.
    #
    # Note: aijobs/migrations/0004 removed the DB-level choices constraint on
    # AiJob.job_type. This was intentional — job types are registered dynamically
    # via the plugin registry (ADR-003 §7) and a hard-coded choices list would
    # require a new migration every time a new job type is added. Validation is
    # enforced here (registry lookup) rather than at the DB layer.
    if priority is None:
        try:
            resolved_priority = get_job_type(job_type).default_priority
        except KeyError:
            raise ValueError(
                f"Unknown job type: {job_type!r}. "
                "Register it via register_job_type() in the relevant app's ai_jobs.py "
                "and import it in apps.py ready()."
            )
    else:
        resolved_priority = priority

    job = AiJob(
        store=store,
        job_type=job_type,
        status=AiJobStatus.NOT_STARTED,
        target_model=target_model,
        target_id=target_id,
        input_payload=input_payload,
        priority=resolved_priority,
        scheduled_at=scheduled_at,
        lang=lang,
        requires_human_review=requires_human_review,
        created_by=created_by,
    )
    job.save()
    return job


def check_quota(store):
    """
    Return True if the store may run more AI jobs this calendar month.

    Rules:
      - No quota row exists => unlimited (return True).
      - is_budget_enforced=False => unlimited regardless of spend (return True).
      - Month has rolled over since current_month => reset counter atomically,
        then return True (fresh month, no spend yet).
      - Otherwise => return True iff current_month_spent_usd < monthly_budget_usd.

    This function does NOT create a missing StoreAiQuota row. Super-admin
    is responsible for provisioning quota rows per store.
    """
    today = timezone.now().date()
    current_month = today.replace(day=1)

    try:
        quota = StoreAiQuota.objects.for_store(store).get()
    except StoreAiQuota.DoesNotExist:
        return True  # no quota configured => unlimited

    if quota.current_month < current_month:
        # New month: reset the counter atomically then allow the job.
        StoreAiQuota.objects.for_store(store).filter(pk=quota.pk).update(
            current_month=current_month,
            current_month_spent_usd=0,
        )
        return True

    if not quota.is_budget_enforced:
        return True

    return quota.current_month_spent_usd < quota.monthly_budget_usd


@transaction.atomic
def record_cost(job_run):
    """
    Add a completed job run's cost to StoreAiQuota and AiJobMetric.

    Called after an AiJobRun finishes (status DONE or ERROR with token usage).
    Uses F() expressions throughout for atomic counter updates under concurrent
    runner writes (no read-modify-write race conditions).

    Both the quota update and the metric upsert happen in one transaction.
    If the store has no quota row, the quota update is a no-op (the filter
    matches nothing); the metric row is still written.

    Args:
        job_run: An AiJobRun instance with prompt_tokens, completion_tokens,
                 total_cost_usd, and a .job FK with .job_type and .store set.
    """
    store = job_run.store
    today = timezone.now().date()
    current_month = today.replace(day=1)

    # Atomically increment the store's monthly spend counter.
    # Filter on current_month so a stale row (old month) is not updated
    # — check_quota() is responsible for resetting it before jobs run.
    StoreAiQuota.objects.for_store(store).filter(
        current_month=current_month
    ).update(
        current_month_spent_usd=F("current_month_spent_usd") + job_run.total_cost_usd,
    )

    # Upsert the daily metric row (get_or_create + atomic F() update).
    metric, _created = AiJobMetric.objects.get_or_create(
        period_date=today,
        store_id=store.pk,
        job_type=job_run.job.job_type,
        model_id=job_run.model_id,
        defaults={
            "run_count": 0,
            "prompt_tokens": 0,
            "completion_tokens": 0,
            "total_cost_usd": 0,
        },
    )
    AiJobMetric.objects.filter(pk=metric.pk).update(
        run_count=F("run_count") + 1,
        prompt_tokens=F("prompt_tokens") + job_run.prompt_tokens,
        completion_tokens=F("completion_tokens") + job_run.completion_tokens,
        total_cost_usd=F("total_cost_usd") + job_run.total_cost_usd,
    )


def reclaim_stale_runs(stale_minutes=10):
    """
    Find IN_PROGRESS runs whose last_heartbeat is older than stale_minutes
    and reset them to NOT_STARTED so the scheduler can retry (ADR-003 §3, AC-223).

    A run whose heartbeat has not been updated for more than stale_minutes is
    assumed dead (runner crashed, network partition, OOM). Resetting to NOT_STARTED
    makes the run available for a new runner to pick up.

    Args:
        stale_minutes: How many minutes without a heartbeat before a run is
                       considered dead. Default 10 (= 3× a typical 3-minute
                       heartbeat interval).

    Returns:
        Count of runs reclaimed (reset from IN_PROGRESS to NOT_STARTED).
    """
    cutoff = timezone.now() - timedelta(minutes=stale_minutes)
    # ADR-031 addendum audit (TICKET-051): .update() used to silently bypass
    # the isolation raise. This one is NOT a bug — the scheduler sweep is
    # deliberately platform-wide (reclaims stale runs across every store),
    # so the correct, explicit scope is cross_store_unsafe(), not for_store().
    stale = AiJobRun.objects.cross_store_unsafe().filter(
        status=AiJobStatus.IN_PROGRESS,
        last_heartbeat__lt=cutoff,
    )
    count = stale.update(status=AiJobStatus.NOT_STARTED, last_heartbeat=None)
    return count
