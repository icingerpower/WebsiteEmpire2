# Security Audit — `pages` app (ADR-018 Static pages + store contact/quotation forms)

**Scope:** T029-SP implementation of ADR-018. Mandatory pre-release safety gate named
in the ADR itself (public forms, raw-HTML content field, email fan-out).
**Auditor:** Safety Agent (defensive review only — no production code changed).
**Date:** 2026-07-10.
**Test evidence:** `python3 manage.py test pages` → **76 tests, OK** (all green) at audit time.

Files reviewed: `pages/` (models, signals, antispam, admin, seeding, templatetags,
management command), `storefront/views_pages.py`, `storefront/views_product_forms.py`,
`storefront/templates/storefront/pages/static_page.html`,
`.../partials/contact_form.html`, `.../partials/static_quotation_form.html`,
`emails/service.py` + `emails/templates/emails/contact_message_received.{html,txt}`,
`permalinks/models.py` / `registry.py` / `signals.py`, `catalog/admin.py`
`QuotationRequestAdmin`, `core/managers.py`, `webecom/settings/{base,production}.py`.

---

## Verdict: **BLOCKED** (original review — superseded)

> **CURRENT STATUS: CLEAR-WITH-NOTES.** All blockers were fixed and re-verified on
> 2026-07-10 — see the "Re-verification (2026-07-10)" section at the end of this document.
> The text below is the original BLOCKED review, preserved for the audit trail; per-finding
> resolution is summarized in that final section.

Two findings must be resolved before release:

- **F1 (HIGH)** — the reserved-top-level-slug protection is completely bypassed on the
  actual production write path. ADR-018 D1 states the validator "protects **every**
  permalink producer … in one place (§XV-4)". It protects none of them at runtime.
  Reproduced live.
- **F2 (MEDIUM→HIGH on PostgreSQL)** — public form fields are written with
  `.objects.create()` and no length validation; on the production PostgreSQL backend an
  over-length `name`/`subject` raises an unhandled `DataError` (HTTP 500) on an
  unauthenticated endpoint; `message` is entirely uncapped.

Everything else is CLEAR or CLEAR-WITH-NOTES. Once F1 and F2 are fixed (small, localized
changes) and re-tested, this area is releasable. The remaining MEDIUM items (F3–F5) are
deployment/config conditions that belong on the release checklist and do not require code
changes in this app, but F3 contradicts an ADR mitigation and must be acknowledged
explicitly by the Release Manager.

---

## CRITICAL

None.

---

## HIGH

### F1 — Reserved-slug validator is never executed for static pages (or products); route-shadowing / invisible-failure control is inert

**Locations:**
- `permalinks/models.py:60` `_validate_slug_not_reserved` — attached only as a **field
  validator** on `Permalink.slug` (`permalinks/models.py:158`). Django field validators
  run on `full_clean()` / ModelForm validation, **not** on `Model.save()`.
- `pages/signals.py:99-110` `sync_static_page_permalink` — creates the Permalink with
  `Permalink(...).save()` (no `full_clean()`).
- `permalinks/signals.py:113-117` `handle_slug_change` — renames via queryset
  `.update(slug=new_path)` (no validation at all).
- `StaticPage.slug` (`pages/models.py:56`) carries **no** reserved-slug validator itself.
- Same gap on products: `permalinks/registry.py register_product/register_collection`
  also use `.save()`.

**Attack / failure scenario (reproduced live):**
A full-access store admin (or the auto-slug path with a title like "Cart", "Search",
"Orders") creates and publishes a `StaticPage` whose slug is a reserved fixed route or an
ISO-639 language code. The publish signal creates an **active** Permalink for that slug
with no validation. Confirmed by direct repro against a test DB:

```
StaticPage(slug="cart", is_published=True)  → Permalink row {'slug':'cart','lang':'en','is_active':True}  CREATED
StaticPage(slug="fr",   is_published=True)  → Permalink row {'slug':'fr','lang':'en','is_active':True}    CREATED
```

Both should have been rejected by `RESERVED_TOP_LEVEL_SLUGS` / the ISO-639 pattern.

**Impact:**
- `storefront/urls.py` registers fixed routes (`cart`, `checkout`, `search`, …) before the
  `<path:slug>` catch-all, so the `cart` page silently 404s forever — precisely the §XV-1
  "invisible failure" the reserved set exists to prevent.
