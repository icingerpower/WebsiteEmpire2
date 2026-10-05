---
name: project-remote-job-worker
description: "ADR-072 remote AI-job worker — token-authed HTTP API + laptop client (run_job.sh) pulling jobs to run locally; nh3 sanitizer"
metadata:
  type: project
---

Remote AI-job worker, shipped 2026-09-07 (ADR-072). Lets Cédric run
`bash scripts/worker/run_job.sh` (or a `~/aspire_rescrape/run_job.sh` wrapper)
from his laptop to pull queued AiJobs from the website over HTTP, run the AI step
LOCALLY via agy/claude, and post results back — works against local (:8099) OR an
online deployment. Design: `docs/adr/ADR-072-remote-ai-job-worker.md`. Commits:
Wave A server 590341a, Wave B client ff20715, i18n c31b145, safety hardening
f06956f, F1 sanitizer 3092f04.

**Why it exists:** run_ai_jobs / run_ai_jobs_cli are management commands needing
direct DB access → local only. Pulling jobs from an ONLINE site needs an HTTP API.

**Server (Wave A):**
- `WorkerToken` (aijobs) — SHA-256 hashed at rest, named, multiple active,
  platform-wide (no store scope), rotate=revoke+create, super-admin managed
  (aijobs/admin.py). `aijobs/tokens.py` mirrors bookkeeping/tokens.py. Migration
  aijobs/0008.
- API `POST /api/worker/jobs/{claim,<id>/heartbeat,<id>/submit,<id>/fail}` in
  aijobs/api.py (+ api_urls), mounted once in webecom/urls (scratch re-exports).
  Bearer-token (401-first), POST-only, @csrf_exempt (webhook justification),
  rate-limited per token. Reuses claim_job/build_job_prompt/persist_job_output/
  fail_job — the remote path is byte-identical to local. Claim creates a leased
  AiJobRun (runner="remote:<name>", last_heartbeat); heartbeat returns live
  cancel_requested (ADR-071 STOP reaches the laptop). Filtered to
  WORKER_ELIGIBLE_JOB_TYPES (the 6 launchable types; all C1/C2 prompts, no PII).
- reclaim_stale_runs now requeues the parent job on a dead lease (was a
  strand-forever gap).

**Client (Wave B):** `scripts/worker/pradize_worker.py` (stdlib-only, reuses the
shared Django-free `aijobs/executor_cli.py`) + `scripts/worker/run_job.sh`.
Config (commit 4e39522, no per-session export): one-time
`python3 scripts/worker/pradize_worker.py configure` writes
`~/.config/pradize/worker.env` (0600) with PRADIZE_BASE_URL + PRADIZE_WORKER_TOKEN;
then `bash scripts/worker/run_job.sh --executor agy --once` needs nothing exported.
Precedence base_url = --base-url > env > file; token = env > file (never argv). Refuses non-https token
send unless localhost or --insecure. Heartbeat thread; on STOP the in-flight
job's output is discarded (not hard-killed — matches server in-flight semantics).

**Safety (GO-WITH-FIXES, all fixed):** F1 stored-XSS — AI HTML rendered |safe was
unsanitized → added nh3==0.3.7 + `catalog/services/html_sanitizer.py`
sanitize_rich_html applied at the 3 persist sinks (product/collection Translation
.description, StaticPageTranslation.body, product-rewrite Product.description);
ReviewTranslation not sanitized (autoescaped). F2 submit CAS (TOCTOU). F3 lease
bound to claiming token + 6h MAX_LEASE_AGE. F4 token-only rate-limit key.
Known follow-up: fail() has the same idempotent-only TOCTOU as submit had.

**Before ONLINE exposure:** F1 done (was the blocker). Also chmod 600 the personal
run_job.sh wrapper (holds the raw token); the laptop agy executor
(--dangerously-skip-permissions) is the real trust boundary. Relates to
[[project_ai_job_launcher]], [[project_review_moderation]], [[feedback_translation_cli]],
[[feedback_shared_cwd_hazard]].
