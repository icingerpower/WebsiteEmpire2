# Known Risks — T028: Up-sell Capture-Window Payment Flow

**Date:** 2026-07-05

---

## Risk: processor_payment_method_id not written by webhook (T029 deferred)

**Severity:** Medium
**Status:** Mitigated (fail-safe, not fail-open); human-approved deferral.

The `payment_intent.succeeded` webhook handler does not yet write
`Order.processor_payment_method_id` with the vaulted pm_xxx returned by Stripe.
The accept_upsell Phase A guard checks that `processor_payment_method_id` is
non-empty before making any processor call. If the field is empty, Phase A returns
400 with a safe error — no charge attempt is made and the order is unaffected.
This means any customer whose PM ID was not persisted at checkout cannot accept an
upsell (they receive an error instead of a charge). No silent data corruption.

Resolution: T029 will implement the webhook write.

---

## Risk: Provisional OrderItem not created on upsell accept (T029 deferred)

**Severity:** Low
**Status:** Human-approved deferral; assumption documented in upsell_service.py.

At accept, an `OrderCharge` row (CAPTURE_IN_PROGRESS) is created for the upsell
amount, but no `OrderItem` snapshot is created (CampaignStep lacks the
`offer_product_variant` FK required for the snapshot). If T029 is delayed, the
order accounting shows a charge with no corresponding item. This is a data quality
issue, not a payment safety issue.

Resolution: T029 adds the FK and the OrderItem creation step.

---

## Risk: thankyou-view token not yet issued (T029 deferred)

**Severity:** Low
**Status:** Human-approved deferral.

The `upsell-act` token used by the accept/decline endpoints requires the thank-you
page to embed it at order completion. Token issuance is not wired in T028 because
the confirmation email / thank-you view is T029 scope. In a live environment, the
upsell endpoints are unreachable without the token — this is the correct state for
now (the funnel cannot be exercised end-to-end before T029 ships).

---

## Risk: Multi-charge refund accounting incomplete

**Severity:** Low
**Status:** Known, documented in ADR-011.

`_handle_charge_refunded` is a stub. Partial refund across multiple `OrderCharge`
rows, PARTIALLY_REFUNDED / REFUNDED state transitions, and the refund admin UX are
a follow-up ticket. The `OrderCharge` rows that T028 creates are the correct targets
for those future refunds. No data loss; future refund work has clean anchors.

---

## Risk: PayPal vault onboarding is dark until enabled

**Severity:** Low
**Status:** By design; gate controlled by `paypal_vault_enabled` flag.

T028 ships vault-aware connector code paths for PayPal. These paths are dark until
a merchant enables `paypal_vault_enabled` on their `ProcessorAccount`. Until then,
PayPal orders take the immediate-capture path and are funnel-ineligible.

---

## Risk: Watchdog race on shutdown during active claim

**Severity:** Very Low
**Status:** Acceptable; atomic re-claim on next scheduled run.

If the Celery worker is killed while a watchdog task holds a `CAPTURE_IN_PROGRESS`
claim, the charge remains stuck in that state. The next watchdog pass (within 2 min)
performs `retrieve_payment_intent_status` before re-capture, resolves the status,
and either finalizes or reverts. No double-charge risk because the idempotency key
(`capture:{order_id}`) prevents duplicate processor calls.

---

## Risk: aijobs tests broken (unrelated to T028 functionality)

**Severity:** Medium (blocking)
**Status:** Active blocker — see RELEASE_CHECKLIST.md.

6 tests in `aijobs.tests.test_aijobs.AiJobModelTest` fail because `aijobs/service.py`
was changed to raise `ValueError` for unregistered job types, but the test helper
`_create_job()` uses `AiJobType.PRODUCT_DESCRIPTION` (not registered). This is
unrelated to T028 payment flows but blocks the release gate per hard rules.

Resolution: Developer fixes `_create_job()` to pass `priority=0` explicitly.

---

## Human decisions made (T028 scope)

| Decision | Date | Outcome |
|---|---|---|
| thankyou-view token issuance deferred | 2026-07-05 | T029 scope |
| UPSELL webhook promotion deferred | 2026-07-05 | T029 scope (watchdog handles finalization) |
| Provisional OrderItem creation deferred | 2026-07-05 | T029 scope (FK missing) |
| PayPal delayed-capture support confirmed | 2026-07-05 | Supports Orders API intent=AUTHORIZE |
| Two-charge baseline is the only shipped path | 2026-07-03 (ADR-007) | No single-combined-capture optimization |
