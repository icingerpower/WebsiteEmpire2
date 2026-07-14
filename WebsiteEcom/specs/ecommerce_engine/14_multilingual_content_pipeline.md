# 14_multilingual_content_pipeline

> Product specification for **TICKET-024: Multilingual Content Pipeline** — what gets
> translated, the per-content-type translation record pattern, the AI-job-driven
> translation workflow, publication gating, URL/hreflang behavior, and the admin UX.

**Status: DRAFT v1 (2026-07-05) — Spec Agent.**
Requirement IDs: `MCP-*` (Multilingual Content Pipeline). Requirements are binding
unless marked `PENDING APPROVAL` or `ASSUMPTION`.

**Relationship to other documents.** This spec *specializes* `07_multilingual_seo.md`
(the binding multilingual/SEO requirements, `ML-*`/`SLUG-*`/`CAN-*`) for the content
pipeline itself. Where 07 and this document overlap, 07 remains authoritative for SEO
behavior; this document is authoritative for the translation data model, job workflow,
and admin UX. Architecture references: **ADR-005** (permalink model — translated slugs),
**ADR-008** (StoreDomain / StoreLanguage / ShippingCountry / `resolve_locale`),
**ADR-003** (AI job system), ADR-009 Appendix B (CampaignStepTranslation — the approved
reference implementation of the pattern).

**Implementation status.** T024 was partially released 2026-07-04
(`docs/releases/T024-multilingual-content-pipeline/RELEASE_NOTES.md`):
`ProductTranslation`, `CollectionTranslation`, the translation→permalink signal,
the `translation` job type registration, hreflang helpers, and store-admin language
columns exist. §10 lists spec-vs-implementation gaps found during this spec pass —
two of them are correctness divergences, not just missing work.

---

## 1. Content inventory — what gets translated

This table specializes `07 §1.5` for the pipeline. "Record" names the translation
storage described in §2.

### 1.1 Translated (customer-visible text)

| Content | Fields translated | Record | Status |
|---|---|---|---|
| **Product** | `title`, `description`, `seo_title`, `seo_description` | `ProductTranslation` (catalog) | Implemented |
| **Product slug** | translated URL slug | `Permalink` row per (store, lang, slug) — **never** in `ProductTranslation` (ADR-005 §2, MCP-011) | Partially implemented (see §10-G1) |
| **Collection** | `title`, `description`, `seo_title`, `seo_description` | `CollectionTranslation` (catalog) | Implemented |
| **Collection slug** | translated URL slug | `Permalink` | Partially implemented |
| **Product image alt text** | `alt_text` per language | `ProductImageTranslation` — NEW (§2.4) | Not implemented |
| **Variant option names/values** | option names ("Color") and values ("Red") | `VariantOptionTranslation` — NEW (§2.4); scope **PENDING APPROVAL** (§8-P2) | Not implemented |
| **Description tab names** | tab labels | part of product description structure — follows the product record | Not implemented (tabs not yet modeled) |
| **CampaignStep** | `title`, `description`, `cta_label` | `CampaignStepTranslation` (campaigns) — **reference implementation**, approved 2026-07-04 (ADR-009 App. B) | Implemented (T027) |
| **Static pages** (FAQ, contact, policies) | title, body, SEO fields, slug | `StaticPageTranslation` — NEW, same pattern; ships with the static-pages ticket, not T024 | Deferred to static-pages area |
| **Menu labels** | nav item labels | keyed by stable menu-item PK (07 §1.5, §VII) | Deferred to menu/storefront area |
| **Tag display labels / product-type display names** | displayed label only; identity (tag id, internal type id) untranslated | deferred vocabulary-translation record | Deferred (v2) — **ASSUMPTION (LOW)**: launch languages can live with untranslated tag chips; smart-collection matching uses untranslated ids so correctness is unaffected |
| **Email templates** (transactional) | per-language template variants | T022's multilingual template model — out of this spec's scope, but MUST follow §2's pattern and §4's job workflow | Deferred to T022 follow-up |

