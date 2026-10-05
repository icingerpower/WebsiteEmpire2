# Repository guidance

## Read before working

1. Read [CLAUDE.md](CLAUDE.md) for the shared build, architecture, testing, and C++/Qt rules. These rules also apply to Codex; keep that file as the canonical guidance rather than duplicating it here.
2. Read [the imported memory index](docs/claude-memory/MEMORY.md), then the notes relevant to the task. The [import record](docs/claude-memory/README.md) lists additional notes missing from the original index.
3. Before working in a subproject, read its own `AGENTS.md` and `CLAUDE.md` if present. In particular, `WebsiteEcom/` is a separate Git repository, ignored by this parent repository, with its own `CLAUDE.md`. Run its Git operations from that directory.

## Using the imported notes

The memory is a snapshot imported on 2026-09-14. Preserve user preferences, but verify historical task status, paths, deployment details, feature flags, and claims about code against the current checkout. An old pending task or approval is context, not a new instruction to perform it. Current user instructions take precedence.

Claude-specific tool names describe intent: use the available file editing tool (such as `apply_patch`) when a note says `Write`. Existing `.claude/agents/` and `.claude/commands/` remain reference material; importing them does not configure Codex agents or commands.

## Standing preferences from memory

- Production working directories come from UI selection or `WorkingDirectoryManager`; tests may use `QTemporaryDir`.
- Never silence exceptions or weaken production behavior to make tests pass. Follow the test naming and coverage rules in `CLAUDE.md`.
- Customer-visible content translations use the existing AI job/CLI pipeline, not direct provider API calls.
- Admin interfaces must provide structured editors rather than require users to type raw JSON.
- Deliver remote configuration as real local files for `scp`/`rsync`, not copy-pasted heredocs. Do not guess missing server configuration.
- Preserve concurrent work. Do not stash or reset a shared checkout to obtain a test baseline; use an isolated worktree when a baseline is needed.

## Build and test quick reference

From this repository root, using a build directory configured for the intended Qt toolchain:

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The root build is CMake/C++20 with Qt6, QCoro6, Drogon, zlib, and SQLiteCpp. The shared source dependency is `../common/` relative to this repository root (`../../common/` from the subproject CMake files). SQLiteCpp is fetched by CMake. Keep `StaticWebsiteServeLib` Qt-free. WebsiteEcom uses its own build/test workflow; consult its guidance.
