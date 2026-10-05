---
name: StaticWebsiteServe architecture
description: Three-subproject Drogon server for serving static websites from SQLite — naming, DB layout, repository pattern, Qt interop strategy, and serving decisions
type: project
originSessionId: 54c75568-9c18-4af8-89b8-a70366f939e7
modified: 2026-08-31T20:06:19.673Z
---
## Serving decisions (chosen)
- **Two-pass rendering**: Pass 1 (WebsiteEmpire) stores `body_html_gz` without menu. Pass 2 (Drogon) concatenates body + in-memory menu fragment at serve time. Menu updates require no page body regeneration.
- **SQLite rsync**: `content.db` and `images.db` generated locally by WebsiteEmpire, pushed to server via rsync. WAL mode ensures Drogon readers never block. `stats.db` is per-language on the server (each Drogon instance writes it in its own `deploy/<lang>/` cwd); PaneDomains::download() checkpoints each WAL over ssh, rsyncs each lang's file, skips langs never deployed ("No such file"), and StatsDbMerger rebuilds local workingDir/stats.db from them (DELETE+re-INSERT in place, ids reassigned — idempotent, keeps open reader connections valid). Viewer: PagesStatsWidget mounted in the "Page Stats" tab (PanePageStats::setWorkingDir). CAVEAT (found 2026-08-31 on healybio): the beacon posts to /stats/* WITHOUT a language prefix, so nginx routes every language's beacon to the default (en) service — only deploy/en/stats.db has rows, page_ids are bare lang-stripped slugs with no language attribution (untranslated slugs collide across langs). is_bot column added 2026-08-31 (BotDetector on User-Agent, "Exclude bot traffic" checkbox in widget); historical rows were manually reclassified all-bot except GSC-clicked pages' longest visits.
- **Redirects table in content.db**: `redirects(old_path, new_path, status_code)` — Drogon's PageController checks this when a path is not found in pages, enabling 301/410 responses without 404 fallthrough.

---

Three subprojects added to WebsiteEmpire2 for serving static websites:

- **StaticWebsiteServeLib** — Qt-free static library (SQLiteCpp). Contains interfaces, DB classes, model structs.
- **StaticWebsiteServe** — Drogon executable. Links StaticWebsiteServeLib.
- **StaticWebsiteServeTests** — Drogon test framework (`drogon_test.h`). Hits real (temp) SQLite files per test.

**Why:** Serve pre-generated static websites from SQLite databases without a filesystem.

**How to apply:** StaticWebsiteServeLib must never depend on Qt or Drogon internals — pure C++/STL only. The Qt app links StaticWebsiteServeLib for write operations; QSqlModel for read/display (two connections on same .db file are safe with WAL mode).

## SQLite databases (one each)

- `content.db` — pages: id, path, html_gz (BLOB), etag
- `images.db` — images: id, blob, mime_type + image_names: domain, filename, image_id (name→id indirection for per-language SEO filenames)
- `stats.db` — displays_clicks: id, page_id, display_at, clicked_at(nullable) + page_session: id, page_id, scrolling_percentage, time_on_page, is_final_page

## Repository pattern

Interfaces in StaticWebsiteServeLib use STL types only:
- IPageRepository, IImageRepository → read content/images
- IStatsWriter → write stats (used by Drogon + Qt)
- IStatsReader → read stats (used by Qt logic layer)
- StatsReaderQSql → Qt-specific impl in WebsiteEmpireLib, exposes QSqlQueryModel for views

## Stats API (JS → Drogon)

- POST /stats/display {"page_id"} → returns {"id": rowid}
- PATCH /stats/click/{id} {"clicked_at"}
- POST /stats/session {"page_id", "scrolling_percentage", "time_on_page", "is_final_page"}
JS uses navigator.sendBeacon() for the unload event to avoid dropped requests.

## ContentDb schema (current)

- `pages(id INTEGER PK, path UNIQUE, domain, lang, etag, updated_at)` — metadata only; no html_gz
- `page_variants(id, page_id FK, label, is_active, html_gz BLOB, etag)` — one row per (page, label); UNIQUE(page_id, label)
- `menu_fragments(id, domain, lang, html_gz BLOB, version_id, updated_at)` — UNIQUE(domain, lang)
- `redirects(old_path PK, new_path nullable, status_code)` — 301 or 410

## Repository implementations (all in StaticWebsiteServeLib)

- `PageRepositorySQLite` → `IPageRepository`: `findByPath`, `findVariant(pageId, label)`, `findActiveVariantLabels(pageId)`
- `MenuRepositorySQLite` → `IMenuRepository`: `findByDomainAndLang`, `findAll` (used for startup cache)
- `RedirectRepositorySQLite` → `IRedirectRepository`: `findByPath`
- `ImageRepositorySQLite` → `IImageRepository`: `findByDomainAndFilename`

## PageController serving flow

1. `findByPath("/" + path)` → PageRecord metadata
2. On miss: check redirects → 301 (Location header) / 410 / 404
3. `findActiveVariantLabels(pageId)` + check `ab_variant` cookie → variant label
4. `findVariant(pageId, label)` → PageVariantRecord (html_gz)
5. Check `If-None-Match` ETag → 304
6. `s_menuCache["domain:lang"]` prepended to html_gz → send as gzip stream
7. Set `ab_variant` cookie (30 days) when multiple active variants

Menu cache loaded at startup via `PageController::loadMenuCache(IMenuRepository*)`.
A/B cookie: `ab_variant`, path `/`, max-age 30 days; `thread_local mt19937` for random assignment.

## Sitemap & robots.txt generation

Generated by `SitemapOrchestrator::generate()` as part of `LauncherPublish::run()` (step 6, after HTML pages are written). All files are gzip-compressed and stored in `page_variants`, served by `PageController::serveFile` like any other page.

Output files:
- `/sitemap.xml` — `<sitemapindex>` listing all child sitemaps
- `/sitemap-recent.xml` — pages updated within the last 7 days (listed first for priority)
- `/sitemap-{lang}-N.xml` — per-language chunks, up to 50,000 URLs each (N is 1-indexed)
- `/robots.txt` — `Allow: /`, disallows legal/utility pages, points to sitemap

Submit `https://{domain}/sitemap.xml` to Google Search Console after deploying.

## Dependencies (not yet installed as of 2026-04-01)

- **Drogon** — build from source (see install notes in StaticWebsiteServe/CMakeLists.txt)
- **SQLiteCpp** — fetched automatically via CMake FetchContent (no manual install needed)
