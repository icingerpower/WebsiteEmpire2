# T016 — Multilingual Domain Model — Release Notes

## Summary

Introduces the multilingual domain layer for the Pradize ecommerce engine:
platform subdomains, custom domains, per-language configuration, and locale
resolution middleware.

## New models

- `StoreDomain` (`stores/models.py:383`) — maps a hostname (subdomain or custom
  domain) to a store + language. Soft-delete guarded via `store__deleted_at`.
- `StoreLanguage` (`stores/models.py:453`) — per-language settings for a store
  (language code, currency, etc.).
- `ShippingCountry` (`stores/models.py:553`) — shipping destination countries
  per store.

## New logic

- `resolve_locale(host, path)` (`permalinks/resolver.py:136`) — resolves an
  incoming request host + path to a `RequestLocale`. Raises `Http404` for
  unknown hosts. Raises `GoneError` for soft-deleted stores. Filters out
  inactive domains and deleted stores.
- `base_url(language)` (`permalinks/resolver.py:218`) — builds the canonical
  base URL for a language.
- `LocaleMiddleware` (`stores/middleware.py:25`) — Django middleware that runs
  `resolve_locale` on every request. Features:
  - `EXCLUDED_PREFIXES` to bypass locale resolution for admin, API, static, and
    media paths.
  - Bare language-prefix redirect (`/fr` → `/fr/`) using `\Z` regex pattern to
    prevent newline-injection bypasses; query string is preserved on redirect.
  - Sets `request.locale` for downstream use.

## Migrations

- `stores/0008_populate_store_domains` — data migration
- `stores/0009_alter_store_custom_domain_and_more` — help_text updates
- `permalinks/0003_alter_permalink_slug` — slug validator update

## Settings

- `PLATFORM_APEX_DOMAIN` — required environment variable. `base.py` raises
  `ImproperlyConfigured` if unset. Development default (`webecom.local`) set in
  `development.py` before base import.

## Deployment steps

1. Set `PLATFORM_APEX_DOMAIN` environment variable on the production server
   before starting the application.
2. Add `"stores.middleware.LocaleMiddleware"` to `MIDDLEWARE` in production
   settings if not already present.
3. Run `python manage.py migrate` — applies stores/0008, stores/0009,
   permalinks/0003 in dependency order.
4. Verify `python manage.py check` passes.
5. Run smoke test: curl the platform apex domain and a known store subdomain;
   confirm 200 / correct language resolution.

## Test coverage

71 dedicated tests in `stores/tests/test_multilingual_domains.py`.
All 805 tests in the full suite pass.
