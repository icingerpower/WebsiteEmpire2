---
name: product-url-scheme
description: Pradize product URLs served byte-exact at /product/<slug> (no trailing slash); image-order scraper bug; cutover gates
metadata:
  node_type: memory
  type: project
  originSessionId: f949a749-5e55-46c3-ace7-2b2bad071483
---

Goal reminder: Pradize (WebsiteEcom) must be a 1:1 copy of pradize.com (a CommerceHQ store) so the
CommerceHQ membership can be cancelled. See [[aspire-import-future]], [[project-pipeline-architecture]].

DECIDED + BUILT 2026-07-23 (ADR-043 Rev 2, all gates green):
- **Product URLs served in place at `/product/<slug>` with NO trailing slash** as the sole canonical —
  byte-exact copy of pradize.com. Collections keep `/collections/<slug>/` (trailing slash). Mixed
  slash policy accepted consciously (P1 = option a). Single-source helpers in `permalinks/paths.py`
  (`product_permalink_slug`, `storefront_path`); `register_product` stores `product/<slug>`; `"product"`
  reserved. Data migration `permalinks/migrations/0007_reprefix_product_permalinks.py` (idempotent,
  reversible). `/product/<slug>` → 200, `/product/<slug>/` → 301, old bare `/<slug>/` → 404 (P2:
  no backcompat redirect, we're pre-prod). SEO-approved (canonical/sitemap/hreflang/JSON-LD all
  single-sourced, no slash split). Full repo suite: 4671 tests, 0 regressions.

- **Image order bug (WebsiteAspire, C++)**: product images were stored in network-COMPLETION order,
  not URL order (concurrent download appended on reply-finish), and failed decodes were silently
  dropped, shifting positions. Fixed via new `WebsiteEmpireLib/aspire/downloader/OrderedImageCollector`
  + `OrderedImageDownloader` (positional insert by URL index, explicit per-slot failure). 3 call sites
  routed through it (LauncherDownload.cpp, WidgetDownloader.cpp x2). Regression test proven.
  pradize.com is CommerceHQ (not Shopify) — image objects are only {id, max_size, path}, NO position
  field; raw array order IS authoritative. Takes effect only on RE-DOWNLOAD + re-import.

- **Gallery cropping** fixed: `object-fit: cover` → `contain` (letterbox) in fashion theme.css
  (main image + thumbnails), neutral surface bg, max-height 80vh.

CUTOVER GATES still open before repointing pradize.com (SEO flagged CRITICAL):
1. **D4 `generate_legacy_url_redirects` NOT yet built** — 20-22% of imported slugs DRIFT from the
   original CommerceHQ slug (793/3951), so ~1 in 5 Pinterest/Google links would 404 at cutover without
   drift-only 301 rows (product/<orig> → product/<ours>, derived from ImportedRecordLink.source_key).
2. **Publication state**: only 147/3951 products active in the scratch demo DB — cutover checklist must
   verify the intended catalog is fully published vs the old sitemap.
3. Re-scrape pradize.com — DONE 2026-07-26 (ran ~59h, throttled by Cloudflare; SIGINT-stopped once stuck in an
   unterminating collection-API pagination loop, page 255+ — a real scraper pagination-termination bug to fix
   next time). Result: 5,705 products / 29.9GB at /home/cedric/aspire_rescrape/aspire/pradize.db (LOCAL, integrity OK),
   a SUPERSET of the April 3,951 catalog. Headless CLI used: `WebsiteAspire --workingDir /home/cedric/aspire_rescrape
   --download pradize` (QT_QPA_PLATFORM=offscreen, nohup). Both C++ fixes were in the binary: image ORDER
   (OrderedImageCollector/Downloader, needed an out-of-line def of PageAttributesProduct::MIN_IMAGE_SIDE_PX to link
   at -O0) + small-image keep-product.

4. Import DONE 2026-07-26 into a FRESH isolated scratch DB (SCRATCH_TMP=/home/cedric/.claude/jobs/f949a749/tmp/importtest2,
   Pradize store pk=2 en/USD). `import_aspired_catalog --store-id 2 --source .../pradize.db --currency USD --rewrite-mode ASSESS`
   → created 5,705 products, 30,279 images (+640 deduped), 38 collections, 0 errors, 2.6GB WebP media. Image order
   verified (champagne product primary = source's 1st image). Full catalog published (all products active + published
   en translations → 5,743 /product/<slug> permalinks) + 2-level menu built (build_menu.py, 10 top-level). Fashion
   theme created+assigned (Theme source_ref='fashion' published, store.theme FK). Demo on :8099 needs: StoreDomain
   localhost->store2 + StoreLanguage(store2,en,domain=localhost,use_path_prefix=False) [unique(store,lang_code)!] +
   fashion Theme assigned, else 404/unstyled. Demo helper scripts in job tmp: publish_all.py (BUG: modifies rows during
   .iterator() → re-loops, but idempotent so end-state correct), create_stores.py (needs WebsiteEcom on PYTHONPATH), build_menu.py.

