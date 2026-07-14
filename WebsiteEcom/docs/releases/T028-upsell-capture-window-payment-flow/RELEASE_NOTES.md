# Release Notes — T028: Up-sell Capture-Window Payment Flow

**Status: NOT RELEASED — release gate BLOCKED (see RELEASE_CHECKLIST.md)**

---

## What this release adds

### Endpoints

- `POST /campaigns/upsell/accept/` — three-phase accept flow (commit guards, processor
  calls with idempotency keys, commit outcomes). Rate-limited to 5 attempts per session
  before auto-decline + 403.
- `POST /campaigns/upsell/decline/` — marks CampaignSession as DISMISSED.

### Background tasks

- `capture_window_watchdog` — Celery beat task every 2 minutes. Claims AUTHORIZED
  charges whose capture window is open, re-queries processor status before re-capture,
  finalizes or reverts atomically. Shared `_revert_claim` helper prevents drift
  between tasks.py and the watchdog.

### Checkout vault wiring

- `setup_future_usage=off_session` sent to Stripe at checkout authorization.
- Payment method ID persisted on `Order.processor_payment_method_id` for off-session
  upsell charge.

### New model fields (via migrations)

| App | Field | Purpose |
|---|---|---|
| campaigns | CampaignSession.upsell_token | Short-lived signed accept/decline token |
| campaigns | CampaignSession.upsell_token_attempts | Rate-limit counter (max 5) |
| campaigns | CampaignSession.upsell_token_expires_at | Token TTL enforcement |
| campaigns | CampaignSession.data_json | Arbitrary session context blob |
| orders | OrderCharge.breach_alerted | Dedup flag: alert sent at most once per breach |
| orders | Order.processor_customer_id | Customer vault ID for off-session charges |
| orders | Order.processor_payment_method_id | Vaulted PM ID for off-session upsell charge |
| payments | ProcessorAccount.paypal_capture_mode | `delayed` (default) / `immediate` |

### Safety properties

- Atomic race guard: accept, decline, and watchdog all use `UPDATE … WHERE
  state='AUTHORIZED' AND affected_rows == 1`; the loser aborts without a processor call.
- Window check (DB `NOW()`) inside the locked transaction before any processor call.
- Idempotency keys: `capture:{order_id}` for original, `upsell:{order_id}:{step_id}` for upsell.
- PayPal: `final_capture: True` on capture payload.
- Breach alert dedup via `OrderCharge.breach_alerted` — alert sent at most once per charge.
- PM ID guard: if `processor_payment_method_id` is empty, Phase A returns 400 and
  aborts before any processor call (fail-safe, not fail-open).

## Explicitly deferred to T029 (human-approved)

- thankyou-view token issuance
- UPSELL webhook promotion (`payment_intent.succeeded` writing PM ID)
- Provisional OrderItem creation (CampaignStep lacks `offer_product_variant` FK)

## Migrations added

- campaigns: 0005, 0006
- orders: 0005, 0006, 0007, 0008, 0009
- payments: 0005, 0006

## Deployment steps (when unblocked)

1. `python3 manage.py migrate` (all apps)
2. Restart Celery beat (picks up `upsell-capture-watchdog` schedule)
3. Restart application server
4. Verify beat log shows `capture_window_watchdog` firing every 2 minutes
5. Verify `/campaigns/upsell/accept/` returns 400 on empty token (smoke test)
