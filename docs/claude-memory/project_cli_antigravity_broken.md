---
name: project_cli_antigravity_broken
description: Antigravity CLI updates were broken (agy ignores stdin); FIXED 2026-07-11 via configurePromptProcess
metadata: 
  node_type: memory
  type: project
  originSessionId: 278f6cc4-cfae-4474-a95f-4ded21e9d5b3
---

For WebsiteEmpire `--update` (and generation) runs, the **Antigravity** CLI reports `OK`/`done` but never actually changes the article — the returned text is effectively a no-op. The **Claude** CLI works correctly and does apply the requested changes. User confirmed this on 2026-07-04.

**Root cause found 2026-07-11:** `agy` does NOT read the prompt from stdin. `--print` is a Go string flag whose *value* is the prompt text. `CliAntigravity::promptArgs()` (in `/home/cedric/Applications/common/aicli/CliAntigravity.cpp`) returns `{--print, -, ...}` following the Claude Code `-p -` stdin convention, so agy receives the literal prompt `"-"` and replies "your request was blank (`-`)" or goes agentic until `--print-timeout` fires. The real prompt fed via `setStandardInputFile` is silently ignored. Verified: `agy --print "Reply PONG"` works; piping to stdin does not. Fix requires passing the prompt as the `--print` argument value (watch Linux MAX_ARG_STRLEN 128 KB per-arg limit — the stdin convention existed to avoid it).

**FIXED 2026-07-11:** `AbstractCli` gained `promptViaStdin()` (default true, false for Antigravity) and `configurePromptProcess(process, baseArgs, prompt, promptPath)`, which substitutes the prompt text for the `-` placeholder for argv-based CLIs. All call sites converted (AbstractCli runPrompt/runPromptAsync, ClaudeRunner, LauncherUpdate/Generation/Improve, PageTranslator, TaxonomyTranslator, CategoryTranslator, HubSeoTranslator, test_generation_coroutine). `runUpdateClaudePrompt` now also calls `cli->preparePrompt()`. Verified end-to-end: `--update --cli Antigravity --limit 1` succeeded first attempt (<1 min vs former 3×20-min timeouts) and added 9 checked symptoms to /rheumatoid-arthritis-genes-biomarkers. Remaining gap: `CommonBlocTranslator` hardcodes Claude-specific flags (`--output-format stream-json`) — still Claude-only. Argv prompts are capped by Linux MAX_ARG_STRLEN (128 KB per arg); larger prompts fail to start and agy has no stdin fallback.

Note: a successful `--update` only rewrites `1_text` in `pages.db` (Level 2). The served site is `deploy/<lang>/content.db` (Level 3) and only refreshes on a **publish** (`PageGenerator::generateAll` wipes and rebuilds content.db from pages.db). See [[project_pipeline_architecture]].
