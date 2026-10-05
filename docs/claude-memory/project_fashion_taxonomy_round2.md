---
name: project-fashion-taxonomy-round2
description: "fashion_taxonomy round-2 expansion — VOCAB_ROUND mechanism, 19 combo tables, 1.35M candidates targeting 1M+ pages"
metadata: 
  node_type: memory
  type: project
  originSessionId: a5b2d84d-664f-4ed4-ab4e-d10754e04c0e
  modified: 2026-09-04T08:09:54.258Z
---

Round 1 of `fashion_taxonomy` exhausted its full cross-product on 2026-08-23: 41,606 candidates, 35,092 recorded (84.3% acceptance). Cédric wanted ≥1M pages, so a round-2 expansion was built the same day (grounded in an `agy --print` keyword-research pass, since Antigravity was trained with Google Ads volume / Trends data):

- Vocab grown: products 25→124, colors 48→84, events 20→55, styles 12→27, fits 13→23, materials 10→22, patterns 10→22, demographics 8→15, seasons 8→12.
- 10 new combo tables (style_product, product_event, color_season, product_demographic, style_event, material_product, style_product_event, color_product_demographic, product_demographic_event, pattern_product_season) + second formula `does_color_go_with` on color_color → 19 combo tables total.
- New total: **1,347,317 theoretical candidates** (~1.13M expected recorded), ~33,700 jobs of 40.

**Key invariant — VOCAB_ROUND** (`GeneratorFashionTaxonomy::VOCAB_ROUND`, currently 2): pages are fixed slices of the cross-product and vocab values are consumed alphabetically (`loadVocabValues` ORDER BY), so ANY seed-list change re-maps every page. Whenever seed lists change after a round has been dispatched, bump VOCAB_ROUND — job ids gain a `.r<round>` key suffix (`combo/color_product.r2/0`), the expanded space re-walks under fresh ids, old Done pages stay dormant, and `rowAlreadyRecorded()` prevents duplicates (re-walk only costs re-assessment). Never edit the .ini by hand for this (the old `.bak` files were that workaround).

The biggest tables: color_product_event 572,880 / style_product_event 184,140 / fit_product_event 156,860 / color_product_demographic 156,240 / product_demographic_event 102,300.

**Article-generation wiring (2026-09-04):** all 19 combo tables are now independently selectable as a generation strategy's source (`GeneratorFashionTaxonomy::getTables()` puts all 19 in `primary`, not just `ColorProductEvent`; `AbstractGenerator::GeneratorTables::primary` semantics changed from "exactly 1 entry" to "0..N entries"). Each combo class implements `AbstractPageAttributes::composeArticleTopic(rowValues)` (new virtual, pure-virtual on `PageAttributesFashionComboBase`) to render its own topic text (e.g. "Black Dress for Funeral"), replacing the old "first non-id DB column" heuristic in `LauncherGeneration.cpp` that used to expose `combo_formula_id` ("direct_transactional") as the topic. `LauncherGeneration` also now orders by `combo_msv DESC` when present (search-volume-first generation) and has a same-run slug dedup. `DialogAddGeneration`'s Source-table picker lists all 19; `GenStrategyTable` caches the id→display-name lookup (was O(generators×primary) per cell render). See plan file (implemented, not just planned) for exact per-table topic templates if extending to a 20th combo table — a missing `composeArticleTopic()` override is a compile error, not a silent bug.