- `slug="fr"` collides with the language-prefix namespace (ADR-008); `LocaleMiddleware`
  treats `/fr/` as a locale root, so the page is unreachable and can perturb locale
  routing.
- The ADR's central claim that this control protects "every permalink producer in one
  place" is false in the shipped code. This is a correctness/robustness invariant the ADR
  designated as required, so it blocks the gate even though the blast radius is confined to
  the offending store (no cross-tenant breach — permalinks are store-scoped, dispatch is
  store-scoped, `unique_together(store, slug)` holds).

**Not a tenant-isolation break:** a page in store A cannot shadow store B — verified. This
is an availability / invisible-failure / broken-invariant issue, not privilege escalation.

**Recommended fix (pick one; option A is closest to ADR intent):**
- **A.** Enforce the reserved check at the persistence boundary for the *content* models,
  not only on `Permalink`. Add `_validate_slug_not_reserved` (and the ISO pattern) as a
  validator on `StaticPage.slug` **and** run `full_clean()` (or an explicit check) in
  `StaticPage.save()` / `StaticPageAdminForm.clean_slug`, plus the same on
  `Product.slug`. This makes the admin reject the slug at entry with a visible error
  (§XV-1/§XV-5).
- **B.** Call `permalink.full_clean(exclude=[...])` before `.save()` in
  `pages/signals.py sync_static_page_permalink` and in
  `permalinks/registry.register_product/collection`, and validate `new_path` inside
  `handle_slug_change` before the `.update()`. Signal-time rejection is louder but later
  than admin-form rejection.
- Either way, add a test that **publishing** a StaticPage with slug `cart`/`fr` is rejected
  (the current `test_permalink_lifecycle.py` only calls `Permalink.full_clean()` directly —
  it proves the validator works in isolation but **not** that the production write path
  invokes it, which is exactly why the bug shipped green).

---

## MEDIUM

### F2 — Public form fields written without length validation → HTTP 500 on PostgreSQL, unbounded `message`

**Locations:** `storefront/views_pages.py`
`_handle_contact_post` (`ContactMessage.objects.create`, lines ~202-210) and
`_handle_quotation_post` (`QuotationRequest.objects.create`, lines ~265-272). Neither calls
`full_clean()` nor truncates/limits input length.

**Scenario:** An anonymous visitor POSTs `name` or `subject` longer than the column
`max_length` (200 / 255). SQLite silently stores it; **PostgreSQL (production) raises
`DataError`**, which is uncaught here → HTTP 500 on a public endpoint (trivially scriptable,
mild DoS + noisy error logging). `message` / quotation `message` are `TextField` with **no
cap at all**, so a bot can post multi-megabyte bodies that are stored and then fanned out
verbatim into notification emails (F4 amplifier).

**Recommended fix:** Cap each field in the view before `create()` (e.g. `name[:200]`,
`subject[:255]`, `message[:5000]` with a translated "message too long" error above the cap),
or call `instance.full_clean()` and render field errors. Add explicit max lengths to the
`message` handling. Add tests for over-length input on both endpoints.

### F3 — Production cache is LocMemCache, not Redis → the documented rate-limit atomicity guarantee is false

**Locations:** `webecom/settings/base.py:159` sets `LocMemCache`;
`webecom/settings/production.py` does `from .base import *` and **never overrides
`CACHES`**. `pages/antispam.py` and ADR-018 both assert "Redis in production ⇒
`cache.incr` is atomic" and treat LocMem as a dev-only limitation.

**Impact:** In production the anti-spam counter is per-process/per-worker and lost on worker
restart. Under N gunicorn workers the effective limit is ~N×5/hour and non-atomic
(`add`+`incr` race). The spam gate is materially weaker than the ADR claims. This is the
`rate_limit_exceeded` correctness assumption being contradicted by committed config.

**Recommended fix:** Define a Redis `CACHES` block in `production.py` (the base.py comment
already anticipates `CACHE_URL`). Until then, the Release Manager must record that
rate-limiting is best-effort-only and NOT a security boundary. Add a deployment check that
`CACHES.default.BACKEND` is a shared backend in production.