### 1.2 NOT translated (identity, money, operations)

- Prices, `compare_at_price`, currency — currency module concern, never translation.
- SKU, vendor string as identity, internal product-type id, tag ids.
- Inventory data (modes, quantities, presale dates).
- Images (binaries are language-agnostic; only `alt_text` is per-language).
- Coupon / gift-card codes.
- Order data, customer data, review content (07 §1.5 — reviews shown in original
  language; machine-translation toggle remains PENDING there).
- `Permalink` `lang` codes, `StoreLanguage.lang_code` — identity.

**MCP-001** — The source language of every translation is always the store's default
language (`StoreLanguage.is_default=True`; legacy mirror `Store.primary_language`).
The base model row (e.g. `Product.title`) *is* the default-language content. There is
no translation record for the default language.

**MCP-002 (restates ML-020, binding)** — No silent fallback for indexable text: a
missing/unpublished translation means the page does not exist in that language (404),
never a mixed-language page.

---

## 2. Translation record pattern

### 2.1 Canonical shape (as implemented — this is now the binding pattern)

Every translatable content type gets its own translation model, mirroring
`CampaignStepTranslation` / `ProductTranslation`:

```
<Type>Translation(StoreOwnedModel):
    <parent FK>          — CASCADE, related_name="translations"
    lang_code            — CharField(10), ISO 639-1 (optionally region: 'pt-br')
    <translated fields>  — one column per translated source field, blank=True
    status               — draft | published (TranslationStatus)
    ai_job               — FK aijobs.AiJob, null, SET_NULL
    created_at / updated_at
    unique: (store, <parent>, lang_code)
    clean(): store must equal parent's store (cross-tenant guard + test)
```

**MCP-010** — One translation model per content type; no generic
EAV/"translated_fields JSON" table. Field-per-column keeps validation (§4.4),
admin forms, and queries trivial. Translated and source content are never mixed in
one row (ML-040, design-pattern-ideas §I).

**MCP-011 (binding — ADR-005 §2)** — Translated **slugs never live in translation
models**. The `Permalink` table (one row per `(store, lang, slug)`) is the sole home
of translated slugs. Rationale: one URL-resolution path (§III), slug-change redirects
handled once in the permalink layer for all content types.

**MCP-012** — `status` is a two-state content enum: `draft` / `published`.
Reconciliation with **ML-041** (which lists `pending / in_progress / published /
error`): the in-flight and error states live on the **AiJob** (`AiJobStatus`:
not_started / in_progress / done / error / skipped), linked via the `ai_job` FK —
the translation *record* only exists once there is content, and it is either visible
(`published`) or not (`draft`). "Absence of a record" is still not a state the
storefront interprets: routing is gated purely on `published` + active `Permalink`.
The admin coverage view (§6) unions record status with pending-job status so the
human sees pending/error, satisfying ML-041's intent. **ASSUMPTION (LOW)** — this
split is what is implemented and released; flagging rather than reopening.

**MCP-013** — `ai_job` is `SET_NULL`: deleting an AiJob never deletes content.
A manually-created translation has `ai_job = NULL` from the start.

**MCP-014** — Only `status='published'` translations are: routable (active
`Permalink`), rendered, sitemapped, hreflang'ed, or included in feeds (ML-041,
ML-011). Reverting `published → draft` deactivates the Permalink → the translated
URL 404s (AC-104). This lifecycle is signal-driven (`catalog/signals.py`
`_sync_translation_permalink`), inside a savepoint so slug collisions never abort
the outer transaction.

### 2.2 Publication → Permalink lifecycle (as implemented, binding)

On `status` transition **to** `published`:
1. If a `Permalink` (active or inactive) exists for `(store, lang_code,
   content_type, object_id)` → reactivate it.
