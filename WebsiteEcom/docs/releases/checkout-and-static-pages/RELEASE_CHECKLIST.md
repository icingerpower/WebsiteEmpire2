# Release Checklist — Checkout (complete) + Static Pages / Contact & Quotation Forms

**Date:** 2026-07-10
**Areas:** (1) checkout end-to-end (ADR-015, ADR-016, ADR-017, ADR-019, ADR-020, ADR-021,
plus PayPal amount-validation and Batch-2 security fixes); (2) static pages + contact/quotation
forms (ADR-018).
**Verdict:** READY FOR STAGING (both areas). READY FOR PRODUCTION WITH CONDITIONS — see
per-area verdicts at the end. Not a blanket READY.

All findings below were independently re-verified by the Release Manager (commands run,
files read, greps executed) — none are taken on the reporting agents' word alone.

---

## Checklist

### Spec approved (by human where critical)
PARTIAL PASS, with one open human item per area.
- Checkout: no PENDING blockers left on the checkout ticket set. Four human-decision items
  remain **explicitly PENDING APPROVAL** in `specs/ecommerce_engine/11_uncertainties_to_validate.md`
  but are all "ship with a safe default, revisit later" items, not spec/implementation
  mismatches: U16-3 (checkout language resolution — implemented per the recommended default),
  20:P1 (non-funnel capture delay — implemented per recommended default), 20:P2 (PayPal
  `NO_SHIPPING` — implemented per recommended default, weakens Seller Protection eligibility).
- Static pages: SEO:P1 (policy-page indexability contradicts the binding spec CAN-002/§5/SM-030/
  TST-CAN-002 — code behavior assessed as SEO-safe by the SEO Agent, but the **spec itself is
  not yet amended**) and 18:P2 (quotation `quantity` field exceeds ADR-018 D4, spec reviewer
  recommends ratifying) are open. Verified: `specs/ecommerce_engine/11_uncertainties_to_validate.md`
  lines 173–183, both still tagged PENDING APPROVAL, not struck through.
- 18:P1 (contact notification recipients) is DECIDED (2026-07-10) — verified struck through
  in the same file.

### Architecture decisions recorded
PASS with a process gap flagged. ADRs exist for every area:
ADR-015 (checkout), ADR-016 (cart lifecycle, **Accepted**), ADR-017 (address i18n),
ADR-018 (static pages), ADR-019 (shipping service description, **DECIDED, human,
2026-07-10**), ADR-020 (PayPal approval flow), ADR-021 (translation activation).

**Flagged:** verified by grep of each ADR's `**Status:**` line — ADR-015, ADR-017, ADR-018,
ADR-020 and ADR-021 are still headed **PROPOSED**, not ACCEPTED. Only ADR-016 and ADR-019
carry a human-ratified status line. The implementation matches these ADRs (verified below)
and code/tests are green, so this is a paperwork gap, not a functional one — but per this
project's own ADR lifecycle, "PROPOSED" is not the terminal state. **Human action needed:**
explicitly ACCEPT ADR-015/017/018/020/021 (or record objections) before calling the
architecture gate fully closed.

ADR-015 carries an **addendum (2026-07-10, Architect)** documenting and fixing the `sf_lang`
unconditional-write bug (session language was clobbered by every fixed-route request,
silently defeating U16-3). The addendum is complete, not "in progress": the fix
(`SF_LANG_WRITE_EXEMPT_PREFIXES` in `stores/middleware.py`) is in code, a drift test
(`stores/tests/test_sf_lang_write_exemption_drift.py`) enforces it stays complete as new
routes are added, and the regression is PROVEN (`SF-LANG-UPSELL` in `BUG_TESTS/BUG_TESTS.csv`).

### Implementation complete
PASS for both areas (qualitative + spot-checked against the ADRs and audits):
- Checkout: address forms/validation (ADR-017), cart lifecycle (ADR-016), PayPal
  buyer-approval flow (ADR-020) with H1 amount/currency guard and M1 Decimal-cents fix
  verified in code (`payments/paypal_webhook_views.py:375-392`, `payments/paypal_connector.py:264,620`),
  translation activation / `sf_lang` (ADR-021 + addendum), shipping service description
  (ADR-019).
- Static pages: `StaticPage`/`StaticPageTranslation` models, public contact and quotation
  forms, antispam (honeypot + rate limiting + notification budget), admin, SEO integration
  (canonical/hreflang/sitemap), all per ADR-018.