### F4 — Contact-form email fan-out has no per-store cap → distributed email-bomb amplifier + synchronous send in request path

**Locations:** `storefront/views_pages.py:59-83` `_notify_full_access_employees` — every
successful contact submission sends one `send_transactional_email` per active full-access
`StoreEmployee`, synchronously, in the request cycle, and writes one `SentEmail` audit row
each.

**Scenario:** Rate limiting is **per IP** (5/hr/store). An attacker with M source IPs
(botnet/proxy pool) sends 5×M submissions/hour, each amplified ×E employees ⇒ 5·M·E emails
and DB writes per hour, plus slow responses (blocking send loop). F3 makes the per-IP cap
weaker still. Honeypot-tripped and rate-limited requests correctly do **not** send (verified
ordering in `_handle_contact_post`), which limits the trivial case but not the distributed
one.

**Recommended fix:** Add a per-store global notification budget (e.g. cache-window cap on
`contact_message_received` sends/store/hour) independent of client IP, and/or move
notification sending off the request path (queue/digest). At minimum document the fan-out
bound. Not blocking for launch given small employee counts, but should be scheduled.

### F5 — Client IP derivation (`REMOTE_ADDR`) is safe against spoofing but fragile behind a reverse proxy

**Locations:** `pages/antispam.py:27-28` `_client_ip` uses `REMOTE_ADDR` only (good: not
attacker-spoofable at the app layer — no `X-Forwarded-For` parsing anywhere, confirmed by
grep). No `SECURE_PROXY_SSL_HEADER` / real-ip handling in settings.

**Impact:** The Contabo deployment runs behind nginx (per project memory). If nginx is not
configured with the real-IP module (or the app is not fronted so `REMOTE_ADDR` is the true
client), **every** visitor presents the proxy's single IP ⇒ the per-IP counter collapses to
one global bucket: one spammer trips the limit for all legitimate users (shared-IP lockout
DoS) and the gate is meaningless. This is the "deployment must terminate proxies correctly"
note the antispam docstring flags — it needs to be an enforced checklist item, not a
comment.

**Recommended fix (release checklist / DEPLOYMENT_HARDENING):** verify nginx sets the real
client IP into `REMOTE_ADDR` (real_ip module / trusted proxy list) and that
`X-Forwarded-For` from clients is stripped at the edge. Add a smoke test that two different
external clients get distinct `REMOTE_ADDR` values.

### F6 — Translation admin FK fields are not store-scoped

**Locations:** `pages/admin.py` `StaticPageTranslationAdmin` (no
`formfield_for_foreignkey`, no `save_model` store assignment; `fields` omits `store`) and
`StaticPageTranslationInline.get_queryset` uses `cross_store_unsafe()`.

**Findings (verified):** The `page` FK on the translation admin resolves to the
`StoreScopedManager` base queryset (`_RaisingQuerySet`) — so a manual add via that admin is
at best broken (raises `IsolationError` / cannot set the non-null `store`), at worst an
unscoped dropdown. Translations are normally written by the AI persist path, so this is not
a live storefront exposure, but the admin surface does not follow the store-scoping
convention the rest of the app enforces.

**Recommended fix:** Add `formfield_for_foreignkey` to scope `page` (and `ai_job`) to
`request.store`, set `obj.store = request.store` in `save_model` for new rows (mirror
`StaticPageAdmin.save_model`), and prefer a `for_store`-based inline queryset over
`cross_store_unsafe()` where the parent store is known. Add a test that the translation
admin cannot reference another store's page.

---

## LOW

### F7 — Honeypot field name `website` is a well-known bot-evasion target
`pages/antispam.py:61` / both form partials use `name="website"`. Sophisticated spam bots
explicitly skip fields named `website`/`url`/`email2`. Honeypots are best-effort by design;
consider a less-guessable, per-store-randomized or timestamped hidden field name as a later
hardening. Not blocking.

### F8 — Contact/quotation share the `"quotation"` rate-limit scope with the product endpoint
`storefront/views_pages.py` `_handle_quotation_post` uses `scope="quotation"`, the same
scope as `views_product_forms.quotation_request_view`. Submissions to the store-level
quotation page and to any product quotation form share one counter per (store, IP). Harmless
(slightly stricter), but note it so it isn't mistaken for a bug later; use distinct scopes if
independent budgets are ever wanted.

