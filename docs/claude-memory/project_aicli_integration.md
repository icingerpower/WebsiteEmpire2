---
name: aicli integration
description: How common/aicli is wired into WebsiteEmpire/WebsiteAspire for multi-CLI support
type: project
originSessionId: 88ace692-d35e-45e5-944a-04410ebf5d21
---
`/home/cedric/Applications/common/aicli/` provides AbstractCli, AvailableCliTable, AvailableCliList, and concrete CLIs (CliClaude, Gemini, Codex, Deepseek, Kimi, Mistral).

**Integration**: AICLI_FILES is compiled into WebsiteEmpireLib (via CMakeLists.txt include). All executables/tests get it through --whole-archive.

**CLI selection**: `AbstractCli::ALL_CLIS().first()` is the default (CliClaude). Launchers accept `--cli <name>` arg.

**Settings key**: `"defaultCli"` in `WorkingDirectoryManager::instance()->settings()`. Set by PaneSettings comboDefaultCli.

**PaneSettings** (WebsiteEmpire): has a vertical QSplitter — top: settings table, bottom: AvailableCliTable (all CLIs + async availability) + comboDefaultCli (AvailableCliList filtered to available).

**Launchers updated** (all parse `--cli <name>` and pass AbstractCli* through):
- WebsiteAspire: ClaudeRunner::runClaudeJob(jobJson, cli), LauncherRunJobs
- WebsiteEmpire: LauncherGeneration (runClaudePrompt, runGenerationSession), LauncherImprove (processImproveJob, runImproveSession), LauncherUpdate (runUpdateClaudePrompt, runUpdateSession)

**PaneGeneration** injects `--cli <name>` into `_startProcess(QStringList args)` (note: by-value now) and `viewGenCommand()`.

**WidgetGenerator** (WebsiteAspire GUI): uses `AbstractCli::ALL_CLIS().first()` directly (no GUI selector yet).

**Why:** Abstraction from hardcoded "claude" so users can choose between Claude, Gemini, Codex, etc.
