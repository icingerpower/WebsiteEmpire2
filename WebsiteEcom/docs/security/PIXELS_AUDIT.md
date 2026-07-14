# Security Audit — Pixel Integrations (TICKET-030 / ADR-022)

**Date:** 2026-07-11
**Auditor:** Safety Agent
**Scope:** `pixels/` app (models, registry, providers, events, service, slot_provider,
admin, 10 templates), storefront wiring (`storefront/views.py` product pixel context,
`storefront/views_checkout.py` initiate_checkout + thank-you claim), the AJAX
add-to-cart bridge in `storefront/templates/storefront/pages/product.html`, and
`orders/migrations/0013_retire_purchase_guard_sentinel.py`.
**Evidence run:** `DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py test
pixels storefront.tests.test_pixel_wiring orders.tests.test_pixel_sentinel_migration`
→ **75 tests, OK**.

**Verdict: CLEAR-WITH-NOTES** (see end).

---

## 1. Script injection via admin-entered pixel IDs — SOUND (with two LOW hardening notes)

Both layers promised by ADR-022 D3 are present and test-pinned:

- **Layer 1 — regex in `clean()`** (`pixels/models.py:78-105`): per-provider strict
  patterns (`pixels/providers.py`: digits-only for facebook/pinterest, `^G-[A-Z0-9]{4,16}$`
  for GA4, `^[A-Z0-9]{10,30}$` for TikTok, strict UUID for Snapchat). Unknown provider
  key also rejected in `clean()`.
  **F1-lesson check (does `clean()` actually run?):** the only production write path is
  the store admin (`pixels/admin.py`, ModelForm → `full_clean()` → `clean()`). A
  repo-wide grep found no API, import, fixture-loading, or AI-job path that writes
  `Pixel` rows. `Pixel.save()` does **not** call `full_clean()`, so shell/fixture/future
  API writes would bypass the regex — acceptable *today* because of layer 2, noted as
  LOW-2 below.
- **Layer 2 — `|escapejs` at render time:** grep over all 10 templates shows **every**
  interpolation escaped (`{{ pixel_id|escapejs }}`, `{{ payload.*|escapejs }}`,
  `{{ event_id|escapejs }}`, `{{ transaction_id|escapejs }}`, `{{ native_event|escapejs }}`)
  with exactly one exception: `{{ content_ids_json }}` (LOW-1 below).
  Test-pinned: `test_base_escapes_hostile_pixel_id` and
  `test_event_template_escapes_hostile_content_name` inject
  `</script><script>alert(1)</script>` and assert neutralization;
  `test_clean_rejects_script_injection_attempt` pins layer 1.
- `native_event` values come from code-constant `event_map` dicts, never from data.
- `config_json` is admin-editable but read by no v1 code (verified by grep) — no
  injection surface yet; it becomes one the day CAPI reads it (future audit item).