2. Else create one, `auto_created=True`, with a default slug (see §3), `is_active=True`.
3. On slug collision: log a warning, leave no permalink — admin must set the slug
   manually. **MCP-015** — this failure MUST also surface in the admin coverage UI
   (§6) as "published, no URL" — a log line alone is the §XV-1 invisible-failure
   class. (Gap §10-G3.)

On transition **from** `published`: deactivate the active Permalink (URL → 404).
Redirect rows are untouched.

### 2.3 Existing implementations (reference)

| Model | App | Translated fields | Notes |
|---|---|---|---|
| `CampaignStepTranslation` | campaigns | title, description, cta_label | Reference implementation; trigger signal in `campaigns/signals.py` is the canonical trigger pattern (§4.2) |
| `ProductTranslation` | catalog | title, description, seo_title, seo_description | Permalink lifecycle wired |
| `CollectionTranslation` | catalog | title, description, seo_title, seo_description | Permalink lifecycle wired |

### 2.4 New records required by this spec (remaining T024 scope)

- **MCP-020 — `ProductImageTranslation`**: `(store, image, lang_code)` unique;
  field `alt_text`. Same status/ai_job columns. No Permalink interaction (alt text
  has no URL). Published gating still applies to rendering (draft alt text renders
  as empty string, never as default-language text on a translated page —
  **ASSUMPTION (LOW)**: empty alt is less harmful than mixed-language alt; revisit
  if SEO Agent objects).
- **MCP-021 — `VariantOptionTranslation`**: keyed by stable option identity, not
  display value or position (§VII). Because variant options currently live in
  `ProductVariant.option_values_json` (no stable option-value PK), **the Architect
  must first give option names/values stable identity** before this record can exist.
  Scope question in §8-P2. **This is the one place the current schema blocks the
  pattern** — flagged to Architect, do not improvise a JSON-keyed translation.

---

## 3. Slugs for translated content

- **MCP-030** — Translated slugs obey all of 07 §3: single shared slugify (NFD,
  SLUG-001), per-(store, language) uniqueness in the flat namespace (SLUG-020),
  collision suffixing `-2, -3` (SLUG-021), auto-redirect on change (SLUG-030),
  ISO-lang-code reserved slugs (ADR-008).