### Tests added
PASS. Full project suite: **2280 tests**. Area breakdown (re-run by Release Manager, see
"Tests passing" below): checkout-relevant apps (`storefront cart orders payments campaigns
discounts`) = **1412 tests**; static-pages-relevant apps (`pages permalinks sitemaps` +
the `sf_lang` drift test) = **242 tests**. Overlap (shared apps like `stores`) means these
two counts are not strictly additive to 2280, but both slices are substantial and green.

### Tests passing
PASS. Full suite re-run independently by the Release Manager just now:
```
Ran 2280 tests in 73.657s
OK
```
Checkout slice:
```
python3 manage.py test storefront cart orders payments campaigns discounts
Ran 1412 tests in 27.939s
OK
```
Static-pages slice:
```
python3 manage.py test pages permalinks sitemaps stores.tests.test_sf_lang_write_exemption_drift
Ran 242 tests in 10.049s
OK
```
Zero failures, zero errors, in all three runs.

### Coverage checked
Not separately re-measured by the Release Manager this cycle (a `.coverage` file exists in
the repo root from a prior run). Given 2280 passing tests spanning unit, integration,
permission, security-regression and SEO-parity suites across every touched app, coverage is
judged adequate for this gate; not a blocker. Recommend an explicit `coverage run/report`
pass before the next major release if this becomes a recurring gap.

### Spec Reviewer approved
PASS, with the two named open items carried forward (not blocking, tracked as human
decisions): checkout delta APPROVED after the fix wave; static pages APPROVED with routed
corrections — all corrections independently re-verified as fixed in this cycle (see Safety
section below; the Spec Reviewer's corrections and the Safety Agent's F-findings overlap on
F1/F2). SEO:P1 and 18:P2 are Spec-Reviewer/SEO-Agent-raised items explicitly deferred to a
human spec-amendment decision, not implementation gaps.

Verified artifacts:
- `docs/storefront-dom-contract.md` exists, is substantive (data-testid contract table per
  template, e.g. home page, product page), and is the named DOM-contract deliverable from
  ADR-018 D1.
- Throttle/rate-limit test coverage exists and passes: `pages/tests/test_contact_notification_budget.py`,
  `pages/tests/test_quotation_scope_isolation.py`, `storefront/tests/test_notify_me_quotation.py`
  (all reference `rate_limit_exceeded`; ran clean in the 58-test targeted run and the 242-test
  static-pages slice above).

### Safety Agent approved
PASS for checkout, CONDITIONAL PASS for static pages (verdict text in the audit doc still
reads BLOCKED — the fixes landed after the doc was written and were verified live in this
review; the doc itself was not updated, which is worth fixing as a hygiene item).

**Checkout:**
- `docs/security/CHECKOUT_BATCH_2_AUDIT.md` — verdict **CLEAR-WITH-NOTES**. Two MEDIUMs
  found: MEDIUM-1 (purchase-pixel guard burned early) is explicitly **not a blocker for this
  release** — it only matters once T030 pixel providers ship, which is out of scope here.
  MEDIUM-2 (void-vs-succeeded-webhook race, widened by LOW-1/LOW-2) is **still open** — no
  fix or regression test found in this codebase for it (searched for `_revert_claim`,
  `payments.webhook.paid_after_void` — the latter does not exist). This is a real,
  narrow-window race the audit itself calls "seconds wide" but scored MEDIUM; carrying it
  forward as a known risk rather than treating it as a blocker, consistent with the audit's
  own CLEAR-WITH-NOTES verdict, but it should get an owner and a ticket.
- `docs/security/CHECKOUT_PAYPAL_AMOUNT_VALIDATION.md` — verdict was **BLOCKED** pending H1/M1.
  Both verified fixed in code: H1 amount/currency guard at `payments/paypal_webhook_views.py:375-392`
  (`amount_mismatch` check before the PAID transition); M1 Decimal-cents conversion at
  `payments/paypal_connector.py:264` (`capture_payment_intent`) and `:620` (`refund`) — no
  `int(float(...) * 100)` pattern remains. This clears the BLOCKED verdict.

