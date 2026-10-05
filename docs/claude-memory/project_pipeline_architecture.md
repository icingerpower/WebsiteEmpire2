---
name: 3-level pipeline architecture
description: Full Level 1 → Level 2 → Level 3 pipeline — DB schemas, A/B testing, permalink history, AI queue, GSC feedback loop
type: project
---

## Three-level overview

| Level | App | Purpose |
|-------|-----|---------|
| 1 | WebsiteAspire | Raw scraped data per scraper (`<scraper-id>.db`, schema from `AbstractPageAttributes`) |
| 2 | WebsiteEmpire | Assembled website data — pages, blocs, translations, menus |
| 3 | Drogon | Pre-compiled gzipped HTML bodies + redirects, served live |

---

## Level 2 database schema

### `pages`
- `id` — stable int, never reused
- `page_type_id` — e.g. "PageTypeArticle"
- `lang`, `domain` — routing keys
- `slug` — current URL path component (also tracked in `page_permalinks`)
- `status` — `pending | ai_processing | completed | needs_update | disabled`
- `l1_source_id`, `l1_content_hash` — which Level 1 record drove this page and its hash at last AI processing; hash mismatch → `needs_update`
- `l1_updated_at` — when Level 1 source last changed (drives AI re-queue)
- `l2_updated_at` — when any bloc on this page last changed (drives Level 3 recompile)
- `l3_generated_at` — when Level 3 body was last compiled; skip recompile if `l2_updated_at ≤ l3_generated_at`

### `page_variants`
Blocs belong to a **variant**, not directly to a page. Every page always has at least one variant labelled `control`.
- `id`, `page_id`, `label` — `control | a | b | ...`
- `is_active` — only active variants are compiled to Level 3
- `promoted_at` — when a winning variant was promoted to control

### `page_blocs`
- `id`, `variant_id`, `display_order`, `bloc_type_id`
- `content` — serialised bloc string (e.g. `"1,3,5"` for categories, plain text for text bloc)
- `updated_at` — granular change tracking per bloc

### `menus`
Menus are shared fragments, not page blocs.
- `id`, `domain`, `lang`, `content_html`, `version_id`, `updated_at`
- Pages store the `menu_version_id` they were last rendered with (drives targeted re-render on menu change)

### `page_permalinks`
Tracks every slug a page has ever had.
- `id`, `page_id`, `lang`, `domain`, `slug`
- `is_current` — exactly one row per (page, lang, domain) is current
- `valid_from`, `valid_until`
- `disposition` — `current | permanent_redirect | parked | deleted`
  - **current**: live canonical URL
  - **permanent_redirect (301)**: old slug, passes SEO equity to current; use when renaming a page
  - **parked**: soft redirect, no SEO signal yet, can be reactivated later
  - **deleted (410)**: tells Google explicitly the page is gone
- `redirect_target_permalink_id` — FK to self; for redirect chains

Level 3 `content.db` gets a `redirects` table (old_path, new_path, status_code) so Drogon serves 301/410 on unknown paths without falling through to 404.

---

## AI queue: Level 1 → Level 2

### Priority scoring (recomputed daily from GSC + performance_db)
```
priority = indexed_weight       * is_indexed_in_gsc
         + ctr_weight           * click_through_rate
         + page_type_multiplier * page_type_performance_score
         + lang_weight          * language_performance_score
```

- `language_performance_score`: aggregate CTR × impressions per language — only translate into languages above threshold
- `page_type_multiplier`: which page type Google rewards in this topic area
- New translations are queued only after the source-language page is indexed

### Idempotency
Each AI job is keyed by `(l1_source_id, page_type_id, lang)`. Re-running produces the same output. Hash comparison (`l1_content_hash`) prevents re-queuing unchanged records.

### Batching
Group Level 1 records by page type for each AI call. AI returns structured output (one bloc content string per record) — not prose — keeping output deterministic and parseable.

---

## Level 2 → Level 3 compilation

### Two-pass rendering (chosen)
- **Pass 1** (WebsiteEmpire, at generation time): run page type's `addCode()` → store `body_html_gz` in Level 3 DB; body does NOT include the menu
- **Pass 2** (Drogon, at serve time): concatenate `body_html_gz` + current menu fragment → full page; menu fragment kept in memory cache
- Menu update = update one fragment record → no page body regeneration needed
- Pages store the `menu_version_id` they were compiled with; stale versions are queued for re-render only when the domain's menu actually changes

### Incremental compilation
Only recompile pages where `l2_updated_at > l3_generated_at`. Store `html_hash` of the body — if hash is unchanged after recompile (e.g. cosmetic L2 change), skip rsync for that page.

---

## Level 3 → hosting: rsync SQLite (chosen)
- WebsiteEmpire generates `content.db`, `images.db` locally
- rsync pushes the SQLite files directly to the hosting server (WAL mode — Drogon readers never block the writer)
- `stats.db` flows in reverse: rsync pulls from server to local for GSC-enhanced analysis
- A change manifest (page ids whose `html_hash` changed) is produced per generation run; used to decide which partial rsync is needed if incremental sync is preferred over full file sync

---

## A/B testing

- Active experiments: Drogon routes via a session cookie with weighted split (e.g. 80/20)
- Stats track `(page_id, variant_id)` in Level 3 `stats.db`
- Promotion: winner variant → mark as `control`, set `promoted_at`, deactivate others
- Level 3 stores one HTML body per active variant per slug
- Non-experiment pages always have exactly one variant (`control`) — zero overhead

---

## GSC feedback loop

- Daily GSC API import → `page_performance` table: `page_id, lang, date, impressions, clicks, avg_position, is_indexed`
- Keep 90 days of history
- Weekly recompute of priority scores → re-order AI job queue
- Three concrete effects:
  1. **Language focus**: pause new translations for languages below performance threshold; invest in improving existing indexed pages
  2. **Content type focus**: deprioritise page types with poor `page_type_multiplier`; shift Level 1 processing toward winning formats
  3. **Topic focus**: categories with impressions but low CTR → AI rewrites titles/meta; categories with neither → reduce Level 1 scraping priority

**Why:** If an AI-generated page isn't being indexed or clicked, generating more like it wastes AI budget. The loop ensures effort concentrates where Google is already rewarding the site.
