# Known Risks — TICKET-027: Up-sell Campaign Model Delta (ADR-009)

---

## Risk 1 — HTTP-level permission gap for platform campaign mutation

Severity: MEDIUM (deferred, not a current blocker)

The model-level permission guard (has_change_permission / has_delete_permission) is
proven correct and regression-tested (T027-B1, T027-B2). However, an HTTP-level
403 integration test — a Django test-client POST to the platform campaign change URL
by a store-admin user — was deferred due to the middleware setup complexity required.

Current exposure: a store-admin who manually crafts a POST to
/admin/campaigns/campaign/<platform-id>/change/ will be blocked at the admin
save_model layer (owner_scope is forced to 'store' and the form raises) — but the
behavior is not covered by an HTTP-level automated test.

Mitigation: covered by T028 or T029 test cycle when the Django test client
infrastructure is already set up for other permission tests.

---

## Risk 2 — CampaignStepTranslation signal propagates exceptions

Severity: LOW

Bug T027-B5 was fixed: exceptions from create_job() now propagate out of the
post_save signal. This means a misconfigured AiJob registry (wrong job type, DB
connection failure) will cause a CampaignStep save to raise a 500. This is the
correct behavior per §XV-1 (no silent failures), but operators must be aware that
AiJob infrastructure problems surface as admin save errors.

Mitigation: T027-B6 fix ensures unknown job types raise ValueError immediately
rather than writing junk rows. The error is visible and actionable.

---

## Risk 3 — build_prompt / persist_output are NotImplementedError stubs

Severity: LOW (by design, documented in ADR-009 Appendix B)

The campaign_step_translation job type is registered and the scheduler recognizes it.
AiJob rows are created. But the CLI runner will call build_prompt / persist_output
and receive NotImplementedError. The runner must be configured to skip or log-and-defer
this job type until the AI job orchestration phase ships.

Mitigation: this is a known deferred item, not a defect. The runner behavior on
NotImplementedError is governed by aijobs architecture (T030 scope). No customer-facing
translation is expected before that phase.

---

## Risk 4 — Backfill in migration 0002 is O(sessions) — safe for empty DB

Severity: NEGLIGIBLE at current stage

backfill_campaign_session_campaign_type iterates all Campaign rows and updates matching
CampaignSession rows. The production DB has no CampaignSession data yet (T028 has not
shipped). On a DB with real session data, this migration would need a batched approach
for large tables.

Mitigation: no customer-facing session data exists before T028. If T027 is ever
applied to a database with pre-existing CampaignSession rows, the migration should be
tested for lock duration before applying to production.

---

## Risk 5 — Cycle detection is bounded by CampaignStep.clean() / validator, not DB

Severity: LOW

Graph cycles are prevented by validate_campaign_activation at the persistence boundary.
There is no DB-level cycle guard (Django cannot express this). If the validator is
bypassed (e.g. direct ORM writes in a management command), a cycle could be stored.

Mitigation: the validator is called from Campaign.clean(), CampaignStep.clean(), and
the admin save_model. Direct ORM bypass is a developer action, not an end-user path.

---

## Deferred human decisions

All T027 human decisions were made on 2026-07-04 (see RELEASE_CHECKLIST.md). No
open PENDING items remain on this ticket.

The following decisions are deferred to TICKET-028:
- PayPal delayed-capture confirmation (does not block T027; blocks Stripe-routed orders
  from using the upsell funnel in production, not the model itself).
