# ADR-028: A/B multi-variant product pages (`ProductPageVersion`)

**Ticket:** TICKET-042 · **Status:** ACCEPTED (human, 2026-07-11) — all 5 PENDING product-level defaults (P1–P5, see the table at the end of this document) approved as recommended.
**Complies with:** design-pattern-ideas §II (one slug function), §III (single resolver), §XI (seo_head in base layer), §XII (redirect history), §XV-1 (no invisible failures) · ADR-004 (analytics, closed vocabulary) · ADR-005 (permalinks) · ADR-008 (multilingual domains) · DECIDED FM-C4 · URL-004 / CAN-020 / META-004 / SM-030 / ROB-004 / FEED-002 · UF-005 · AF-12 (resolves its "page A/B test data model" PENDING)

## Decision

A product may have N **page versions** — same product, same price, same SKU variants, same translated text, **different image set** — each served at its own URL (`/<product-slug>-<suffix>/`), registered in the Permalink table under a dedicated content type, canonicalized to the primary product URL, excluded from sitemap/hreflang/feeds, and measured per-version (views, add-to-carts, orders) via the existing analytics beacon `properties` payload plus a soft `page_version_id` stamp on cart/order items. Traffic split is **link-level only** (the merchant drives Pinterest/ads traffic to each version URL); there is no automatic assignment.

---

## Context

- Spec anchors: 00 positioning ("multi-variant product pages for Pinterest content and A/B testing"), 02 feature matrix P2, UF-005 (03), AF-12 (04, PENDING data model — resolved here), 07 §URL-004/CAN-020/META-004/SM-030/ROB-004, AC-017/AC-090/AC-093, TST-CAN-020/TST-SM-030, 15 §"same template, swapping only the image set".
- **Only images vary.** UF-005 step 2 is explicit: "All other product content (title, price, description, variants, CTA) is identical across versions." This ADR does NOT introduce per-version copy, layout, or price — that would multiply translation rows (ADR-014 pipeline) and reopen FM-C4. Anyone extending this to copy variants must write a new ADR.
- Existing integration points, verified in code:
  - `permalinks/templatetags/permalinks_tags.py` — `_resolve_canonical_url()` uses the current URL as canonical for all pages, with an explicit deferral comment naming this ticket as the point where variant pages get canonical → primary.
  - `sitemaps/sitemaps.py` — carries a TODO: "add a `.exclude(content_type=ab_variant_ct)` filter" once the A/B content type exists (the comment attributes it to TICKET-029; it is this ticket, TICKET-042 — fix the comment).
  - `feeds/tests/test_adr_named_cases.py::ABVariantSlugNeverInLinkTest` — armed negative test: once variant URLs exist, extend it to assert `g:link` is still the primary permalink.
  - `analytics/models.py::EventType` — **closed vocabulary per ADR-004 §2; no new event types may be added without amending ADR-004.** `Event.properties` (JSONField) exists for event-specific payload.
  - `storefront/views.py::product_page(request, product)` — renders per-request, **no page cache exists today**, images fetched as `ProductImage … order_by("display_order")`.
  - `orders/models.py::OrderItem` — snapshot-fields invariant ("never join to the catalog for historical display").
  - 05 §2: "A/B variant URL image sets reference these [ProductImage] rows."

### Naming — the collision hazard

