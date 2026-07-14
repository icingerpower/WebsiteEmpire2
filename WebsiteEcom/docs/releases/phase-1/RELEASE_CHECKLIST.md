# Release Checklist — Phase 1 (Technical Foundation)

> Filled by Release Manager Agent. Initial: 2026-07-03. Final verdict: 2026-07-03 (2nd invocation).
> Evidence column cites the specific file or command output used to verify each item.

| # | Gate | Status | Evidence |
|---|------|--------|----------|
| 1 | Spec approved (human where critical) | PARTIAL — human decisions recorded in `11_uncertainties_to_validate.md`; critical decisions resolved (payment enums, vault backend, inventory scope, org grouping, upsell model) | `specs/ecommerce_engine/11_uncertainties_to_validate.md` |
| 2 | Architecture decisions recorded (ADRs) | PASS | ADR-001 through ADR-007 in `specs/ecommerce_engine/09_architecture_decisions.md`; all 7 ACCEPTED; ADR-007 amended 2026-07-03 with 9 safety amendments |
| 3 | Implementation complete (Phase 1 T001–T020) | PASS | All 20 Phase-1 tickets implemented; apps: core, stores, catalog, discounts, orders, payments, customers, analytics, aijobs, permalinks, cart |
| 4 | Tests added | PASS | 616 tests across 57 test files spanning all apps |
| 5 | Tests passing | PASS | `python3 manage.py test --verbosity=0` — Ran 616 tests in 24.215s — OK (re-verified 2nd invocation, 2026-07-03) |
| 6 | Coverage checked | NOT CHECKED | Coverage tool not run; 616 tests cover all implemented Phase 1 apps |
| 7 | Spec Reviewer approved | PASS | `docs/releases/phase-1/SPEC_REVIEW_APPROVAL.md` — 3rd pass, APPROVED, dated 2026-07-03; all BLOCKERs and MAJORs resolved; 3 MINORs accepted as fast-follows |
| 8 | Safety Agent approved | PASS | `docs/releases/phase-1/SAFETY_CLEAR_TO_RELEASE.md` — CLEAR TO RELEASE, dated 2026-07-03; C1/H1/H2/M2/L2 CLOSED; M1/M3/M4/L1/L3/L4 as named fast-follow tickets with Phase 2 gates. Upsell checklist `docs/security/SECURITY_CHECKLIST_upsell_capture_window.md` updated to APPROVED (14/14 boxes checked). |
| 9 | SEO Agent approved | N/A — Phase 1 has no public/indexable pages | Phase 1 is backend foundation only; storefront is Phase 2 |
| 10 | Designer approved | N/A — Phase 1 has no storefront UI | Admin UI present but Designer review not required for Phase 1 |
| 11 | Migrations reviewed | PASS | `python3 manage.py makemigrations --check` exits 0 ("No changes detected"). Three metadata-only migration files generated and committed: `aijobs/0004_alter_aijob_job_type.py`, `discounts/0004_alter_discountcode_times_used.py`, `payments/0005_alter_organizationrule_fallback_rule_and_more.py`. No DB schema impact. |
| 12 | Settings documented | PASS | `specs/ecommerce_engine/06_settings_requirements.md` — complete 28-group settings inventory. `pradize/settings/production.py` documented with comments for all env vars. |
| 13 | Deployment checklist ready | NOT PRESENT | No deployment checklist document created yet |
| 14 | Rollback plan ready | NOT PRESENT | See `ROLLBACK_PLAN.md` (created now by Release Manager) |
| 15 | Known risks listed | NOT PRESENT | See `KNOWN_RISKS.md` (created now by Release Manager) |
| 16 | Human decisions listed | PASS | `specs/ecommerce_engine/11_uncertainties_to_validate.md` — all resolved items clearly marked DECIDED/RESOLVED; open items marked with urgency |

## Verified security controls (spot-checked in code)

| Finding ID | Control | Verification |
|------------|---------|--------------|
| H1 | Beacon event cap | `MAX_EVENTS_PER_BEACON = 50` in `analytics/ingest.py` line 29 |
| H2 | Webhook idempotency | Guarded `UPDATE ... WHERE status='PENDING'` in `payments/webhook_views.py`; PayPal: "Only transitions once" comment in `paypal_webhook_views.py` line 281 |
| M2 | PayPal multi-account webhook | `webhook_views.py` iterates all active `ProcessorAccount` rows; tries each `webhook_secret` in turn |
| L2 | Discounts admin store scoping | `discounts/admin.py` line 142: `DiscountCode.objects.for_store(store)` |
| C1 | Analytics cross-tenant | `analytics.Event` uses soft `store_id` with explicit filter; `check_purchase_mismatch.py` uses `cross_store_unsafe()` (auditable) |

## Production settings verified

- `DEBUG = False` — production.py line 11
- `SECURE_HSTS_SECONDS = 31536000` — production.py line 63
- `SECURE_HSTS_INCLUDE_SUBDOMAINS = True` — production.py line 64
- `SECURE_HSTS_PRELOAD = True` — production.py line 65
- `SESSION_COOKIE_SECURE = True` — production.py line 61
- `CSRF_COOKIE_SECURE = True` — production.py line 62
- `SECURE_SSL_REDIRECT = True` — production.py line 60
- `SECRET_KEY` from env — production.py line 13

## Fast-follow items (accepted, not blocking)

| # | Finding | Phase 2 gate |
|---|---------|--------------|
| M1 | Webhook order–account cross-org check | Before T028 ships |
| M3 | Per-email coupon concurrency | Before high-volume launch |
| M4 | Client-controlled beacon fields | Before Phase 2 analytics launch |
| L1 | EncryptedCharField silent decrypt on bad key | Before first production ProcessorAccount |
| L3 | Anonymous session storage amplification | Before public storefront |
| L4 | No startup FERNET_KEY validation | Before first production deployment |