### LOW-1 — `content_ids_json` is `mark_safe(json.dumps(...))` without HTML-safe escaping
**Location:** `pixels/registry.py:80` (`"content_ids_json": mark_safe(json.dumps(content_ids))`),
consumed by 4 event templates.
**Scenario:** `json.dumps` does not escape `<`/`>`; a content id containing `</script>`
would terminate the inline script block. Today all four call sites build content_ids
exclusively from `catalog_item_id()` (`pixels/events.py:26-38` — PK-derived digits and
`_`), so this is **not currently exploitable**. It is a latent breakout if a future
payload passes user-influenced ids (e.g. merchant SKU strings, a temptation when
aligning with T033 catalog feeds).
**Fix:** apply the same `</>/&` replacement used by
`storefront/views.py::_serialize_jsonld` (or Django's `_json_script_escapes`) before
`mark_safe`. One line, removes the trap permanently.

### LOW-2 — `Pixel.save()` does not enforce `full_clean()`
**Location:** `pixels/models.py`.
**Scenario:** any non-ModelForm write (shell, fixture, future settings API/import)
skips the regex boundary; only `escapejs` then stands between an arbitrary string and
the inline script. `escapejs` is sufficient for breakout prevention, but the design
intent is two independent layers on every path.
**Fix:** override `save()` to call `self.full_clean()` (rows are admin-volume, cost is
nil), or document the admin-only-write invariant in the model docstring as a hard rule.

### INFO-1 — `escapejs` used in HTML-attribute/URL contexts
`ga_base.html` (`src="...gtag/js?id={{ pixel_id|escapejs }}"`) and the facebook
`<noscript>` img use `escapejs` inside HTML attributes. `escapejs` happens to escape
`< - > - & - " - ' - =` to `\uXXXX`, so attribute breakout is impossible, but it is the
wrong escaper for the context (the regex is the real guard here). No action required;
be aware when editing these templates.

## 2. Cross-tenant leakage — SOUND

- `Pixel` extends `StoreOwnedModel`; the default manager raises on unscoped access
  (ADR-001 §4). `PixelsSlotProvider.render` does one
  `Pixel.objects.for_store(store).filter(is_active=True)` query, with `store` taken
  from `request.store` (host-resolved by `core/middleware.py`); `store is None` →
  render `""`.
- Admin (`pixels/admin.py`): `get_queryset` scoped to `request.store` (empty queryset
  when absent); `save_model` force-assigns `obj.store = request.store` on creation and
  `store` is not in `fields` — no spoofing of another store's row. Registered on
  `store_admin_site` only (not super-admin, not default site).
- No caching of slot output anywhere (`render_slot` renders per request; no fragment
  cache, no per-store cache keys to get wrong).
- Dedicated test: `test_store_a_pixel_absent_on_store_b_render` (passes).
- `claim_purchase_pixels` scopes both `Pixel` and `FiredPixel` with `for_store(store)`;
  `order_thank_you` loads the order via `Order.objects.for_store(store)` + signed token
  (store mismatch → 404), so a store-A token cannot claim/fire on store B.

## 3. AJAX add-to-cart bridge (`product.html:169-219`) — ONE MEDIUM

Verified good:
- **CSRF preserved:** `new FormData(form)` includes the `{% csrf_token %}` hidden input;
  `credentials: 'same-origin'` sends the session cookie. `/cart/add/` remains under
  `CsrfViewMiddleware`.
- **Progressive enhancement:** without JS (or without `fetch`/`FormData`) the plain
  POST works unchanged; the bridge script only renders when the normal product form
  renders (`pixel_product_payload_json` empty for quotation/sold-out/ask-available).
- **Payload block is safe:** `#pixel-product-payload` is serialized with
  `_serialize_jsonld` (escapes `<`, `>`, `&` as `\uXXXX`) — no script breakout via
  product titles.
- **No new open redirect from the bridge itself:** `window.location = response.url`
  assigns a fetch-final URL (always http/https, never `javascript:`); `/cart/add/`
  redirects through `_safe_redirect` and the product form submits no `next` field, so
  the final URL is `/cart/` (or a server-chosen relative path). A cross-origin redirect
  would fail the (default-CORS) fetch → `.catch` → plain submit.

### MEDIUM-1 — `response.ok` never checked: false AddToCart pixel fires + broken failure UX
**Location:** `storefront/templates/storefront/pages/product.html:198-214`.
**Scenario:** `fetch()` only rejects on *network* errors. On an HTTP error —
`400 Out of stock` (a real race: stock hit zero after page load), `400` from the
service `ValueError`, `403` CSRF failure (expired session), `500` — the `.then` branch
still runs:
1. the `pradize:pixels` AddToCart event is dispatched → **every installed provider
   reports a conversion signal for an add that failed** (false ad-platform
   optimization data, and Meta/TikTok bid against it);
2. `window.location = response.url` navigates the shopper to a **GET of
   `/cart/add/`**, which is `@require_POST` → 405 error page. Compared to the non-JS
   baseline (the 400 body is at least displayed), this is a lost-sale regression on
   exactly the paths the comment claims are protected ("add-to-cart itself must still
   work — NFR-1"). The `.catch` fallback (`form.submit()`) genuinely covers only
   network-level failures.
**Fix:** in `.then`, branch on `response.ok`: dispatch the bridge event and navigate
only when ok; on `!ok`, either `form.submit()` (server renders the error exactly as
non-JS) or `window.location = form.action`-free error handling. Add a wiring test
asserting no `pradize:pixels` dispatch markup path on a 400 (or at minimum a JS-free
Django test pinning `/cart/add/` 400 behavior stays renderable).
**Note:** this is an analytics-integrity + UX defect, not an exploitable vulnerability
— it does not block on security grounds, but it should be fixed before release because
it silently corrupts the very conversion data the feature exists to produce.

### LOW-3 — duplicate-submit surface
**Location:** same block.
(a) The `.catch` path calls `form.submit()` after a fetch that may have *reached* the
server (connection dropped after processing) → duplicate cart add. (b) The submit
button is not disabled while the fetch is in flight → a double-click adds twice.
Both are shopper-visible and self-correctable in the cart; no security impact.
**Fix (optional):** disable the submit button on first submit; accept the retry-add
risk as-is (cart adds are not payments).

### LOW-4 — adjacent: `_safe_redirect` accepts backslash-prefixed URLs
**Location:** `cart/views.py:307-316` (pre-existing, surfaced while auditing the
bridge's redirect chain).
**Scenario:** `next=/\evil.com` passes the `startswith("/") and not startswith("//")`
check, and browsers normalize `/\` to `//` — a protocol-relative external redirect.
Reachability is poor (POST-only + CSRF token required, and no storefront form emits a
`next` field today), so this is hardening, not an active hole.
**Fix:** use `django.utils.http.url_has_allowed_host_and_scheme`, or additionally
reject `"\\"` in `next_url`.

## 4. Purchase claim integrity — SOUND (one ADR wording error, LOW)

- **PAID gate, two independent layers, verified:** `order_thank_you`
  (`views_checkout.py:1678`) only calls `claim_purchase_pixels` when
  `payment_status == PAID`; the service (`pixels/service.py:38`) re-checks and returns
  `set()` otherwise. Test-pinned: `test_pending_order_returns_empty_set_defense_in_depth`,
  `test_failed_order_returns_empty_set`. The MEDIUM-1 scenario from
  `CHECKOUT_BATCH_2_AUDIT.md` (PENDING `processing` render consuming the claim) is
  closed.
- **Insert-before-fire:** `get_or_create` against `unique_together (order, pixel_type,
  event)` commits the row before any snippet exists; the slot provider renders a
  purchase snippet only when the provider key is in `purchase_pixel_providers`
  (created=True this render) — reload/back-button render nothing
  (`slot_provider.py:95-97`, pinned by `test_reload_emits_no_purchase_snippet`,
  `test_reload_creates_no_new_rows_and_returns_empty_set`). Concurrency pinned by
  `test_concurrent_claim_race_yields_one_created_true`.
- **No credential leakage:** `FiredPixel` stores only (store, order, pixel_type,
  event, fired_at); service logging writes no pixel IDs or secrets (there are no
  secrets in v1 — client-side IDs are public by nature).
- **Sentinel migration (`orders/migrations/0013`):** forward converts each
  `__purchase_guard__` row into per-provider rows for providers active *at migration
  time*, then deletes the sentinel — so a pre-T030 PAID order revisited after upgrade
  finds its per-provider claims already consumed and fires nothing
  (`test_post_migration_thank_you_reload_fires_nothing_further`). Reverse recreates one
  sentinel per order having any per-provider purchase row and is idempotent
  (`test_reverse_is_idempotent`). Reverse does **not** restore sentinels for stores
  that had zero active pixels at forward time — consequence-free, because pre-T030 code
  rendered no pixels at all (the `fire_purchase_pixel` context was already dead), so
  rollback semantics hold. Genuinely reversible for all practical purposes.

### LOW-5 — late-fire on provider install + revisit; ADR D6 claim is wrong as written
**Location:** `pixels/service.py:42-50`; ADR-022 D6 "Late provider install" bullet;
migration edge `test_sentinel_with_no_active_providers_is_just_deleted`.
**Scenario:** ADR-022 states a provider installed after an order's first PAID render
"never retro-fires for old orders". Incorrect: thank-you URLs are permanently
bookmarkable, and a revisit *after* installing a new provider yields
`created=True` for that provider → a Purchase event fires days or months after the
order (wrong-day attribution). The same applies to pre-T030 orders on stores that had
no active pixels at migration time (sentinel deleted, no per-provider rows created,
provider installed later, shopper revisits). Analytics integrity only — no security
impact, platform `event_id` does not help (it dedups double-fires, not late-fires).
**Fix:** correct the ADR wording; optionally skip claims for orders older than a
recency window (e.g. `paid_at < now - 7d` → return without creating rows).

## 5. initiate_checkout ordering fix — VERIFIED

`storefront/views_checkout.py:631-666`: both `record_initiate_checkout` and the
`('initiate_checkout', payload)` append are strictly inside `if updated == 1:` after
the conditional UPDATE (`WHERE initiate_event_fired=False`). The regression test is
real, not decorative: `test_initiate_checkout_event_not_recorded_when_update_claim_lost`
(`storefront/tests/test_checkout.py:250-277`) forces the UPDATE to return 0 while the
stale in-memory flag reads False, and asserts `record_initiate_checkout` is never
called. Wiring tests additionally pin first-GET-renders / reload-renders-nothing and
Pinterest-renders-nothing. No `FiredPixel` row is involved pre-order (non-null order
FK), matching the ADR.

## 6. Payload data / PII / consent (D7 restated) — ACCURATE, one risk-note correction

- **No shopper PII in any payload** (`pixels/events.py` verified): payloads contain
  value (2-dp Decimal string), ISO currency, PK-derived `content_ids`, product title
  (`content_name`), quantities, `order-<pk>-purchase` event_id, `str(order.pk)`
  transaction_id. No email, no names, no addresses. Purchase uses `order.total` +
  `order.currency` (correct per AC-161); other events use store default currency.
- **GDPR consent descope — restated honestly:** v1 fires marketing pixels with no
  consent gate on EU storefronts. ADR-022 D7 flags this correctly and loudly as a
  live GDPR/ePrivacy exposure tracked as a cross-backlog blocker; the architecture
  (all output through `slot.pixels`, gated later by `slot.consent`) makes retrofit a
  slot-provider-only change. T030 must not be treated as done-for-EU-launch until
  that decision lands. Nothing further to add — the accepted exposure is stated
  accurately in the ADR.

### MEDIUM-2 — thank-you pixels transmit the *permanent* order-access URL to ad platforms; ADR risk note understates this
**Location:** every `*_base.html` page_view auto-fire on
`storefront/pages/thank_you.html`; `storefront/tokens.py:39-44` ("Never enforce
max_age — thank-you URLs should be permanently bookmarkable").
**Scenario:** GA4 (`page_location`), Meta (`dl`), TikTok/Snapchat/Pinterest all collect
the full page URL on page_view. The thank-you URL *is* the signed, never-expiring
capability token that renders order details (items, payment summary, discount lines).
Those URLs therefore land durably in third-party ad-platform logs and are visible to
anyone with access to the store's ad accounts (agencies, exports, breach of an ad
account). ADR-022 Risks describes the leaked-thank-you-token scenario as
"analytics-integrity impact only" — that is inaccurate once pixels ship: it is also a
confidentiality exposure, amplified by the token having no expiry. This is
industry-common on ecommerce thank-you pages, but the *non-expiring* token makes it
worse than the norm.
**Fix (choose one, plus correct the ADR risk note):** (a) enforce a max_age on the
order-*detail* rendering (after N days the same URL renders a minimal "order
confirmed" shell without items/addresses — keeps bookmarkability); (b) move the token
to a query-param stripped via `history.replaceState` *before* the pixels slot renders
(ordering-fragile — not preferred); (c) accept explicitly, but with the ADR note
corrected to name the confidentiality dimension.

### INFO-2 — third-party script execution is the feature
All five loaders pull self-updating scripts from provider CDNs into every storefront
page; SRI is impossible by design (providers rotate content). This is the accepted
nature of pixels, correctly bounded: admin-only configuration, strict ID validation,
per-store rows, and preview suppression (`PREVIEW_SUPPRESSED_SLOTS` contains
`"pixels"`, regression-pinned by `test_render_slot_suppresses_pixels_in_preview_mode`).

## 7. Denial / breakage isolation — SOUND

Two genuine layers, both verified in code and tests:
- `render_slot` (`storefront/templatetags/storefront_tags.py:126-137`) wraps each
  provider's `is_enabled`/`render` in try/except — a raising slot provider can never
  break the page.
- `PixelsSlotProvider.render` (`slot_provider.py:102-110`) additionally wraps **each
  Pixel row** — one provider's template blowing up skips that row and still renders the
  others (partial output for the failed row is limited to its already-appended base
  snippet, which is valid standalone). Orphan rows (provider key with no registry
  entry) render nothing, `logger.error` (not debug), and surface in
  `validate_settings` — pinned by `test_render_skips_orphan_row_without_raising`,
  `test_render_logs_error_for_orphan_row`,
  `test_valid_and_orphan_rows_together_render_only_the_valid_one`,
  `test_validate_settings_reports_orphan_row`. A provider snippet *throwing in the
  browser* is likewise isolated: each snippet is its own `<script>` block, and the
  bridge listeners are per-provider (`document.addEventListener` each) — one broken
  third-party script does not stop the others' listeners.

---

## Findings summary

| # | Severity | Finding | Location |
|---|---|---|---|
| MEDIUM-1 | MEDIUM | AJAX bridge ignores `response.ok`: false AddToCart pixel on failed adds + 405 dead-end navigation on HTTP errors (fallback only covers network failures) | `storefront/templates/storefront/pages/product.html:198-214` |
| MEDIUM-2 | MEDIUM | Thank-you pixels ship the never-expiring order-access URL to ad platforms; ADR-022 risk note says "analytics-integrity only" — understated | `pixels/templates/pixels/*_base.html` on thank-you + `storefront/tokens.py:39-44`; ADR-022 Risks |
| LOW-1 | LOW | `content_ids_json` = `mark_safe(json.dumps(...))` without `<` escaping — latent `</script>` breakout if content_ids ever become user-influenced (safe today: PK-derived only) | `pixels/registry.py:80` |
| LOW-2 | LOW | `Pixel.save()` doesn't call `full_clean()` — regex layer inert outside the admin ModelForm path (escapejs still holds) | `pixels/models.py` |
| LOW-3 | LOW | Duplicate-submit surface in the bridge (catch-path re-POST; no in-flight button disable) | `product.html:210-214` |
| LOW-4 | LOW | Pre-existing: `_safe_redirect` accepts `/\evil.com` (backslash normalization) — poor reachability, harden anyway | `cart/views.py:307-316` |
| LOW-5 | LOW | Late-fire Purchase on provider-install + thank-you revisit; ADR D6 "never retro-fires" claim is factually wrong | `pixels/service.py:42-50`; ADR-022 D6 |
| INFO-1 | INFO | `escapejs` in HTML-attribute contexts (works, wrong tool) | `ga_base.html`, `facebook_base.html` noscript |
| INFO-2 | INFO | Third-party self-updating scripts, no SRI possible — inherent, correctly bounded | all `*_base.html` |

**No CRITICAL or HIGH findings.** Injection defenses are genuinely two-layered and
test-pinned; tenant isolation is correct at every read and write; the purchase claim
is race-safe, PAID-gated twice, and the sentinel migration is sound and reversible;
the initiate_checkout ordering fix is real and has a genuine failure-pinning
regression test.

## Human checklist before release

- [ ] Fix MEDIUM-1 (`response.ok` branch in the bridge) — small template-JS change +
      one wiring test; corrupted conversion data is the feature's own failure mode.
- [ ] Decide MEDIUM-2 disposition (token expiry on order detail vs explicit
      acceptance) and correct the ADR-022 risk-note wording either way.
- [ ] Correct ADR-022 D6 "never retro-fires" wording (LOW-5).
- [ ] Confirm the GDPR/consent cross-backlog blocker is tracked as gating **EU**
      launch (ADR D7 already says so — verify it is on the launch checklist).
- [ ] Optional hardening in the same pass: LOW-1 (one-line escape), LOW-2
      (`full_clean` in `save()`), LOW-4 (`url_has_allowed_host_and_scheme`).

## Verdict

**CLEAR-WITH-NOTES** — no security blocker. Release should be conditioned on
MEDIUM-1 (a functional/analytics-integrity defect in the new bridge, trivially fixed)
and an explicit human decision on MEDIUM-2 + the ADR risk-note correction. All LOW
items are hardening and may ship as fast-follows.
