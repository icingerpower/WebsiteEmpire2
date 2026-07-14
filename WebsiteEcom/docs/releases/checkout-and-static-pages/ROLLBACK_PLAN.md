# Rollback Plan — Checkout (complete) + Static Pages / Contact & Quotation Forms

**Date:** 2026-07-10

This release spans two areas that can be rolled back independently, since they touch
largely disjoint apps. The one shared surface is `stores/middleware.py`
(`SF_LANG_WRITE_EXEMPT_PREFIXES`), called out below.

---

## Area 1: Checkout

### What was added / changed

**Application code:**
- `storefront/views_checkout.py`, `storefront/address_meta.py`, `storefront/forms_checkout.py`,
  `storefront/tokens.py` — checkout flow, address validation (ADR-017), signed tokens.
- `storefront/static/storefront/js/checkout.js`, `storefront/templates/storefront/pages/{checkout,checkout_payment,thank_you}.html`.
- `payments/paypal_connector.py`, `payments/paypal_webhook_views.py`, `payments/webhook_views.py`,
  `payments/processor.py` — PayPal buyer-approval flow (ADR-020), H1 amount/currency guard,
  M1 Decimal-cents fix.
- `cart/service.py`, `cart/tasks.py`, `cart/checkout.py` — cart lifecycle (ADR-016).
- `orders/service.py`, `orders/models.py` — FiredPixel guard, void/GC paths.
- `stores/middleware.py` — `SF_LANG_WRITE_EXEMPT_PREFIXES` (ADR-015 addendum; also touches
  the static-pages area via the `/products/` and `/campaigns/upsell/` entries).

**Database schema (migrations):**
| App | Migration(s) | Notes |
|---|---|---|
| cart | `0004_checkoutstate`, `0005_backfill_converted_cart_session_keys` | CheckoutState model |
| orders | `0005`–`0011` | gift_card, discount_code, model gaps, breach_alerted, checkout fields |
| payments | `0005`–`0006` (+ any PayPal-flow migrations) | fallback_rule, paypal_capture_mode |
| campaigns | `0007_campaignsessiontoken_offered_amount_cents` | upsell token amount tracking |
| shipping | `0003_shippingzone_service_description` | ADR-019 |

Run `python3 manage.py showmigrations` against the target environment before rollback to
get the exact applied set — the table above is the migration set added/likely-added in this
release window, not a guaranteed-exhaustive list; verify against the deploy's actual
migration history.

### Rollback procedure

1. **Disable new checkout entry points first (fastest, no data risk).** If the PayPal
   buyer-approval flow or a specific checkout step is the problem, gate it behind a feature
   flag / short-circuit in `storefront/urls.py` or `views_checkout.py` rather than a full
   code revert — checkout is revenue-critical and a full revert risks losing the H1/M1
   PayPal fixes, which are security-relevant.
2. **If a full revert is required:** revert the application code changes via git to the
   pre-release commit range for `storefront/`, `payments/`, `cart/`, `orders/`,
   `campaigns/` (token amount) and `stores/middleware.py`. Do **not** revert
   `stores/middleware.py`'s `SF_LANG_WRITE_EXEMPT_PREFIXES` in isolation without also
   checking the static-pages area (see "Shared surface" below) — it fixes a real bug
   (`sf_lang` session clobbering) for both areas.
3. **Migrations:** all schema fields added in this window are additive
   (new nullable/defaulted columns and new tables). Leaving them in place after a code
   revert is safe. Only reverse-migrate if the column names collide with something else or
   ops specifically wants the schema clean:
   ```bash
   python3 manage.py migrate cart <prev>
   python3 manage.py migrate orders <prev>
   python3 manage.py migrate payments <prev>
   python3 manage.py migrate campaigns <prev>
   python3 manage.py migrate shipping <prev>
   ```
   Determine `<prev>` from `showmigrations` on the actual deployed environment.
4. **In-flight financial state:** as with prior payment-flow releases, `OrderCharge` rows
   already `CAPTURED`/`PAID` are real money movements and are **not** undone by a code or
   schema rollback. Any charge captured through the new PayPal H1-guarded path or the
   ADR-020 approval flow that needs reversing must go through the payment processor
   dashboard or the existing refund flow — never by deleting/mutating the local row alone.
5. **PayPal-specific:** if rolling back ADR-020 specifically (buyer-approval completion),
   confirm no PayPal orders are stuck in `CREATED`/`PAYER_ACTION_REQUIRED` — those need the
   approval link surfaced to the shopper or the order voided; a code revert mid-flow will
   orphan them exactly like the pre-existing `retrieve_payment_intent_raw_status` stub gap
   already documented in `docs/security/CHECKOUT_PAYPAL_AMOUNT_VALIDATION.md` (question 2).

---

## Area 2: Static pages + contact/quotation forms

### What was added

**New app:** `pages/` (models: `StaticPage`, `StaticPageTranslation`, `ContactMessage`,
`QuotationRequest`; `pages/signals.py`, `pages/antispam.py`, `pages/admin.py`,
`pages/seeding.py`, `pages/templatetags/`, a management command for seeding).

