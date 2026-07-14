# ADR-035: TimescaleDB analytics migration

**Status:** ACCEPTED (human, 2026-07-11 — already decisive at proposal time, no pending items: **defer the hypertable conversion, ship the vendor-detection guards now**). Implements `TICKET-044` (Phase 3, P2, complexity **M**). Revises ADR-004 (analytics second DB, router, event retention). Depends on TICKET-012 (analytics DB provisioning), TICKET-013 (`aggregate_metrics`, `AggregatedMetric`, current `purge_events` DELETE-based purge).

## Decision

Make the analytics app **run correctly on both plain Postgres and TimescaleDB**, loudly reporting which mode it is in (§XV-1), by guarding the one migration that creates the hypertable and the one command that purges old data. **Do not convert `Event` to a hypertable now.** At zero production traffic there is nothing yet to prove the migration against, and ADR-004 already designed the schema to be identical on both backends for exactly this reason — the guarded, deferred posture costs almost nothing to build today and everything to skip if traffic arrives before it's built.

## Context

ADR-004 called TimescaleDB "strongly recommended" but shipped on the "minimum acceptable alternative": a plain Postgres `analytics` DB (`analytics/db_router.py::AnalyticsRouter`), with `Event` as an un-partitioned table (`analytics/migrations/0001_initial.py`) and `purge_events` doing a plain table-scan `DELETE FROM analytics_event WHERE created_at < %s` (`analytics/management/commands/purge_events.py`) — explicitly commented as a stand-in "until TICKET-044". `aggregate_metrics` reads raw events via `connections['analytics']` raw SQL and writes idempotent `AggregatedMetric` rows (delete-then-insert per `(store_id, metric_type, period_start)`) — this stays the reporting source of truth regardless of storage backend. There is currently no production traffic; ADR-004's 500+ orders/day/store estimate is a target, not a measured load.

## Options considered

**A. Convert now** — add the `TimescaleDB` extension + hypertable migration immediately, switch `purge_events` to `drop_chunks`, add native compression on closed chunks. Pro: matches ADR-004's original recommendation, no future migration to schedule. Con: extension install is an ops-side dependency on every environment (dev, staging, prod) before there is a single real event to chunk; a migration that can't be exercised under real load risk-tests nothing; TimescaleDB adds an operational surface (chunk interval tuning, compression policy tuning, upgrade compatibility with future Postgres major versions) the team has never operated, for a load that doesn't exist yet.

**B. Defer entirely, do nothing** — leave `Event` as a plain table and `purge_events` as a DELETE, revisit only when traffic materializes. Pro: zero work now. Con: violates §XV-1 — there is no vendor-detection or "which mode am I in" signal anywhere, so a future engineer adding TimescaleDB in production **silently** gets partition-unaware code (the DELETE purge still runs, defeating `drop_chunks`, and nobody would know without reading the ADR).

**C. Guard now, convert later (chosen)** — write the hypertable-creation migration and the `drop_chunks` purge path now, both **gated behind an explicit vendor+extension check** that is a no-op on plain Postgres, and leave the actual hypertable conversion as an opt-in step (a flag or a follow-up migration run explicitly per-environment) rather than something that fires unconditionally on `migrate`.

## Chosen option

C. Concretely:

