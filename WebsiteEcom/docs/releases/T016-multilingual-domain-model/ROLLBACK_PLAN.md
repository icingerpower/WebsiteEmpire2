# T016 — Rollback Plan

## Trigger condition

Roll back if any of the following occur after deploy:
- locale resolution returns wrong language for a known host
- 404 or 500 on previously working store URLs
- Unhandled `GoneError` reaching the user
- `ImproperlyConfigured` on startup (means env var not set)
- Performance regression on request latency due to middleware DB hit

## Rollback steps

### 1. Disable the middleware (immediate, zero-downtime)

Remove `"stores.middleware.LocaleMiddleware"` from `MIDDLEWARE` in the
production settings file and restart the application server. This stops all
locale resolution without touching the database or migrations.

### 2. Revert migrations (if model changes cause breakage)

The three migrations introduced by T016 are reversible:

```
python manage.py migrate permalinks 0002
python manage.py migrate stores 0007
```

This undoes `permalinks/0003`, `stores/0009`, and `stores/0008` in reverse
dependency order. Run `--fake` first in a staging environment to confirm the
reversal is clean before running against production.

### 3. Revert application code

If step 1 and 2 are insufficient, revert the code commits that introduced
T016:

```
git revert <T016-commit-range>
```

Then restart the application server.

### 4. Verify after rollback

- `python manage.py migrate --check` — confirms no pending migrations
- `python manage.py check` — confirms no configuration errors
- Curl the apex domain — confirms the application starts and responds

## Data safety

- `stores/0008` is a data migration populating `StoreDomain` rows. Reverting
  with `migrate stores 0007` will drop these rows. If they were created or
  modified in production after deploy, export them first:
  ```
  python manage.py dumpdata stores.StoreDomain > storedomain_backup.json
  ```
- No user data or order data is touched by T016.
