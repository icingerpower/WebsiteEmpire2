# Known Risks — TICKET-025 + TICKET-026

## Risk 1 — Sitemap cache on LocMemCache in development / single-process production

Severity: LOW (development only) / MEDIUM (production misconfiguration)

`@cache_page(60*60)` on `sitemap_language_view` uses Django's configured cache
backend. In development (LocMemCache) this is per-process and non-shared — each
Gunicorn worker caches independently. In production with Redis this is not an issue.

Mitigation: Code comment in `sitemaps/views.py` explicitly states "production should
use Redis." Deployment checklist step 7 covers this. Risk is operational, not a
code defect.

---

## Risk 2 — Sitemap scale (50k URL limit not enforced)

Severity: LOW (current store sizes) / MEDIUM (future large catalogs)

Django's built-in Sitemap framework does not paginate. The pre-generation Celery task
in `tasks.py` documents the upgrade path (write paginated XML files to disk / S3 and
serve statically). There is no enforcement today.

Mitigation: Upgrade path documented in `sitemaps/tasks.py`. Risk surfaces only when
a store exceeds ~50k indexable permalinks.

---

## Risk 3 — lastmod uses created_at (never updated on edit)

Severity: LOW

`Permalink.created_at` is used as `lastmod` in the sitemap. If a product or collection
page content changes significantly, `lastmod` does not update, so crawlers may
de-prioritize re-crawling.

Mitigation: Tracked. A future task can add `updated_at` to Permalink and use it as
`lastmod`. No indexing correctness risk; only crawl efficiency impact.

---

## Risk 4 — Disallow /superadmin/ publicly signals admin path

Severity: LOW

robots.txt disallows `/superadmin/` explicitly. This confirms to any reader that
the superadmin path is `/superadmin/`. The Django admin is already protected by
authentication; this is a minor information disclosure with no exploitability.

Mitigation: Accepted. The alternative (omitting from robots.txt) may allow crawlers
to index superadmin login pages. Disallowing is the correct trade-off.

---

## Risk 5 — ROB-002 custom robots.txt text not yet wired

Severity: LOW

The `robots_txt_view` contains a TODO comment for reading `store_settings.robots_txt_custom`
once T007 (general settings model) is implemented. Until then, no custom lines can be
appended per store.

Mitigation: Tracked as deferred to T007. Current behavior is correct for all stores
with no custom robots.txt requirements.

---

## Risk 6 — A/B variant URLs appear in sitemap if A/B variant content type is added later

Severity: LOW (future)

The spec (FM-C4) excludes A/B variant URLs from the sitemap. The current implementation
has no A/B variant content type to exclude. When T029 introduces A/B variants,
`PermalinkSitemap.get_urls()` must be updated to filter them out.

Mitigation: Tracked as deferred to T029. No risk in current state.

---

## Risk 7 — N+1 queries if ShippingCountry filter is not batched correctly

Severity: LOW (addressed)

Safety Agent flagged N+1 risk on ShippingCountry intersection. The implementation uses
a single `StoreLanguage.objects.filter(shipping_countries__isnull=False).distinct()`
query to compute `configured_lang_codes` — not a per-language loop.

Mitigation: Implemented correctly. Verified in `sitemaps/sitemaps.py` lines 121-125
and `permalinks/hreflang.py` lines 78-82.

---

## Non-risks (confirmed resolved)

- Cross-store cache collision: cache key includes `store.pk` — resolved.
- Single-member hreflang (CAN-007): `len(entries) < 2` guard — resolved.
- Thin collection in sitemap: `configured_lang_codes` + collection_object_ids
  annotation — resolved.
- x-default on unconfigured language: gated on `configured_lang_codes` — resolved.
