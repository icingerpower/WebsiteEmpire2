# RELEASE NOTES — TICKET-024: Multilingual Content Pipeline

**Date:** 2026-07-04
**Ticket:** TICKET-024
**ADRs referenced:** ADR-005, ADR-003

---

## What was delivered

### Data layer

- `ProductTranslation` model: per-product, per-language translated text (title, description, seo_title, seo_description) with `TranslationStatus` (draft / published) lifecycle. Store-scoped via `StoreOwnedModel`.
- `CollectionTranslation` model: same lifecycle as `ProductTranslation`.
- Translated slugs live exclusively in `PermalinkTranslation` (ADR-005 §2) — not duplicated in the translation models.
- Migration: `catalog/migrations/0005_add_product_collection_translations.py`

### Signal-driven permalink sync

- `_sync_translation_permalink` in `catalog/signals.py` runs inside a savepoint (`with transaction.atomic():`). A slug collision on publish rolls back only the savepoint, leaving the outer transaction intact (F2 safety fix, proven by regression test).

### AI job orchestration

- Job type `"translation"` registered in `catalog/ai_jobs.py` with `default_priority=100` — highest-priority queue position per ADR-003.
- `aijobs/registry.py`: `JobTypeDefinition.default_priority: int = 0` field.
- `aijobs/service.py`: `create_job()` resolves `default_priority` from registry when `priority=None` is passed.
- `build_prompt` / `persist_output` raise `NotImplementedError` by design; to be implemented in the AI orchestration phase.

### hreflang infrastructure

- `permalinks/hreflang.py`: `get_hreflang_entries(store, content_model_class, object_id)` — reads only active `PermalinkTranslation` rows; respects enabled languages.
- `permalinks/templatetags/permalinks_tags.py`: `{% hreflang_tags content_object %}` inclusion tag.
- End-to-end HTTP rendering deferred to T029 (requires storefront).

### Admin (stores)

- `StoreLanguageStoreAdminAdmin`: `has_delete_permission` always returns `False` (ML-012 — languages are disable-only); `add_language_view` stub; `translation_coverage` column (N/D P% or —); `target_countries` column; ML-012 warning on `is_enabled` True→False transition.
- `StoreDomainAdmin`: `routing_mode` and `language_count` computed columns.

---

## Acceptance criteria covered

| AC | Description | Status |
|---|---|---|
| AC-100 | ProductTranslation persists per store/product/lang | DONE |
| AC-101 | Publishing creates active permalink | DONE |
| AC-103 | hreflang entries exclude inactive permalinks | DONE |
| AC-104 | Unpublished translation leaves permalink inactive | DONE |
| AC-091 | Translation data is isolated by store | DONE |

---

## Known limitations (tracked, non-blocking)

- `add_language_view` POST does not set the `domain` FK — non-functional stub, must be completed before the add-language flow is used in production. Follow-up ticket required.
- hreflang end-to-end HTTP 404 test deferred to T029.
- AI job `build_prompt` / `persist_output` stubs raise `NotImplementedError` — by design.