ENRICHMENT (ADR-039) DONE on scratch importtest2 (2026-07-27):
- Sizes: DONE deterministically (size_fix + measurements) — canonical US-2|UK/AU-6|EU/DE-32|FR/ES-34 live in dropdown.
- Descriptions: 5,669/5,705 (99.4%) agy rewrites generated, all AWAITING_REVIEW. Switched rewrite_mode ASSESS→ALWAYS
  (100% of raw scored 0-39). run_ai_jobs_cli DEFAULT --limit is 50 (pass --limit 6000 for full run). Executor=agy
  (/home/cedric/.local/bin/agy, no direct API). 36 residual failures left with raw text — mostly attribute_whitelist
  (agy adds silk/polyester/leather not in source; guard correctly blocks — needs Opus escalation or manual, not worth grinding).
- Rewrite output stored on AiJobOutput (fields: content, field_id, run, is_accepted) via run__job; NOT on ProductEnrichmentState.

D4 legacy redirects DONE (generate_legacy_url_redirects built + 15 tests): on the 5,705 catalog → 4,485 already-canonical
(zero-hop), 1,056 drift 301s created, 164 skipped (renamed-collision). Verified live 301/200.

PREMIUM PROMPT (2026-07-27): rewrite prompt upgraded in catalog/enrichment_ai.py (_PRODUCT_REWRITE_TEMPLATE) —
premium editorial fashion voice + STRIP shipping/returns/stock/pricing policy (ADR-044 Part 1). New ERROR check
`check_policy_content` (phrase-level lexicon) + check_length_ratio normalizes source via _strip_policy_sentences,
in catalog/enrichment_checks.py. 140 enrichment tests + full catalog (841) pass. Validated on 15-product sample
(incl champagne) — all passed every check, policy gone, tone premium; approved + LIVE on :8099 demo for Cédric review.
These 4 enrichment files (enrichment_ai.py, enrichment_checks.py, test_enrichment_ai_jobs.py, test_enrichment_checks.py)
are UNCOMMITTED (the 4 URL/D4/cropping/ADR-044 commits on WebsiteEcom branch master EXCLUDED them).
NOTE: WebsiteEcom is its OWN git repo on branch **master** (parent WebsiteEmpire2 .gitignores it, is on main w/ the C++ fixes).

CONSEQUENCE: product pages now have NO shipping/returns reassurance text at all — it MUST return via ADR-044 Part 2
global policy block (3-level cascade, designed not built). Build Part 2 before cutover.

SIZE GUIDE (catalog/services/size_guide.py, render-time, no migration, 2026-08-01/02): self-validating +
range-deriving. Bust/Waist/Hips shown as RANGES (Hips from source your_hips; Bust/Waist derived low=point,
high=point*(1+0.14), 14% tolerance calibrated from 2,674 hip pairs); Length kept SINGLE (garment height, Cédric's
rule); broken columns (implausible/non-monotonic) DROPPED + logged; guide hidden if nothing survives. WAIST gated
to BOTTOMS ONLY (WAIST_CATEGORY_ALLOWLIST = skirt/skirts/pants/trousers/shorts/leggings/two-piece bottoms) via
CollectionProduct membership (word-boundary match; product_type only has 3 values, useless); dresses/tops hide waist.
Tests: catalog+storefront 1486 pass. Uncommitted (with size_guide.py + the 4 enrichment files).

PENDING DECISIONS (Cédric, when back): (a) validate the 15 live premium samples' tone; (b) if good, WIDER regenerate
all ~5,700 with new prompt (~19h agy run, replaces the old policy-laden AWAITING_REVIEW rewrites) then approve;
(c) build ADR-044 Part 2 global policy block; (d) commit the 4 enrichment files; (e) C++ fixes commit in parent repo.

SUPERSEDED PENDING: (a) REVIEW/APPROVAL of the 5,669 rewrites to go LIVE (E6 gate: batch-approve unlocks after 200 individually
reviewed; approve scripts in job tmp: approve_batch.py/approve_all.py) — nothing rewritten shows on storefront until approved;
(b) production cutover (import into real prod DB, run D4, publish, repoint DNS, cancel CommerceHQ). Demo still on :8099 → importtest2.

Demo DB (scratch, throwaway): /home/cedric/.claude/jobs/f949a749/tmp/importtest/scratch_default.sqlite3
store pk=2 = Pradize (en/USD). Server on :8099 via scratch_settings.py + scratch_urls.py (--noreload
--nostatic, no-cache static). Recurring: setsid/nohup servers die (exit 144) — use run_in_background.