`ProductVariant` already means **SKU variant** (size/color, price, inventory). Reusing "variant" for page-level A/B would be a permanent source of bugs and mis-scoped queries. **DECIDED: the entity is `ProductPageVersion`** ("page version" is also UF-005's own wording). The word "variant" is banned from every new identifier in this feature: model names, field names, analytics keys, admin labels, template context (`page_version`, `page_version_id`, "A/B page versions"). Spec prose keeps saying "A/B variant URL"; code never does.

## Options considered

1. **Data model**
   - (a) Rows on `Permalink` only (a `primary_permalink` FK, no entity) — no place for the image set, no admin object, no attribution id.
   - (b) Full content-override entity (per-version title/description) — multiplies `ProductTranslation` rows per language × version; contradicts UF-005; rejected.
   - (c) **`ProductPageVersion` entity owning an ordered subset of the product's existing `ProductImage` rows, zero translatable fields** — chosen.
2. **URL scheme**
   - (a) Query param (`?v=2`) — invisible to Permalink resolver, stripped by canonical logic, bad Pinterest surface; rejected.
   - (b) Path prefix (`/v/2/<slug>/`) — new reserved route, breaks "one resolver" shape; rejected.
   - (c) **Suffix on the product slug: `<product-slug>-<suffix>`** — already decided in 07 §URL table (`/[lang/]<product-slug>-<variant-slug>/`) and AC-017/AC-093 (`/dress-v2/`); chosen.
3. **Traffic split**
   - (a) **Link-level (merchant-driven): each URL is a distinct destination; no assignment logic** — chosen. Matches the ticket ("Pinterest surface"), UF-005 ("shopper arrives at one of the product's variant URLs"), and avoids all cookie/cache/CDN-vary complexity.
   - (b) Automatic 50/50 assignment at the canonical URL (cookie + alternate render) — changes cache semantics, needs consent review (ADR-025) for the assignment cookie, and is not what the spec asks. Deferred; would be a new ADR.
4. **Measurement**
   - (a) New event types (`page_version_view`…) — violates ADR-004 §2 closed vocabulary; rejected.
   - (b) **`page_version_id` key inside `Event.properties` on existing `page_view` / `add_to_cart` / `purchase` events, plus a soft `page_version_id` stamp on `CartItem` → copied to `OrderItem`** — chosen. Money/order truth stays in the main DB (ADR-004 §3b: purchase is written server-side); behavioral counts stay in the analytics DB.
   - (c) UTM-style (`utm_content=v2` on inbound links) — merchant-configurable but unenforceable and lossy; not chosen as the mechanism (UTMs keep working orthogonally for campaign attribution).

## Chosen option — full design

### 1. Data model (app: `catalog`)

**`ProductPageVersion(StoreOwnedModel)`**
| field | type | notes |
|---|---|---|
| `product` | FK Product, CASCADE, `related_name="page_versions"` | |
| `name` | CharField(255) | Internal admin label ("Model 2 photos"). **Never rendered to shoppers** → not translatable. |
| `slug_suffix` | CharField(64) | Normalized through `core.slugs.make_slug()` on save (§II — the ONE slug function). Non-empty. |
| `is_active` | Boolean, default True | Inactive = URLs 302 to primary (see §3). |
| `position` | int | Admin ordering. |
| `created_at` | auto | |

- `unique_together (product, slug_suffix)`; store-scoped manager like every `StoreOwnedModel`.
- **Cap: max 10 versions per product** (permalink-bloat guard) — ASSUMPTION (LOW), enforced in `clean()` + service.
- `hreflang_exempt` property returning `True` — reuses the existing suppression path in `hreflang_tags` (CAN-020: variant URLs emit zero hreflang) with **no template-tag change**.
- **No translatable fields, by construction.** The AI translation pipeline (ADR-014) is untouched; no `AiJob` types are added.

**`ProductPageVersionImage(StoreOwnedModel)`**
| field | type | notes |
|---|---|---|
| `page_version` | FK ProductPageVersion, CASCADE, `related_name="images"` | |
| `image` | FK catalog.ProductImage, CASCADE | Must belong to the same product — validated at save. |
| `display_order` | int | |

- `unique_together (page_version, image)`. The image set is an **ordered subset of the product's existing `ProductImage` rows** (05 §2's stated design) — merchants upload new photos to the product first, then compose versions. No second binary store, no per-version alt-text (alt text stays on `ProductImage`/`ProductImageTranslation`).
- **A version must reference ≥ 1 image to be activated** (otherwise it would render identically to the primary — a meaningless test and a pure duplicate page) — ASSUMPTION (LOW).

**Attribution stamps (main DB):**
- `CartItem.page_version_id` — `IntegerField(null=True)`, **soft ID, deliberately not an FK**, following the `OrderItem` snapshot invariant and the ADR-004 soft-ID rule: attribution must survive version deletion. Set by `cart.service.add_item(..., page_version_id=None)` when the add-to-cart originated from a version page. Note `unique_together (cart, variant)`: if the same SKU variant is added again from a different page version, `add_item` increments quantity and **keeps the first stamp** (first-touch per line) — documented behavior.
- `OrderItem.page_version_id` — same type; copied from `CartItem` at order creation like the other snapshot fields.

