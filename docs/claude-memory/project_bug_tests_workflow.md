---
name: project-bug-tests-workflow
description: "Bug-proven unit test workflow — tracking file location, script path, first-run results, known NOT_PROVEN bugs to retry"
metadata: 
  node_type: memory
  type: project
  originSessionId: 4dacd386-545a-41fc-8a1c-4e07fd5e681c
---

Workflow that finds bug-fix commits in git history, writes unit tests, then **proves** each test by toggling the bug on/off (revert → test must FAIL; restore → test must PASS).

**Tracking file:** `BUG_TESTS/BUG_TESTS.csv` (repo root)  
**README:** `BUG_TESTS/README.md`  
**Workflow script:** `.claude/projects/.../workflows/scripts/bug-proven-unit-tests-wf_69cf5435-561.js`  
**Run ID for resume:** `wf_69cf5435-561`

**Why:** Resume uses `resumeFromRunId` — the script reads `BUG_TESTS.csv` at start and skips hashes already marked PROVEN or SKIPPED.

## First-run results (2026-06-22)

| Category | Count |
|---|---|
| Bug-fix commits found | 23 |
| PROVEN (test written + validated) | 7 |
| NOT_PROVEN | 9 |
| BUILD_FAILED | 1 |
| SKIPPED (untestable) | 6 |

## PROVEN tests in the codebase

- `test_pagegen_article_untranslated_lang_excluded_from_available_pages` — `test_page_generator.cpp`
- `test_pagegen_hub_diacritic_slug_strips_accents` — `test_page_generator.cpp` (also exposed `PageGenerator::categoryHubSlug` as public static)
- `test_pagebloccategory_translated_name_uses_english_permalink` — `test_website_page_bloc_category.cpp`
- `test_protocol_parse_duplicate_field_id_appends_content` — `test_translation_protocol.cpp` (was already committed)
- `test_imagerepository_findbydomainandfilename_falls_back_to_empty_domain` — `StaticWebsiteServeTests/test_repositories.cpp`
- `test_pagecontroller_servefile_sitemap_xml_content_type` — NEW `StaticWebsiteServeTests/test_page_controller.cpp`
- `test_imagecontroller_serveimage_svg_content_type` — NEW `StaticWebsiteServeTests/test_image_controller.cpp`

## NOT_PROVEN bugs to retry next run

See `BUG_TESTS/README.md` for full table. Key ones:
- `7e4b7f8` — SVG blob origin: test class written but absent at HEAD; needs separate commit
- `abcdeeee`, `7f96e750`, `a0e4b7ce`, `1cbbae66`, `ff0a76b5` — test methods missing (agents claimed success but never wrote them)
- `b8d3dda4`, `176e09aa` — test and fix co-committed; can't prove by revert
- `2283de67` — revert causes merge conflict (symbol later used by many callers)

## Bug fixed in workflow script

`git restore .` in the Validate phase was wiping test files written by TestWrite.  
**Fix applied:** Step 6 now restores ONLY the files from the bug commit (not all tracked files).  
Command: `git show <hash> --name-only | tail -n +7 | xargs git restore`
