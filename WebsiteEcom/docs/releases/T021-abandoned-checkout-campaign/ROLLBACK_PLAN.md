# Rollback Plan — T021: Abandoned Checkout Campaign

---

## Pre-deployment checklist

Before running migrations in production:

1. **Back up the database.**
   ```
   pg_dump -Fc webecom_prod > webecom_prod_pre_T021_$(date +%Y%m%d_%H%M).dump
   ```

2. **Stop Celery beat** to prevent `scan_abandoned_checkouts` from firing during migration.
   ```
   systemctl stop celery-beat
   ```

3. **Apply migrations** (additive — no data loss, safe under live traffic):
   ```
   python3 manage.py migrate campaigns
   ```

4. **Restart Django** (to pick up new models and URL routes).

5. **Restart Celery worker** (to pick up new task definitions).

6. **Restart Celery beat** (to activate the `scan-abandoned-checkouts` schedule).

7. **Smoke test:**
   - Confirm `scan_abandoned_checkouts` appears in beat logs every 5 minutes.
   - Place a test order and confirm `suppress_abandoned_checkout` fires without error
     in worker logs.

---

## Rollback procedure

### Step 1 — Stop beat and workers
```
systemctl stop celery-beat celery-worker
```
This prevents new `AbandonedCheckoutEmailSend` rows from being created and
new emails from being sent.

### Step 2 — Revert application code
Roll back to the pre-T021 git ref:
```
git checkout <pre-T021-commit>
```
Restart Django. The new URL `GET /checkout/resume/<token>/` will 404 (route gone).

### Step 3 — Revert migrations
T021 adds two migrations:
- `campaigns/0004_alter_email_step_delay_helptext` (help_text only — zero risk)
- `campaigns/0003_abandoned_checkout` (new tables + extended columns)

```
python3 manage.py migrate campaigns 0002_campaign_model_delta
```

This reverses 0003 and 0004. The reverse migration:
- Drops `AbandonedCheckoutEmailStep` and `AbandonedCheckoutEmailSend` tables.
- Removes constraints and index added to `CampaignSession`.
- Removes `cart`, `customer_email`, `abandoned_at` columns from `CampaignSession`.
- Makes `CampaignSession.order` NOT NULL again (any sessions without an order will
  cause this step to fail — see Risk R-002 below).
- Removes the `email_step` FK and exactly-one-of constraint from `CampaignIssuedCode`.

### Step 4 — Restart services
```
systemctl start celery-worker celery-beat
```

---

## Rollback risks

### R-002 (HIGH): CampaignSession rows without order FK
Migration 0003 made `CampaignSession.order` nullable. If any `CampaignSession` rows
exist with `order=NULL` (created by the abandoned-checkout scan) at rollback time,
the reverse migration will fail when attempting to restore the NOT NULL constraint.

Mitigation before rollback:
```sql
-- Delete abandoned-checkout sessions that have no order.
-- Review count first.
SELECT COUNT(*) FROM campaigns_campaignsession WHERE order_id IS NULL;
-- If acceptable to lose this data:
DELETE FROM campaigns_campaignsession WHERE order_id IS NULL;
```
Only then run `migrate campaigns 0002_campaign_model_delta`.

### R-003 (LOW): In-flight `AbandonedCheckoutEmailSend` rows
Emails already dispatched cannot be recalled. No rollback action needed — the
email has already left the system. HMAC tokens in already-sent emails will 404
after code rollback (resume URL removed), which is acceptable (link becomes inactive).