### 2. URLs and the resolver (ADR-005 — no new resolution path)

- Each active version gets **Permalink rows with `content_type=ContentType(ProductPageVersion)`**, `object_id=version.pk`, slug = `f"{product_permalink.slug}-{slug_suffix}"`, **one per language in which the product has an active permalink** (the suffix is language-neutral; the translated part is the product slug it is appended to). ASSUMPTION (LOW): all-languages automatic, not per-language opt-in.
- Rows are maintained by signal/service (mirror of `permalinks/registry.py` + `permalinks/signals.py` patterns), all writes in one transaction:
  - **version created / reactivated** → create (or reactivate) permalinks for every language with an active product permalink. Collisions with existing slugs hit the `(store, lang, slug)` unique constraint → the whole creation fails atomically with an admin-visible error asking for a different suffix (§XV-1 — never partial-language registration).
  - **product slug changed** (any language) → the existing `slug_changed` machinery renames the version permalinks of that language and writes `SlugRedirect` rows (301, chain-collapsed) for them exactly as for the primary.
  - **translated product permalink published later** (ADR-021 activation) → create the matching version permalinks for that language.
  - **version deactivated** → version permalinks `is_active=False` + `SlugRedirect` **302 → primary slug** (temporary, reversible — matches UF-005's "falls back to normal product page"; a 302 to the canonical target is strictly safer than serving duplicate content).
  - **version deleted** → permalinks `is_active=False` + `SlugRedirect` **301 → primary slug** (permanent — Pinterest link equity consolidates; §XII: never let an indexed/pinned URL die as 404). New `RedirectTrigger` value `page_version_change` (additive enum).
- **Edge case (must be tested):** a 2-letter product slug + 2-letter suffix composes a slug matching the ISO-639 reserved pattern (`go` + `en` → `go-en`). `Permalink.save()` already rejects it (reserved-slug validator runs on every write); the admin must surface that rejection as a suffix-validation error, not a 500.
- **Dispatch:** `permalinks/registry.py::resolve_storefront_path` gains one branch: `model_name == "productpageversion"` → load version (store-scoped), then:
  - version or product missing → 404;
  - `version.is_active is False` or `product.status != ACTIVE` → defensive 302 to the primary product URL (the redirect row should normally exist, this is belt-and-braces);
  - else → `product_page(request, product, page_version=version)`.
- `resolve_permalink` / `{% permalink_url %}` / menus / breadcrumbs / emails never emit version URLs — nothing links to them internally; they are entered only from external traffic and the admin's preview links. Feeds resolve product-CT permalinks only, so `g:link` cannot regress (see §5).

### 3. Rendering (storefront)

`product_page(request, product, page_version=None)`:
- `page_version` set and active → `images = [pvi.image for pvi in page_version.images.order_by("display_order")]`; **everything else identical** (same template — 15 §"same template, swapping only the image set"; same variants, price, translation, CTA, breadcrumbs).
- Context gains `page_version` (for the beacon config and the ATC form's hidden `page_version_id` field).
- No caching change: product pages render per-request today; this ADR adds no cache and therefore no vary/invalidation surface.

### 4. SEO head (the critical part — DECIDED FM-C4, URL-004, CAN-020, META-004)

| tag | primary page | version page |
|---|---|---|
| `rel=canonical` | self | **primary product URL, same language** |
| `robots` | — | — (**no noindex** — URL-004: canonical-only so Pinterest link equity consolidates) |
| `og:url` | = canonical | **the version's own URL** (Pinterest needs distinct pins) |
| `og:image` | first product image | **first version image** |
| hreflang | full group | **none** (via `hreflang_exempt=True`) |
| sitemap | included | **excluded** |
| robots.txt | allowed | **allowed** (ROB-004 — crawlers must be able to fetch the canonical tag) |

Mechanism:
- The view (only when rendering a version) resolves the primary path — active product-CT `Permalink` for `(store, lang)`, the same lookup pattern `product_page` already uses for collections — and sets `request.seo_canonical_path = f"/{primary_slug}/"`.
- `{% canonical_url %}`'s `_resolve_canonical_url` is split: the **canonical** tag prefers `request.seo_canonical_path` when present; the **og:url** tag keeps using the current URL. This is a **deliberate, spec-mandated exception** to SEO-review L3 ("canonical and og:url must always agree") — META-004 explicitly overrides og:url on variant pages. The docstrings of both tags must state this exception and cite META-004, or a future refactor will "fix" it back.
- If the primary permalink cannot be resolved (data corruption), emit **no canonical at all and log an error** — never fall back to self-canonical on a version page (that is the duplicate-content bug FM-C4 exists to prevent) (§XV-1: loud, not wrong).
- `sitemaps/sitemaps.py`: implement the existing TODO — `.exclude(content_type=page_version_ct)` (and correct the stale TICKET-029 attribution in the comment). Satisfies SM-030 / TST-SM-030 / AC-093's "exactly one sitemap entry per product".

### 5. Feeds (FEED-002 — already test-armed)

No code change expected: `feeds/items.py` resolves the product's own permalink (product content type). Required work is the **activation note** in `ABVariantSlugNeverInLinkTest`: add the negative case — create a `ProductPageVersion` with permalinks for the same product, rebuild the feed, assert `g:link` (item dict AND rendered XML) still equals the primary canonical URL exactly.

### 6. Measurement (ADR-004 — amendment, not extension)

- **No new `EventType`.** ADR-004 §2 is amended (one line): `page_view`, `add_to_cart`, and `purchase` events MAY carry `properties.page_version_id` (positive int; absent on primary pages / non-product pages). The beacon contract is versioned (ADR-004 risk note) — the ingest raw-SQL writer needs no change (properties is already passed through as JSON), only the beacon config emitted by the product-page template.
- **Views:** version pages emit `page_view` with `product_id` (soft column, as today) + `properties.page_version_id`.
- **Add-to-cart:** the ATC event fired from a version page carries the same key; server-side, `add_item()` stamps `CartItem.page_version_id`.
- **Purchase:** the server-side purchase event (written at payment confirmation per ADR-004 §3b) carries `properties.page_version_ids = [distinct non-null stamps of the order's items]`. Order/revenue truth for the report comes from the **main DB** `OrderItem` stamps, not from beacon events.
- **Aggregation (survives the 90-day purge):** the nightly job (TICKET-013 family) adds two `AggregatedMetric` metric types — `page_version_views` and `page_version_atc` — with `dimension_key = f"{product_id}:{page_version_id}"`, where `page_version_id = 0` means the primary page (its baseline row aggregates product-page `page_view`s without the key). Additive; existing metrics untouched.
- **v1 report ("basic statistics" per spec 00):** per product, one row for primary + each version: page views, add-to-carts, orders (count of orders with ≥1 stamped item), units, revenue (sum of stamped `OrderItem.line_total`), conversion rate = orders ÷ views. Deleted versions display as "(deleted #id)" — soft-ID convention from ADR-004. **No significance testing in v1** — PENDING below.
- **Known attribution limits (accepted, documented in the report UI):** a shopper who sees a version pin but later buys via the primary URL is attributed to primary (link-level split has no cross-visit identity); a line re-added from a different version keeps its first stamp. UTM first-touch attribution (ADR-004 §4) is unchanged and orthogonal.

**Addendum (2026-07-11, wave-3 spec review).** The bullet above ("`page_view`
with `product_id` (soft column, as today)") described the pre-existing beacon
contract as unchanged by this ADR. The shipped implementation went further:
`{% analytics_beacon %}` (`storefront/templatetags/storefront_tags.py`) now
populates the **top-level `product_id` column** on the `page_view` event
itself (not just inside `properties`) whenever `page_type == "product"` —
`previously always NULL for page_view events` per the implementation's own
comment. This is an **additive behavior change to ADR-004's event schema**,
not a bug: the aggregation dimension key this ADR introduces
(`dimension_key = f"{product_id}:{page_version_id}"`, §6 above) requires a
real `product_id` to key by, and `ADR-004`'s original `page_view` event never
populated that soft column (only `add_to_cart`/`purchase` did). The spec
review pass assessed this change as **in-scope** — it is the minimum schema
population needed to make the `page_version_views` aggregation this ADR
specifies actually work, rather than a scope-creeping change to unrelated
event types (`add_to_cart`/`purchase` `product_id` population is unchanged).
Cross-reference `ADR-004` (analytics/pixels event schema) — that ADR's own
"§2 event schema" text should be treated as amended by this note the same
way §6's opening bullet already amends it for `properties.page_version_id`.

### 7. Admin

- Product change page, "A/B page versions" section: list + create/edit (name, suffix with live URL preview per language, active toggle, ordered image picker limited to the product's images), per-language preview links, delete with the 301-redirect warning. Gated by the same **catalog module permission** as product editing (ADR-001 matrix); the results report sits under the reports/analytics module permission.
- Version rows appear in the permalinks admin like any other permalink (they are ordinary rows) — no special casing.
- AF-12's toggle ("Create multiple URL variants of this product page") = the presence of active versions; no separate store-level feature flag (keep it simple — an inactive feature is simply "no versions configured").

### 8. Explicit non-goals (v1 boundary)

- No per-version copy/title/description/layout/price (would need new ADR + translation design).
- No automatic traffic assignment at the canonical URL (new ADR: cookies, consent category, cache vary).
- No per-version pixels behavior change: `view_content` payload (product, SKU variant, price, currency — `pixels/events.py`) is **identical** on version pages; the catalog item id never changes because page versions are not catalog items. `page_view` pixels fire normally. Only `og:*` meta differs.
- No effect on upsells, campaigns, checkout, inventory (stock is shared — UF-005 edge case), or the AI job system.

## Why

- Reuses every hardened mechanism instead of inventing parallels: one resolver (§III), one slug function (§II), the existing redirect machinery (§XII), the existing hreflang-exemption hook (§XI), the closed analytics vocabulary + properties payload (ADR-004), the snapshot invariant on order items. The new surface is two small tables, one dispatch branch, one canonical override, one sitemap exclude, two aggregate metric types.
- The name `ProductPageVersion` makes the page-variant vs SKU-variant distinction structurally impossible to confuse.
- Zero translatable fields means the feature cannot multiply translation cost or interact with the AI pipeline — the most expensive failure mode of option 1(b) is designed out.
- Link-level split matches the actual product need (Pinterest pins per image set) and keeps version pages cacheable-in-principle and deterministic per URL.

## Risks

1. **Canonical regression is the existential risk**: a bug that self-canonicalizes version pages (or omits the sitemap exclude) creates store-wide duplicate content. Mitigated by AC-017/AC-093/TST-CAN-020/TST-SM-030 as release-gating tests and the armed feed test.
2. **canonical ≠ og:url divergence** may be "fixed" by a well-meaning refactor (L3 says they must agree). Mitigated: docstring exception citing META-004 + a named test asserting divergence on version pages and agreement on primaries.
3. **Permalink/redirect row growth**: bounded by the 10-version cap and languages; redirect chains stay flat (write-time collapse already exists).
4. **Suffix collisions & reserved-pattern composition** (`go`+`en`): surfaced as validation errors; atomic all-languages registration prevents partial states.
5. **Attribution bias** (cross-URL journeys, first-stamp-wins): accepted for "basic statistics"; documented in the report.
6. **Analytics purge**: raw per-version views vanish after retention — covered by the two aggregate metric types from day one.
7. Merchants may expect indexation of version pages ("more SEO surface") — they are canonicalized by design (FM-C4); admin help text must say so.

## Rollback strategy

- Fully additive feature. **Soft disable:** deactivate all versions → permalinks inactive + 302s to primary; storefront/SEO/feeds behave exactly as pre-feature; analytics keys simply stop appearing.
- **Hard rollback:** drop the two tables + the two stamp columns; version permalinks deactivated with 301s to primary (never 404 — pinned URLs must keep resolving); `properties.page_version_id` in historical events is inert JSON; `AggregatedMetric` rows for the two metric types are harmless orphans.
- The canonical-override change in the template tags degrades to exactly today's behavior when `request.seo_canonical_path` is never set.

## Tests required

Release-gating (SEO + Safety gates named below):
1. **AC-017 / TST-CAN-020** — version URL: HTTP 200, canonical = primary (same language, absolute, trailing slash), zero hreflang tags, own `og:url` + version `og:image`, absent from every sitemap.
2. **AC-090** — primary product page still self-canonical (regression on the tag split); **AC-093** — one sitemap entry per product; no two URLs share a canonical unless version→primary.
3. **TST-SM-030** — full sitemap contains no version URL (content-type exclude), all languages.
4. **Feed negative case** — extend `ABVariantSlugNeverInLinkTest` per its activation note: with a version configured, `g:link` (dict + rendered XML) is still the primary URL.
5. **Resolver/dispatch** — version URL renders version images and nothing else changed; inactive version → 302 primary; deleted version → 301 primary; `redirect_type` machinery (chain-collapse) covered for `page_version_change`.
6. **Slug lifecycle** — product slug change renames version permalinks + creates 301s per language; late-published translation creates the version permalink for that language; suffix collision and `go`+`en` reserved-pattern composition rejected with admin-visible errors, atomically (no partial languages).
7. **Canonical failure mode** — unresolvable primary permalink on a version page emits NO canonical and logs an error (never self-canonical).
8. **Measurement** — beacon config on version page carries `page_version_id`; `add_item()` stamps `CartItem`; stamp copied to `OrderItem` at order creation; first-stamp-wins on quantity increment; server-side purchase event carries `page_version_ids`; aggregation writes both metric types with `product_id:page_version_id` keys and the `:0` primary baseline; report shows "(deleted)" for purged version ids.
9. **Permissions** — version CRUD gated by catalog module permission; results report by reports permission; store-scoping compliance test for both new models (`StoreOwnedModel` suite).
10. **Pixels** — `view_content` payload on a version page byte-identical to the primary page's (same product/variant/price).

**Gates:** SEO Agent review is **mandatory before release** (canonical/og divergence, sitemap, redirects, robots — the whole point of the feature). Safety Agent review required for: the beacon `properties.page_version_id` ingestion (public input — must be validated/coerced to int server-side before raw-SQL insert), the ATC form's `page_version_id` field (must be validated as an integer belonging to the same store+product, else stored as NULL — never trusted into SQL or FK), and the admin CRUD (standard module-permission audit).

## DECIDED (product-level defaults — approved human, 2026-07-11)

| # | Item | Decision |
|---|---|---|
| P1 | Max page versions per product | 10 |
| P2 | Version permalinks created for **all** languages with an active product permalink (vs per-language opt-in) | all languages, automatic |
| P3 | Activation requires ≥ 1 image in the version's set | yes |
| P4 | Statistical significance display in the results report | none in v1 (counts + CR only); revisit with AF-109 (order-bump split-test reporting) so A/B reporting lands once, consistently |
| P5 | Deactivation redirect semantics | 302 (reversible) vs delete = 301 (permanent) |

All five approved as recommended (human, 2026-07-11). See
`specs/ecommerce_engine/11_uncertainties_to_validate.md` (`## Items from
ADR-028/ADR-029/ADR-030`) and the ADR-028 index row in
`specs/ecommerce_engine/09_architecture_decisions.md`.

## Implementation split (two Developer PRs under TICKET-042)

- **042-A — entity, URLs, rendering, SEO, admin CRUD**: models + migrations, permalink lifecycle signals/service, dispatch branch, `product_page` image swap, canonical/og tag split, sitemap exclude, admin section, tests 1–7, 9 (CRUD part), 10.
- **042-B — measurement + report**: beacon key, `add_item` stamp + order copy, server-side purchase properties, aggregation metric types, results report view + permissions, feed negative-case activation, tests 4, 8, 9 (report part).

042-A is releasable alone (feature works, unmeasured); 042-B has no storefront-SEO surface. Safety gate applies to both (beacon/form input in B, admin in A); SEO gate applies to 042-A.
