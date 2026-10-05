---
name: project_generation_never_overwrites_text
description: Article generation never overwrites existing 1_text; --update changes are safe from later generate runs
metadata: 
  node_type: memory
  type: project
  originSessionId: 278f6cc4-cfae-4474-a95f-4ded21e9d5b3
---

Running article generation (`--update`-independent generation launcher) after an `--update` run does **not** cancel or overwrite the updated article text.

Why (LauncherGeneration.cpp):
- New-article generation only writes `1_text` for pages that do not yet exist (id==0, new permalink). All existing permalinks are excluded from the source-DB virtual-page list (~lines 928-938).
- `findPendingByTypeId` additionally excludes any page that already has `page_data`.
- The retry queue only re-queues pages at `ContentReady` (state 1) or `MainImageReady` (state 2, with SocialMedia flag). Pages at `Complete` (3) / `SocialComplete` (4) are never re-queued.
- Even the "smart retry" path for re-queued pages regenerates only SVG + social-media images — it reads existing `1_text` (never rewrites it).

So the "N retry page(s) queued" / "wanted to update previous article" message during generation only reprocesses IMAGES of incomplete pages; it does not touch article text. See [[project_pipeline_architecture]].
