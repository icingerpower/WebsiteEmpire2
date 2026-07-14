# Known Risks — Checkout (complete) + Static Pages / Contact & Quotation Forms

**Date:** 2026-07-10. All items below were independently verified against current code/docs
by the Release Manager in this review cycle (not carried forward unverified).

## Deploy-config risks (block PRODUCTION, not STAGING)

1. **`redis` package not in `requirements.txt`.** Required by `production.py`'s
   `CACHES["default"]["BACKEND"] = "django.core.cache.backends.redis.RedisCache"`, which
   the F3 security fix made mandatory (production now raises `ImproperlyConfigured` at
   startup without `CACHE_URL`). Installed in this dev venv (`redis==7.4.0` per `pip
   freeze`) but not declared as a project dependency — works here by accident, will not
   work on a clean production install. **Owner: Human/DevOps. Action: add to
   requirements.txt before deploy.**
2. **`psycopg2-binary` also not in `requirements.txt`** (newly found in this review, not
   previously carried). `production.py` uses `ENGINE: django.db.backends.postgresql` for
   both the `default` and `analytics` databases, which needs a psycopg driver. Same
   "works by accident in this venv" situation as #1. **Owner: Human/DevOps. Same action.**
3. **Nginx real-IP handling for the rate limiter (F5).** `pages/antispam.py::_client_ip`
   uses `REMOTE_ADDR` only (correct — not attacker-spoofable at the app layer), but if
   nginx does not set the real client IP into `REMOTE_ADDR` (real_ip module / trusted proxy
   list), every visitor collapses to one shared bucket and the rate limiter becomes either
   meaningless or a shared-lockout DoS vector. Same concern applies to any checkout-adjacent
   rate limiting. **Owner: DevOps. Action: verify + smoke-test with two distinct external
   clients before public launch.**
4. **Access-log scrubbing of Stripe return-redirect query params.** Stripe's
   `?payment_intent_client_secret=...` return-redirect param will appear in web server/proxy
   access logs (standard Stripe pattern, not a code bug). `docs/security/CHECKOUT_BATCH_2_AUDIT.md`
   INFO-4 flags this as needing an explicit acceptance note or log-scrubbing rule at the
   nginx/CDN layer. **Owner: DevOps/Security.**
5. **No nginx `limit_req` (or equivalent) in front of public checkout/token endpoints or
   the contact/quotation forms.** Deploy-level throttling is the intended defense-in-depth
   layer on top of the application-level rate limiter (which is itself contingent on #3
   above). **Owner: DevOps, before public launch.**

## Functional / correctness risks (not launch-blocking per the source audits, but real)

6. **CHECKOUT_BATCH_2_AUDIT MEDIUM-2 — `void_pending_order` can race a payment that
   succeeded at the processor but isn't yet recorded locally.** Verified still open: no
   `payments.webhook.paid_after_void` alert or pre-cancel-check exists in
   `orders/service.py` or `payments/webhook_views.py` as of this review. Window is "seconds
   wide" per the audit but the stale-checkout retry flow (ADR-010) makes the precondition
   realistic. Widened by LOW-1 (GC staleness clock not touched by `.update()` calls) and
   LOW-2 (`checkout_payment_return` stamps `authorized_at` without checking
   `payment_status`). **Owner: Developer + After-Bug Test Agent — needs a ticket.**
7. **CHECKOUT_BATCH_2_AUDIT MEDIUM-1 — purchase-pixel guard burned on first thank-you
   render regardless of payment status.** Confirmed not yet fixed, but explicitly not a
   blocker for *this* release — it only matters once T030 pixel providers consume the
   `fire_purchase_pixel` flag, which is out of scope here. **Owner: Developer, before T030.**
8. **PayPal Vault (off-session upsell charging) gated on merchant onboarding.**
   `payments/paypal_connector.py:68 supports_off_session_charge` returns `False`. Post-purchase
   upsell off-session charging works for Stripe only until PayPal vaulting is onboarded.
   Known limitation, not a bug.
9. **JS runtime behavior has no automated harness.** No `jest`/`playwright`/`cypress` files
   found anywhere in the repo (confirmed by filesystem search). Stripe Elements mount,
   country-field swap on address forms, and the upsell widget's `window.location.reload()`
   behavior are covered only by template-level/Python-side assertions, not a real browser
   harness. Residual risk for anything that only breaks at the JS/DOM layer.
10. **L1 — title separator spec-vs-practice contradiction.** `specs/ecommerce_engine/07_multilingual_seo.md`
    META-001 specifies `<page title> — <store name>`; both `static_page.html:6` and
    `product.html:6` render `... | {{ store }}`. Cosmetic, engine-wide, needs a one-line
    decision (amend spec or templates) — not a functional risk.
11. **Local dev DB migration drift.** `manage.py showmigrations` shows ~16 unapplied
    migrations against the checked-in `db.sqlite3` (normal dev drift — the test runner uses
    its own migrated DB). Not a release risk; flagged only so it is not mistaken for one.
    Standard `manage.py migrate` at deploy time is unaffected.
12. **Documentation drift.** `docs/security/STATIC_PAGES_AUDIT.md`'s verdict text still
    reads BLOCKED even though F1–F8 are all verified fixed in current code. Purely a
    documentation hygiene issue (confirmed by re-reading the actual code, not the doc) —
    recommend updating the doc so a future reader does not have to re-derive this.

## Human decisions still open (see RELEASE_CHECKLIST.md "Human decisions listed" for full detail)

13. ADR ratification: ADR-015, ADR-017, ADR-018, ADR-020, ADR-021 remain status `PROPOSED`.
14. SEO:P1 — policy-page indexability spec-vs-implementation contradiction; spec amendment
    recommended by the SEO Agent, needs human sign-off.
15. 18:P2 — quotation-page `quantity` field exceeds ADR-018 D4; ratify or remove.
16. 20:P1 — non-funnel PayPal/Stripe capture-delay default; accept or request a follow-up.
17. 20:P2 — PayPal `NO_SHIPPING` weakens Seller Protection for physical goods; accept for v1
    or request address-forwarding before high-volume physical-goods PayPal stores go live.
