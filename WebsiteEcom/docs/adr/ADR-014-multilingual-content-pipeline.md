# ADR-014: Multilingual content pipeline — stable variant option entities, translation records, job workflow, permalink fix

**Status:** ACCEPTED (2026-07-05) — implements the remaining scope of TICKET-024
per `specs/ecommerce_engine/14_multilingual_content_pipeline.md` (MCP-*), incorporating the
human-approved decisions of 2026-07-05: stable `VariantOption`/`VariantOptionValue` entities
(spec P2 option 1), per-record confirmation on explicit re-translate of manually edited
records (spec P3 option 1), auto-requeue on source change skipping overridden records
(spec P4 option 1).

**Extends:** ADR-003 (AI job system), ADR-005 (permalink model — translated slugs live in
`Permalink`, never in translation records), ADR-008 (StoreDomain/StoreLanguage/`resolve_locale`,
accepted 2026-07-03), ADR-009 Appendix B (`CampaignStepTranslation` — approved reference
implementation of the translation-record pattern).

**Binding constraints:** design-pattern-ideas §I (validate assembled AI output before persisting;
append on duplicate field id; all-or-nothing multi-field commits), §II (single NFD slugify),
§III/§XV-4 (single permalink resolution path), §IV/§XV-3 (explicit job states, heartbeat
reclaim, sub-job types), §VII/§XV-2 (stable opaque IDs as translation keys — never positions
or display values), §XV-1 (no invisible failures), §XV-5 (validation at persistence
boundaries), §XV-6 (idempotent jobs).

---

## Decision (summary)

1. **Stable variant option entities.** Three new `catalog` models — `VariantOption`,
   `VariantOptionValue`, `VariantOptionAssignment` (explicit through model with a denormalized
   `option` FK enforcing "one value per option per variant" at the DB layer) — replace
   `ProductVariant.option_values_json` as the source of truth. The JSON column is frozen
   (kept, deprecated) until a post-release cleanup ticket; the data migration is reversible.