**Static pages:**
- `docs/security/STATIC_PAGES_AUDIT.md` — verdict text says **BLOCKED** (F1 HIGH, F2
  MEDIUM→HIGH). Independently re-verified in this review, all findings are now fixed in
  code with tests, matching `BUG_TESTS/BUG_TESTS.csv`:
  - **F1** (reserved-slug bypass) — fixed. `permalinks/models.py` `Permalink.save()` now
    calls `_validate_slug_not_reserved` before `super().save()`; `permalinks/signals.py
    handle_slug_change` fetch+mutate+`.save()`s instead of a bare `.update()`; `pages/models.py
    StaticPage.clean()` and `StaticPage.save()` both call the same validator. Proof test:
    `pages/tests/test_reserved_slug_publish_path.py` (5 tests, PROVEN in BUG_TESTS.csv,
    re-ran green in this session).
  - **F2** (unbounded/uncapped public form fields → HTTP 500 on Postgres) — fixed. Both
    contact/quotation views and the product notify-me/quotation views now introspect
    `Model._meta.get_field(name).max_length` and cap `message` against
    `pages.antispam.PUBLIC_FORM_MESSAGE_MAX_LENGTH`. Proof tests:
    `pages/tests/test_public_form_length_limits.py` +
    `storefront/tests/test_notify_me_quotation.py::ProductFormLengthValidationTest` (PROVEN
    in BUG_TESTS.csv, re-ran green).
  - **F3** (production cache is LocMemCache, rate-limit atomicity claim false) — fixed.
    `webecom/settings/production.py` now requires `CACHE_URL` and raises
    `ImproperlyConfigured` at startup if absent; `CACHES["default"]["BACKEND"]` is
    `django.core.cache.backends.redis.RedisCache`. Verified by reading the file directly.
    Residual gap: the `redis` Python package is not declared in `requirements.txt` — see
    Known Risks.
  - **F4** (email fan-out has no per-store cap) — fixed. `_notify_full_access_employees`
    (`storefront/views_pages.py:64`) is explicitly commented "Security audit F4: gated by a
    per-store daily notification budget"; dedicated test file
    `pages/tests/test_contact_notification_budget.py` exists and passes.
  - **F5** (client-IP derivation fragile behind a reverse proxy) — **not a code fix** (the
    audit itself frames this as a deploy-level condition, not a code bug). Confirmed
    `pages/antispam.py:_client_ip` still uses `REMOTE_ADDR` only, with an explicit docstring
    note "the deployment must terminate proxies correctly (note for the release checklist)".
    Carried to Known Risks / deployment checklist, as intended.
  - **F6** (translation admin FK fields not store-scoped) — fixed for the real edit surface.
    `pages/admin.py StaticPageTranslationAdmin` now has `formfield_for_foreignkey` (scopes
    `page`/`ai_job` to `request.store`) and `save_model` (stamps `obj.store`). One read-only
    display artifact remains: `StaticPageTranslationInline.get_queryset` still calls
    `.cross_store_unsafe()` — acceptable because that inline has
    `has_add_permission=False`/`has_change_permission=False` and is nested under an
    already-store-scoped `StaticPageAdmin` parent; it cannot be used to reach or edit another
    store's row. Test: `pages/tests/test_admin.py`.
  - **F7** (honeypot field name `website` is a well-known bot-evasion target) — fixed. Both
    form partials and `pages/antispam.py:81` now use `hp_company`, not `website`; the source
    comment explicitly states `hp_company` "does not appear on public honeypot-name
    skip-lists".
  - **F8** (contact/quotation share the `"quotation"` rate-limit scope) — fixed.
    `storefront/views_pages.py` contact uses `scope="contact"`; the store-level quotation
    view uses `scope="quotation_general"` (explicitly commented as distinct from the
    product-form `scope="quotation"`); `pages/tests/test_quotation_scope_isolation.py`
    covers it.

  All F1–F8 fixes re-verified against actual source in this review (not taken on report
  text). **Recommendation:** re-title/update `docs/security/STATIC_PAGES_AUDIT.md`'s verdict
  line from BLOCKED to reflect the fixed state, or add a dated addendum — the doc currently
  contradicts the code it describes, which is confusing for anyone reading it cold.

### SEO Agent approved
PASS-WITH-FIXES (static pages) — carried verdict, independently spot-checked:
- `docs/seo/STATIC_PAGES_SEO_REVIEW.md` H1 (republish orphans translated permalinks) — fixed.
  `pages/tests/test_permalink_lifecycle.py` now has
  `test_republish_reactivates_published_translation_permalink` and
  `test_republish_does_not_reactivate_permalink_of_reverted_translation`.
- H2 (translated-slug change creates no 301) — fixed, engine-wide (products, collections,
  static pages share the fix). Verified test file
  `permalinks/tests/test_translated_slug_redirects.py` exists with
  `test_translated_slug_change_creates_redirect_and_301s` (present 3×, once per content
  type based on class grouping), `test_unpublished_translation_slug_change_creates_no_redirect`,
  `test_republish_after_unpublish_with_new_slug_creates_no_redirect`,
  `test_chain_collapse_first_old_slug_redirects_to_newest`,
  `test_renaming_back_to_previous_slug_raises_and_does_not_corrupt_table`. Ran green as part
  of the `permalinks` app run (`pages sitemaps permalinks` = 239 tests, OK).
