---
name: project-ai-job-launcher
description: "Super-admin AI Job Launcher + running-jobs monitor (ADR-071) — run/stop registered job types, cancellation engine"
metadata:
  type: project
---

Super-admin AI Job Launcher, shipped 2026-09-07 (ADR-071, commits: T-AIJL-0 fix
7aa1abf, Wave 1 fd06912, Wave 2 06997da, hardening 117473a, i18n 56e1e7d).
Design: `docs/adr/ADR-071-super-admin-ai-job-launcher.md`. Requested by Cédric:
a page listing every job type with inline params (defaults) + one-click Run,
Run→Stop toggle, a one-by-one monitor page, "last finished" line.

**Two super-admin pages** (on `super_admin_site`, hung off `AiJobAdmin.get_urls()`):
- Launcher `/superadmin/aijobs/aijob/launcher/` — one row per registered job
  type; launchable types render `launch_params` as inline widgets (defaults
  prefilled) + store picker (single OR "All stores") + Run; non-launchable show
  disabled + reason. Run→Stop when a batch is active; "Last job finished at" from
  Max DONE AiJobRun. Routes: `_launch` (POST), `_stop` (POST), `_launcher_status`
  (GET JSON, ~10s poll), `_monitor`, `_stop_one` (POST). URL names
  `aijobs_aijob_{launcher,launch,stop,launcher_status,monitor,stop_one}`.
- Monitor `/superadmin/aijobs/aijob/running/` — all NOT_STARTED+IN_PROGRESS jobs,
  per-row Stop.

**Engine (Wave 1):**
- `JobTypeDefinition` gained `launch_params: list[LaunchParam]` + `launch(store,
  params)->dict` + `launch_disabled_reason`; `is_launchable()`,
  `validate_launch_params()` (typed coercion, defaults, required/choice checks,
  int must be >=0, HTML-checkbox bool = absent means False).
- Launchable v1: translation, email_template_translation, review_translation
  (disabled while REVIEW_TRANSLATION_ENABLED off — no backdoor), product_rewrite,
  rewrite_assess, size_chart_fix. NOT launchable: ai_product_review (stub),
  campaign_step_translation (stub). sales_chat is not a registry type.
- Backfill logic extracted to services: `catalog/services/translation_backfill.py`,
  `reviews/services/translation_backfill.py`, `emails/services/translation_backfill.py`.
- **Cancellation:** `AiJobStatus.CANCELLED` + `AiJob.cancel_requested` (migration
  aijobs/0007). `stop_jobs(job_type, store=None)` / `stop_job(pk)` = pure UPDATEs,
  no external calls. Runner checkpoints (catalog/services/ai_job_runner.py):
  claim-time, pre-call, post-call (writes terminal CANCELLED AiJobRun + record_cost,
  discards output). fail_job never requeues a cancelled job. STOP = all
  queued+running of that job_type, store-scoped (P2). In-flight provider calls
  finish then get discarded (no mid-HTTP kill) — honest, documented.

**Also fixed here (T-AIJL-0, latent ADR-070 bug):** the shared runner
`persist_job_output()` is a hardcoded per-job-type dispatch and had NO
`review_translation` branch (registry persist_output callbacks are never invoked
by the runner) → an enabled review-translation job failed. Added the branch +
a through-the-runner regression test. See [[project_review_moderation]].

**Resumable enrichment selection (ADR-078, shipped 2026-09-10, commit eef2fe1):**
Fixed a real launcher footgun Cédric hit: "Rewrite Assessment, Max=50" queued 0.
Root cause — `queue_rewrite_stage` did `Product...order_by("pk")[:limit]`, slicing
the first-N products by PK BEFORE the per-product eligibility check, so every run
re-scanned the same low-PK prefix and never advanced (a store whose first 50
products were already handled → 0 queued). Fix (catalog/services/enrichment_queue.py):
`limit` now = budget of products ACTED ON (not a scan window); candidates are
DB-filtered (positive-list eligibility + per-stage applicability `Exists`) and
ordered `last_processed_at ASC NULLS FIRST, pk ASC` → successive launches advance
through the whole catalog (resumption emergent — acted-on rows leave the eligible
set). Permanent/row-less skips (SKIPPED_HUMAN_EDIT, size-fix not-applicable,
measurements not-ready) excluded in SQL, never consume budget nor starve the walk.
One shared `iter_enrichment_candidates` + `_run_budgeted` helper serves rewrite +
size_fix + measurements. New opt-in per-run `redo_after_days` (unset/0 = never redo
on age; older-than-N re-enters via APPROVED->STALE->requeue; orthogonal to
content-drift STALE + requeue_stale/requeue_assessed). New nullable indexed
`ProductEnrichmentState.last_processed_at` = AI-processing clock, written ONLY by
the pipeline (7 sites), NEVER by a human review verb or the write-time STALE flip
(Cédric's P3); migration catalog/0028 (AddField + backfill=updated_at, reversible).
`limit=None` (CLI only; launcher limit is required=50) keeps the full sweep with
scan-time drift detection; in budgeted mode drift on approved rows delegated to
ADR-039 A3 write-time flip hooks (P2 approved trade-off). Launcher label
"Max products to scan"->"Max jobs to queue" + redo_after_days field; CLI
`--redo-after-days`. +32 tests (test_enrichment_resumable_selection.py); full suite
1834 OK. Gates: jobs+i18n checkers OK, Release Manager GO. **2 non-blocking
follow-ups deferred:** (a) makemessages housekeeping to retire the orphaned
"Max products to scan" msgid (.po was hand-edited surgically to avoid a 1000-line
reorder diff); (b) gettext-wrap the `_coerce_launch_param` negative-int error
(pre-existing, shared with the `limit` param). Release docs under
docs/releases/ADR-078-resumable-enrichment-selection/.

**Safety verdict GO**, no material findings (authz on all 6 routes, CSRF/POST-only,
tenant, cancellation accounting, CSP all pinned by tests). **RELEASE-CHECKLIST
CAVEAT:** there is NO global spend ceiling — per-store `StoreAiQuota` (opt-in,
`is_budget_enforced`) is the only server-side spend guard, checked at enqueue
only. "All stores" runs loop every store respecting per-store quota + a per-run
`limit` cap; to bound platform-wide spend, provision + enforce StoreAiQuota rows.
Relates to [[project_local_8099_scratch_env]] (scratch DB migrated to 0007).