---

## INFO / CLEAR (verified safe — do not regress)

- **`|safe` exposure is exactly as scoped (D2).** Grep of all four `|safe` sites
  (`product.html:141,145`, `collection.html:27`, `static_page.html:33`) confirms `|safe` is
  applied **only** to admin/AI-authored `body`/`description`. Customer-submitted content
  (contact `name/subject/message`, quotation `customer_name/message`) is **never** rendered
  with `|safe`: admin list/detail auto-escape, and both email templates render
  `{{ contact_message.* }}` through normal auto-escaping (no `|safe`, verified in
  `contact_message_received.{html,txt}`). No stored-XSS path from buyer input. (Note: the
  AI-translated `body` is also `|safe`; it derives from admin-authored source, consistent
  with the accepted product-description precedent — tracked under the still-open platform
  sanitizer NFR-T3.)
- **No email header injection / sender spoofing.** The notification subject is
  `New contact message on {{ store.name }}` (store name only — no buyer input in headers);
  `From` is `DEFAULT_FROM_EMAIL`; the buyer email is placed only in the body, never as
  `From`/`Reply-To`. `send_mail` receives a fixed recipient list of employee addresses.
- **CSRF enforced.** Both partials emit `{% csrf_token %}`, `CsrfViewMiddleware` is active,
  and `test_isolation_and_csrf.py::test_post_without_csrf_token_is_rejected` passes.
- **No mass-assignment.** Views read named POST fields explicitly and pass them to
  `create()`; no `**request.POST` binding.
- **Store isolation on writes.** Contact and quotation `create()` calls set
  `store=request.store`; the `page` passed in is already store-scoped by the resolver;
  `StaticPage/ContactMessage/QuotationRequest` admin `get_queryset` all use `for_store`;
  `test_isolation_and_csrf.py` asserts store scoping. `ContactMessage`/`QuotationRequest`
  admins are `has_add_permission=False`, `has_delete_permission=False` (audit data
  preserved), and status is editable only at `pages` **full** access (read-only at
  `limited`) — matches project convention.
- **Draft pages are unreachable.** Drafts create no Permalink (seeding + publish signal),
  the resolver returns `None` (404) with no permalink, and `static_page_view` independently
  re-checks `is_published` → 404. Unpublish deactivates **all** language permalinks. No
  direct translation-slug guess reaches a draft.
- **Seeding is idempotent / safe.** `seed_pages_for_store` uses `get_or_create((store,
  slug))`; the post_save Store signal and the management command share it; re-runs create 0
  dupes (test output shows `0 page(s) created` on second run). Concurrent creation of the
  *same* store is not possible (single creation point); per-slug `get_or_create` tolerates
  partial prior state.
- **PII minimization.** No client IP is persisted (`ContactMessage` stores none); the email
  service masks recipients in logs (`recipient[:3] + "***"`).

---

## Conditions to clear the BLOCK

1. **Fix F1** — enforce `RESERVED_TOP_LEVEL_SLUGS` + ISO-639 rejection on the static-page
   (and product) write path, with a test that proves **publishing** a reserved-slug page is
   rejected (not just that `Permalink.full_clean()` raises in isolation).
2. **Fix F2** — cap/validate public form field lengths before `create()`; add over-length
   tests for both endpoints.
3. **Acknowledge F3 + F5 on the release checklist** — define a shared (Redis) production
   cache and verify real-client-IP handling behind nginx; until F3 is fixed, the Release
   Manager records that anti-spam is best-effort, not a security boundary.
4. **Schedule F4, F6** (email fan-out cap; translation-admin store scoping) — not launch
   blockers but should have owners.

Re-run `manage.py test pages` after the F1/F2 fixes; both should stay green with the new
regression tests added by the After-Bug Test Agent.

---

## Re-verification (2026-07-10) — Verdict: **CLEAR-WITH-NOTES**

All blockers and secondary findings from the original review were fixed and independently
re-verified by the Safety Agent against the current code. The original F1 reproduction was
re-run live against a fresh test database.

### Per-finding status

