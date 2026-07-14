# KNOWN RISKS — TICKET-024: Multilingual Content Pipeline

**Date:** 2026-07-04

---

## Non-blocking tracked limitations

### R1 — add_language_view POST does not set domain FK (HIGH FOLLOW-UP PRIORITY)

`StoreLanguageStoreAdminAdmin.add_language_view` is a stub. The POST path does not set the `domain` FK on `StoreLanguage`. If an operator uses this view in production before a follow-up ticket completes it, a `StoreLanguage` row will be created without a domain assignment, which will cause `resolve_locale` to fail for that language.

**Mitigation:** The view must not be linked to from the production admin until the follow-up ticket is implemented and reviewed. Accepted as a known non-functional stub.

### R2 — hreflang not tested end-to-end over HTTP (LOW)

`get_hreflang_entries` and the `{% hreflang_tags %}` template tag are unit-tested in isolation. End-to-end HTTP rendering (confirming the `<link rel="alternate" hreflang="...">` tags appear in a live storefront response) is deferred to T029, which requires storefront page rendering. Until T029 ships, hreflang correctness is test-only, not production-verified.

**Mitigation:** Unit test `test_ac103_hreflang_entries_has_no_inactive_permalinks` and `test_get_hreflang_entries_returns_active_permalinks_only` cover the data-layer correctness. T029 is in the approved ticket backlog.

### R3 — AI job build_prompt / persist_output raise NotImplementedError (KNOWN BY DESIGN)

The `"translation"` job type is registered and priority-tested but its `build_prompt` and `persist_output` methods are not implemented. Any code path that enqueues and executes a translation job will raise `NotImplementedError` at runtime.

**Mitigation:** No code path currently calls `build_prompt` or `persist_output` outside of the AI orchestration phase. This is intentional per the phased delivery plan (AI orchestration phase follows T024).

### R4 — Slug collision on publish leaves translation in draft (LOW)

If a translated slug collides with an existing permalink at publish time, the savepoint rolls back and the `ProductTranslation` remains in `DRAFT` status silently (no error surfaced to the admin user in the current implementation).

**Mitigation:** The outer transaction is never poisoned (proven by regression test `test_signal_slug_collision_does_not_poison_outer_transaction`). The silent failure path should be addressed in a UX follow-up that surfaces a conflict error to the operator. Tracked, not blocking.

---

## Human decisions made (no open items)

All uncertainties for TICKET-024 were resolved in prior gates. No open items remain in `specs/ecommerce_engine/11_uncertainties_to_validate.md` specific to this ticket.
