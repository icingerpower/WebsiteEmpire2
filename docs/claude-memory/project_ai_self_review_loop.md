---
name: project-ai-self-review-loop
description: "ADR-073 client-side AI self-review/refine loop for job executors — produce→critic→fix, hold-for-review on give-up"
metadata:
  type: project
---

Client-side AI self-review / refine loop, shipped 2026-09-07 (ADR-073). Opt-in
quality gate layered on the AI-job executor pipeline (remote worker + local
run_ai_jobs_cli). Design: `docs/adr/ADR-073-client-side-ai-self-review-refine-loop.md`.
Commits: Wave 1 engine fd0b46e, Wave 2 client wiring 6f6028a, i18n 0e4bac7,
Safety F-1/F-2 3c0db2f. NO migration (reuses existing review scaffolding).

**Flow (per job, when --review passed):** producer executor runs → a CRITIC
executor pass judges the output against the job's requirements → if flagged,
re-run producer with the critique appended → up to --max-review-iterations
(default 2) → then decide: critic ok → submit clean; give-up/critic-unavailable
→ submit FLAGGED (needs_review).

**Key decisions (all ACCEPTED):**
- `--review` is OPT-IN, DEFAULT OFF (2–6× spend multiplier per job). With it off,
  the path is byte-identical to before. Flags on BOTH pradize_worker.py and
  run_ai_jobs_cli: --review/--max-review-iterations/--critic-executor[-cmd].
- Generic client-side critic prompt v1 (aijobs/refine_loop.py build_critic_prompt,
  producer prompt + candidate BOTH DATA-marker-wrapped; strict
  {"ok","issues","severity"} contract; parse fail → flagged, never clean).
- **HOLD-don't-persist on give-up** (critical): a flagged candidate is stored in
  `AiJobOutput(field_id="held_candidate", is_accepted=False)` + WARNING
  AiJobValidation + requires_human_review=True; persist_job_output is NOT called,
  so nothing reaches the live model (persisting-as-draft would UNPUBLISH live
  translations → 404). Held text is structurally unreachable on the storefront
  (renderers select PUBLISHED rows only). EXCEPTION: product_rewrite/size_chart_fix
  persist normally (already land AWAITING_REVIEW) + attach critic notes.
- Review surface: super-admin AiJobAdmin "Held for review" filter + Approve-&-
  publish (replays the untouched persist_job_output → nh3 sanitize + validate +
  fingerprint + Permalink all run once at publish) / Reject (terminal, discard).
  Both write AiJobReview audit rows; both skip already-resolved jobs (F-1).

**Shared engine:** `aijobs/refine_loop.py` (stdlib-only, Django-free, import-guarded)
— run_refine_loop(produce, critic, max_iterations, cancel_check)→RefineResult;
make_executor_callables/build_review_notes. Submit API gained needs_review +
review_notes (≤4000). Cancel honored before every producer/critic call (worker
heartbeat spans the whole loop; CLI check_cancelled_before_call).

**Safety GO-WITH-FIXES (all fixed):** F-1 reject-not-terminal (approve could
resurrect a rejected job) + F-2 approve non-atomic → both fixed 3c0db2f. F-3
(INFO, PRE-EXISTING, separate follow-up): EmailTemplateTranslation.body_html is
NOT nh3-sanitized and is rendered via engine.from_string() → approved AI text
runs as a Django template (context exposure). Worth a future email-focused audit.
Relates to [[project_ai_job_launcher]], [[project_remote_job_worker]].
