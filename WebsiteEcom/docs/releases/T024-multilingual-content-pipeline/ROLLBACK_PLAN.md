# ROLLBACK PLAN — TICKET-024: Multilingual Content Pipeline

**Date:** 2026-07-04

---

## Scope

One new migration introduced. No changes to existing tables. No new Django settings. The admin changes in `stores/admin.py` are additive (no schema impact).

---

## Rollback steps

### Step 1 — Revert the migration

```bash
python3 manage.py migrate catalog 0004
```

This drops the `catalog_producttranslation` and `catalog_collectiontranslation` tables.

### Step 2 — Remove the ai_jobs import from apps.py

In `catalog/apps.py`, remove or comment out the import of `catalog.ai_jobs` (the line that registers the `"translation"` job type). This prevents the registry from seeing a job type whose migration has been reverted.

### Step 3 — Deploy the previous code revision

Revert the code to the commit before TICKET-024 implementation. The `StoreLanguageAdmin` and `StoreDomainAdmin` changes in `stores/admin.py` carry no migration; reverting code is sufficient.

---

## Data impact

- Any `ProductTranslation` / `CollectionTranslation` rows written in production are lost on migration revert.
- Any `PermalinkTranslation` rows created by the signal are NOT automatically cleaned up. Run a manual DELETE after rollback if needed:
  ```sql
  DELETE FROM permalinks_permalinktranslation
  WHERE content_type_id IN (
      SELECT id FROM django_content_type
      WHERE app_label = 'catalog' AND model IN ('producttranslation', 'collectiontranslation')
  );
  ```
- `AiJob` rows with `job_type = 'translation'` remain in the database but will no longer resolve against a registered type. They are inert and can be cleaned up manually.

---

## Verification after rollback

```bash
python3 manage.py migrate --list | grep catalog
python3 -m pytest catalog/ stores/ aijobs/ -q
```

Both must exit clean.
