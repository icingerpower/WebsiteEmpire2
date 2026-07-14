# Release Checklist — T021: Abandoned Checkout Campaign

**Release Manager verdict date:** 2026-07-05
**Verdict:** APPROVED

---

## Checklist

### Spec approved
PASS. T021 is described in `specs/ecommerce_engine/10_implementation_tickets.md` §318–329
with acceptance criteria AC-070–AC-075 and AC-182 in `08_acceptance_tests.md`.
Human-approved decisions recorded (UF-E cart token 30-day expiry, AF-C4 send-delay wizard
field, ADR-010 ACCEPTED 2026-07-05).

### Architecture decisions recorded
PASS. ADR-010 present at `docs/adr/ADR-010-abandoned-checkout-campaign.md` with
`Status: ACCEPTED (2026-07-05)`.
Minor note: `specs/ecommerce_engine/09_architecture_decisions.md` index table still shows
PROPOSED for ADR-010 (stale copy-paste row — the canonical status lives in the ADR file).
Not a blocker; recommend updating the table as housekeeping.

### Implementation complete
PASS. All deliverables present:
- Models: `AbandonedCheckoutEmailStep`, `AbandonedCheckoutEmailSend`, `CampaignSession`
  extensions, `CampaignIssuedCode` extensions — `campaigns/models.py`
- Service: `create_abandonment_session`, `suppress_abandoned_checkout`,
  `resolve_campaign_for_store` — `campaigns/service.py`
- Tasks: `scan_abandoned_checkouts` (beat, every 5 min),
  `send_abandoned_checkout_email` (worker) — `campaigns/tasks.py`
- Resume view: GET (IMPRESSION) + POST (INTERACTION + redirect) — `cart/views.py`
- Email extension: `send_campaign_email`, `_serialize_campaign_context` — `emails/service.py`
- Suppression wiring: `payments/webhook_views.py`, `payments/paypal_webhook_views.py`,
  `orders/admin.py` (verified by grep)
- Beat schedule entry: `webecom/settings/base.py` L240–246

### Tests added
PASS. Three test files totalling 2 900 lines:
- `campaigns/tests/test_campaigns_ticket021.py` — 1 988 lines (47 scenarios + 4 gap
  extensions covering main service, tasks, resume view, email service, suppression)
- `campaigns/tests/test_campaigns_t021_regression.py` — 358 lines (T021-B1–T021-B4)
- `campaigns/tests/test_campaigns_t021_safety_regression.py` — 554 lines (T021-B5–T021-B7)

### Tests passing
PASS. `python3 manage.py test campaigns cart emails --verbosity=2`
Result: **Ran 200 tests in 1.892s — OK**, 0 failures, 0 errors.

### Coverage checked
PASS (sufficient). 200 test cases covering the full scenario surface. Formal
coverage percentage measurement not run; test volume and scenario breadth accepted
by Test Agent and Spec Reviewer.

### Spec Reviewer approved
PASS. Spec Reviewer found 6 issues in first pass; Developer fixed all 6; Spec Reviewer
re-reviewed and approved.

### Safety Agent approved
PASS. Safety Agent blocked on first pass (B1+H1+M1+M2+M3+L2+L3); Developer fixed all
findings; Safety Agent re-reviewed and approved.

### SEO Agent approved
PASS. SEO Agent blocked on first pass (missing noindex, X-Robots-Tag, Referrer-Policy on
resume URL); Developer fixed all; SEO Agent re-approved.
Evidence: `grep "X-Robots-Tag|Referrer-Policy" cart/views.py` — both headers confirmed
in production code and as comments explaining rationale.

### Designer approved
NOT APPLICABLE. T021 introduces no customer-facing UI beyond the resume-cart redirect
page. The resume URL renders the existing cart page via redirect to /checkout/ — no new
template to review.

### Migrations reviewed
PASS.
- `campaigns/migrations/0003_abandoned_checkout.py` — 15 additive operations: nullable
  FK changes, new models with constraints, RunPython backfill of
  `customer_email` from existing orders, indices, CheckConstraints,
  conditional UniqueConstraints. No DROP, no data-loss operations.
- `campaigns/migrations/0004_alter_email_step_delay_helptext.py` — help_text only,
  zero schema impact.
- `python3 manage.py migrate --check` — no output (all migrations applied).
- `python3 manage.py makemigrations --check --dry-run` — "No changes detected".

### Settings documented
PASS. `webecom/settings/base.py` L240–246: `CELERY_BEAT_SCHEDULE` entry
`"scan-abandoned-checkouts"` with comment citing TICKET-021 and ADR-010 Q4,
explaining ±5 min precision trade-off.

### Deployment checklist ready
PASS. See `ROLLBACK_PLAN.md` in this directory. Pre-deployment steps documented there.

### Rollback plan ready
PASS. See `ROLLBACK_PLAN.md`.

### Known risks listed
PASS. See `KNOWN_RISKS.md`.

### Human decisions listed
Decisions made by the human during T021:
1. DECIDED UF-E (2026-07-04): resume token = HMAC signed with `django.core.signing`,
   30-day expiry matching cart lifetime, stateless.
2. DECIDED AF-C4 (2026-07-04): `send_delay_hours` field lives in the email wizard wizard
   ("Send after" number + hours/days dropdown), not on `Campaign`.
3. ADR-010 ACCEPTED (2026-07-05): full architecture for abandoned checkout approved.

---

## Evidence summary

| Check | Command / source | Result |
|---|---|---|
| 200 tests pass | `python3 manage.py test campaigns cart emails` | OK — 200 in 1.892 s |
| No pending migrations | `python3 manage.py migrate --check` | (no output — clean) |
| No undetected model changes | `python3 manage.py makemigrations --check --dry-run` | No changes detected |
| 3 test files present | `ls campaigns/tests/test_campaigns_ticket021.py …` | All 3 present |
| 7 T021-B entries in BUG_TESTS | `grep T021-B BUG_TESTS/BUG_TESTS.csv | wc -l` | 7 |
| No direct AI API calls | `grep openai|anthropic.client campaigns/tasks.py …` | 0 hits |
| cross_store_unsafe documented | same grep | 4 hits, all with sanctioned-comment |
| ADR-010 ACCEPTED | `docs/adr/ADR-010-abandoned-checkout-campaign.md` line 3 | ACCEPTED (2026-07-05) |
| SEO headers present | `grep X-Robots-Tag|Referrer-Policy cart/views.py` | Both present |
| Suppression in 3 files | `grep suppress_abandoned_checkout payments/… orders/…` | Present in all 3 |
| _serialize_campaign_context | `grep _serialize_campaign_context emails/service.py` | Present |
