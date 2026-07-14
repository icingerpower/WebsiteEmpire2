# Release Notes — T021: Abandoned Checkout Campaign

**Release date:** 2026-07-05
**Ticket:** TICKET-021
**ADR:** ADR-010 (ACCEPTED 2026-07-05)
**Acceptance criteria:** AC-070–AC-075, AC-182

---

## What this release delivers

### Abandonment detection (AC-071)
A global Celery beat task (`scan_abandoned_checkouts`, every 5 minutes) scans active
carts with an `abandoned_checkout` campaign. A cart is considered abandoned when:
- It has at least one item,
- The session does not have a completed order, and
- The elapsed time since last activity exceeds the first email step's `send_delay_hours`.

On detection, a `CampaignSession` is created with `abandoned_at` stamped and the
`ABANDONED` state. Per-step `AbandonedCheckoutEmailSend` rows are enqueued
(`send_abandoned_checkout_email` worker task).

### Timed email sequence (AC-071, AC-074)
`AbandonedCheckoutEmailStep` model (StoreOwnedModel) stores per-campaign email steps
with a `send_delay_hours` integer (required, min 1, default 1). Store admins configure
steps via the email wizard (Type / Style / Copy / Send-after).

Each `send_abandoned_checkout_email` worker call:
1. Acquires a `select_for_update` lock to prevent race conditions.
2. Checks the cart is not converted; if converted, bulk-cancels all sibling SCHEDULED
   sends (B4 fix).
3. Checks `send_delay_hours` elapsed from `abandoned_at`.
4. Idempotently issues a `DiscountCode` if the step has a reward.
5. Mints a HMAC resume token (`django.core.signing`, 30-day expiry).
6. Calls `send_campaign_email` with a serialized context (credential-exfiltration safe).
7. Updates send status to SENT using a store-scoped query (`for_store`) so `store_id`
   always appears in the WHERE clause (B7 fix).

### Tokenized resume-cart link (AC-072, AC-073)
`GET /checkout/resume/<token>/` — stateless HMAC token (30-day expiry, signed with
Django `SECRET_KEY`). Expired or forged tokens return a uniform 410 response.
- GET: advances `NOT_STARTED → IMPRESSION`, renders the cart, sets `noindex` +
  `X-Robots-Tag: noindex, nofollow` + `Referrer-Policy: no-referrer` headers (B6 fix).
- POST: advances `IMPRESSION → INTERACTION`, redirects to `/checkout/` (B6 fix).

### Suppression on purchase completion (AC-074, AC-182)
`suppress_abandoned_checkout(order)` cancels all open `SCHEDULED` sends for any
`CampaignSession` matching the order's `customer_email` across sessions (cross-session
matching). Wired via `transaction.atomic()` savepoints in:
- Stripe webhook handler (`payments/webhook_views.py`)
- PayPal webhook handler (`payments/paypal_webhook_views.py`)
- Order admin mark-complete action (`orders/admin.py`)

### Recovery-revenue attribution (AC-075)
`CampaignSession.conversion_order` is set when suppression fires via a completed order,
enabling revenue attribution queries against `AbandonedCheckoutEmailSend`.

### Credential-exfiltration protection (Safety B5)
`_serialize_campaign_context()` in `emails/service.py` converts all Django model
instances in the template context to sanitized plain dicts before rendering
store-authored email templates. The `store` dict exposes only `{name, subdomain}`.

---

## New models

| Model | App | Notes |
|---|---|---|
| `AbandonedCheckoutEmailStep` | campaigns | StoreOwnedModel; per-campaign email step with `send_delay_hours` |
| `AbandonedCheckoutEmailSend` | campaigns | StoreOwnedModel; per-session per-step send record with status state machine |
| `CampaignSession` (extended) | campaigns | +`cart` FK (nullable), +`customer_email`, +`abandoned_at`, CheckConstraint, conditional UniqueConstraint, scan index |
| `CampaignIssuedCode` (extended) | campaigns | +`email_step` FK (nullable), exactly-one-of constraint |

---

## New migrations

| Migration | Type | Notes |
|---|---|---|
| `campaigns/migrations/0003_abandoned_checkout.py` | Additive + RunPython | 15 operations; no data loss |
| `campaigns/migrations/0004_alter_email_step_delay_helptext.py` | Help-text only | Zero schema impact |

---

## New tasks (Celery)

| Task name | Type | Schedule |
|---|---|---|
| `campaigns.tasks.scan_abandoned_checkouts` | Beat | Every 5 minutes (`*/5`) |
| `campaigns.tasks.send_abandoned_checkout_email` | Worker | On demand (delay queued by beat scan) |

---

## Bugs fixed during T021 development

| Bug ID | Description |
|---|---|
| T021-B1 | `_scan_store` used unscoped reverse-FK on StoreOwnedModel — raised IsolationError silently |
| T021-B2 | `validate_campaign_activation` used unscoped `AbandonedCheckoutEmailStep.objects.filter` |
| T021-B3 | `MaxRetriesExceededError` FAILED update inside rolled-back `atomic()` — send stuck at SCHEDULED |
| T021-B4 | Cart-CONVERTED race guard only cancelled the directly processed send, not sibling sends |
| T021-B5 | Live Django model instances passed to template engine — credential exfiltration possible |
| T021-B6 | GET resume URL advanced IMPRESSION→INTERACTION — automated link scanners inflated counts |
| T021-B7 | SENT status update bypassed store-scoped query — `store_id` absent from WHERE clause |

All 7 bugs have PROVEN regression tests in `BUG_TESTS/BUG_TESTS.csv`.

---

## Deferred items (not blockers)

- GDPR legal basis for emailing non-purchasers: policy gate required before production
  activation of the campaign type (tracked in `11_uncertainties_to_validate.md`).
- Multi-language email steps: `AbandonedCheckoutEmailStepTranslation` model — future ticket.
- `send_transactional_email` credential-exfiltration class vulnerability: tracked as
  systemic improvement, out of T021 scope.
- `_RaisingQuerySet` not overriding `.update()`/`.delete()`: systemic improvement,
  out of T021 scope.
