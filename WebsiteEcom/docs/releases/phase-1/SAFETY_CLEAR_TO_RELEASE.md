# SAFETY AGENT — Phase 1 CLEAR TO RELEASE

> Formal Safety Agent release verdict for gate B3 (`KNOWN_RISKS.md` open blocker B3,
> `RELEASE_CHECKLIST.md` gate #8). Issued by the Safety Agent after the Phase 1
> security audit and the re-review of all remediation work.

- **Date:** 2026-07-03
- **Scope:** Phase 1 (TICKET-001 through TICKET-020R, all remediation items).
  Apps in scope: core, stores, catalog, discounts, orders, payments, customers,
  analytics, aijobs, permalinks, cart.
- **Out of scope:** Phase 2 features (storefront, upsell funnel T027/T028 payment
  flow, public SEO pages). The upsell capture-window design was reviewed separately —
  see `docs/security/SECURITY_CHECKLIST_upsell_capture_window.md`, now **APPROVED**
  (ADR-007 amendments applied, resolved 2026-07-03).

---

## 1. Audit areas covered

1. **Multi-tenant isolation** — organization/store scoping of every ORM query path,
   `for_store()` manager usage, soft-FK `store_id` filtering, auditable
   `cross_store_unsafe()` escape hatch.
2. **Admin exposure & permissions** — super-admin vs store-admin boundaries,
   `StoreEmployee` access gating, admin querysets scoped per store.
3. **Payment webhooks** — Stripe and PayPal signature verification, replay/idempotency
   handling, webhook-vs-application race conditions, multi-account secret resolution.
4. **Payment routing engine & processor credentials** — ADR-006 routing decision flow,
   `DecisionLog` content (no credential leakage), `EncryptedCharField`/Fernet secrets
   handling, `FERNET_KEY` lifecycle.
5. **Checkout & discounts** — checkout idempotency (AC-064), coupon/gift-card
   concurrency (atomic decrement, `SELECT FOR UPDATE` / guarded conflict-update
   patterns), over-redemption resistance.
6. **Analytics beacon ingestion** — public stats endpoints: flooding/amplification,
   payload caps, client-asserted fields, PII leakage into events.
7. **Sessions & anonymous visitors** — session creation policy, anonymous-cart
   storage amplification, cookie flags.
8. **Production settings hardening** — DEBUG, ALLOWED_HOSTS, HSTS, secure cookies,
   SSL redirect, SECRET_KEY sourcing, error leakage.

---

## 2. Findings summary

| ID | Finding | Severity | Status | Resolution / tracking |
|----|---------|----------|--------|-----------------------|
| C1 | Cross-tenant analytics leak | Critical | **CLOSED** | `StoreEmployee` gate on analytics access + explicit `store_id` filtering; `cross_store_unsafe()` for the one legitimate cross-store audit path; **6 regression tests** |
| H1 | Beacon flooding (unbounded events per POST) | High | **CLOSED** | `MAX_EVENTS_PER_BEACON = 50` enforced in `analytics/ingest.py` |
| H2/R1 | Webhook race (double-processing on concurrent delivery) | High | **CLOSED** | `@transaction.atomic` + `select_for_update` / guarded `UPDATE … WHERE status='PENDING'` — the transition happens exactly once (Stripe `payments/webhook_views.py`, PayPal `paypal_webhook_views.py`) |
| M1 | Webhook order–account cross-org check | Medium | **FAST-FOLLOW** | Tracked as ticket; must land before T028 (upsell) ships — see `KNOWN_RISKS.md` |
| M2 | PayPal multi-account webhook verification | Medium | **CLOSED** | Webhook handler iterates **all** active `ProcessorAccount` rows and tries each `webhook_secret`, instead of assuming a single account |
| M3 | Per-email coupon concurrency at flash-sale scale | Medium | **FAST-FOLLOW** | Tracked as ticket; pattern is correct today (`ON CONFLICT … DO UPDATE … WHERE use_count < per_email_limit`), contention hardening before high-volume launch |
| M4 | Client-controlled beacon fields (product_id/collection_id not validated against catalog) | Medium | **FAST-FOLLOW** | Tracked as ticket; analytics-only impact, no SQLi, no financial data path — before Phase 2 analytics launch |
| L1 | `EncryptedCharField` failure mode on key change (InvalidToken at read time) | Low | **FAST-FOLLOW** | Tracked as ticket; failure is loud, diagnosis aid before first production `ProcessorAccount` |
| L2 | Discounts admin missing `for_store` scoping | Low | **CLOSED** | `discounts/admin.py`: `DiscountCode.objects.for_store(store)` |
| L3 | Anonymous session storage amplification | Low | **FAST-FOLLOW** | Tracked as ticket; before public storefront launch |
| L4 | No startup `FERNET_KEY` validation | Low | **FAST-FOLLOW** | Tracked as ticket; add to `AppConfig.ready()` before first production deployment |

**Closed: C1, H1, H2/R1, M2, L2 — all Critical and High findings are closed.**
**Fast-follow: M1, M3, M4, L1, L3, L4 — all Medium/Low, each with a named Phase 2 gate
in `KNOWN_RISKS.md` and `RELEASE_CHECKLIST.md`. None is exploitable for financial or
cross-tenant data impact in the Phase 1 deployment profile (no public storefront, no
production processor credentials yet, super-admin-provisioned stores).**

---

## 3. Positive confirmations (verified correct as implemented)

- **Payment routing engine (ADR-006/T020):** routing decisions are logged to
  `DecisionLog` without processor credentials or secrets; decision entries are
  audit-suitable.
- **Checkout:** idempotent per AC-064; no double-order path found under retried
  submissions.
- **Webhook signature verification:** both Stripe and PayPal webhooks verify
  signatures before any state change; unverifiable payloads are rejected without
  side effects. PayPal verification covers every active `ProcessorAccount` (M2 fix).
- **Webhook state transitions:** single-transition guarantee under concurrency
  (H2/R1 fix) — guarded UPDATE / `select_for_update` inside `@transaction.atomic`.
- **Tenant isolation:** store-scoped managers used across admin and query paths;
  the only cross-store read is the explicit, auditable `cross_store_unsafe()` in
  `check_purchase_mismatch.py`.
- **Production hardening** (`pradize/settings/production.py`): `DEBUG = False`,
  `SECURE_SSL_REDIRECT = True`, `SESSION_COOKIE_SECURE = True`,
  `CSRF_COOKIE_SECURE = True`, HSTS 31536000 with subdomains + preload,
  `SECRET_KEY` from environment.
- **Secrets at rest:** processor credentials stored via `EncryptedCharField`
  (Fernet); no plaintext credentials in fixtures, logs, or `DecisionLog`.
- **Regression coverage:** the C1 fix carries 6 dedicated regression tests; the
  full suite (616 tests) passes.
- **Upsell capture-window (Phase 2 pre-work):** ADR-007 was amended per the Safety
  Agent review (two-charge baseline, atomic accept race guard, processor pinning,
  tokenized thank-you access control, provisional items, multi-charge refund model —
  all 9 amendments). The dedicated checklist is now APPROVED with all implementation
  boxes checked. T028 remains gated on Phase 2, not on this release.

---

## 4. Verdict

## **CLEAR TO RELEASE**

Phase 1 (TICKET-001 through TICKET-020R, including all remediation items) is cleared
from a safety standpoint, with the understanding that the six fast-follow items
(M1, M3, M4, L1, L3, L4) are tracked as tickets with named Phase 2 gates and **do not
block Phase 1 completion**. All Critical and High findings are closed with verified
code-level controls and regression tests.

Conditions carried forward (informational, owned by the Release Manager / Phase 2
planning — not blockers):

1. M1 must be closed before T028 (upsell payment flow) ships.
2. L1 + L4 must be closed before the first production `ProcessorAccount` credential
   is stored / first production deployment.
3. M4 before the Phase 2 analytics launch; M3 before high-volume launch; L3 before
   public storefront launch.

*Safety Agent, Pradize ecommerce engine — 2026-07-03.*