- **MCP-031** — Default slug at auto-publish: slugified **translated title**
  (produced by the translation job's `slug` output field, §4.3). Fallback when the
  job provided no slug: slugified translated title from the record; last resort the
  default-language slug (URL still resolves — a duplicated slug string across
  languages is legal per SLUG-020).
- **MCP-032 (correctness, restates URL patterns of 07 §2)** — Product permalinks
  live at the language root: slug `=` `<product-slug>` (NOT `products/<slug>`);
  collections under `collections/<slug>`. **The current signal writes
  `products/{slug}` — divergence §10-G1, must be fixed before storefront launch.**
- **MCP-033** — A `slug_only` translation sub-job (§4.3) can (re)generate the
  translated slug without touching text fields — used when the admin edits the
  default-language slug or when the auto-created slug collided.

---

## 4. Translation job workflow

### 4.1 Non-negotiable execution rule

**MCP-040 (binding, user-mandated)** — Every customer-visible translation is produced
by the **AiJob CLI runner system** (ADR-003 §7 runners: `claude_code` / `codex` /
`gemini_terminal`; `api` is a fallback runner type inside the same job system).
**No code path may call an LLM API directly to translate content.** The only job
creation path is `aijobs.service.create_job()`.

- Job types: `translation` (catalog, registered in `catalog/ai_jobs.py`) and
  `campaign_step_translation` (campaigns). Both `default_priority=100` — highest
  priority in the queue (ADR-003 §1); `create_job(priority=None)` resolves this from
  the registry.
- `requires_human_review=False` — translations auto-publish by default (extra-spec:
  "Published automatically (example: translation jobs)"). Store-level opt-in to
  human review is a settings toggle — **ASSUMPTION (LOW)**, default off.
- **MCP-041** — The job processor is a management command run by the CLI runner
  loop. **As of this writing no `run_ai_jobs` command exists** (only
  `reclaim_stale_ai_runs`); `build_prompt`/`persist_output` for both translation job
  types are `NotImplementedError` stubs by design. Completing runner + persist is
  the core remaining T024 implementation work (§10-G2). The command name
  `run_ai_jobs` is the working name — final name owned by the AI-orchestration
  implementation.

### 4.2 Triggers (when jobs are created)

The canonical trigger is `campaigns/signals.py::_trigger_translation_jobs_on_step_save`.
Catalog MUST mirror it:

- **MCP-050** — `post_save` on Product / Collection: when the object has translatable
  content **and is published** (Product `status='active'`; Collection
  `is_published=True`), create one `translation` AiJob per configured `StoreLanguage`
  of the store, **excluding** the store default language, for every language that
  does not yet have a `published` translation record. Draft products get no jobs
  (translating unpublished content wastes budget). **ASSUMPTION (LOW)** on the
  published-only gate — the campaign trigger fires regardless of campaign status;
  products differ because drafts are routine.
- **MCP-051 — flood guard (as implemented for campaigns, binding)**: skip creating a
  job when a NOT_STARTED / IN_PROGRESS job already exists for the same
  `(job_type, target_model, target_id, lang)`.
- **MCP-052 — retranslation on source update**: when a *published* object's
  translatable source fields change, existing `published` translations are stale.
  Behavior interacts with manual overrides — see §8-P3 (PENDING APPROVAL). Until
  decided: source-field changes create new translation jobs for all configured
  languages (the AC-U5 idempotency guard is the job-level SKIPPED check), and
  re-translation **overwrites** prior AI-produced content but **never** manually
  edited records (detected per §6.3).
- **MCP-053** — Adding a `StoreLanguage` (ML-010 modal) offers "queue translation
  jobs now" for the whole catalog (per ADR-008 §3). Jobs are created in bulk with
  `created_by='human'`.
- **MCP-054** — Manual triggers from admin (§6): per-object per-language
  "Translate", per-object "Re-translate all", list-level bulk "Translate all
  missing". All go through `create_job()` with `created_by='human'`.
- **MCP-055** — Every job creation respects `StoreAiQuota` (check_quota) — quota
  exhaustion surfaces as a visible admin error, never silent non-creation (§XV-1).

### 4.3 Sub-job types (per TICKET-024 description)

| Sub-type | Input | Output fields (`AiJobOutput.field_id`) |
|---|---|---|
| `full` | all translatable source fields + context | `title`, `description`, `seo_title`, `seo_description`, `slug` |
| `slug_only` | translated title (existing record) | `slug` |
| chunked `full` | long descriptions split proactively (ADR-003 §5, `chunk_index`/`total_chunks`) | `description` chunks recombined; AC-221: a failed chunk B never destroys persisted chunk A |

**MCP-060** — The job `input_payload` carries: source text per field, source and
target lang codes, store context (store name for META formulas), and the length
limits of META-003 (60/160) as instructions. Over-length output is accepted with a
WARNING-severity `AiJobValidation` row (META-003).

### 4.4 Validation and persistence (ADR-003 §6 + TICKET-015 checks)

- **MCP-061** — `persist_output` writes/updates the translation record
  (update-or-create on `(store, parent, lang_code)`), sets `ai_job`, and sets
  `status='published'` (auto-publish) — which fires the Permalink lifecycle (§2.2).
  Persist happens ONLY after all ERROR-severity validation checks pass.
- **MCP-062** — Minimum ERROR checks for translation output: field-id completeness
  for the sub-type; non-empty `title`; length-ratio sanity vs source; bracket/tag
  balance for rich description content. WARNING checks: META length limits;
  **language-identity check** (output actually in target language) — mechanism is
  open item **AC-U5 (MEDIUM, Architect)**; until it exists, record the absence: no
  check row is written, not a fake pass.
- **MCP-063** — Failed validation ⇒ run ERROR, no record written/modified (AC-220);
  retries per `max_retries`; heartbeat-reclaim per ADR-003 §3.

---

## 5. URL routing, canonical, hreflang (delegation)

Routing is **DECIDED — ADR-008 (ACCEPTED 2026-07-03)**, not reopened here:

- Language detection = `resolve_locale(host, path)` — dedicated domain
  (`pradize.fr` = FR) **and/or** path prefix (`pradize.com/fr/`) per `StoreLanguage
  (domain FK + use_path_prefix)`. Both coexist in one store. No country in URLs.
  Subdomains are just hostnames (`StoreDomain` rows) — there is no separate
  "subdomain mode".
- Canonical per language: self-referencing absolute URL composed by
  `base_url(store_language)` + Permalink slug — single composition point (CAN-003,
  ADR-008 §2b).
- hreflang: generated from the same resolver map, only for `published` translations
  ∩ target countries ∩ indexable (CAN-004/005); `x-default` = store default language
  (CAN-006). Emission block ownership and quality gates = **T025**
  (`07 §4`); this pipeline's obligation is only: publication state must be readable
  by the resolver map (done via `Permalink.is_active`), and
  `permalinks/hreflang.py::get_hreflang_entries` reads active permalinks of enabled
  languages only (implemented).
- Unpublished translation ⇒ 404 (AC-104); disabled language ⇒ 410 whole namespace
  (ML-012, ADR-008 §2a-5).

**MCP-070** — This spec adds no routing behavior. Any pipeline code needing a URL
calls the `PermalinkResolver` / `base_url` — never composes URLs (URL-003).

---

## 6. Admin UX

### 6.1 Product / Collection admin — Translations tab

**MCP-080** — Each Product and Collection admin page gets a **Translations** section
listing one row per configured non-default `StoreLanguage`:

| Column | Content |
|---|---|
| Language | code + display name |
| Status | `missing` (no record, no pending job) / `queued` / `in progress` / `error` (from linked AiJob) / `draft` / `published` (record status) — the union view of MCP-012 |
| URL | translated permalink (link) or "no URL — slug collision" warning (MCP-015) |
| Manually edited | flag per §6.3 |
| Last updated | record `updated_at` |
| Actions | "Translate" (missing/error) / "Re-translate" (existing) / "Edit" (opens the translation form) / "Publish"/"Unpublish" |

- **MCP-081** — "Re-translate all" button on the object level: queues `full` jobs for
  every configured language (subject to §6.3 override protection).
- **MCP-082** — The translation edit form shows source (default-language) text
  read-only beside each translated field, with META-003 length counters.

### 6.2 Product list — bulk action

**MCP-083** — Product changelist action **"Translate all missing"**: for the selected
products (or filtered set), create jobs for every (product, language) with no
published record and no non-terminal job (MCP-051 guard applies). Confirmation
screen shows job count and estimated cost (from `estimate_tokens` × model pricing)
before queuing — **ASSUMPTION (LOW)** on showing cost estimate.

### 6.3 Manual override protection

**MCP-084** — When a store admin edits any field of a translation record through the
admin form, the record is marked **manually overridden** (mechanism: a
`manually_edited_at` timestamp column, set when the editor is a human and the change
did not come from `persist_output`; NULL = pure AI content). Overridden records are
**skipped by automatic re-translation** (MCP-052) — behavior on *explicit* human
"Re-translate" is §8-P3 **PENDING APPROVAL**. Field-level (vs record-level)
granularity was considered and rejected for v1 (complexity; a translator editing one
field has effectively taken ownership of the record) — **ASSUMPTION (MEDIUM)**,
revisit if stores report lost partial edits.

### 6.4 Settings — language & coverage (ADR-008 §3, in T024 scope)

Implemented per release notes; requirements restated for QA:

- Languages table with translation-coverage column (`N/D published, P%` reading the
  translation records) — the column that makes ML-011 visible.
- `StoreLanguage` disable-only in store admin (no delete); ML-012 warning on disable.
- Add Language modal (ML-010: code, domain/prefix choice, target countries) —
  **known-broken stub** (§10-G4): POST does not set the `domain` FK. Must be
  completed before the flow is exposed.

---

## 7. Acceptance criteria

Existing (08 + release notes): AC-100 (translated slug/title/meta render), AC-101
(publish creates active permalink), AC-102 (menus resolve current-language URLs —
storefront/T029), AC-104 (unpublished ⇒ 404), AC-091 (hreflang ∩ countries; also
store isolation of translation data), AC-220 (failed validation ⇒ ERROR, nothing
saved), AC-221 (chunk B failure preserves chunk A), AC-U5 (language-identity check
— open).

New criteria from this spec:

- **AC-MCP-01** — Saving an active Product in a store with default EN + configured FR,
  DE creates exactly 2 `translation` AiJobs (fr, de), priority 100; saving again
  before they finish creates none (MCP-051).
- **AC-MCP-02** — A draft Product save creates no translation jobs (MCP-050).
- **AC-MCP-03** — `persist_output` on a `full` job creates/updates the
  ProductTranslation, links `ai_job`, publishes it, and an active Permalink exists at
  the **root-level** translated slug (`/<fr-slug>/`, not `/products/...`) (MCP-032,
  MCP-061).
- **AC-MCP-04** — Deleting the AiJob leaves the translation intact, `ai_job=NULL`
  (MCP-013).
- **AC-MCP-05** — A manually edited FR translation is not modified by a subsequent
  automatic source-update retranslation pass; the DE (untouched) translation is
  (MCP-052/084).
- **AC-MCP-06** — Slug collision at auto-publish: record is published, no permalink,
  admin Translations tab shows "no URL" warning (MCP-015).
- **AC-MCP-07** — No module outside `aijobs` imports an LLM SDK/HTTP client for
  translation; grep-level check that translation content writes happen only in
  `persist_output` or admin forms (MCP-040).
- **AC-MCP-08** — Quota exhausted ⇒ manual "Translate" surfaces a visible error
  message; no job silently dropped (MCP-055).

---

## 8. Decisions

### DECIDED (not reopened)

- **D1 — URL scheme for multilingual**: **DECIDED by ADR-008 (ACCEPTED
  2026-07-03)** — per-language choice of dedicated domain or path prefix on a shared
  domain; both coexist per store; no country in URLs; single `resolve_locale`.
  *The orchestrator asked to mark this PENDING APPROVAL; it is recorded here as
  already decided by an accepted ADR — reopening requires a human decision to
  supersede ADR-008, which this spec does not do.*
- **D2 — Translation execution path**: AiJob CLI runner only, `default_priority=100`,
  auto-publish. User-mandated; implemented pattern approved 2026-07-04 (ADR-009 App. B).
- **D3 — Translated slugs live in the permalink table**, never in translation
  records (ADR-005 §2).

### PENDING APPROVAL

- **P2 — Variant option translation scope.** 07 §1.5 already commits to translating
  option names *and* values, keyed by stable PK; the blocker is that options are
  currently JSON without stable identity (MCP-021). Options:
  1. **(recommended)** Architect introduces stable option/option-value entities;
     translate **both names and values** (per 07 §1.5) — names like "Color" are
     high-visibility on product pages and cart lines; a translated value under an
     untranslated name looks broken.
  2. Translate values only, render option names from a fixed per-language platform
     glossary (cheap, but wrong for custom option names like "Frame finish").
  3. Defer all variant option translation to v2 (violates 07 §1.5 — needs explicit
     human approval to descope).
- **P3 — Manual override vs re-translation.** When a record is manually edited
  (MCP-084) and the admin clicks "Re-translate" / "Re-translate all" explicitly:
  1. **(recommended)** Warn per overridden record ("manually edited on <date> —
     overwrite?") with per-record opt-in; bulk/automatic passes always skip.
  2. Skip silently always (protects edits, but admins cannot easily refresh a stale
     manual edit).
  3. Overwrite on explicit action without warning (data loss risk — rejected).
- **P4 — Retranslation trigger granularity (MCP-052).** When source fields change:
  1. **(recommended)** Re-queue automatically for all languages (simple, matches
     "translations are cheap and highest-priority"), skipping overridden records.
  2. Mark translations `stale` (new flag) and let the admin trigger re-translation
     from the coverage view (more control, more UI).

---

## 9. Out of scope (recorded in 12_out_of_scope.md where durable)

- Supplier database import/migration (global exclusion).
- Review content translation (07 §1.5 PENDING — legal/UX).
- Static-page translation records (deferred to the static-pages ticket; must follow §2).
- Transactional-email multilingual templates (T022 follow-up; must follow §2/§4).
- Storefront rendering of translated pages end-to-end (T029) and hreflang HTTP tests
  (deferred per release notes).
- Non-Latin-script slug strategy (SLUG-003, PENDING in 07).

---

## 10. Spec-vs-implementation gaps (QA input — Spec Reviewer / Developer)

| # | Severity | Gap |
|---|---|---|
| G1 | **HIGH (correctness)** | `catalog/signals.py` auto-creates product permalinks with slug `products/{slug}` and uses the **default-language** slug. 07 §2 / URL-001 require root-level product slugs (`/<slug>/`), and MCP-031 requires the translated slug. Must be fixed before any storefront/sitemap consumes these rows; existing wrong rows need a data migration. |
| G2 | HIGH (missing core) | No job processor command exists (`run_ai_jobs` working name); `build_prompt` / `persist_output` are `NotImplementedError` stubs for both translation job types; catalog has **no trigger signal** creating translation jobs on Product/Collection save (campaigns has one). §4 is therefore spec'd but not runnable end-to-end. |
| G3 | MEDIUM | Slug-collision at auto-publish only logs a warning (invisible failure, §XV-1). Needs the MCP-015 admin surfacing. |
| G4 | MEDIUM | `add_language_view` POST does not set the `domain` FK (known limitation in release notes) — ML-010 flow unusable until fixed. |
| G5 | MEDIUM | No `manually_edited_at` mechanism exists — MCP-084 unimplemented; without it, any future retranslation pass would clobber human edits. Implement before enabling MCP-052 auto-retranslation. |
| G6 | LOW | `ProductImageTranslation`, `VariantOptionTranslation` (blocked on P2), bulk changelist action, per-object Translations tab (§6.1) not yet implemented. |
| G7 | LOW | `AiJobType.TRANSLATION` enum vs registry string types — registry is authoritative (comment in models.py); no action, recorded for reviewers. |

---

## Open items in this document

| ID | Level | Item | Owner |
|---|---|---|---|
| P2 | **PENDING APPROVAL** | Variant option translation scope (needs Architect entity work) | Human + Architect |
| P3 | **PENDING APPROVAL** | Manual-override protection on explicit re-translate | Human |
| P4 | **PENDING APPROVAL** | Retranslation trigger granularity (auto vs stale-flag) | Human |
| AC-U5 | MEDIUM (pre-existing) | Language-identity validation mechanism | Architect |
| G1 | HIGH | `products/` slug prefix + default-language slug in auto-permalink | Developer (bug) |
| MCP-012 | ASSUMPTION (LOW) | Two-state record status + AiJob status union vs ML-041's four-state wording | Spec Reviewer to confirm |
