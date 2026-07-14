# RELEASE CHECKLIST — TICKET-024: Multilingual Content Pipeline

**Date:** 2026-07-04
**Validator:** Release Manager Agent
**Verdict:** READY

---

## Gate results

| Gate | Status | Evidence |
|---|---|---|
| Spec approved | PASS | TICKET-024 at line 346 of `specs/ecommerce_engine/10_implementation_tickets.md`; ADR-005 ACCEPTED, ADR-003 ACCEPTED in `specs/ecommerce_engine/09_architecture_decisions.md` |
| Architecture decisions recorded | PASS | ADR-005 (URL resolution / permalink model) and ADR-003 (AI Job System) both ACCEPTED; multilingual domain model recorded as ADR-008 ACCEPTED |
| Implementation complete | PASS | `ProductTranslation`, `CollectionTranslation`, `TranslationStatus` in `catalog/models.py` (lines 300, 305, 365); `_sync_translation_permalink` signal in `catalog/signals.py`; `get_hreflang_entries` in `permalinks/hreflang.py`; `hreflang_tags` template tag in `permalinks/templatetags/permalinks_tags.py`; `StoreLanguageStoreAdminAdmin` with `has_delete_permission=False`, `add_language_view`, `translation_coverage`, `target_countries`, ML-012 warning in `stores/admin.py`; `StoreDomainAdmin` with `routing_mode` and `language_count`; `default_priority=100` in `catalog/ai_jobs.py`; `default_priority: int = 0` field and resolution in `aijobs/registry.py` and `aijobs/service.py` |
| Tests added | PASS | 13 translation tests in `catalog/tests/test_translations.py`; 7 admin tests in `stores/tests/test_admin.py`; 2 priority tests in `aijobs/tests/test_aijobs.py`; F2 regression test `test_signal_slug_collision_does_not_poison_outer_transaction` (line 411, `catalog/tests/test_translations.py`) |
| Tests passing | PASS | 827/827 passed in 38.57s (run confirmed by Release Manager) |
| Coverage checked | PASS | All acceptance criteria AC-100/101/103/104/091 have corresponding tests; priority path tested end-to-end |
| Spec Reviewer approved | PASS | Spec Reviewer 2nd pass APPROVED (with test gaps that Test Agent 2nd pass addressed) |
| Safety Agent approved | PASS | Safety BLOCKED → F1 (admin scoping), F2 (savepoint), F3 (save_formset + clean()) all fixed; F2 PROVEN regression test confirmed |
| SEO Agent approved | N/A | hreflang utility implemented per ADR-005 §11; no storefront rendering in this ticket (T029 scope) |
| Designer approved | N/A | No new public-facing UI in this ticket; admin UI improvements covered by Spec Reviewer |
| Migrations reviewed | PASS | `catalog/migrations/0005_add_product_collection_translations.py` exists; depends on `aijobs 0004`, `catalog 0004`, `stores 0009`; `makemigrations --check` exits clean |
| Settings documented | PASS | No new Django settings introduced; `default_priority=100` is code-level configuration in `catalog/ai_jobs.py` |
| Deployment checklist ready | PASS | See ROLLBACK_PLAN.md — migration apply step is the only deployment action |
| Rollback plan ready | PASS | See ROLLBACK_PLAN.md |
| Known risks listed | PASS | See KNOWN_RISKS.md |
| Human decisions listed | PASS | No new human decisions required; all uncertainties resolved in prior pipeline gates |

---

## Test run evidence

```
827 passed in 38.57s
```

Confirmed by running: `python3 -m pytest -q` with `DJANGO_SETTINGS_MODULE=webecom.settings.development`

## Migration check evidence

```
No changes detected
```

Confirmed by running: `python3 manage.py makemigrations --check`
