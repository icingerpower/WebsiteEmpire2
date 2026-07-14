# Rollback Plan — Phase 1

> Phase 1 is a technical foundation — nothing is publicly deployed at end of Phase 1.
> "Rollback" here means: if a critical issue is found in the first 24–48h of Phase 2
> development (new code written on top of Phase 1), what is the procedure to revert?

## What is at risk

Phase 1 has no production deployment. All code is in the local repository. There is
no live traffic, no live database, and no customer data in play.

The only risk window is: Phase 2 development starts, a developer builds on Phase 1
models, and then a critical flaw in a Phase 1 model or service is discovered.

## Rollback procedure

### Scenario A — Bug in a Phase 1 model field or migration

1. Identify the affected migration file and its dependencies.
2. If the migration only changed `help_text` or `choices` (no DB column change),
   reverting is a code change only — no DB operation needed.
3. If the migration changed a DB column:
   - Create a new compensating migration.
   - Apply it with `python3 manage.py migrate`.
   - Commit and run tests before resuming Phase 2 work.

### Scenario B — Architectural flaw discovered in a core service

1. Create a new git branch from the Phase 1 HEAD commit.
2. Fix the flaw with a Developer Agent ticket.
3. Run the full test suite (616 tests expected).
4. Spec Reviewer and Safety Agent re-review the affected area.
5. Release Manager re-approves before Phase 2 resumes.

### Scenario C — Critical security finding

1. Immediately stop Phase 2 development on the affected area.
2. Safety Agent produces a targeted fix.
3. All tests must pass (add regression test for the fix).
4. Safety Agent re-issues CLEAR for the patched area.
5. Resume Phase 2.

## Fast-follow items deferred to Phase 2

The following were accepted as fast-follows and are NOT rollback triggers:

| Item | What it protects | Action in Phase 2 |
|------|-----------------|-------------------|
| M1 | Webhook order–account cross-org integrity | Implement before T028 |
| M3 | Per-email coupon concurrency at scale | Implement before high-volume launch |
| M4 | Beacon field injection (analytics-only) | Implement before Phase 2 analytics launch |
| L1 | Silent decrypt on misconfigured FERNET_KEY | Add startup validation before first production credential |
| L3 | Anonymous session storage amplification | Add session size cap before public storefront |
| L4 | No startup FERNET_KEY validation | Add to Django AppConfig.ready() before production |

## What would NOT require a rollback

- Help-text or choices changes to existing model fields (no schema impact).
- Adding new optional fields with defaults (additive, safe to apply).
- New test files.
- Documentation changes.

## Data recovery baseline (for Phase 2 production launch)

This section is a placeholder — Phase 1 has no production data. Before Phase 2
goes live, a data backup and restore procedure must be documented as part of the
Phase 2 deployment checklist.
