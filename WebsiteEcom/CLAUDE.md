# Pradize — Django Ecommerce Engine (WebsiteEcom)

Multi-tenant Django ecommerce engine: multilingual product pages, admin + super-admin,
catalog, SEO, storefront generation, settings validation, social/media/analytics
integrations, shipping/tracking/catalog-feed extensibility, AI job orchestration.

Supplier database import/migration is **explicitly out of scope for now** — the
priority is the reusable ecommerce engine. Do not create a supplier import agent
or supplier import code.

## Main rule

**Do not rush into coding.** Build from specification → architecture →
implementation → tests → review → safety → release. No production code before
the relevant spec and architecture are approved.

## Master Orchestrator (this session)

The main Claude Code session IS the Master Orchestrator. It:
- Manages the full workflow, decides which agent acts next, maintains task state.
- Enforces gates; prevents agents from skipping specification, tests, review, safety, or release validation.
- Summarizes progress and blockers.

Orchestrator restrictions:
- Does NOT write production code (delegates to the developer agent).
- Does NOT silently change product requirements.
- Does NOT override the spec, architecture decisions, tests, or human decisions.
- If agents disagree: summarize the disagreement and request a human decision.

Orchestrator status report format (use at the end of every work cycle):
- Current task
- Current responsible agent
- Blockers
- Next action
- Files changed
- Tests run
- Remaining risks

## The team (subagents in ../.claude/agents/)

| Agent | subagent_type | Model | Use for |
|---|---|---|---|
| Spec Agent | `spec-agent` | fable | Screenshot-to-spec, spec writing/updates |
| Spec Reviewer / QA Auditor | `spec-reviewer` | fable | Compare screenshots/spec/code/UI/tests after implementation |
| Architect | `architect` | fable (override sonnet for routine refinements) | ADRs, schema, plugin/registry, AI job orchestration design |
| Developer | `developer` | sonnet | Implement approved tickets |
| Test Agent | `test-agent` | sonnet | Meaningful tests + coverage |
| After-Bug Test Agent | `after-bug-test` | sonnet | Proven regression test for every bug fix |
| Safety Agent | `safety-agent` | fable (override sonnet for patch support) | Security audits before release |
| Designer | `designer` | sonnet | UI polish, the 3 themes; behavior-preserving |
| SEO Agent | `seo-agent` | fable (override sonnet for implementation) | SEO architecture + review of indexable pages |
| Release Manager | `release-manager` | sonnet | Final gate: checklist, rollback plan, verdict |

Model usage rule: Fable only where correctness has high leverage
(screenshot-to-spec, spec review, architecture decisions, security audits, SEO
architecture, final review before major implementation). Sonnet for routine
Django implementation, CRUD, templates, admin pages, tests, refactors,
documentation, release checklists. The session model does not need to be Fable —
each agent's model comes from its definition or a per-call override.

## Standard pipeline (for every new area)

1. Spec Agent writes or updates spec.
2. Human approves critical choices.
3. Architect Agent writes ADRs and implementation plan.
4. Developer Agent implements one coherent area.
5. Test Agent writes tests.
6. Developer Agent fixes test failures.
7. Designer Agent improves UI if needed.
8. SEO Agent reviews if the area affects public/indexable pages.
9. Spec Reviewer compares screenshot/spec/code/rendered UI/tests.
10. Developer Agent fixes missing or changed features.
11. Safety Agent reviews if the area involves auth, data, files, admin, public input, scripts, or deployment risk.
12. Release Manager validates readiness.

## Global definition of done

A feature is done only when: it exists in the approved spec · it is implemented ·
it is visible/usable in UI if required · it has meaningful tests · edge cases are
tested · permissions are tested when relevant · SEO behavior is tested when
relevant · safety was reviewed when relevant · it matches screenshots/spec · no
feature was removed without approval · no dependency was added without approval ·
settings and documentation were updated · the Release Manager says it is ready.

## Source materials

- Screenshots + spec documents: `/home/cedric/Dropbox/Applications/WebsiteEmpire2/spec-ecom/`
  - `extra-spec-ecom.txt` — requirements beyond the screenshots
  - `design-pattern-ideas.txt` — 15 binding architecture lessons from WebsiteEmpire2
- Specifications: `specs/ecommerce_engine/` (00–12, see folder)

## Uncertainty handling (all agents)

- Critical uncertainty → ask the human before implementation.
- Medium uncertainty → suggest 2-3 options, recommend one, mark `PENDING APPROVAL`.
- Low uncertainty → simplest reasonable default, mark `ASSUMPTION`, continue.
All open items live in `specs/ecommerce_engine/11_uncertainties_to_validate.md`.
