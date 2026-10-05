---
name: project-pradize-agent-team
description: "Pradize Django ecommerce engine in WebsiteEcom/ — 11-agent team setup, spec sources, workflow gates"
metadata: 
  node_type: memory
  type: project
  originSessionId: 2fb2255e-bacf-4a07-9f5d-ff3310d355c6
---

Pradize = multi-tenant Django ecommerce engine being built in `WebsiteEmpire2/WebsiteEcom/` (empty as of 2026-07-02; will deploy to VPS 2 per [[project-hosting-architecture]]).

- Spec sources: `/home/cedric/Dropbox/Applications/WebsiteEmpire2/spec-ecom/` — ~82 screenshots (admin-001..021 + super-admin-05..13), 5 super-admin SVG diagrams (page map, organizations, processor accounts, org rules, payment methods), `extra-spec-ecom.txt` (extra requirements), `design-pattern-ideas.txt` (15 binding architecture lessons from WebsiteEmpire2 — Architect must read before any decision).
- Agent team: 10 subagent definitions in `WebsiteEmpire2/.claude/agents/` (spec-agent, spec-reviewer, architect, safety-agent, seo-agent on Fable; developer, test-agent, after-bug-test, designer, release-manager on Sonnet). Master Orchestrator = main session; workflow/gates/DoD in `WebsiteEcom/CLAUDE.md`.
- Specs live in `WebsiteEcom/specs/ecommerce_engine/` (00–12 numbered files + screen_inventory_parts/).
- Hard rules: spec → architecture → implementation → tests → review → safety → release; no coding before spec approval; supplier DB import is out of scope for now; Fable only for high-leverage work (screenshot-to-spec, spec review, ADRs, security audits, SEO architecture).

**Why:** the user wants strict gate enforcement and will validate critical choices personally.
**How to apply:** when working in WebsiteEcom, act as orchestrator, delegate via the named subagents, never let the developer agent bypass spec/test/review gates.
