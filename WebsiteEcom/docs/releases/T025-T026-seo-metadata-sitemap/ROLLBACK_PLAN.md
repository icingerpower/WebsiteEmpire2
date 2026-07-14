# Rollback Plan — TICKET-025 + TICKET-026

## Deployment checklist

1. Confirm all 208 tests pass on the target environment before deploying.
2. Deploy `sitemaps/` app: add `"sitemaps"` to `INSTALLED_APPS` in settings.
3. Wire sitemap and robots.txt URLs in the root `urls.py` (or per-store URL conf).
4. Run `python manage.py check` — confirm 0 issues.
5. No `migrate` required (sitemaps app has no models).
6. Configure Celery beat to run the `generate_sitemaps` task on the desired schedule.
7. In production: configure Redis cache backend so `@cache_page(60*60)` on
   `sitemap_language_view` uses a shared cache (not the per-process LocMemCache).
8. Smoke-test `/robots.txt`, `/sitemap.xml`, `/sitemap-<default_lang>.xml` for each
   deployed store domain.
9. Verify a thin collection (< 3 active products) returns `noindex` in the rendered
   `<meta>` tag and is absent from `/sitemap-<lang>.xml`.
10. Verify a product page with multiple ShippingCountry-configured languages returns
    correct `<link rel="alternate" hreflang="...">` tags matching the sitemap alternates.

---

## Rollback procedure

T025 and T026 are pure read-path additions — no schema changes, no data migrations,
no destructive writes. Rollback is low-risk.

### If rollback is needed after deployment

1. Revert the git commit(s) covering T025/T026 changes.
   ```
   git revert <commit-hash> --no-edit
   ```
2. Remove `"sitemaps"` from `INSTALLED_APPS`.
3. Remove sitemap/robots.txt URL routes from `urls.py`.
4. Redeploy application.
5. Flush the cache (Redis FLUSHDB or invalidate the sitemap keys) so stale
   cached sitemap XML is cleared.
6. No `migrate --reverse` required (no migrations were added).

### Impact of rollback

- `/robots.txt`, `/sitemap.xml`, `/sitemap-<lang>.xml` return 404 until the sitemaps
  app is re-added or replaced.
- noindex meta tag disappears from thin collection pages — thin collections become
  crawlable again. This is not a data-loss risk; it is an indexing quality regression
  that will self-correct when the feature is re-deployed.
- hreflang tags revert to the pre-T025 behavior (no ShippingCountry gating, no
  single-member guard) — minor SEO regression, not a data risk.

### Partial rollback (T026 only, keep T025)

Possible: revert only the `sitemaps/` app addition. T025 changes
(`catalog/templatetags/catalog_tags.py`, `permalinks/hreflang.py`,
`permalinks/templatetags/permalinks_tags.py`) are independent and can remain deployed.