2. **Translation records.** `ProductTranslation` / `CollectionTranslation` are extended with
   `manually_edited_at` (MCP-084 override flag) and `source_fingerprint` (staleness detection).
   New models following the same pattern: `VariantOptionTranslation`,
   `VariantOptionValueTranslation`, `ProductImageTranslation` (MCP-020/021).
   **Translated slugs stay in `Permalink`** (MCP-011, binding ADR-005 §2) — there is no `slug`
   column on any translation model; the "manual slug" protection is realized on the
   `Permalink` row (`auto_created=False` + `trigger=MANUAL`), not via an `is_slug_manual`
   column (see Question 2 — deliberate divergence from the task's literal field list).
3. **Option translation protocol.** Option names and values of one product are translated in
   **one job per (product, lang)** — `job_type='translation'`, `sub_type='options'` — with
   output field ids keyed by stable PKs (`option_{pk}_name`, `value_{pk}_value`), never by
   position or display value (§VII).
4. **Retranslation triggers.** Catalog mirrors `campaigns/signals.py`: `post_save` on
   Product/Collection creates one `translation` AiJob per enabled non-default `StoreLanguage`
   that is missing a published record **or** whose record is stale
   (`source_fingerprint` mismatch) and not manually overridden. Flood guard = existing
   non-terminal job for the same `(job_type, target, lang, sub_type)` (MCP-051). Overridden
   stale records are skipped and surfaced as "stale — manually edited" in the coverage UI;
   they are only re-translated through the explicit per-record confirmation flow.
5. **`manage.py run_ai_jobs`** (MCP-041) is specified as the CLI runner loop: claim
   (`select_for_update(skip_locked=True)`, priority order) → heartbeat → `build_prompt` →
   CLI runner (`claude_code` → `codex` → `gemini_terminal`; `api` last, only if membership
   credits remain) → §I-safe output assembly → `validate_output` → `persist_output`
   (transactional, all-or-nothing) → `record_cost`. `build_prompt`/`persist_output` for the
   `translation` job type are implemented in `catalog/ai_jobs.py` (replacing the
   `NotImplementedError` stubs).
6. **Permalink URL bug fix (spec §10-G1, HIGH).** The translation-publish signal creates
   product permalinks at the **language root** (`{translated-slug}`), not `products/{slug}`,
   and derives the slug from the **translated title** (`_slug_hint` from the job output →
   slugified translated title → default-language slug). Collision handling gains bounded
   `-2..-N` suffixing (SLUG-021) instead of give-up-and-log (fixes §10-G3's invisible
   failure). A reversible data migration rewrites existing wrong `products/…` rows.
7. **Admin UX** per spec §6: Translations section on Product/Collision change views (union
   status: missing/queued/in progress/error/draft/published + "no URL" and
   "stale — manually edited" warnings), changelist bulk action "Translate all missing" with
   count + cost estimate confirmation, per-language Translate/Re-translate, per-record
   overwrite confirmation for manually edited records.

---

## Context

- T024 was partially released 2026-07-04: `ProductTranslation`, `CollectionTranslation`, the
  translation→permalink lifecycle signal, and the `translation` job-type registration exist.
  What is missing (spec §10): the job processor (`run_ai_jobs`), real
  `build_prompt`/`persist_output`, catalog trigger signals, variant option identity +
  translations, the manual-override mechanism, the admin surfaces — and the permalink signal
  writes a **wrong URL scheme** (`products/{default-lang-slug}`) that violates ADR-008/URL-001
  and MCP-031/032.
- `ProductVariant.option_values_json` stores `[{"option_name": "Color", "value": "Red"}]` —
  display values with no stable identity. Spec MCP-021 explicitly blocks option translation on
  this: a JSON-keyed translation would repeat WebsiteEmpire2's §VII menu-translation trap
  (position/display-value keys break on rename, reorder, duplicate labels).
- Grep confirms `option_values_json` is currently read only by `catalog/models.py` (docstring),
  its creating migration, and one test file — no admin form, view, or template consumes it yet.
  The entity migration is therefore cheap **now** and expensive later.
- The human approved on 2026-07-05: P2 option 1 (stable entities, translate names **and**
  values), P3 option 1 (per-record confirmation on explicit re-translate of overridden
  records; automatic passes always skip), P4 option 1 (auto-requeue on source change,
  skipping overridden records; skipped records need explicit re-trigger).

---

## Question 1 — Shape of the stable variant option entities

### Options considered

- **Option A — `VariantOption` + `VariantOptionValue` + explicit `VariantOptionAssignment`
  through model carrying a denormalized `option` FK.**
- **Option B — plain `ManyToManyField(ProductVariant, VariantOptionValue)`** (implicit through
  table).
- **Option C — keep `option_values_json`, add a side table mapping (product, option_name,
  value) → surrogate id** for translation keying only.

### Chosen option: **A**

```python
class VariantOption(StoreOwnedModel):          # "Color", "Size", "Frame finish"
    product   = FK(Product, CASCADE, related_name="options")
    name      = CharField(255)                 # source-language display name
    position  = SmallIntegerField(default=0)   # display order ONLY — never identity (§XV-2)
    created_at
    Meta:
        unique_together = [("store", "product", "name")]
        indexes  = [Index(fields=["product", "position"])]
        ordering = ["position", "id"]          # id tiebreak → deterministic order

class VariantOptionValue(StoreOwnedModel):     # "Rouge", "M"
    option    = FK(VariantOption, CASCADE, related_name="values")
    value     = CharField(255)                 # source-language display value
    position  = SmallIntegerField(default=0)
    created_at
    Meta:
        unique_together = [("store", "option", "value")]
        indexes  = [Index(fields=["option", "position"])]
        ordering = ["position", "id"]

class VariantOptionAssignment(StoreOwnedModel):
    variant      = FK(ProductVariant, CASCADE, related_name="option_assignments")
    option       = FK(VariantOption, CASCADE, related_name="+")   # denormalized
    option_value = FK(VariantOptionValue, CASCADE, related_name="assignments")
    Meta:
        constraints = [
            UniqueConstraint(fields=["variant", "option"],
                             name="one_value_per_option_per_variant"),
            UniqueConstraint(fields=["variant", "option_value"],
                             name="uniq_variant_option_value"),
        ]
    # clean(): option_id == option_value.option_id; store matches variant/option/value stores.
```

Compatibility helper: `ProductVariant.option_values` (read-only property) returns the legacy
`[{"option_name": …, "value": …}]` shape from the new tables, ordered by option position, so
any code written against the JSON shape keeps working during the transition.

### Why

- The invariant "a variant selects exactly one value per option of its product" is the whole
  point of variant options; only the denormalized `option` FK on the through row lets the DB
  enforce it (`UniqueConstraint(variant, option)`). A plain M2M (Option B) cannot express it
  and pushes the guard into application code — §XV-5 says persistence-boundary validation.
- An implicit M2M through table also cannot carry the `store` FK, breaking the
  StoreOwnedModel compliance test (ADR-001 §4). An explicit through model is required either
  way; making the `option` denormalization part of it costs one column.
- Option C keeps the display value as identity ("Rouge" renamed to "Rouge foncé" orphans its
  translation) — exactly the §VII trap MCP-021 forbids. Rejected.
- `unique (store, product, name)` / `unique (store, option, value)` make options a controlled
  per-product vocabulary; renames are UPDATEs to one row, so all translation records and
  assignments follow automatically — stable identity is the PK, as required.
- `(product, position)` and `(option, position)` indexes serve the only hot read (ordered
  option matrix for the product page / variant picker).

---

## Question 2 — Where translated slugs and the manual-slug flag live

The task brief for this ADR listed a `slug` column and `is_slug_manual` flag on
`ProductTranslation`. This **conflicts** with binding MCP-011 / ADR-005 §2 ("translated slugs
never live in translation models — `Permalink` is the sole home"), with the approved
reference implementation (`CampaignStepTranslation` has no slug), and with the released
`ProductTranslation` schema. The conflict is resolved in favor of the binding ADR/spec.

### Options considered

- **Option A — `slug` + `is_slug_manual` columns on the translation record** (task's literal
  field list).
- **Option B — slugs stay in `Permalink`; "manual slug" = `Permalink.auto_created=False` +
  `trigger=MANUAL`, set when a human edits the slug.** Auto-regeneration (re-translation,
  `slug_only` jobs) may only rewrite permalinks where `auto_created=True`.
- **Option C — slugs in `Permalink` plus a new `is_slug_manual` boolean on `Permalink`.**

### Chosen option: **B**

### Why

- Option A creates two homes for the same slug string. Every divergence between them is a
  §III two-representation bug (wrong hreflang, orphaned redirects); ADR-005 §2 was accepted
  precisely to prevent this. Supersession of an accepted ADR requires a human decision this
  ADR does not have.
- `Permalink` already carries the exact semantics needed: `auto_created` (machine vs human)
  and `trigger` (`manual`/`slug_change`/`import`). Option C would add a third overlapping
  flag — redundant state to keep consistent for no gain.
- The protection the task actually requires — "manual slugs must not be auto-overwritten on
  re-translation" — is one rule under B: **the pipeline never updates a permalink whose
  `auto_created=False`**. Admin slug edits set `auto_created=False, trigger=MANUAL` in the
  permalink admin/save path. Slug-change redirects continue to work unchanged (permalink
  layer owns them).
- Field naming follow-through: the translation columns mirror the **source model's** field
  names (MCP-010 "one column per translated source field") — so `title` (not `name`) on
  `ProductTranslation` (already released), `name` on `VariantOptionTranslation` (mirrors
  `VariantOption.name`), `value` on `VariantOptionValueTranslation`.

---

## Question 3 — Translation model shape (new + extended)

All translation models keep the ratified pattern (spec §2.1): parent FK CASCADE
`related_name="translations"`, `lang_code` (10), one column per translated source field,
`status` draft/published, `ai_job` FK SET_NULL, `created_at/updated_at`,
`unique (store, parent, lang_code)`, `clean()` cross-tenant guard.

**Two new columns on every translation model** (added to the released
`ProductTranslation`/`CollectionTranslation` by migration, included in the new models):

| Column | Type | Purpose |
|---|---|---|
| `manually_edited_at` | DateTimeField null | MCP-084 record-level override flag. Set when a human saves the record through an admin form; **never** set by `persist_output`. NULL = pure AI content. |
| `source_fingerprint` | CharField(64) blank | SHA-256 hex of the source-language field values this record was translated from. Written by `persist_output` and by admin saves. Staleness = stored fingerprint ≠ current fingerprint of the source object. |

Fingerprint helper (single implementation, `catalog/translation_fingerprint.py` or
`core`): `fingerprint(*values) = sha256("\x1f".join(values)).hexdigest()`. Field order per
content type is fixed and documented next to the helper.

**Why a fingerprint rather than `updated_at` comparison or signal-time diff only:** comparing
`updated_at` timestamps produces false staleness on every unrelated save (price change would
mark all translations stale); a signal-time field diff alone cannot answer "is this record
stale?" later (needed by the coverage UI and by the idempotent SKIP check in the runner —
§XV-6 "is step N already complete?" must be answerable from persisted state, §XV-3 state is
explicit, not inferred). One 64-char column answers both.

**New models (Phase 2):**

```python
class VariantOptionTranslation(StoreOwnedModel):
    option  = FK(VariantOption, CASCADE, related_name="translations")
    lang_code, name (255, blank), status, ai_job (SET_NULL,
        related_name="variant_option_translations"),
    manually_edited_at, source_fingerprint, created_at/updated_at
    unique (store, option, lang_code)

class VariantOptionValueTranslation(StoreOwnedModel):
    option_value = FK(VariantOptionValue, CASCADE, related_name="translations")
    lang_code, value (255, blank), status, ai_job (SET_NULL,
        related_name="variant_option_value_translations"),
    manually_edited_at, source_fingerprint, created_at/updated_at
    unique (store, option_value, lang_code)

class ProductImageTranslation(StoreOwnedModel):        # MCP-020
    image = FK(ProductImage, CASCADE, related_name="translations")
    lang_code, alt_text (255, blank), status, ai_job (SET_NULL,
        related_name="product_image_translations"),
    manually_edited_at, source_fingerprint, created_at/updated_at
    unique (store, image, lang_code)
```

None of the three interacts with `Permalink` (no URLs). Rendering gate:

- `ProductImageTranslation`: draft/missing ⇒ empty `alt` on translated pages (spec MCP-020
  assumption — never default-language alt).
- Option/value translations: rendered when published. When a product's option coverage for a
  language is incomplete (new value added minutes ago), the storefront renders the
  **source-language string for the missing records only**, the coverage UI shows the language
  as "incomplete — options", and the auto-requeue trigger (Question 5) has already queued the
  gap at priority 100. **DECIDED (2026-07-05)** — show source-language label for missing option
  value translations; 404ing revenue pages on every catalog edit is rejected. This is a narrow,
  visible, short-lived fallback; it disappears once the AI job runs (typically minutes).

---

## Question 4 — Option translation job granularity

### Options considered

- **Option A — one AiJob per (VariantOption, lang) and per (VariantOptionValue, lang).**
- **Option B — one AiJob per (product, lang) covering all of the product's options and
  values, `sub_type='options'`, PK-keyed field ids.**

### Chosen option: **B**

`target_model='catalog.Product'`, `target_id=product.pk`, `lang=<target>`,
`input_payload = {"sub_type": "options", "options": [{"id": 12, "name": "Color",
"values": [{"id": 55, "value": "Rouge"}, …]}, …], "existing": {…}, "source_lang": …,
"target_lang": …}`.
Output protocol (§VII): `AiJobOutput.field_id` ∈ `option_{pk}_name`, `value_{pk}_value` —
stable opaque PKs, never positions, never display strings. `persist_output` parses PKs back,
verifies each PK belongs to the target product **and** store (§XV-5), and update-or-creates
the two translation-record types in one transaction.

### Why

- A 3-option × 8-value product in 4 languages under Option A is 132 job rows for one product
  save — queue noise that starves the scheduler and makes the flood guard fire constantly.
  Under B it is 4 jobs.
- One prompt containing the whole option matrix gives the model the context to translate
  consistently ("Color: Rouge/Bleu" as a set), and the PK-keyed dict protocol is exactly the
  fix WebsiteEmpire2 landed on for menus (§VII) — proven against reorder/duplicate-label
  scrambling.
- All-or-nothing persist per (product, lang) avoids half-translated option matrices
  (§I: validate all fields before committing any).
- Sub-jobs stay individually retryable via `sub_type` (§IV): `full` (product text),
  `options`, `slug_only`, `image_alt` are four independent schedulable conditions on the same
  `translation` job type, each with its own flood-guard key.

---

## Question 5 — Retranslation trigger and manual-override protection

Implements approved P3 option 1 + P4 option 1 and MCP-050…055.

### Trigger signals (catalog mirrors `campaigns/signals.py`)

- `pre_save` Product/Collection: cache current source-field values (`_old_translatables`,
  same transient-attribute pattern as `_old_status`).
- `post_save` Product (status `active` only — MCP-050; drafts create no jobs) and Collection
  (`is_published=True`): for each `StoreLanguage` of the store with `is_enabled=True` and
  `is_default=False`:
  1. **Missing** — no published record ⇒ queue `full`.
  2. **Stale** — published record, `source_fingerprint` ≠ current fingerprint, and
     `manually_edited_at IS NULL` ⇒ queue `full` (auto-requeue, P4 option 1).
  3. **Stale + overridden** — fingerprint mismatch and `manually_edited_at IS NOT NULL` ⇒
     **no job**; the coverage UI shows "stale — manually edited" (never invisible, §XV-1);
     only the explicit per-record confirmed re-translate (below) queues it.
  4. Flood guard (MCP-051): skip when a NOT_STARTED/IN_PROGRESS job exists for the same
     `(job_type='translation', target_model, target_id, lang)` with the same payload
     `sub_type` (checked in Python on the candidate rows — volumes are small).
- `post_save` on `VariantOption` / `VariantOptionValue` / `VariantOptionAssignment` (and
  delete signals for values): queue an `options` sub-job per language for the parent product
  under the same rules. `ProductImage.alt_text` change ⇒ `image_alt` sub-job.
- MCP-053 (add-language bulk) and MCP-054 (admin manual triggers) call the same
  `create_job()` path with `created_by='human'`.
- **Quota (MCP-055 refinement):** human-initiated paths pre-check `check_quota()` and show an
  immediate visible error. Signal paths always create the job (creation is free — a raise
  inside `post_save` would abort catalog saves); the **runner** defers quota-exhausted stores
  at claim time, and the AI-jobs admin dashboard derives a visible "blocked by quota" state
  from `check_quota(store) is False` + NOT_STARTED jobs. No job is ever silently dropped.

### Manual-override write rules

- Admin translation forms set `manually_edited_at=now()` whenever a human changes any
  translated field (record-level, per MCP-084's approved v1 granularity).
- `persist_output` **never** sets it, and refuses to modify a record whose
  `manually_edited_at` is non-NULL unless the job payload carries `"overwrite_manual": true`
  — in that case it also resets `manually_edited_at=NULL` (the record is AI content again).
  A refused persist marks the job **SKIPPED** (its idempotent-short-circuit meaning) and
  writes a WARNING `AiJobValidation` row `manual_override_skipped` so the dashboard shows why
  (§XV-1).
- `overwrite_manual` is set by exactly one code path: the explicit re-translate confirmation
  view. "Re-translate" / "Re-translate all" on an object with overridden records renders a
  per-record confirmation screen — "FR translation manually edited on {date} — overwrite?" —
  with per-record opt-in checkboxes (P3 option 1). Unchecked records get no job; checked ones
  get jobs with `overwrite_manual=true`. Bulk changelist actions and automatic passes never
  set the flag.

---

## Question 6 — `manage.py run_ai_jobs` (MCP-041) and the translation callbacks

The command is the generic runner loop owned by the AI-orchestration area (ADR-003); this ADR
fixes its **contract** so T024 is runnable end-to-end, and fully specifies the
translation-type callbacks.

### Command contract

```
manage.py run_ai_jobs [--once | --limit N] [--store SLUG] [--job-type TYPE]
                      [--job-id N] [--runner claude_code|codex|gemini_terminal|api]
```

Loop iteration:
1. `reclaim_stale_runs()` (existing, ADR-003 §3).
2. **Claim** in one transaction: `select_for_update(skip_locked=True)` over
   `AiJob(status=NOT_STARTED, scheduled_at NULL or ≤ now)`, upstream `AiJobDependency` all
   DONE, store passes `check_quota()`, ordered `(-priority, created_at)` (translations at 100
   go first). Set job IN_PROGRESS, create `AiJobRun(status=IN_PROGRESS, runner, model_id)`.
3. **Idempotency short-circuit (§XV-6):** registry-level `should_skip(job)` hook — for
   translations: a published record already matching the current source fingerprint (and not
   `overwrite_manual`) ⇒ job SKIPPED, no runner invocation.
4. `build_prompt(job)` (registry) → invoke the runner subprocess. Runner order: CLI runners
   first (`claude_code`, then `codex`, then `gemini_terminal`), `api` **last and only if
   membership credits remain** (platform setting). A heartbeat thread updates
   `run.last_heartbeat` every 60 s while the subprocess lives.
5. **Assemble output per §I:** concatenate all assistant blocks in emit order; strip
   continuation suffixes (`re.sub(r'(?:_continued)?_part\d+$|_continued\d*$', '', field_id)`);
   on duplicate field id **append**, never overwrite; assert the runner's stop reason is
   normal completion where the runner exposes it. Write `AiJobOutput` rows (unique
   `(run, field_id)` — update on duplicate).
6. `validate_output` → `AiJobValidation` rows written pass **and** fail (§XV-1). Any failed
   ERROR-severity check ⇒ run ERROR, `retry_count += 1`, job back to NOT_STARTED while
   `retry_count < max_retries` else ERROR; **nothing persisted** (AC-220).
7. `persist_output(job_run, outputs)` inside `transaction.atomic()` → job + run DONE →
   `record_cost(run)`.

### `translation` callbacks (`catalog/ai_jobs.py`, replacing the stubs)

- **`build_prompt`** — from `input_payload`: source field texts, `source_lang`/`target_lang`,
  store name (META formulas), META-003 limits (60/160) as instructions, the **existing
  translation content when present** (incremental update: "here is the current FR text;
  update it to reflect the changed source" — cheaper and more stable than fresh translation),
  and for `options` sub-type the PK-keyed matrix of Question 4. Output format instruction:
  one `field_id` per field, exact ids given in the prompt (`title`, `description`,
  `seo_title`, `seo_description`, `slug` for `full`; PK-keyed ids for `options`;
  `alt_{pk}` for `image_alt`). Long descriptions are chunked proactively before the token
  limit (`chunk_index`/`total_chunks`, ADR-003 §5); a failed chunk B never destroys persisted
  chunk A (AC-221).
- **`validate_output`** (MCP-062) — ERROR severity: field-id completeness for the sub-type;
  non-empty `title` (or `name`/`value` for options); length-ratio sanity vs source (≥ 35 %
  past ~200 chars, §I — handles CJK compression); bracket/tag balance for rich descriptions;
  for `options`: every returned PK exists, belongs to the target product, no missing PK.
  WARNING severity: META length limits (over-length accepted with a WARNING row). The
  language-identity check remains open item AC-U5 — until it exists **no check row is
  written** (absence recorded, never a fake pass).
- **`persist_output`** — routes by sub-type. `full`: update-or-create the translation record
  on `(store, parent, lang_code)` honoring the override guard (Question 5); set `ai_job`,
  `source_fingerprint`, `status=PUBLISHED` (auto-publish, `requires_human_review=False`);
  a `slug` output is placed on the instance as `_slug_hint` **before** save so the existing
  permalink signal (sole permalink writer, §XV-4) consumes it. `options`: PK-parse and
  update-or-create `VariantOptionTranslation`/`VariantOptionValueTranslation` rows —
  all-or-nothing. `slug_only`: set `_slug_hint` and re-save the published record (signal
  updates the auto-created permalink). `image_alt`: `ProductImageTranslation` rows.

---

## Question 7 — Permalink URL bug fix (spec §10-G1 HIGH, §10-G3 MEDIUM)

### The bug

`catalog/signals.py::_post_save_product_translation` passes
`slug_factory=lambda: f"products/{instance.product.slug}"` — wrong on two axes:
products must live at the **language root** (`/{slug}/`, URL-001 / ADR-008 / MCP-032), and
the slug used is the **default-language** slug, not the translated one (MCP-031). Collections
(`collections/{slug}`) have the right prefix but the wrong (default-language) slug.
Additionally, on slug collision the signal logs a warning and creates nothing — an invisible
failure (§XV-1, G3).

### Fix (signal)

1. Product `slug_factory` chain (MCP-031, first non-empty wins):
   `getattr(instance, "_slug_hint", None)` (job-provided translated slug) →
   `make_slug(instance.title)` (slugified translated title, shared NFD slugify §II) →
   `instance.product.slug` (default-language slug — legal duplicate across languages,
   SLUG-020). **No `products/` prefix.**
2. Collection `slug_factory`: same chain with `collections/` prefix and
   `instance.collection.slug` fallback.
3. Collision + reserved-slug handling in `_sync_translation_permalink`: on `IntegrityError`
   or reserved-ISO-code validation failure, retry `-2`, `-3`, … bounded at 50 (SLUG-021).
   Only if the bound is exhausted: log **and** leave the "published, no URL" condition, which
   the admin coverage UI derives at render time (published record with no active permalink
   for its lang — MCP-015; no schema change needed).
4. The pipeline only ever rewrites permalinks with `auto_created=True` (Question 2's manual
   protection).

### Data migration (existing wrong rows)

For every `Permalink` whose `content_type` is `catalog.Product` and whose slug starts with
`products/`: strip the prefix; if a published translation record exists for
`(store, lang, product)`, prefer `make_slug(translation.title)`; apply the suffix loop on
collision or when the stripped slug matches the reserved ISO-code pattern (a product slugged
`it` would otherwise be rejected). Each rewrite is logged. **No `SlugRedirect` rows are
created** for the corrected paths — no storefront serves these URLs yet and they were never
sitemap-emitted (spec G1: "must be fixed before any storefront/sitemap consumes these rows").
**ASSUMPTION (LOW)**: if any store is found to be live on the old paths before this ships,
the migration switches to creating 301 redirects instead — one boolean in the migration.
Reverse migration re-adds the `products/` prefix (suffix additions are not reverted —
documented lossiness, acceptable pre-launch).

Collections keep their prefix; their rows are only rewritten when a published translation
provides a translated slug (same preference rule), also reversible.

---

## Question 8 — Admin UX architecture (spec §6)

- **Translations section** (template-driven section on the Product/Collection change view fed
  by a single coverage query, not an inline formset — the rows are language states, not
  editable child records): one row per enabled non-default `StoreLanguage` with the MCP-080
  columns. Status is the union view (MCP-012): `missing` / `queued` / `in progress` / `error`
  (from the newest non-terminal or errored AiJob) / `draft` / `published` (record), plus
  derived warnings **"no URL — slug collision"** (published record, no active permalink) and
  **"stale — manually edited"** (fingerprint mismatch + `manually_edited_at`). Actions per
  row: Translate / Re-translate / Edit / Publish / Unpublish.
- **Translation edit form**: source text read-only beside each field (MCP-082), META length
  counters; saving sets `manually_edited_at` (Question 5). Option/value translations are
  edited in a per-product "Option translations" matrix view (option × language), same rules.
- **Changelist bulk action "Translate all missing"** (MCP-083): for the selection, count
  (object, lang) pairs with no published record and no non-terminal job; confirmation screen
  shows job count and cost estimate (`estimate_tokens` × model pricing); then bulk
  `create_job(created_by='human')`. Quota pre-checked with a visible error.
- **Explicit re-translate with override confirmation** (P3 option 1): intermediate
  confirmation view listing overridden records with per-record opt-in; confirmed records get
  jobs with `overwrite_manual=true` (Question 5). Non-overridden records queue directly.
- **Manual slug protection**: the permalink admin (and any slug-edit affordance on the
  Translations tab) sets `auto_created=False, trigger=MANUAL` on human slug edits — from then
  on the pipeline never touches that permalink; a `slug_only` job against it is SKIPPED with
  a visible WARNING validation row.

---

## Implementation plan (phased — each phase is one Developer ticket, testable alone)

**Phase 1 — Stable option entities (T024-A)**
`VariantOption`, `VariantOptionValue`, `VariantOptionAssignment` + reversible data migration
from `option_values_json` (option order = first-seen across variants ordered by
`variant.position`; JSON column frozen/deprecated, dropped only in a post-release cleanup
ticket after a parity check). `ProductVariant.option_values` compatibility property. Admin
CRUD for options/values on the product page. No translation yet.

**Phase 2 — Translation models (T024-B)**
Migration adding `manually_edited_at` + `source_fingerprint` to
`ProductTranslation`/`CollectionTranslation`; new `VariantOptionTranslation`,
`VariantOptionValueTranslation`, `ProductImageTranslation`; fingerprint helper;
cross-tenant `clean()` guards + compliance-test registration.

**Phase 3 — AiJob integration (T024-C)**
Real `build_prompt` / `validate_output` / `persist_output` / `should_skip` in
`catalog/ai_jobs.py` (sub-types `full`, `options`, `slug_only`, `image_alt`); catalog trigger
signals (Product/Collection/option models/ProductImage) with flood guard, staleness rule,
override skip; `_slug_hint` consumption in `persist_output`. Depends on Phases 1–2.

**Phase 4 — `run_ai_jobs` command (T024-D)**
Claim/heartbeat/assemble/validate/persist/record_cost loop per Question 6; runner adapters
CLI-first, `api` last behind the membership-credit setting; `--once`/`--limit`/`--job-id`
flags for tests and manual ops. Depends on Phase 3 for an end-to-end testable type.

**Phase 5 — Permalink bug fix + data migration (T024-E)**
Signal fix (root-level translated slug chain, collision suffix loop, auto_created-only
rewrites) + the Question 7 data migration. Independent of Phases 1–4 — **may ship first**;
it is the HIGH-severity correctness item.

**Phase 6 — Admin UX (T024-F)**
Translations section + coverage query, translation edit form with `manually_edited_at`,
option-translation matrix view, bulk action with cost confirmation, explicit re-translate
per-record confirmation flow, manual-slug protection wiring. Depends on Phases 2–3 (status
data) and 5 (URL column correctness).

Suggested order: **5 → 1 → 2 → 3 → 4 → 6.**

---

## Migration strategy

1. All schema migrations are additive; no column is dropped in T024 (`option_values_json`
   removal is a separate post-release ticket gated on a parity assertion).
2. Data migrations (`option_values_json` → entities; `products/…` permalink rewrite) are
   reversible as specified in Questions 1 and 7 and log every row they touch.
3. Existing released rows: `ProductTranslation`/`CollectionTranslation` gain the two new
   columns with NULL/`''` defaults — semantics "unknown provenance, not overridden, fingerprint
   unset". An unset fingerprint is treated as **stale** (will be re-queued once on the next
   source save) — correct behavior for rows produced before validation existed. `ASSUMPTION
   (LOW)`.
4. Signal fix (Phase 5) lands **before** the permalink data migration in the same release, so
   no new wrong rows are created after the rewrite.

---

## Risks

- **Option data migration mis-grouping** — variants of one product with inconsistent JSON
  spellings ("Color" vs "Colour") become two options. Mitigation: migration report lists
  per-product option counts > 3 for human review; entities are editable/mergeable in admin.
- **MCP-002 tension on option fallback** (Question 3) — flagged PENDING APPROVAL; if the
  strict rule is chosen instead, the render gate changes but no schema changes.
- **Runner output pathologies** — mitigated by §I assembly rules + persistence-boundary
  validation; residual risk is prompt-format drift per CLI runner version, contained in the
  runner adapter layer.
- **Signal fan-out cost** — post_save triggers on Product now do smart-rule evaluation AND
  translation-job bookkeeping. The trigger only queries when translatable fields changed
  (pre_save cache) and creates at most one job per language; bulk imports should use the
  bulk paths (MCP-053) rather than per-row saves.
- **Two writers on permalinks** avoided by construction (`_slug_hint` + signal remains the
  sole creator), but a future direct `Permalink` writer would reintroduce it — guarded by a
  grep-level test (AC-MCP-07 analogue for permalinks).
- **Job flood via option edits** — bounded by the per-(product, lang, sub_type) flood guard;
  worst case one `options` job per language per edit burst.

## Rollback strategy

- Phases are independently revertible: each is additive code + reversible migrations.
- Phase 5 signal fix rollback = revert commit; data migration has a reverse operation
  (prefix re-add).
- Phase 1 rollback = reverse data migration (drops entity rows); `option_values_json` is
  still intact because it is never rewritten or dropped in T024.
- `run_ai_jobs` is a new command — rollback is "do not run it"; no schema involvement.
- Translation-model column additions roll back by reverse migration; the columns are
  nullable/defaulted so partial rollback cannot break released reads.

## Tests required

- **Phase 1:** entity constraints (one value per option per variant — DB level; per-store SKU
  isolation analogue for option names/values); migration forward/backward round-trip on a
  fixture with multi-option variants; `option_values` property parity with legacy shape;
  cross-tenant `clean()` guards.
- **Phase 2:** unique `(store, parent, lang_code)` on all five translation models; SET_NULL
  on AiJob delete (AC-MCP-04); fingerprint helper determinism + field-order stability.
- **Phase 3:** AC-MCP-01 (active product, EN default + fr/de ⇒ exactly 2 jobs at priority
  100; re-save ⇒ none), AC-MCP-02 (draft ⇒ no jobs), AC-MCP-05 (manually edited FR skipped by
  auto pass, DE re-translated), stale-fingerprint requeue, disabled/default language
  exclusion, option-edit ⇒ `options` sub-job with PK-keyed payload, `persist_output`
  override guard ⇒ SKIPPED + WARNING validation row, options persist all-or-nothing on a bad
  PK, AC-221 chunk isolation.
- **Phase 4:** claim ordering by priority; skip_locked concurrency (two runners, one job);
  heartbeat + reclaim; ERROR validation ⇒ nothing persisted, retry then terminal ERROR
  (AC-220); SKIPPED short-circuit on fingerprint match; `record_cost` called once per run;
  quota-exhausted store deferred visibly; continuation-suffix/duplicate-field-id assembly
  unit tests (AC-MCP-07 grep test: no LLM SDK import outside `aijobs`).
- **Phase 5 (regression, After-Bug agent — bug fix):** publishing a ProductTranslation
  creates an active permalink at the **root-level translated** slug, never `products/…`
  (AC-MCP-03), proven failing on the pre-fix signal; collision ⇒ `-2` suffix; reserved ISO
  slug ⇒ suffix; exhausted suffixes ⇒ published-no-URL derivable (AC-MCP-06); data migration
  rewrites fixture rows and reverses; manual permalink (`auto_created=False`) untouched by
  `slug_only` persist.
- **Phase 6:** coverage row status union (all six states + both warnings); bulk action count
  + flood-guard dedup (AC-MCP-08 quota error visible); explicit re-translate confirmation
  gates `overwrite_manual`; admin form save sets `manually_edited_at`; permission tests
  (store admin sees only own store's translations).

## Consequences / follow-ups

- Spec Agent: mark P2/P3/P4 DECIDED in `14_multilingual_content_pipeline.md` referencing this
  ADR; record the Question 3 option-fallback item and the MCP-055 creation-vs-execution quota
  refinement in `11_uncertainties_to_validate.md`.
- AC-U5 (language-identity validation) remains open — slot is reserved in `validate_output`.
- Post-release cleanup ticket: drop `option_values_json` after parity check.
- T025 (hreflang emission) and T029 (storefront rendering) consume this pipeline unchanged
  via `Permalink.is_active` and the resolver map.