**Touched existing apps:**
- `permalinks/models.py` (`Permalink.save()` now validates reserved slugs — engine-wide),
  `permalinks/signals.py` (`handle_slug_change` fetch+mutate+save instead of bare `.update()`;
  translated-slug 301 support), `permalinks/registry.py`.
- `storefront/views_pages.py`, `storefront/views_product_forms.py` (length caps, F2 fix),
  `storefront/templates/storefront/pages/static_page.html`,
  `.../partials/{contact_form,static_quotation_form}.html`.
- `catalog/signals.py` (`_sync_translation_permalink` — savepoint fix, shared with static
  pages' translation permalink sync).
- `sitemaps/sitemaps.py` (policy-page hreflang-alternate suppression, lastmod source).
- `emails/service.py`, `emails/templates/emails/contact_message_received.{html,txt}`.
- `webecom/settings/production.py` (Redis `CACHES` requirement — F3 fix).
- `stores/middleware.py` (`/products/` entry in `SF_LANG_WRITE_EXEMPT_PREFIXES` — shared
  with checkout's `sf_lang` fix).

**Database schema (migrations):** `pages/migrations/0001_initial` onward (new app — the
entire `pages` app's tables), plus `permalinks/migrations/0005_alter_permalink_slug` (or
equivalent — the reserved-slug validator addition), plus a `catalog` migration if the
translation savepoint fix required one (it did not add fields, only signal logic — verify
with `git log --oneline -- catalog/migrations/` if uncertain).

### Rollback procedure

1. **Disable the public-facing surface first.** The lowest-risk rollback is removing the
   `pages` app's URLs from `storefront/urls.py` (contact/quotation forms, static page
   catch-all) rather than uninstalling the app — this stops new public submissions
   immediately without touching data or other apps' behavior.
2. **If a full app removal is required:**
   ```bash
   python3 manage.py migrate pages zero
   ```
   then remove `"pages"` from `INSTALLED_APPS` and revert the touched files in
   `permalinks/`, `storefront/`, `catalog/`, `sitemaps/`, `emails/`, `stores/middleware.py`
   listed above via git.
3. **Reserved-slug validator (permalinks fix):** this fix also protects `Product`/
   `Collection` slugs (F1 was engine-wide, not `pages`-only). **Do not revert
   `permalinks/models.py Permalink.save()`'s validator call as part of a `pages`-only
   rollback** — doing so silently re-opens the reserved-slug bypass for products and
   collections too, which is a regression unrelated to whatever prompted the `pages`
   rollback. If `pages` must be fully removed, keep the permalinks-level fix.
4. **Translated-slug 301 fix (SEO H2):** same caution — this fix lives in
   `catalog/signals.py::_sync_translation_permalink` and is shared by
   `ProductTranslation`/`CollectionTranslation`/`StaticPageTranslation`. Do not revert it as
   part of isolating a `pages`-only problem.
5. **Redis cache requirement (F3 fix, `production.py`):** if this is rolled back (reverting
   to `LocMemCache` in production), explicitly re-open F3 in the security record — the
   anti-spam rate limiter becomes best-effort/non-atomic again across gunicorn workers. Only
   do this if Redis is genuinely unavailable in the target environment, and treat it as a
   temporary degradation, not a clean state.
6. **Data:** `ContactMessage`/`QuotationRequest` rows already submitted are business records
   (audit trail — admin has no delete permission on them by design). A rollback should not
   delete existing submissions; only new submissions are affected by disabling the URLs/app.
7. **Seeded pages:** `seed_pages_for_store` is idempotent (`get_or_create`). If the `pages`
   app is removed and later re-added, re-running the seed command is safe and will not
   duplicate rows for stores that already have them (verified: "0 page(s) created" on
   second run in the test suite).

---

## Shared surface — `stores/middleware.py` `SF_LANG_WRITE_EXEMPT_PREFIXES`

Both areas touch this list (checkout: `/checkout/`, `/cart/`, `/search/`, `/orders/`,
`/campaigns/upsell/`; static pages: `/products/`). If only one area is being rolled back,
remove only that area's prefix entries, not the whole list — removing the checkout entries
re-opens the original `sf_lang`-clobbering bug for checkout; removing `/products/` re-opens
it for the notify-me/quotation forms. `stores/tests/test_sf_lang_write_exemption_drift.py`
will fail loudly if a fixed route is left uncategorized after a partial rollback — run it
as a post-rollback smoke check:
```bash
python3 manage.py test stores.tests.test_sf_lang_write_exemption_drift
```

## What is NOT rolled back by this plan

- Real money already captured/paid through the checkout flow (Stripe or PayPal) — reversed
  only via the processor or the existing refund flow.
- `ContactMessage`/`QuotationRequest` business records already submitted by real customers.
- The reserved-slug and translated-slug-301 engine-wide fixes, if the *other* area (whichever
  one is not being rolled back) still depends on them.
