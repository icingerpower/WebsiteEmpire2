---
name: feedback-shared-cwd-hazard
description: The WebsiteEcom working dir may be shared by another concurrent Claude session; git stash there can wipe uncommitted work
metadata: 
  node_type: memory
  type: feedback
  originSessionId: f949a749-5e55-46c3-ace7-2b2bad071483
  modified: 2026-09-05T09:19:25.071Z
---

Observed 2026-09-05 (root cause corrected): while building ADR-068, my OWN
Web Push developer subagent ran `git stash` to get a "pre-my-changes baseline"
test run. Because all my subagents SHARE one working dir
(`/home/cedric/Applications/WebsiteEmpire2/WebsiteEcom`), that stash briefly wiped
the uncommitted build off disk, and the Test agent running CONCURRENTLY in the
same dir saw the files vanish, mis-attributed it to "another session," and
recovered via `git stash apply`. It was an internal collision between two of my
own concurrent subagents, NOT a foreign user session (verified: user had no
other session; no dangling stash remained).

**Why it matters:** my concurrently-running subagents are NOT isolated from each
other in git — one agent's `git stash`/`checkout`/destructive git op is visible to
all and can clobber another's uncommitted work.

**Don't tell a subagent to run a "baseline / pre-my-changes" test run** — it will
reach for `git stash` and collide with siblings. If a before/after comparison is
needed, use worktree isolation instead.

**How to apply:**
- Commit finished, verified work PROMPTLY (don't leave a big build uncommitted
  across many subagent cycles) — this is the main protection.
- When starting a large multi-agent build, consider spawning the implementing
  agent with `isolation: "worktree"` so it works on an isolated git worktree,
  immune to a foreign session's stash in the main checkout.
- If files unexpectedly vanish, check `git stash list` before assuming data loss.
- If the user has a second Claude/editor session open on this repo, suggest
  closing it during heavy uncommitted work.

**Recurred 2026-09-09 with a REAL peer session (not my subagents):** while my
Designer restyled the checkout, a separate Claude session (`websiteecom-19`, seen
via ListAgents) was doing an ADR-076 "PWA install affordance" redesign in the
SAME working dir. `git status` showed ~20 unexpected uncommitted files (pwa_install,
webpush_optin, footer, offline, 404, cart, base_checkout, pwa-register.js, ADR-076,
tests, specs, locale). Worse, we had CO-EDITED shared files: base.html (14 PWA
lines vs my 4), the 3 theme.css (5–8 PWA vs my 27), context_processors.py — so
the changes were intermixed on disk and could NOT be cleanly split into separate
commits. Detection: `git diff <file> | grep '^+' | grep -c pwa|checkout` per file
to see whose lines dominate. Resolution (Cédric chose "commit combined now"):
two labeled commits (eff0199 checkout carrying the shared files; 19182c6 the PWA
work, message explicitly stating it's the concurrent session's work committed to
prevent loss, no false Co-Authored-By). Same working tree = no merge, HEAD just
advances; the peer session continues on top with a clean tree.
**Lesson reinforced:** when ListAgents shows peer sessions and `git status` has
unexpected files, STOP before committing, classify shared vs isolated files, and
get the user's call — they own the other session.

Relates to [[project_local_8099_scratch_env]] (that's a separate settings/DB
concern, not the same-cwd stash hazard).