- H3 (spec contradicts shipped policy-page indexability behavior) — **not resolved**: this
  is the SEO:P1 human decision item, still PENDING APPROVAL in
  `specs/ecommerce_engine/11_uncertainties_to_validate.md`. Code behavior is judged SEO-safe
  by the SEO Agent; the spec itself has not been amended. Carried as a human decision, not a
  blocker (SEO Agent's own verdict already treats it as doc-only / non-blocking pending sign-off).
- M1–M3, L1–L8: none are release blockers per the SEO Agent's own table; L1 (title separator
  `|` vs spec's `—`) independently confirmed still present verbatim in both
  `static_page.html:6` and `product.html:6` — cosmetic, engine-wide, carried to Known Risks.
- Checkout has no public/indexable pages in scope — N/A for the checkout area itself, but
  `/checkout/` and `/orders/.../thank-you/` are correctly `noindex` per prior reviews (not
  re-audited this cycle; no changes to that logic detected).

### Designer approved
N/A for this gate — no visual/theme changes were in scope for this release; both areas are
backend logic, forms, and SEO plumbing. No designer sign-off artifact found or expected.

### Migrations reviewed
PASS. `python3 manage.py makemigrations --check --dry-run` → **"No changes detected"**
(re-run by Release Manager; exit 0) — model state matches the committed migration files for
every app, including `pages` (new app in this release) and the checkout-touching apps
(`cart`, `orders`, `payments`, `campaigns`, `permalinks`, `sitemaps`, `stores`).

Note: the local dev database (`db.sqlite3`) has ~16 unapplied migrations relative to the
migration files (confirmed via `showmigrations`) — this is expected dev-environment drift
(the test runner builds its own migrated DB per run) and is not a release gate; standard
`manage.py migrate` at deploy time applies them. Flagging only so it isn't mistaken for a
migration-authoring problem.

### Settings documented
PASS with one real gap. `webecom/settings/production.py` documents its required environment
variables in its module docstring (`SECRET_KEY`, `DATABASE_URL`, `ALLOWED_HOSTS`) and fails
loudly (`ImproperlyConfigured`) for `PLATFORM_APEX_DOMAIN` (base.py) and `CACHE_URL`
(production.py) if unset — verified by triggering both errors directly. With a full env var
set (`SECRET_KEY`, `DATABASE_URL`, `ALLOWED_HOSTS`, `CACHE_URL`, `PLATFORM_APEX_DOMAIN`),
`manage.py check --deploy` reports exactly **one** issue:
```
WARNINGS:
?: (security.W009) Your SECRET_KEY has less than 50 characters...
```
— an artifact of using a dummy `SECRET_KEY=x` for this dry run, not a real production
concern (a real deploy generates a proper long secret). No other `check --deploy` warnings
were raised: `SECURE_SSL_REDIRECT`, `SESSION_COOKIE_SECURE`, `CSRF_COOKIE_SECURE`,
`SECURE_HSTS_*` are all already set correctly in `production.py`.

**Real gap found (not previously flagged in the carried list): `psycopg2-binary` is also
missing from `requirements.txt`.** `production.py` uses
`"ENGINE": "django.db.backends.postgresql"` for both the `default` and `analytics`
databases, which requires a psycopg driver to be installed; it is present in this dev venv
(`psycopg2-binary==2.9.12` per `pip freeze`) but, like `redis`, not declared as a project
dependency. Add both `redis` and `psycopg2-binary` (or `psycopg[binary]`) to
`requirements.txt` before production deploy.

### Deployment checklist ready
PARTIAL. No single consolidated `DEPLOYMENT_HARDENING.md` file exists yet — several audits
(`CHECKOUT_BATCH_2_AUDIT.md`, `STATIC_PAGES_AUDIT.md`) reference one by name as the intended
destination for their deploy-level notes but it has not been created. The items are not
lost — they are itemized in this checklist's Known Risks carry-forward and in
`ROLLBACK_PLAN.md` — but there is no single ops-facing checklist file today.
**Recommendation:** create `docs/DEPLOYMENT_HARDENING.md` consolidating: Redis cache config,
nginx real-IP handling, access-log scrubbing of Stripe redirect params, nginx rate limiting,
Celery beat entries, `redis`/`psycopg2-binary` dependency additions.

### Rollback plan ready
PASS — see `ROLLBACK_PLAN.md` in this directory.

### Known risks listed
PASS — see `KNOWN_RISKS.md` in this directory.

### Human decisions listed
PASS. Consolidated from `specs/ecommerce_engine/11_uncertainties_to_validate.md` (all
verified still open in this review) and the ADR-status gap found above:
1. **ADR ratification** — ADR-015, ADR-017, ADR-018, ADR-020, ADR-021 remain status
   `PROPOSED`; human should explicitly ACCEPT or object.
2. **SEO:P1** — amend `07_multilingual_seo.md` (CAN-002/§5/SM-030/TST-CAN-002) to match the
   shipped, SEO-Agent-endorsed policy-page behavior, or require a code change instead.
3. **18:P2** — ratify quotation-page `quantity` field in ADR-018 D4, or remove it.
4. **20:P1** — accept the non-funnel PayPal/Stripe capture-delay default (watchdog-at-expiry,
   2 days PayPal / 7 days Stripe), or request immediate-capture-when-no-funnel as a follow-up.
5. **20:P2** — accept `NO_SHIPPING` for PayPal v1 (weakens Seller Protection for physical
   goods), or request address-forwarding as a follow-up before high-volume physical-goods
   stores go live on PayPal.
6. **PayPal Vault** — off-session upsell charging for PayPal remains gated on merchant
   onboarding (`supports_off_session_charge` returns `False` for the PayPal connector,
   verified in `payments/paypal_connector.py:68`); Stripe-only for now, human-known limitation.
7. **L1 title separator** — spec says `—`, both `static_page.html` and `product.html` use
   `|`; cosmetic, needs a one-line decision either way.

---

## Per-area verdicts

### Checkout

- **READY FOR STAGING: YES.** All checkout-relevant tests green (1412 tests), H1/M1 PayPal
  findings fixed and verified in code, `sf_lang` addendum fixed and drift-tested, migrations
  clean.
- **READY FOR PRODUCTION: YES, WITH CONDITIONS.** Conditions and owners:
  1. Add `redis` and `psycopg2-binary` to `requirements.txt` — **Human/DevOps**, before deploy.
  2. Configure nginx real-IP handling so `REMOTE_ADDR` reflects the true client (affects
     rate limiting generally, including checkout-adjacent endpoints) — **DevOps**, before
     public launch.
  3. Configure access-log scrubbing (or accept as-is) for the Stripe
     `payment_intent_client_secret` return-redirect query param — **DevOps/Security**,
     before public launch.
  4. Add nginx `limit_req` (or django-ratelimit) in front of public checkout/token
     endpoints (LOW-3 in CHECKOUT_BATCH_2_AUDIT) — **DevOps**, before public launch.
  5. Schedule (not launch-blocking) a fix + regression test for MEDIUM-2 (void-vs-succeeded-
     webhook race) — **Developer + After-Bug Test Agent**, ticket owner TBD.
  6. Human sign-off on 20:P1, 20:P2, ADR-015/017/020/021 acceptance — **Human**.
  7. MEDIUM-1 (purchase-pixel guard) must be fixed **before T030 pixel providers ship**, not
     before this release — **Developer**, tracked, not a condition of *this* release.

### Static pages + contact/quotation forms

- **READY FOR STAGING: YES.** F1–F8 all verified fixed with PROVEN/passing regression tests;
  SEO H1/H2 fixed and tested; 242 relevant tests green; migrations clean.
- **READY FOR PRODUCTION: YES, WITH CONDITIONS.** Conditions and owners:
  1. Add `redis` (and `psycopg2-binary`) to `requirements.txt` — **Human/DevOps** — this is
     the same F3-closing dependency the checkout area also needs; do it once, for both areas.
  2. Configure nginx real-IP handling (F5) — **DevOps**, same condition as checkout, shared
     fix.
  3. Human decision on SEO:P1 (spec amendment vs. code change) — **Human**.
  4. Human decision/ratification on 18:P2 (quotation `quantity` field) — **Human**.
  5. Update `docs/security/STATIC_PAGES_AUDIT.md`'s stale BLOCKED verdict text to reflect
     the fixed state (hygiene, not a functional blocker) — **Safety Agent / doc owner**.
  6. Schedule (not launch-blocking): F4/F6 follow-ups already fixed in this cycle, no longer
     open; F5 deploy-level condition carried above.

No test failures, no unresolved HIGH/CRITICAL security findings, no spec/implementation
mismatches were found in this review. All BLOCKED items found in prior audit documents are
independently confirmed fixed in current code, with PROVEN regression tests where the
bug-fix workflow applies.
