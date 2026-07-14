# Release Notes — TICKET-025 + TICKET-026

## SEO Metadata Layer and Sitemap / robots.txt

Release date: 2026-07-04
Apps affected: `catalog`, `permalinks`, `sitemaps`

---

## TICKET-025 — SEO Metadata Layer

### What ships

**Thin-collection noindex (AC-092)**
Collections with fewer than 3 active products receive `<meta name="robots"
content="noindex">`. The threshold constant `COLLECTION_INDEXABLE_THRESHOLD = 3` is
defined in `catalog/templatetags/catalog_tags.py`. The same predicate gates collection
URLs in the sitemap (SM-030).

**robots meta tag (AC-076)**
A `{% noindex_if_thin collection %}` template tag renders the noindex meta when the
collection is below threshold. The tag outputs noindex-only (no nofollow) so link
equity still flows through thin-collection pages.

**hreflang suppressed for single-language stores (CAN-007)**
The `{% hreflang_tags %}` template tag returns an empty render when fewer than 2
qualifying alternates are present. This prevents malformed hreflang sets on
single-language stores or stores with only one ShippingCountry-configured language.

**ShippingCountry intersection in page-level hreflang (CAN-004(b))**
`permalinks/hreflang.py` restricts hreflang alternates to StoreLanguages that have at
least one ShippingCountry row. Languages not yet configured for shipping are excluded.
Cache key includes `store.pk` to prevent cross-store collision.

---

## TICKET-026 — Sitemap + robots.txt

### What ships

**`sitemaps` Django app** (new app, no migrations)

- `PermalinkSitemap` — generates hreflang-aware `<url>` blocks for all active
  non-redirected permalinks. Applies `configured_lang_codes` (StoreLanguages with at
  least one ShippingCountry) to both `lang_to_sl` alternates and the x-default
  selection (CAN-004(b), CAN-006). Excludes thin collections (SM-030).
- `sitemap_index_view` (`/sitemap.xml`) — lists one `<sitemap>` per enabled language.
- `sitemap_language_view` (`/sitemap-<lang>.xml`) — per-language sitemap XML, cached
  1 hour via `@cache_page(60*60)`.
- `robots_txt_view` (`/robots.txt`) — generated per domain; disallows cart, checkout,
  orders, search, admin, superadmin, feeds; lists all sitemap URLs.
- `generate_sitemaps` Celery beat task — pre-generates sitemap XML to warm the cache
  on a schedule. Upgrade path to sitemap file pre-generation documented for > 50k URLs.

### Test count

49 tests in `sitemaps/tests/`, covering PermalinkSitemap (product presence,
collection threshold, ShippingCountry hreflang filter, x-default), sitemap index,
per-language view caching, robots.txt content, Celery task registration.

---

## Bug fixes included

Both bugs were discovered during pipeline review and have PROVEN regression tests in
`BUG_TESTS/BUG_TESTS.csv`:

- **T025-thin-collection-sitemap**: PermalinkSitemap was including all active
  collection permalinks regardless of product count. Fix: `get_urls()` annotates
  active product counts and excludes collections below threshold.
- **T026-hreflang-shipping-country**: sitemap hreflang alternates were built from all
  enabled StoreLanguages without ShippingCountry intersection. Fix: `configured_lang_codes`
  set gates both `lang_to_sl` and x-default.

---

## Deferred items (tracked, non-blocking)

| Item | Deferred to |
|---|---|
| Per-store noindex threshold configurability | T007 (general settings model) |
| ROB-002 custom robots.txt text | T007 |
| 0-product collection 404 at view layer | T017 (storefront theme) |
| A/B variant exclusion from sitemap | T029 (when A/B variant content type exists) |
| Disallow /superadmin/ (Safety note) | Already implemented in this release |
| Sitemap pagination (50k URL limit) | Pre-generation upgrade path noted in tasks.py |
| lastmod uses created_at (minor) | Tracked; low priority |

---

## Files changed

```
catalog/templatetags/catalog_tags.py           — COLLECTION_INDEXABLE_THRESHOLD, is_collection_indexable, noindex tag
catalog/tests/                                 — threshold and noindex tests
permalinks/hreflang.py                         — ShippingCountry filter, store.pk cache key
permalinks/templatetags/permalinks_tags.py     — CAN-007 len(entries) < 2 guard
permalinks/tests/                              — hreflang guard tests
sitemaps/                                      — new app (apps.py, sitemaps.py, views.py, tasks.py, urls.py, templates/, tests/)
specs/ecommerce_engine/11_uncertainties_to_validate.md  — per-store threshold deferral recorded
BUG_TESTS/BUG_TESTS.csv                       — T025 and T026 PROVEN entries added
```
