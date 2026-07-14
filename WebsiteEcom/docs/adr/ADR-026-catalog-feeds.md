# ADR-026: Catalog feeds (TICKET-033)

**Status:** ACCEPTED (human, 2026-07-11) — the field-mapping table (D4) is
signed off as designed, and all nine PENDING product items (P-1…P-9, see the
table at the end) are approved as their recommended defaults. T033-A/T033-B
are cleared for implementation.

Implements `TICKET-033` (Facebook Dynamic Product Feed + Google Shopping
per-country feeds). Spec anchors: `07_multilingual_seo.md` §10
(FEED-001…FEED-008), `06_settings_requirements.md` §10, `04_admin_flows.md`
AF-016, acceptance criteria AC-210…AC-213, TST-FEED-*.

**Extends (frozen — not re-opened here):**
- **ADR-022 D4** — `pixels/events.py::catalog_item_id()` is the frozen item-ID
  convention explicitly shared with T033 feeds. This ADR *consumes* it; it does
  not restate it. Non-negotiable: see D5 and "Tests required" (drift test).
- **ADR-023 / FEED-C1 (DECIDED)** — `currency/resolver.py::display_amount()` is
  the only place display-currency conversion happens; the feed converter calls
  it (the resolver's own docstring already names "the T033 per-country catalog
  feed converter" as a caller). FEED-008: each feed is priced in its target
  country's display currency.
- **ADR-008** — the feed matrix source: one feed per (provider, country,
  language) where the (language → countries) pairs come from
  `stores.StoreLanguage` → `ShippingCountry` (FEED-001/ML-030), and absolute
  URLs come from `permalinks.resolver.base_url(language)`.
- **ADR-005 / URL-003** — product `link` values go through the permalink
  resolver map (canonical primary URLs only, never A/B variant URLs).
- **ADR-001 §4** — all new models are `StoreOwnedModel` / store-scoped.

**Binding constraints (design-pattern-ideas):** §III/§XV-4 (single resolver —
URLs and currency both), §V (audit every mutation path that must invalidate a
materialized artifact; nightly rebuild as safety net), §IX (registry pattern —
provider #4 is one class, never a call-site edit), §XV-1 (exclusions and
failures visible in admin, never silent), §XV-3 (feed status is an explicit
state enum, not decoration), §XV-6 (idempotent regeneration jobs).

---

## Decision

Build a new `feeds` Django app providing token-protected, pre-generated,
per-(provider × country × language) catalog feed files for **Google Shopping**
and **Meta/Facebook Dynamic Product Ads** (Pinterest deferred, one registry
class away), rendered as **RSS 2.0 XML with the `g:` Google namespace for both
providers**, with one item **per active variant** (`id =
pixels.events.catalog_item_id(product, variant)`, `item_group_id =
str(product.pk)`), prices via `currency.resolver.display_amount()` in the feed
country's display currency (FEED-008), links via the permalink resolver,
event-driven debounced regeneration (30-min beat + nightly full rebuild), an
explicit per-feed status state machine with recorded per-product exclusion
reasons, and a store-admin (`/admin/`) surface with per-provider enable/scope,
copyable feed URLs, status banners, regenerate-now and token rotation.

Sub-decisions D1–D8 below. Two Developer tickets: **T033-A** (engine) and
**T033-B** (admin surface + launch checks).

---

## Context

- The reference UI (admin-013) shows an FB DPA feed screen with a
  products-to-sync scope, a read-only feed URL + copy button, and a green
  "Your feed is up to date" banner. Owner annotation requests Google Shopping
  and Pinterest as well. `07_multilingual_seo.md` §10 turned this into
  FEED-001…FEED-008 (FEED-008/FEED-C1 already DECIDED).
- What exists today and is reused, not reinvented:
  - `pixels/events.py::catalog_item_id(product, variant)` → `"{p.pk}_{v.pk}"`
    / `"{p.pk}"` — frozen (ADR-022 D4). The storefront **always passes a
    variant** to pixel events (`storefront/views.py` uses `default_variant`
    for view_content/add_to_cart; purchase uses each order item's variant), so
    the IDs the platforms have already learned are **variant-level**. Dynamic
    retargeting silently breaks if feed IDs disagree (ADR-022 Risks).
  - `currency/resolver.py::display_amount(base, store_ccy, display_ccy)` +
    `apply_rounding()` — the single conversion/rounding path (ADR-023).
  - `permalinks/resolver.py::base_url(language)` + active `Permalink` rows —
    the single URL composition path (ADR-005/ADR-008). Map membership already
    equals "published translation ∧ enabled language", so unpublished
    translations are un-linkable *by construction*.
  - `stores.StoreLanguage` + `stores.ShippingCountry` — the feed matrix source
    (ADR-008 §1: "feed matrices hang off this row").
  - `catalog` models: `Product` (status draft/active/archived, `vendor`,
    `requires_shipping`), `ProductVariant` (per-variant `price`,
    `compare_at_price`, `sku`, `inventory_mode` — 7 modes incl. `sold_out`,
    `quotation`, `presale` —, `quantity`, `presale_ships_at`, `is_active`),
    `ProductImage` (product-level, `is_primary`), `ProductTranslation`
    (published-only rule), `Collection`/`CollectionProduct` (scope selector).
  - `sitemaps` app — the closest serving precedent is **dynamic per-request
    generation with `cache_page(1h)`**; its pre-generation is an acknowledged
    TODO (`sitemaps/tasks.py` is a placeholder). Assessed in D3: feeds do NOT
    copy the dynamic approach — FEED-005 mandates pre-generation semantics
    (explicit status, debounced job), and AC-211 requires persisted exclusion
    reasons, which request-time generation cannot surface.
  - Celery + beat are already wired (`webecom/celery.py`,
    `CELERY_BEAT_SCHEDULE` via settings namespace; precedents:
    `campaigns.tasks.capture_window_watchdog`, `sitemaps.tasks`).
- Gaps found while designing (fixed by this ADR):
  - `feeds` is listed as reserved in URL-001 but is **missing from
    `permalinks/models.py::RESERVED_TOP_LEVEL_SLUGS`** (the code set stops at
    `currency`). Must be added in T033-A (manual addition with a comment, like
    `chat` — feed routes live in `webecom/urls.py`, so the
    `pages/tests/test_reserved_slugs.py` drift test does not auto-cover them).
  - There is **no country → display-currency mapping anywhere** in the
    currency app (`StoreCurrencySetting` is per-store enablement, not
    per-country). FEED-008 requires "the target country's display currency",
    so D4a introduces the single mapping + resolution function.

---

## Options considered

**O1 — Generation strategy**
- (a) Dynamic per-request with `cache_page`, like sitemaps v1.
- (b) Pre-generated files written by a debounced Celery job, served by a thin
  token-checking view. **Chosen.**
- (c) Fully static files served by NGINX with signed paths (no Django).

**O2 — Feed format**
- (a) One renderer: RSS 2.0 XML with `g:` namespace for both Google and Meta
  (Meta officially ingests Google-namespace RSS/XML). **Chosen.**
- (b) Per-provider native formats (Google XML + Meta CSV/TSV).

**O3 — Item granularity**
- (a) One item per product (parent only), `id = str(product.pk)`.
- (b) One item per **active variant**, `id = catalog_item_id(product,
  variant)`, `item_group_id = str(product.pk)`. **Chosen.**

**O4 — Feed URL auth**
- (a) Plain public URL, obscurity only (reference UI behaviour).
- (b) Per-store secret token as a query param, 403 without it,
  rotatable from admin. **Chosen** (mandated by AC-213 and the ticket text).
- (c) HTTP Basic auth (Merchant Center supports it).

**O5 — Country → currency source**
- (a) Per-`ShippingCountry` admin-editable currency field.
- (b) Platform-global static ISO-3166 → ISO-4217 map in code, plus a single
  resolution function with **loud ERROR** when the mapped currency has no
  rate. **Chosen.**
- (c) Always store default currency (violates FEED-008 — rejected outright).

**O6 — Where feed config lives**
- (a) Fields on `Store` / future general-settings model.
- (b) New `feeds` app with per-provider `FeedConfig` rows + per-target
  `FeedTarget` state rows. **Chosen.**

---

## Chosen option (full design)

### D1 — App layout and provider registry (§IX)

New app `feeds/`:

```
feeds/
  models.py        # FeedConfig, StoreFeedToken, FeedTarget
  registry.py      # FeedProvider base + register()/get()/all_providers()
  providers.py     # GoogleShoppingProvider, FacebookDPAProvider (self-register)
  currencies.py    # COUNTRY_TO_CURRENCY map + feed_display_currency()
  items.py         # build_feed_items(target) -> iterator of FeedItem dicts
  renderer.py      # RSS 2.0 XML writer (streaming, escaped)
  tasks.py         # regenerate_dirty_feeds (beat), rebuild_all_feeds (nightly)
  signals.py       # dirty-marking receivers
  views.py         # feed_view (token check + FileResponse)
  admin.py         # T033-B
```

`FeedProvider` mirrors `pixels/registry.py` exactly (module-level `_registry`,
self-registration at import from `FeedsConfig.ready()`), and satisfies the
mandatory connector interface:

| Interface member | Feed meaning |
|---|---|
| `key` | Frozen registry key: `google`, `facebook` (later `pinterest`). Equals `FeedConfig.provider` choices and the URL segment. |
| `name` | Admin card title ("Google Shopping", "Facebook Dynamic Product Ads"). |
| `required_settings` | `[]` in v1 — feeds need no credentials; the feed URL is *given to* the platform. Declared anyway so the settings-validation architecture sees the connector. |
| `validate_settings()` | Validates `FeedConfig` (scope mode + collections non-empty when `scope=collections`). |
| `is_enabled(store)` | `FeedConfig.objects.for_store(store).filter(provider=key, is_enabled=True).exists()`. |
| `run(target)` | Generate one `FeedTarget`'s file (called by tasks, never by views). |
| `health_check(store)` | Worst status across the provider's `FeedTarget` rows (feeds all `up_to_date` → OK; any `error` → RED with reasons). Surfaces on the admin dashboard per §XV-1. |
| `item_attributes(item)` | Per-provider attribute name/value tweaks over the shared item dict (D4). |

Both v1 providers share the renderer; per-provider deltas are data
(attribute maps), not code — provider #3 (Pinterest, same Google item schema
per spec §10) is one subclass + one `register()` call.

### D2 — Feed matrix, URLs, token protection

- **Matrix (FEED-001):** one `FeedTarget` per (store, provider,
  `StoreLanguage`, `country_code`) where the provider is enabled for the
  store, the language `is_enabled`, and `country_code ∈` that language's
  `ShippingCountry` rows. A reconciliation step in the beat task creates
  missing targets and **deletes** orphaned targets + their files (a target
  whose language was disabled or country removed must stop being served —
  ML-012; the 403/404 is the visible state, no zombie files).
- **URL scheme (FEED-001):** `/feeds/<provider>/<country>-<lang>.xml?token=…`
  e.g. `https://mystore.com/feeds/google/us-en.xml?token=…`,
  `https://mystore.fr/feeds/facebook/fr-fr.xml?token=…`.
  - Served on the **StoreLanguage's own domain**; the view resolves
    (store, domain) via `request.locale` (LocaleMiddleware / ML-004 single
    resolver) and looks up the `FeedTarget` scoped `.for_store()` — a feed is
    unreachable from any other store's host (multi-tenant isolation is the
    conjunction of host resolution + store-scoped lookup).
  - Routes registered in `webecom/urls.py` **before** the storefront
    catch-all (like `/chat/`), and `"feeds"` added to
    `RESERVED_TOP_LEVEL_SLUGS` with a `chat`-style comment (the
    urls-generated drift test doesn't see webecom-level routes).
  - Country segment is lowercase in the URL, normalised to uppercase for the
    `ShippingCountry` lookup.
- **Auth (AC-213):** per-store secret token (`StoreFeedToken`, 32-byte
  `secrets.token_urlsafe`), required as `?token=`; comparison via
  `hmac.compare_digest`; missing/wrong token → **403** with an empty body
  (never the XML, never a reason that confirms feed existence). Rotatable
  from admin (rotate = generate new value + show the new URLs; old token dies
  immediately — AC-213's rotation scenario). One token per store, not per
  provider (AC-213 wording: "the token is per-store"); per-provider tokens
  were considered (finer-grained rotation) and rejected to match the AC.
  This is the platform norm: Merchant Center / Meta fetch a
  public-but-secret URL on schedule. `robots.txt` already emits
  `Disallow: /feeds/` (ROB-001, shipped).

### D3 — Generation strategy: pre-generated, debounced (FEED-005)

Sitemaps v1 generates dynamically per request with a 1-hour cache. Feeds
deliberately do **not** copy that, for reasons the sitemap precedent itself
concedes (its pre-generation TODO):

1. FEED-005 mandates an explicit per-feed status
   (`up_to_date/regenerating/stale/error`) with last-generated timestamp —
   a status requires a persisted generation record, which request-time
   generation doesn't have.
2. AC-211 requires the admin to show **per-product exclusion reasons** —
   only computable at generation time and must be stored.
3. Feed size: one item per variant × per country × per provider; a 2,000-
   variant store shipping 4 countries serves 16 files. Generating that inside
   a Google fetch request risks timeouts; Google/Meta fetch on *their*
   schedule and tolerate staleness measured in minutes.

Mechanics:

- **Dirty marking (signals.py):** `post_save`/`post_delete` receivers mark all
  of a store's `FeedTarget` rows `is_dirty=True` (coarse per-store
  granularity — regeneration is cheap enough; per-object precision is not
  worth the invalidation-audit risk of §V) for: `Product`, `ProductVariant`,
  `ProductImage`, `ProductTranslation`, `Permalink`, `CollectionProduct`
  (scope=collections), `ShippingCountry`, `StoreLanguage`, `FeedConfig`,
  `StoreFeedToken` (rotation regenerates nothing but is logged), and —
  platform-wide, per FEED-008 — `CurrencyRate` / `CurrencyConverterSettings`
  (marks **all** stores' targets dirty; rates change at most daily).
  **Post-review note (2026-07-11, Architect adjudication):** `CollectionProduct`
  and `CurrencyRate` are wired on `post_save` only, not `post_delete` — an
  accepted deviation from the "post_save/post_delete" framing above, not an
  oversight. Concretely: a `CollectionProduct` row **deleted** (a product
  removed from a `scope=collections` feed's collection) and a `CurrencyRate`
  row **deleted** do not immediately mark targets dirty; both removals are
  covered instead by the nightly `rebuild_all_feeds` safety net (≤24h
  staleness, same §V posture as any other signal-bypassing mutation — see
  Risks). Future improvement, not required for T033-A/T033-B: a dedicated
  `_mark_store_dirty()` helper also wired to `post_delete` for these two
  models, so removals get the same 30-min debounce as additions instead of
  waiting for the nightly rebuild.
- **Debounced beat task** `feeds.tasks.regenerate_dirty_feeds` every **30
  min** (FEED-005 default; setting `FEEDS_REGENERATE_INTERVAL_MIN`): reconcile
  the matrix, then for each dirty target: set `status=REGENERATING`, clear
  `is_dirty`, build, atomic-write, set `UP_TO_DATE` (or `ERROR` + message).
  A change arriving mid-generation re-sets `is_dirty` → next tick regenerates
  (no lost update). Idempotent and safe to re-run at any point (§XV-6).
- **Nightly full rebuild** `feeds.tasks.rebuild_all_feeds` — the §V safety net
  for mutation paths that bypass signals (bulk updates, raw SQL, future
  imports).
- **Storage & serving:** files under
  `FEEDS_ROOT/<store_id>/<provider>/<country>-<lang>.xml`, written to a
  temp file then `os.replace()` (atomic — a fetcher never sees a partial
  feed). The view streams via `FileResponse` after the token check
  (`X-Accel-Redirect` is a production optimisation, not v1). The path is
  composed internally from validated segments only — nothing from the request
  reaches the filesystem path.
  **Post-audit update (2026-07-11, security audit F3):** originally specified
  as `MEDIA_ROOT/feeds/...`; moved to a dedicated `FEEDS_ROOT` setting
  (default `BASE_DIR / "feeds_files"`, env-overridable) because `MEDIA_ROOT`
  is published at `/media/` (DEBUG static serving and the production nginx
  mapping, since product images live there too), which made the token check
  above bypassable via `/media/feeds/...` with no token at all. See
  `docs/security/FEEDS_ENGAGEMENT_AUDIT.md` F3.
- **Status state machine (§XV-3):** `PENDING` (never generated) →
  `REGENERATING` → `UP_TO_DATE` | `ERROR`; `STALE` = `is_dirty` and older
  than the debounce interval ×2 (computed property for the banner). Fetching
  a `PENDING` target returns **503 Retry-After**, an `ERROR` target keeps
  serving the last good file (if any) — a bad generation never blanks a live
  ad catalog; the error is loud in admin instead.

### D4 — Field mapping (sign-off artifact) — SIGNED OFF (human, 2026-07-11)

Shared item dict built once in `items.py`; providers only rename/format.
**Both columns below are emitted from the same values — no per-provider
recomputation of money, IDs, or URLs.**

| Value (source of truth) | Google Shopping attr | Meta/Facebook attr | Rule |
|---|---|---|---|
| `catalog_item_id(product, variant)` — **frozen, imported from `pixels.events`** | `g:id` | `g:id` | NON-NEGOTIABLE. PK-based, slug changes never change it (AC-210). |
| `str(product.pk)` | `g:item_group_id` | `g:item_group_id` | Always emitted, including single-variant products (harmless; keeps grouping stable when a second variant appears). |
| Title: published `ProductTranslation.title` for the feed language; source `Product.title` for the store's source language. Multi-variant products append variant label: `"{title} — {variant.title}"` | `g:title` | `g:title` | Truncate 150 chars. Published-only rule (ML-020 lesson): a draft translation never leaks. |
| Description: same translation rule, `strip_tags` → plain text | `g:description` | `g:description` | Truncate 5000 chars (both platforms' limit). Never empty: falls back to title. |
| `base_url(store_language) + "/" + permalink.slug + "/"` via the resolver map — canonical primary URL, never an A/B variant URL | `g:link` | `g:link` | Item **excluded** if no active Permalink for (product, lang) — by construction this equals the published-translation gate (ADR-008). |
| Primary `ProductImage` (is_primary, else lowest display_order) as **absolute** URL: `MEDIA_URL` if absolute (S3 prod), else `https://{domain.host}{image.url}` — one helper `absolute_media_url(store_language, file)` | `g:image_link` | `g:image_link` | Product-level images (no per-variant image model exists); all variants share them. |
| Remaining images (max 10) | `g:additional_image_link` | `g:additional_image_link` | Same helper. |
| Price: **regular** price = `compare_at_price` if set **and > `variant.price`**, else `variant.price` (the Risks-guard rule, applied here — never `compare_at_price` alone without the guard), converted via `display_amount(value, store.default_currency, feed_currency)` and formatted `"{amount} {CCY}"` | `g:price` | `g:price` | FEED-008: feed-country display currency for BOTH price fields. Decimal only, never float. **Corrected 2026-07-11 (human adjudication):** this row previously read "`compare_at_price` if set else `variant.price`" with no guard, which contradicted the Risks section's guard below on a merchant-misconfigured `compare_at_price <= price`. The Risks guard wins — see Risks and `11_uncertainties_to_validate.md`. |
| Sale price: `variant.price` converted the same way, **only when `compare_at_price` is set and > price** | `g:sale_price` | `g:sale_price` | FEED-007's timed-promo `sale_price_effective_date` is deferred until the timed-promo entity exposes dates (PENDING P-5). |
| Availability from `inventory_mode` (table below) | `g:availability` (+ `g:availability_date` for preorder) | `g:availability` | See D4b. |
| `"new"` constant | `g:condition` | `g:condition` | v1 hardcoded (AF-016). |
| `product.vendor` when non-empty | `g:brand` | `g:brand` | Omitted when blank. |
| `variant.sku` when non-empty | `g:mpn` | `g:mpn` | Shopify-compatible convention (variant SKU as MPN). No GTIN field exists → `g:gtin` never emitted in v1 (PENDING P-4: dedicated barcode field). |
| `identifier_exists = "no"` when NOT (brand ∧ mpn) | `g:identifier_exists` | — | Google's documented escape hatch for identifier-less products; Meta needs nothing. |
| — omitted in v1 — | `g:google_product_category` | `fb_product_category` | Optional since 2021; Google auto-categorizes. PENDING P-3 (manual Type mapping vs AI job). |
| `product.requires_shipping == False` → `g:shipping` weight/price omitted; physical goods: item-level shipping **omitted in v1**, configured account-side in Merchant Center | `g:shipping` | — | PENDING P-7: emit per-country shipping blocks from shipping zones (T032) once per-product override semantics are DECIDED. |

#### D4a — Feed currency resolution (FEED-008)

New single resolution function (§XV-4), `feeds/currencies.py`:

- `COUNTRY_TO_CURRENCY: dict[str, str]` — platform-global static ISO-3166 →
  ISO-4217 map in code (like the pixel event maps: data, reviewed in PR, no
  admin surface; ~250 entries, stdlib only).
- `feed_display_currency(store, country_code) -> str`:
  1. `mapped = COUNTRY_TO_CURRENCY.get(country_code)`;
  2. if `mapped == store.default_currency` → identity, always works;
  3. elif `mapped` has a `CurrencyDefinition` **and** a `CurrencyRate` →
     return `mapped` (conversion via `display_amount()` per item);
  4. else → raise `FeedCurrencyError` → the whole `FeedTarget` goes to
     `ERROR` with the actionable message *"No exchange rate for {CCY}
     ({country}); add a CurrencyRate at /superadmin/ or remove {country}
     from this language's target countries"*.
  Loud failure chosen over silently emitting store-currency prices: Google
  Merchant rejects a CA feed priced in USD (FEED-008 explicitly refuses to
  rely on Merchant-side conversion), so the silent fallback would just move
  the failure to Google's side where it's invisible in our admin (§XV-1).
  Note the shopper-facing picker is *not* a prerequisite:
  `StoreCurrencySetting` enablement is deliberately ignored here (it gates
  the storefront picker, not feed pricing).

#### D4b — Availability per inventory mode (per-variant; FEED-004/AC-212)

| `inventory_mode` | Feed behaviour |
|---|---|
| `no_tracking` | `in stock` |
| `fixed_qty`, quantity > 0 | `in stock` |
| `fixed_qty`, quantity == 0/NULL | `out of stock` — **included** (keeps ad item history, AC-212) |
| `sold_out` (always sold out) | `out of stock` — **included** (AC-212 explicit) |
| `fake_server` | `in stock` (display-only scarcity; the variant is purchasable) |
| `presale`, `presale_ships_at` set | `preorder` + `g:availability_date = presale_ships_at` (FEED-004) |
| `presale`, `presale_ships_at` NULL | **excluded**, reason `presale_no_date` (Google requires the date for preorder; silent omission would get the item disapproved invisibly) |
| `ask_when_available` | `out of stock` — included (page exists, not purchasable now) |
| `quotation` | **excluded entirely**, reason `quotation_mode` (no fixed price — FEED-004/AC-212) |

### D5 — Variant handling (drift-critical)

One feed item per `ProductVariant` with `is_active=True`, because the pixels
already emit **variant-level** `content_ids` everywhere
(`storefront/views.py` passes `default_variant` to view_content/add_to_cart;
purchase emits per-order-item variant IDs). A parent-only feed would mean the
platforms receive `"12_34"` from pixels and find only `"12"` in the catalog —
dynamic retargeting dies silently (ADR-022 Risks). Therefore:

- `feeds/items.py` **imports `catalog_item_id` from `pixels.events`** — it is
  the only permitted way to produce `g:id`. Re-implementing the f-string
  locally is forbidden (drift test below).
- `item_group_id = str(product.pk)` groups variants for both platforms.
- Variant-distinguishing attributes (`g:color`, `g:size`) from
  `VariantOptionAssignment` are **deferred** (PENDING P-6: needs an
  option-name → attribute mapping heuristic and translations); v1
  distinguishes variants via the title suffix (D4) — accepted by both
  platforms.
- Per-variant `price`/`availability` are naturally correct (both live on the
  variant). Images are product-level (no per-variant image model exists).

### D6 — Exclusions (FEED-004/FEED-006, loud per §XV-1)

Evaluated at generation, per item, in this order; every exclusion is recorded
(reason enum + product/variant id) into `FeedTarget.exclusions_json` and
surfaced on the admin status page (AC-211 requires the reason to be visible):

1. Scope (FEED-006): `FeedConfig.scope` = `all` | `collections` (+ M2M);
   products outside scope are simply not candidates (not recorded — they are
   a configuration choice, not a defect).
2. `product.status != 'active'` → not a candidate (draft/archived never
   leak — also not recorded, by definition of "published catalog").
3. No active Permalink for (product, feed language) → `no_translation`.
4. Zero `ProductImage` rows → `no_image` (AC-211: Google rejects imageless
   items; excluded **loudly**).
5. **Unrecognized `inventory_mode` value → `unknown_inventory_mode`** (added
   2026-07-11, defensive). Should never occur given the model's `choices=`
   constraint, but generation code must not crash on legacy/corrupted rows or a
   future mode value the feed engine hasn't been taught yet — excluded loudly
   (recorded in `exclusions_json`, surfaced in admin) rather than raising and
   aborting the whole target's generation.
6. `inventory_mode == quotation` → `quotation_mode`.
7. `presale` without `presale_ships_at` → `presale_no_date`.
8. Variant `is_active=False` → skipped silently (admin's own toggle).

Per-country shippability exclusion (FEED-003 "product not shipped to CA is
excluded from the CA feed") is **deferred**: no per-product shipping-country
data model exists yet (T032's per-product override semantics are themselves
PENDING). v1 treats `ShippingCountry` as the admin's declared targeting; the
existing ADR-008 launch cross-warning already flags zone/target divergence.
Recorded as PENDING P-8.

AC-211's broken-image-URL edge ("exclude after N failed fetches") is out of
v1 scope — we never fetch our own image URLs; noted in Risks.

### D7 — Data model (all additive; no changes to existing tables)

| Model | Fields | Notes |
|---|---|---|
| `FeedConfig` `[S]` | `provider` (choices = registry keys), `is_enabled` (default False), `scope` (`all`/`collections`), `collections` M2M → Collection, timestamps | Unique `(store, provider)`. The admin card binds here. |
| `StoreFeedToken` `[S]` | `token` (unique, `secrets.token_urlsafe(32)`), `rotated_at` | One row per store, created lazily. `rotate()` replaces the value atomically. |
| `FeedTarget` `[S]` | `provider`, `store_language` FK (PROTECT), `country_code`, `status` (`pending/regenerating/up_to_date/error`), `is_dirty` (default True), `last_generated_at`, `last_error` (text), `item_count`, `excluded_count`, `exclusions_json`, `file_path` | Unique `(store, provider, store_language, country_code)`. Index on `(store, is_dirty)`. Rows reconciled by the beat task (D2). |

Everything else (currency map, provider registry) is code, not schema —
consistent with ADR-022's "platform data in code" precedent.

### D8 — Admin surface (T033-B) + gates

- **Where:** store admin `/admin/` (06 §10; permission: Apps — Full access
  per AF-016), one "Catalog feeds" section with one card per registered
  provider (registry-driven — a new provider appears with zero admin-code
  changes, like pixels).
- **Per card:** enable toggle, scope selector (+ collection picker),
  the list of this provider's feed URLs (one per country×lang, read-only,
  copy button, token included), per-target status banner (green
  `up_to_date` / amber `regenerating`/`stale` / red `error` with
  `last_error`) — the reference UI's decorative banner becomes real state —,
  `item_count`/`excluded_count`, expandable exclusion list (product, reason),
  **Regenerate now** button (marks targets dirty + enqueues the task
  immediately; does not generate in-request), last-generated timestamp.
- **Token rotation:** store-level "Rotate feed token" button with an explicit
  warning ("all feed URLs change; update Merchant Center / Business Manager
  data sources"). Rotation is instant-revoke.
- **Launch readiness:** feeds are **not** required for launch (06 §10 —
  every field "Required for launch? no"). One warning-level check added:
  provider enabled but all its targets `error`/`pending` for > 24h.
- **Safety gate (named):** Safety Agent review required before release —
  public endpoint + token auth + file serving. Data-exposure assessment
  baked into the design: feed bodies contain only already-public storefront
  data (active products' titles/descriptions/prices/URLs/images); drafts,
  archived, quotation prices, costs/margins (no such fields exist on these
  models), customer data and A/B variant URLs never enter the pipeline; the
  token gates bulk scraping, `hmac.compare_digest` prevents timing leaks;
  403 body is empty; the file path never derives from request input.
- **SEO gate (named):** SEO Agent review — `link` canonical-primary rule,
  robots `Disallow: /feeds/` (already shipped), no sitemap/hreflang
  interaction. TST-FEED-008 belongs to this gate.

### D9 — Ticket split (two Developer tickets)

- **T033-A — Feed engine:** `feeds` app, models + migrations, registry +
  google/facebook providers, currency map + `feed_display_currency`,
  item builder (imports `catalog_item_id`), streaming XML renderer
  (escaping!), signals, beat + nightly tasks, `feed_view` + URL registration,
  `RESERVED_TOP_LEVEL_SLUGS += "feeds"`, engine tests incl. the drift tests.
- **T033-B — Admin surface:** cards, status page with exclusions, regenerate
  button, token rotation, launch-readiness warning, admin/permission tests.
  Depends on T033-A. (Designer pass optional afterwards; SEO + Safety gates
  before release.)

---

## Why

- **Variant-level items** are forced by the frozen pixel convention — the
  platforms already receive `"{p}_{v}"` IDs from every storefront event; the
  feed must present the same ID space or retargeting fails invisibly. This
  is also what Google/Meta document as best practice (item per variant +
  `item_group_id`).
- **Pre-generation** is the only strategy satisfying FEED-005's explicit
  status machine and AC-211's persisted exclusion reasons; it also keeps the
  public endpoint O(1) (token check + sendfile) regardless of catalog size.
  Diverging from the sitemap's dynamic v1 is deliberate and matches the
  sitemap's own stated evolution path.
- **One RSS/`g:`-namespace renderer for both providers** halves the format
  surface; Meta's ingestion of Google-namespace XML is long-standing and
  documented, and spec §10 already treats Pinterest as "same item schema as
  Google".
- **Static country→currency map + loud ERROR** keeps FEED-008 honest with
  zero new admin surface and zero new dependencies; the failure lands in our
  admin (actionable) instead of in Merchant Center (invisible).
- **Registry/connector pattern** is mandated (§IX) and already proven twice
  (pixels, slot providers); feeds are the canonical "regularly added/removed"
  feature class.

## Risks

- **ID drift despite the freeze** — someone "simplifies" the import into a
  local f-string; retargeting breaks silently. Mitigated by the two-layer
  drift test (below) and the `catalog_item_id` docstring's ADR-amendment
  requirement.
- **Signal-bypassing mutations** (bulk `update()`, future imports) leave
  feeds stale up to 24h — accepted; the nightly rebuild is the mandated
  safety net (FEED-005, §V). Staleness is visible (`STALE` banner).
- **CurrencyRate change fan-out** marks every store dirty at once → a
  regeneration burst. Bounded: rates change ≤ daily (ECB task), generation is
  per-target and idempotent; the beat task processes targets sequentially per
  run. If it ever hurts, shard by store — no design change.
- **Large catalogs**: memory is bounded by `.iterator()` + streaming writes,
  but a 50k-variant store × 8 targets is real CPU; acceptable at current
  scale (internal platform), revisit with per-target duration metrics
  (`last_generated_at` deltas make this observable).
- **Broken image URLs** (AC-211 edge): we emit URLs we host; a 404 would mean
  our own media is broken. No fetch-verification in v1 — Google's item
  disapprovals in Merchant Center are the detector. Recorded as a conscious
  gap.
- **Token in URL** ends up in platform configs and possibly screenshots —
  inherent to the platform-fetch model (AC-213 acknowledges it); rotation is
  the remedy and is one click.
- **`compare_at_price` misuse** (merchant sets it lower than price) would
  emit an invalid sale_price pair; guard: emit `sale_price` only when
  `compare_at_price > price`, else emit `price = variant.price` alone.

## Rollback strategy

- Feature is fully additive: new app, new tables, new URL prefix, one
  frozenset entry. Rollback = disable the two beat entries, remove the URL
  include (requests → 404), and (optionally) `migrate feeds zero` — no
  existing table is touched, no data loss outside the feeds app.
- Per-store rollback is just `FeedConfig.is_enabled=False` (targets get
  reconciled away; files deleted).
- External blast radius on rollback: Merchant Center/Business Manager fetches
  start failing → platforms mark items stale after their grace period. That
  is the platform-standard degradation and recoverable by re-enabling.

## Tests required

**Drift tests (NON-NEGOTIABLE, the freeze):**
1. `test_feed_item_id_equals_catalog_item_id` — generate a feed for a store
   with multi-variant products; for every `<g:id>` in the XML, assert
   equality with `pixels.events.catalog_item_id(product, variant)` computed
   independently from the DB.
2. `test_feed_builder_imports_frozen_id` — assert
   `feeds.items` resolves `catalog_item_id` to the *same function object* as
   `pixels.events.catalog_item_id` (`feeds.items.catalog_item_id is
   pixels.events.catalog_item_id`) — a local re-implementation cannot pass.
3. `test_purchase_content_ids_subset_of_feed_ids` — place an order, build the
   purchase pixel payload, generate the feed: every `content_ids` entry
   appears as a `g:id` in the feed of the order's store/language.

**AC coverage:**
- AC-210: valid XML, all required fields present, PK-based id stable across a
  slug change; `&`, `<`, `>` in title/description are escaped (dangerous-data
  case).
- AC-211: imageless product absent from every provider's feed AND its
  exclusion reason `no_image` visible in `exclusions_json`/admin.
- AC-212: `fixed_qty` at 0 → included `out of stock`; `sold_out` → included
  `out of stock`; `quotation` → absent; inventory change marks target dirty →
  next beat run reflects it (TST-FEED analog of AC-212's edge).
- AC-213: no token → 403; wrong token → 403 (empty body); correct token →
  200 XML; rotation invalidates the old token immediately; token from store A
  never opens store B's feed **and** store A's feed is 404/403 on store B's
  domain (cross-tenant test, both axes).

**FEED rules:**
- FEED-008/TST-FEED-008: store default USD, language EN targeting US+CA with
  a CAD rate → US feed prices in USD (identity, unrounded), CA feed prices in
  CAD equal to `display_amount()` output (call the resolver in the test — no
  independent formula); missing CAD rate → target `ERROR` with the actionable
  message, previous file still served.
- FEED-007 mapping: `compare_at_price=40, price=25` → `price "40.00 …"`,
  `sale_price "25.00 …"`; `compare_at_price` NULL → no `sale_price`;
  `compare_at_price <= price` → no `sale_price`.
- Availability matrix: one test per inventory mode incl. `presale` with date
  (→ `preorder` + `availability_date`) and without (→ excluded
  `presale_no_date`), `fake_server` → `in stock`, `ask_when_available` →
  `out of stock`.
- Translation gate: product published in EN, FR translation draft → item in
  EN feed, absent from FR feed with `no_translation` reason; FR published →
  FR feed uses translated title/description and the FR permalink on the FR
  base_url.
- Link rule: `g:link` equals `base_url(language) + '/' + permalink.slug +
  '/'`; never contains an A/B variant slug.

**Engine behaviour:**
- Matrix reconciliation: enabling a provider / adding a ShippingCountry
  creates targets; removing them deletes targets and files.
- Dirty/debounce: product save marks all store targets dirty; beat run clears
  dirty and produces files atomically (no partial file observable — write is
  temp + `os.replace`); a `PENDING` target serves 503.
- Scope: `collections` scope with product outside the collection → not a
  candidate; `validate_settings()` fails on `collections` scope with empty
  M2M.
- Reserved slug: `"feeds"` present in `RESERVED_TOP_LEVEL_SLUGS`; creating a
  product with slug `feeds` raises ValidationError.
- Registry: unknown provider key in URL → 404; `health_check` RED when a
  target is `ERROR`.

---

## DECIDED items (product-level — human, 2026-07-11: all approved as recommended)

All nine items below were PROPOSED with a recommended default and are now
**DECIDED (human, 2026-07-11): approved as recommended, as stated in each row.**
None blocks T033-A/T033-B; mirrored in `11_uncertainties_to_validate.md`.

| # | Question | DECIDED (recommended default, approved 2026-07-11) |
|---|---|---|
| P-1 | Pinterest feed in v1? (06 §10 says IN_SPEC; 07 §10 marks it PENDING for v1) | Defer — the registry makes it one subclass later; ticket text mandates Google + Facebook only. |
| P-2 | Country-tracking parameter on `g:link` (FEED-003 "may carry") | None in v1 — keeps links identical to canonicals; platforms provide their own click tagging. |
| P-3 | `google_product_category` source (manual product Type vs AI job) | Omit in v1 (optional attribute; Google auto-categorizes). Later: AI job proposal per FEED-002. |
| P-4 | Dedicated GTIN/barcode field on `ProductVariant` | Defer; `identifier_exists=no` + `mpn=sku` covers v1. Needs a small catalog migration + admin field when decided. |
| P-5 | `sale_price_effective_date` from timed promos (FEED-007) | Defer until the timed-promo entity (admin-021-02) exposes start/end dates; v1 maps `compare_at_price`. |
| P-6 | `g:color`/`g:size` from `VariantOptionAssignment` | Defer — needs option-name→attribute mapping heuristic + translated values; title suffix suffices for v1. |
| P-7 | Item-level `g:shipping` blocks from shipping zones | Defer to Merchant Center account-level shipping settings until T032's per-product override semantics are DECIDED. |
| P-8 | Per-country per-product shippability exclusion (FEED-003) | Defer with P-7 — no per-product country data exists yet; ADR-008's launch cross-warning covers the store-level divergence. |
| P-9 | Regeneration cadence (11 Part C "feed cadence details") | Confirmed FEED-005 defaults: 30-min debounced beat + nightly full rebuild, both settings-overridable. |
