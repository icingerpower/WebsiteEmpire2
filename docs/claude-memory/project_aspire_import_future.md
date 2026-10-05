---
name: aspire-import-future
description: Future goal — import WebsiteAspire-scraped sites into the Pradize ecommerce engine; source data location
metadata: 
  node_type: memory
  type: project
  originSessionId: f949a749-5e55-46c3-ace7-2b2bad071483
---

Cédric's stated future goal (2026-07-11): import websites aspired by WebsiteAspire (the Qt C++ scraper)
into the Pradize/WebsiteEcom Django engine. Two sites were already aspired, data at:
`/home/cedric/Dropbox/freelancers/projects/workingDirectory/WebsiteEmpire2_aspire/aspire/aspire`

STATUS (2026-07-17): BUILT and gate-cleared. Cédric approved ADR-038 + ADR-039 (§0 ledger)
and the import was implemented end-to-end in this session, DRAFT-only, all spec+safety gates green.

- **ADR-038 Phase 1 importer** (`import_aspired_catalog` mgmt command + `AspireCatalogImporter`
  service + `ImportedRecordLink` model): parses both AspiredDb profiles (pradize.db fashion USD,
  vogelvoerkopen.db pet-food EUR), decodes Qt QDataStream image blobs → WebP, DRAFT products,
  size-only variant axis (color/sexe/age → tags), idempotent re-import preserving human edits.
  Security-hardened: XSS fail-loud on scraped HTML, pixel-bomb + blob-size caps, read-only source DB.
- **ADR-039 Phase 2 enrichment**: rewrite_mode ALWAYS/NEVER/ASSESS; AiJob types rewrite_assess
  (Haiku), product_rewrite (Sonnet→Opus on 2nd reject, NO Fable), size_chart_fix; all via the
  `run_ai_jobs` CLI runner (no direct API, AST-enforced); automatic-control checks; the interactive
  `review_enrichments` human QC CLI (E6 200-warmup batch gate); a state-machine transition guard
  so nothing bypasses human review; enrichment-ready NEVER auto-publishes (stays DRAFT).

STATUS (2026-07-19): BUILT + VALIDATED AT FULL SCALE. The entire import→enrichment pipeline
ran end-to-end on the full 3,951-product pradize catalog (isolated scratch DB): import (0 errors,
21k WebP images), canonical sizes (2,494 women's-clothing), measurements, 144 agy rewrites approved,
146 enriched products published + screenshot-verified (premium storefront). Runbook:
docs/IMPORT_ENRICHMENT_RUNBOOK.md (the authoritative production procedure).

Key pieces: `run_ai_jobs_cli --executor agy` bridges the enrichment pipeline to the agy/claude CLI
(subprocess, no direct API). agy rate ≈12s/product → ~13h for 3,951 (run overnight / batched).
Scale-only bugs found + fixed during the full run (would block ANY production import): slug
collisions (idempotent SHA-256 dedup), per-row error isolation, length_ratio floor 0.5→0.33
(agy writes concise ~0.45), collection-page draft-leak (count/paginate visible-only).

OPERATIONAL: import from a LOCAL COPY of pradize.db, never Dropbox (command refuses unless
--allow-dropbox-path). Follow-ups: women's SHOE conversion table (shoes skip the clothing table);
F6 translation S3 activation (before any 2nd language); F7 collection-level enrichment. The full
production run into the REAL dev/prod DB is Cédric's to execute per the runbook. Related: [[project-pipeline-architecture]].
