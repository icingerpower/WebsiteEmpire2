---
name: working_directory_sources
description: Working dirs must come from UI selection or WorkingDirectoryManager, never Qt standard paths
type: feedback
---

Working directories must come from exactly two approved sources:
1. UI user selection (passed explicitly)
2. `WorkingDirectoryManager::instance()->workingDir().path()` (or a subdirectory)

**Why:** The project manages all data under a single user-chosen root. Using Qt standard paths (`QStandardPaths`, `QDir::temp()`, `QDir::home()`, etc.) bypasses this and scatters data in unexpected locations — this was caught when `PaneAspire` was using `QStandardPaths::AppLocalDataLocation`.

**How to apply:** In any production code that needs a working dir, always receive it as a parameter or retrieve it via `WorkingDirectoryManager::instance()->workingDir()`. The only acceptable exception is tests, which use `QTemporaryDir`.
