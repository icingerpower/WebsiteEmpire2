# Bug-Proven Unit Tests

This folder tracks the ongoing effort to cover every historical bug fix with a unit test
that has been **proven** to detect the regression — not just a test that happens to pass.

## What "proven" means

A test is `PROVEN` only if:
1. The test passes at HEAD (the fix is present).
2. The fix commit is reverted (`git revert --no-commit`) → the test **fails**.
3. The revert is removed (`git restore <bug-files>`) → the test **passes** again.

A test that passes on a fixed codebase but was never run against the buggy one is not proven.

## Tracking file

`BUG_TESTS.csv` — one row per bug-fix commit found in the last 100 commits.

| Column | Description |
|---|---|
| `commit_hash` | Full 40-char git hash of the fix commit |
| `commit_message` | One-line commit message |
| `test_file` | Path to the test file (relative to repo root) |
| `test_method` | Slot name (Qt) or `DROGON_TEST` name |
| `status` | See status legend below |
| `notes` | Reason for non-PROVEN status, or proof summary |

### Status legend

| Status | Meaning |
|---|---|
| `PROVEN` | Test written AND validated (fails with bug, passes with fix) |
| `TEST_WRITTEN` | Test written; validation pending |
| `NOT_PROVEN` | Validation ran but test did not correctly catch the bug |
| `BUILD_FAILED` | cmake/make failed during validation |
| `WRITE_FAILED` | Could not write a unit test for this bug |
| `SKIPPED` | Not unit-testable (GUI, SSH/network, deploy scripts, etc.) |

## How to run the workflow

The workflow script lives at:
```
.claude/projects/.../workflows/scripts/bug-proven-unit-tests-wf_69cf5435-561.js
```

To resume from where you left off (skips all `PROVEN`/`SKIPPED` rows in the CSV):

```
Workflow({
  scriptPath: ".claude/projects/.../workflows/scripts/bug-proven-unit-tests-wf_69cf5435-561.js",
  resumeFromRunId: "wf_69cf5435-561"
})
```

On each run, the workflow:
1. Reads `BUG_TESTS.csv` and collects hashes already marked `PROVEN` or `SKIPPED`
2. Scans the last 100 commits for bug fixes not already tracked
3. Writes unit tests for new testable bugs
4. Validates each test (reintroduce bug → must fail; remove → must pass)
5. Merges results back into `BUG_TESTS.csv`

## NOT_PROVEN root causes (first run)

Several bugs could not be fully proven. Common reasons:

### 1. Test and fix co-committed
Commits `b8d3dda4` and `176e09aa` added both the production fix and its regression test in
the same commit. Reverting the fix also removes the test, so the test cannot fail in the
buggy state. **Fix:** separate test commits from fix commits in future work.

### 2. Test not actually written
A few TestWrite agents reported `success=true` but the test method was never found at
validation time. This was the main failure mode — the agents hallucinated success.
These bugs need a fresh manual TestWrite pass.

### 3. Revert creates merge conflict
Commit `2283de67` introduced `resolvePermalink()`, which was later called from many other
files. Reverting it at HEAD causes compilation errors because the callers still reference
the removed symbol. **Fix:** write the test without reverting — test the behavior directly.

### 4. Test does not call production code
`aac5d2df` — the written test uses only local helper functions and never calls
`LauncherUpdate.cpp`, so it passes in every state of the production code.
**Fix:** rewrite to call the actual production path.

## NOT_PROVEN bugs to re-attempt

| Commit | Message | Reason | Action needed |
|---|---|---|---|
| `7e4b7f8` | Fix SVG: accept blob origin | Test class absent from HEAD | Commit the new test class separately |
| `abcdeeee` | Fix hub hreflang | Test method missing | Rewrite TestWrite |
| `7f96e750` | Fix hub grid CJK excerpt | Test method missing | Rewrite TestWrite |
| `a0e4b7ce` | Fix sitemap chunk URLs | Test method missing | Rewrite TestWrite |
| `b8d3dda4` | Strip _partN continuation suffix | Test co-committed with fix | Accept as-is (test exists, just not provable by revert) |
| `176e09aa` | Fix menu translation field IDs | Test co-committed with fix | Accept as-is |
| `aac5d2df` | Fix SVG write to images.db | Test doesn't call production code | Rewrite to use LauncherUpdate |
| `1cbbae66` | Fix page update with different prompts | Test method missing | Rewrite TestWrite |
| `ff0a76b5` | Fix sitesearch param for non-English | Test class missing | Rewrite TestWrite |
