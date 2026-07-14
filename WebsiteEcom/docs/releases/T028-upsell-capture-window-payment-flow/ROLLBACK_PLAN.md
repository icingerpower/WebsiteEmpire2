# Rollback Plan — T028: Up-sell Capture-Window Payment Flow

**Date:** 2026-07-05

## What was added

### Database schema changes (migrations)

Reversible via standard `migrate --fake` or `migrate <app> <prev>`.

| App | Migration | Reversal migration |
|---|---|---|
| campaigns | 0005 (capture_window_minutes, upsell_token fields on CampaignSession) | 0004 |
| campaigns | 0006 (data_json on CampaignSession) | 0005 |
| orders | 0005–0009 (gift_card, discount_code, model_gaps, processor_customer_id, breach_alerted) | 0004 |
| payments | 0005–0006 (fallback_rule, paypal_capture_mode) | 0004 |

### Application code

All T028 logic is contained in:
- `campaigns/views.py` — `accept_upsell`, `decline_upsell` endpoints
- `campaigns/tasks.py` — `capture_window_watchdog` Celery task
- `campaigns/upsell_service.py` — service layer used by both views and watchdog

### Celery beat schedule

`webecom/settings/base.py` — `upsell-capture-watchdog` entry.

---

## Rollback procedure

### 1. Stop the watchdog (immediate)

Remove or disable the `upsell-capture-watchdog` beat schedule entry in
`webecom/settings/base.py` and restart Celery beat:

```
supervisorctl restart celery-beat
```

This stops new watchdog executions. Any watchdog currently in-flight completes
atomically (the claim UPDATE is safe to abandon mid-flight — the `CAPTURE_IN_PROGRESS`
charge will be reclaimed on the next scheduled run, which will not come if beat is stopped).

### 2. Disable the upsell endpoints (immediate)

Route `/campaigns/upsell/accept/` and `/campaigns/upsell/decline/` to a 503 or
remove them from `urls.py`. This prevents new accept/decline submissions.

### 3. Handle charges in CAPTURE_IN_PROGRESS

Any `OrderCharge` rows stuck in `CAPTURE_IN_PROGRESS` at rollback time must be
manually reviewed:
- If processor confirmed capture: update `charge_status` to `CAPTURED`, `order_status`
  to `PAID` via Django admin.
- If processor has no record: update `charge_status` to `FAILED`, trigger the
  soft-degrade path (upsell item removal).

### 4. Revert database migrations (if needed)

Only needed if the schema changes must be removed. All T028 fields added are
nullable or have defaults, so leaving them in place is safe and avoids additional
risk.

```bash
python3 manage.py migrate campaigns 0004
python3 manage.py migrate orders 0004
python3 manage.py migrate payments 0004
```

Campaigns 0004 rollback drops `capture_window_minutes`, `upsell_token`,
`upsell_token_attempts`, `upsell_token_expires_at`, and `breach_alerted` fields.

### 5. Remove application code

Revert `campaigns/views.py`, `campaigns/tasks.py`, `campaigns/upsell_service.py`
to the T027 state via git.

---

## What is NOT rolled back by this plan

- `OrderCharge` rows already in `CAPTURED` state — those payments were real and
  are not reversed by a code rollback. Any refund for a captured upsell charge must
  go through the payment processor admin or the existing refund flow.
- Checkout vault wiring (`setup_future_usage`, processor PM ID persistence) — these
  changes are in the checkout flow, not T028 endpoints. Removing them is a separate
  rollback (T010-CART scope) and should not be done as part of a T028-only rollback.