1. **Vendor/extension-aware `RunPython` migration** (not a bare `RunSQL`): checks `connection.vendor == 'postgresql'` **and** `SELECT * FROM pg_extension WHERE extname = 'timescaledb'` before running `SELECT create_hypertable('analytics_event', 'created_at', if_not_exists => TRUE, migrate_data => TRUE)`. On any other vendor, or when the extension is absent, the migration is a documented no-op — it must log at INFO which mode it detected, never merely skip silently (§XV-1). This makes "is Timescale active here?" answerable by reading a log line, not by inspecting the DB by hand.
2. **`purge_events` gets the same branch, not a rewrite:** detect hypertable presence (`SELECT hypertable_name FROM timescaledb_information.hypertables WHERE hypertable_name = 'analytics_event'`); if present, call `SELECT drop_chunks('analytics_event', older_than => INTERVAL '%s days')`; if absent, **fall back to the existing DELETE** (today's code, unchanged) rather than erroring. The command must print which path it took — an operator watching the cron log must be able to tell "chunk-drop" from "table-scan delete" without reading source.
3. **`aggregate_metrics` is untouched.** It already reads via raw SQL against `connections['analytics']` and writes `AggregatedMetric` through the ORM; neither depends on whether the underlying table is a hypertable. This ADR does not touch it.
4. **Continuous aggregates are explicitly out of scope for v1** — Timescale's continuous-aggregate views are a genuine future optimization (replacing the nightly `aggregate_metrics` cron with an incrementally-maintained materialized view), but they are a second, independent migration with their own correctness risk (aggregate staleness policies, refresh windows) and are **not needed** until `aggregate_metrics`'s runtime becomes a measured problem. Noted here as the documented future path, not designed further.
5. **Release checklist item, not application code:** installing the `timescaledb` extension on the target Postgres instance (or provisioning a Timescale-flavored managed Postgres) is an ops/deploy step, added to the release checklist as a per-environment toggle, independent of a code deploy.

## Why

**Recommendation, not a hedge:** at zero production traffic, converting to a hypertable today buys nothing measurable and adds an operational dependency (extension install across every environment, a chunk-management policy nobody has tuned against real data) before there is any data to tune it against. But doing *nothing* (Option B) would leave exactly the kind of invisible-failure trap this project's own binding lessons warn against: a plain-Postgres purge command that silently keeps doing table-scan deletes forever even after someone installs TimescaleDB in production, because nothing in the code ever checked. The guarded/deferred posture (C) is cheap now (a vendor check + a log line, on top of code ADR-004 already wrote to be storage-agnostic) and removes the single most likely real incident: someone installs the extension expecting `drop_chunks` semantics and gets silent `DELETE`s instead, or vice versa.

## Risks

- **Guard drift:** if a future schema change to `Event` is made only against one backend and not tested against the other, the "identical schema on both backends" invariant (ADR-004) silently breaks. Mitigated by a CI matrix note (run the analytics test suite against both a plain-Postgres and a Timescale-enabled Postgres container) — flagged as a test-infra follow-up, not blocking this ADR.
- **`if_not_exists=>TRUE, migrate_data=>TRUE` on a non-empty table** is a real-data operation with lock implications on production-sized tables; because there is no production traffic yet, this risk is currently theoretical, but the ADR text and the release checklist must say so plainly for whoever runs it first against a populated table.
- **Deferring the decision means someone must remember to revisit it.** Tracked explicitly: the migration and purge guards ship in this ticket (so the *code* is never a blocker), but the *extension install* + hypertable conversion is a separate, explicitly-triggered follow-up once a store's traffic approaches the ADR-004 500+ orders/day estimate — not an automatic side effect of any future `migrate`.

## Rollback strategy

Identical to ADR-004's original rollback design, made real by this ADR: because the schema is byte-identical on both backends, "rollback" from Timescale to plain Postgres (or never converting at all) is a connection-string-and-chunk-maintenance-job change, not an application-code change. If the vendor/extension guard ever misdetects, the safe failure mode is the plain-Postgres path (DELETE-based purge, no hypertable) — never a crash, never a silent skip of purging entirely.

## Tests required

1. Migration guard: `create_hypertable` is invoked when (and only when) both `vendor == 'postgresql'` and the extension is present; asserted via a mocked `pg_extension` check on both branches.
2. Migration guard is a true no-op (no exception, no side effect) on SQLite/plain-Postgres-without-extension test settings — the existing test suite must keep passing unmodified on `webecom/settings/test.py`.
3. `purge_events`: hypertable-present path calls `drop_chunks` with the correct retention interval; hypertable-absent path executes the existing DELETE unchanged (regression test against the current behavior).
4. `purge_events` prints/logs which path it took (assert on captured stdout/log record) — this is the §XV-1 visibility requirement, not incidental.
5. Regression: `aggregate_metrics` produces identical `AggregatedMetric` rows before/after the migration guard is added, run against a fixture with both a hypertable and a plain table (AC-080/081/082 "regression suite must pass unchanged on both backends").

## Ops / release checklist note (TICKET-044 implementation)

Per the "Chosen option" step 5 above, installing the `timescaledb` extension on a target Postgres instance is an **ops/deploy step, not an application-code change**, and it is **not** performed by this ADR's code. Whoever assembles the release checklist for the wave that ships TICKET-044 should add:

- [ ] Confirm the target environment's Postgres instance does **not** unexpectedly already have `timescaledb` installed (i.e. no accidental hypertable conversion on first `migrate` against a populated `analytics_event` table — see the "Risks" section above on `if_not_exists=>TRUE, migrate_data=>TRUE` lock implications on non-empty tables).
- [ ] If/when a store's traffic approaches the ADR-004 500+ orders/day estimate and the hypertable conversion is actually wanted: install the `timescaledb` extension on that environment's Postgres (`CREATE EXTENSION IF NOT EXISTS timescaledb;`, or provision a Timescale-flavored managed Postgres) **before** re-running `migrate` — this is the trigger that flips `analytics_timescale.is_timescale_active()` from `False` to `True` and makes the guarded migration/`purge_events` paths active. This step is never automatic and is not part of any code deploy.
- [ ] After installing the extension, confirm via the application logs that the migration logged the "converting analytics_event to a hypertable" INFO line (not the no-op line) on next `migrate`, and that a subsequent `purge_events` run logs `mode=drop_chunks` rather than `mode=DELETE`.
- [ ] Continuous aggregates remain out of scope (see "Chosen option" point 4) — do not add them as part of this checklist item.
