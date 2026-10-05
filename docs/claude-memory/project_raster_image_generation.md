---
name: raster-image-generation-pipeline
description: "How strategy-driven raster (non-SVG) AI image generation works — GenStrategyTable fields, GenPageQueue raster prompts, the generate/review/retry loop, per-image tracking, and the publish-time durability gate"
metadata: 
  node_type: memory
  type: project
  originSessionId: f918aa22-775f-4416-ab29-0f92b87429d1
  modified: 2026-09-02T09:42:18.724Z
---

Built 2026-09-02 for a fashion listicle use case ("What shoes to wear with a burgundy
dress? 10 ideas" — LAYR & LAYR-inspired rule voice, 8-12 real photos, one per idea, on
the Fashion taxonomy site). Before this, only SVG (vector, text-based) image generation
worked — CLIs are only ever read via stdout text, which can't carry raster bytes.

## Strategy schema (GenStrategyTable)
- `imageInstructions` (free text, was a dead field before this) — style/voice guidance
  for raster images, e.g. "photorealistic fashion photography, LAYR & LAYR rule voice".
  Non-empty enables `GenPageQueue::wantsRasterImage()`.
- `imageCountMin`/`imageCountMax` (ints, default 0/0 = unenforced) — 0 means no count
  stated to the AI and no gating; when set, `buildContentPrompt()` states the exact
  range, over-long AI output is trimmed to `imageCountMax` before attempting generation
  (bounds CLI-call cost), and under-count (< `imageCountMin`) blocks the page from
  reaching `Complete` (same fallback as an SVG the AI never referenced — page stays
  `ContentReady`, next run's content regen gets another shot).
- Edited in PaneGeneration's "Image" tab (mirrors the SVG tab exactly), persisted to
  strategies.json.

## Antigravity CLI collision (real pre-existing bug, fixed as a prerequisite)
`CliAntigravity::preparePrompt()` used to match the substring "Image filename" to decide
whether to inject a "use generate_image tool, write a file" preamble — but the existing
SVG prompt also contains that string while explicitly requiring text-only output. Fixed
by keying off `AbstractCli::RASTER_IMAGE_PROMPT_MARKER`, a sentinel only
`GenPageQueue::buildRasterImagePrompt()` emits.

## Generation flow (LauncherGeneration.cpp)
Non-.svg `[IMGFIX]` refs (previously silently skipped) now drive
`runRasterImageGeneration()` per ref: bounded-timeout (5 min) generate call →
`QImage::load()` the output file → bounded-timeout review call (grades OK/FAIL like
LauncherUpdate's `runSvgReview()`) → up to 3 attempts with reviewer feedback folded in →
`ImageWriter::writeQImage()` on success. Requires `cli->canGenImages()` (Codex/Antigravity
only) — a page just stays `ContentReady` with an incapable CLI, never fabricates failure.

## Durability (crash/quota-safety — explicit user requirement)
- `cli->classifyError()` (QuotaExceeded/AuthRequired) on either the generate or review
  call pauses the whole session gracefully — NOT recorded as a failed attempt, status
  stays Pending, no attempts burned. Next `--generation` run resumes cleanly.
- Per-image outcomes durably tracked in new `page_raster_images` table (page_id, ref_id,
  status Pending/Success/FailedFinal, attempts, last_error) via `IPageRepository`:
  `ensureRasterImagePending`, `recordRasterImageAttempt`, `allRasterImagesTerminal`
  (Success or FailedFinal — unblocks `Complete`), `allRasterImagesSuccess` (Success only
  — the stricter publish gate).
- **Publish-time gate** (`PageGenerator::_writePage` call site, `PageGenerator.cpp`):
  a page with any raster image not yet `Success` is skipped entirely during publish —
  the previously-published variant (if any) is left untouched. This is the actual
  guarantee against a crash/quota-interrupted article going live half-imaged; the
  `Complete` state alone was not sufficient since `PageGenerator` never checked
  `generation_state` before this and published on `1_text` non-empty alone.

## Known gap (out of scope, flagged not fixed)
This is a general pattern already used for SVG, not something this feature introduced:
retrying a `ContentReady` page fully regenerates article content (not just images) —
previously-successful image filenames may not reappear, orphaning their `images.db`
rows. Acceptable/pre-existing; not changed.

See [[project_bug_tests_workflow]] for unrelated retry-tracking conventions;
[[project_static_website_serve]] for the sibling stats-pipeline work done the same week.