| ID | Sev | Status | Evidence |
|----|-----|--------|----------|
| **F1** | HIGH | **FIXED** | Reserved/ISO-639 check now runs on `Permalink.save()` (`permalinks/models.py:188-205`) — the funnel every producer's `.save()` passes through — **and** at the user-facing boundary in `StaticPage.clean()` (`pages/models.py:130-135`, raises a field error on `slug`). Original repro re-run: `slug="cart"` → `ValidationError` (0 Permalink rows, 0 StaticPage rows); `slug="fr"` → `ValidationError` (0 rows); renaming a published page to `slug="checkout"` → `ValidationError`, canonical permalink unchanged (`our-story`). PROVEN regression tests in `pages/tests/test_reserved_slug_publish_path.py` (5 tests). |
| **F2** | MED (HIGH on PG) | **FIXED** | Field lengths capped **before** `.objects.create()` in both handlers (`storefront/views_pages.py:222-238` contact, `:321-333` quotation), derived from `model._meta.get_field(...).max_length` so they can't drift from the schema; `message` bounded by `PUBLIC_FORM_MESSAGE_MAX_LENGTH = 5000` (`pages/antispam.py:32`) with a translated over-length error. Regression tests in `pages/tests/test_public_form_length_limits.py` (8 tests). |
| **F3** | MED | **FIXED** | `webecom/settings/production.py` now **requires** `CACHE_URL` and configures `RedisCache`, failing loudly at startup if unset (`production.py:65-78`) — the anti-spam rate limiter is atomic in production, restoring the ADR mitigation. |
| **F4** | MED | **FIXED** | Per-store, IP-independent daily notification budget `CONTACT_NOTIFICATION_DAILY_CAP = 50` (`pages/antispam.py:43`, `contact_notification_budget_exceeded()` at `:97`); gated in `_notify_full_access_employees` (`storefront/views_pages.py:82`) so a distributed submission flood can no longer amplify into an unbounded email/DB fan-out. |
| **F5** | MED | **OPEN — deploy checklist (accepted)** | By design an operational control, not app code. `_client_ip` remains `REMOTE_ADDR`-only (correct: not app-spoofable). nginx real-IP configuration is tracked in `docs/releases/checkout-and-static-pages/RELEASE_CHECKLIST.md` (line ~166 note + deploy step ~305 "Configure nginx real-IP handling so `REMOTE_ADDR` reflects the true client"). Release Manager must confirm at deploy time. |
| **F6** | MED | **FIXED** | `formfield_for_foreignkey` added to the translation admin (`pages/admin.py:184-203`) scoping FK choices to `request.store` (mirrors `campaigns/admin.py`). |
| **F7** | LOW | **FIXED** | Honeypot field renamed `website` → `hp_company` (`pages/antispam.py:81` default, both form partials updated) — off common bot skip-lists. |
| **F8** | LOW | **FIXED** | Store-level general quotation now uses `scope="quotation_general"` (`storefront/views_pages.py:290`), separated from the product endpoint's `scope="quotation"` — independent per-form budgets. |

### Verification performed
- Re-ran the original F1 repro script (create + publish `cart`/`fr`, rename-to-`checkout`)
  against a fresh test DB: all rejected with `ValidationError`, zero persisted rows,
  canonical permalink intact.
- Read the fix sites for F2/F3/F4/F6/F7/F8 and confirmed each matches the reported change.
- Ran `manage.py test pages.tests.test_reserved_slug_publish_path
  pages.tests.test_public_form_length_limits` → **13 tests, OK**.
- Coordinator reports the full suite at **2280 OK**.

### Remaining notes (non-blocking)
- **F5** stays OPEN as an accepted deploy-checklist item (nginx real-IP). Until verified in
  production, the rate limiter's per-IP keying is only as trustworthy as the edge config.
- **NFR-T3 platform HTML sanitizer** remains the standing open item for admin/AI-authored
  `|safe` bodies (product/collection/static-page) — unchanged by this ticket, correctly
  out of scope, still owned by the Safety Agent as a named follow-up.
- **F7** honeypot is best-effort by design; fine as-is.

**New verdict: CLEAR-WITH-NOTES.** No open code-level security defect in the `pages` app.
The single remaining item (F5) is an operational deployment control tracked on the release
checklist and must be confirmed by the Release Manager before go-live.
