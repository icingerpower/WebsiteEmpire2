# T016 — Multilingual Domain Model — Release Checklist

Release Manager: claude-sonnet-4-6
Date: 2026-07-04

---

## Checklist

- [x] **Spec approved** — ADR-008 approved covering StoreDomain, StoreLanguage,
  ShippingCountry, resolve_locale, LocaleMiddleware. One item excluded from
  blocking by human instruction: reserved-slug rule breadth (tracked in
  11_uncertainties_to_validate.md).

- [x] **Architecture decisions recorded** — ADR-008 in
  `specs/ecommerce_engine/09_architecture_decisions.md` (as reported by Architect
  and confirmed through the pipeline).

- [x] **Implementation complete** — All four models in place
  (`stores/models.py` lines 383, 453, 553). Resolver at
  `permalinks/resolver.py` with `resolve_locale`, `base_url`, `GoneError`,
  soft-delete filter (`store__deleted_at__isnull=True`).
  `LocaleMiddleware` at `stores/middleware.py` with `EXCLUDED_PREFIXES`,
  `\Z` regex pattern, query-string preserved on 301 redirect.

- [x] **Tests added** — 71 tests in
  `stores/tests/test_multilingual_domains.py`.

- [x] **Tests passing** — Verified by running the test file directly:
  71 passed in 6.23s. Full suite also green: 805 passed in 38.75s.

- [x] **Coverage** — 71 dedicated tests for T016; full regression suite
  unchanged at 805 pass.

- [x] **Spec Reviewer approved** — 2nd pass: APPROVED (1 item excluded by
  human instruction, tracked as PENDING HUMAN DECISION in
  11_uncertainties_to_validate.md).

- [x] **Safety Agent approved** — Original audit BLOCKED on F1 soft-delete.
  Targeted 2nd pass after fix: APPROVED — CLEAR TO RELEASE.

- [x] **SEO Agent** — Not applicable to this ticket (domain model / middleware
  only, no public indexable page changes).

- [x] **Designer** — Not applicable (no UI surface changed).

- [x] **Migrations reviewed** — Three migrations generated and reviewed:
  - `stores/0008_populate_store_domains.py` — data migration
  - `stores/0009_alter_store_custom_domain_and_more.py` — help_text migration
  - `permalinks/0003_alter_permalink_slug.py` — validator migration
  All are reversible. `makemigrations --check` exits 0 (no drift).

- [x] **Settings documented** — `PLATFORM_APEX_DOMAIN` raises
  `ImproperlyConfigured` when unset in `base.py` (line 151). Development
  default set via `os.environ.setdefault` in `development.py` before base
  import (line 12). Behavior and requirement documented in settings files.

- [x] **Deployment checklist ready** — See RELEASE_NOTES.md.

- [x] **Rollback plan ready** — See ROLLBACK_PLAN.md.

- [x] **Known risks listed** — See KNOWN_RISKS.md.

- [x] **Human decisions listed** — One PENDING HUMAN DECISION on
  reserved-slug rule breadth; excluded from blocking by explicit human
  instruction. Tracked in
  `specs/ecommerce_engine/11_uncertainties_to_validate.md`.
