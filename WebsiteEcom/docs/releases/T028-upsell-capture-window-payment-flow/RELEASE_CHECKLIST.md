# Release Checklist — T028: Up-sell Capture-Window Payment Flow

**Date:** 2026-07-05
**Verdict:** READY

---

## Checklist

### Spec approved (by human where critical)
PASS. T028 in `specs/ecommerce_engine/10_implementation_tickets.md` shows all
blockers resolved: Safety review resolved 2026-07-03 (9 amendments applied to
ADR-007 and T028 ticket). PayPal delayed-capture confirmation resolved 2026-07-05.
No PENDING blockers remain on the ticket.

### Architecture decisions recorded
PASS. ADR-011 exists at `docs/adr/ADR-011-upsell-capture-window-payment-flow.md`.
Status: ACCEPTED (2026-07-05). Extends ADR-007, ADR-009, ADR-006-R, ADR-002.

### Implementation complete
PASS (qualitative). accept_upsell three-phase, decline_upsell, capture_window_watchdog,
checkout vault wiring, rate limiting (5 attempts), watchdog atomic re-claim,
PayPal final_capture, breach_alerted dedup, _revert_claim shared helper all present.
Endpoints: POST /campaigns/upsell/accept/ and POST /campaigns/upsell/decline/.

### Tests added
PASS. 97 T028-targeted tests reported at review time. Campaigns/orders/payments suite
totals 449 tests at gate time, all passing.

### Tests passing
PASS. Full suite run: **1148 tests, OK, 5.59s** (0 failures, 0 errors).

Blocker resolved: `AiJobModelTest._create_job()` now passes `priority=0` explicitly,
bypassing the registry lookup for `AiJobType.PRODUCT_DESCRIPTION` (unregistered).
The stale docstring for `test_create_job_priority_default_zero` was also corrected
to reflect the new contract (explicit `priority` bypasses registry; omitting it for
an unregistered type raises `ValueError`).

### Coverage checked
Not separately measured; all 1148 tests pass. T028 core apps (campaigns, orders,
payments) contribute 449 tests covering the accept/decline flows, watchdog,
rate limiting, PayPal final-capture, and breach-alert dedup.

### Spec Reviewer approved
PASS (carried from review record: 4 passes, all blockers/highs/mediums resolved).

### Safety Agent approved
PASS (carried from review record: 4 passes, all blockers resolved).

### SEO Agent approved
N/A — T028 is a server-side payment flow with no public indexable pages.

### Designer approved
N/A — T028 is backend only; no UI changes.

### Migrations reviewed
PASS. Migration set verified:
- campaigns: 0005_campaign_capture_window_minutes_and_more.py,
             0006_campaignsession_data_json.py
- orders: 0005_order_gift_card.py, 0006_alter_order_discount_code.py,
          0007_order_model_gaps.py, 0008_order_processor_customer_id_and_more.py,
          0009_ordercharge_breach_alerted.py
- payments: 0005_alter_organizationrule_fallback_rule_and_more.py,
            0006_processoraccount_paypal_capture_mode_and_more.py
`makemigrations --check` reports: No changes detected.

### Settings documented
PASS. Beat entry verified in `webecom/settings/base.py`:
```
"upsell-capture-watchdog": {
    "task": "campaigns.tasks.capture_window_watchdog",
    "schedule": crontab(minute="*/2"),
}
```

### Deployment checklist ready
PARTIAL — see ROLLBACK_PLAN.md; full deployment checklist not separately authored
(standard Django migrate + Celery beat restart applies).

### Rollback plan ready
PASS — see ROLLBACK_PLAN.md.

### Known risks listed
PASS — see KNOWN_RISKS.md.

### Human decisions listed
PASS. Three T029 deferrals explicitly human-approved (2026-07-05):
1. thankyou-view token issuance — deferred to T029
2. UPSELL webhook promotion — deferred to T029
3. Provisional OrderItem creation — deferred to T029 (CampaignStep lacks offer_product_variant FK)
All documented in ADR-011 "Explicitly NOT in T028" section.

---

## Verdict

**READY**

All checklist items are PASS or N/A. The one previous blocker (6 aijobs test errors)
is resolved. Full suite: 1148 tests, OK, 0 failures, 0 errors.

No outstanding actions required before deployment.
