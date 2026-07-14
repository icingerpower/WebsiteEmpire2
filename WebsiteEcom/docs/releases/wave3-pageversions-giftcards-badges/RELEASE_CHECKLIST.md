# Release Checklist — Wave 3: Page Versions, Gift-Card Campaigns, Security Badges + Isolation-Hardening Chain

**Date:** 2026-07-11
**Scope:**
- Page versions (TICKET-042 / ADR-028)
- Gift-card campaigns (TICKET-045 / ADR-029)
- Security badges (TICKET-046 / ADR-030)
- Isolation-hardening chain (TICKET-050/051/052/053 / ADR-031 + 3 addenda / ADR-032)

**Verdict:** READY — see full verdict section at the end of this document for per-area conditions.

All evidence below was independently re-verified by the Release Manager (test run, migration
check, `check --deploy`, ADR table, BUG_TESTS.csv rows, and two spot-checked code fixes) rather
than taken on the reporting agents' word alone. Where a claim could not be independently
re-verified from an artifact, that is stated explicitly rather than passed through silently.

---

## Checklist

### Spec approved (by human where critical)
PASS.
- ADR-028 (page versions): **ACCEPTED (human, 2026-07-11)** — all 5 PENDING product defaults
  (P1 cap 10, P2 all-languages automatic permalinks, P3 ≥1 image required to activate, P4 no
  significance testing in v1, P5 302/301 redirect semantics) approved as recommended.
- ADR-029 (gift-card campaigns): **ACCEPTED (human, 2026-07-11)** — 5-year French statutory
  floor, refund policy (deactivate unspent / flag spent for manual review), and the
  self-gifting brake (zero-charge skip + per-recipient frequency cap) all approved as
  recommended.
- ADR-030 (security badges): **ACCEPTED (human, 2026-07-11)** — the deliberate deviation from
  the CommerceHQ reference screenshots (no third-party certification marks shipped as
  built-in assets, since the platform cannot verify any store's actual enrollment) is an
  explicit human-approved product-integrity decision, not an oversight.
- ADR-032 (admin unique validation): **ACCEPTED (Architect adjudication, 2026-07-11)** —
  correctly not escalated to a human decision: "no product-level choice involved — 500
  becomes a form error," per the ADR table entry.
- Table entries verified directly: `specs/ecommerce_engine/09_architecture_decisions.md`
  lines 28–31 and the ADR-031 amendment notes at lines 69–72.

### Architecture decisions recorded
PASS. All five ADR artifacts exist and are internally consistent with the table:
- `docs/adr/ADR-028-ab-variant-pages.md` — header confirms "Status: ACCEPTED (human, 2026-07-11)".
- `docs/adr/ADR-029-gift-card-campaigns.md` — header confirms "Status: ACCEPTED (human, 2026-07-11)".
- `docs/adr/ADR-030-security-badges.md` — header confirms "Status: ACCEPTED (human, 2026-07-11)".
- `docs/adr/ADR-031-relation-bound-m2m-reads.md` — base decision + 2 addenda, both internal
  "Status: ACCEPTED" headers confirmed (lines 94, 172); is an *amendment* to ADR-001 §4, not a
  standalone numbered row — confirmed by reading `09_architecture_decisions.md` lines 69–72
  directly (three dated 2026-07-11 amendments to the store-isolation ADR).
- `docs/adr/ADR-032-admin-unique-validation.md` — header confirms "Status: ACCEPTED (Architect
  adjudication...)".

### Implementation complete
PASS (qualitative, code read directly, not just reports):
- Page versions: `catalog/migrations/0013_productpageversion_productpageversionimage.py`
  creates `ProductPageVersion`/`ProductPageVersionImage`; `cart/views.py`
  `_validate_page_version_id`; `permalinks/registry.py` redirect creation/rename cascade;
  `sitemaps/sitemaps.py` content-type exclusion; `storefront/views.py` canonical/og/JSON-LD
  branching (including the SEO FIX-1 patch, confirmed present at `storefront/views.py:563-592`).
- Gift-card campaigns: `discounts/migrations/0005_gift_card_campaigns.py` creates
  `GiftCardCampaign` + `DiscountCode.refund_flagged`/`refund_flagged_reason`;
  `discounts/service.py` issuance/idempotency/locking; `payments/webhook_views.py` +
  `payments/paypal_webhook_views.py` REFUNDED-transition whitelist (W3-1 fix, confirmed
  present, both processors).
- Security badges: `badges/migrations/0001_initial.py`; `badges/badge_presets.py` registry;
  `SecurityBadgeSlotProvider` wired into the existing `slot.security_badge` slot.
- Isolation chain: `core/managers.py` `_RaisingQuerySet` closes the terminal-method family
  (confirmed by direct read — `update()`, `delete()`, `count()`, `aggregate()`, `exists()`
  [narrow validation window only], `iterator()`, `aiterator()`, `explain()` all raise);
  `core/formsets.py` `StoreSafePKFormSetMixin`; `core/models.py` validation-window wrapper;
  `core/admin.py` `StoreScopedFormFieldsMixin` / `StoreUniqueValidationFormMixin`;
  `core/widgets.py` `CsvListFormField` (confirmed present, `bound_data`/`prepare_value`/
  `to_python` all overridden as ADR-032 D5 describes); `catalog/models.py:115` confirms
  `tags = models.JSONField(default=list, blank=True)`.

### Tests added
PASS. Full suite grew to **3463 tests** (was 3437 at the Safety Agent's re-verification pass,
993/1071 at its first pass scoped to the six directly-touched apps). BUG_TESTS.csv contains
proven, revert→fail→restore→pass regression rows for every named bug fix in this release (see
"Tests passing" below for the isolation-chain row-by-row confirmation).

### Tests passing
PASS — **re-run myself, not taken on report:**
```
DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py test
...
Ran 3463 tests in 45.861s
OK
```
0 failures, 0 errors. Matches the Safety Agent's carried figure exactly (3463 — the audit's own
re-verification pass had reported 3437 with a note that BUG_TESTS rows were being added after;
the delta is accounted for by the isolation-chain's own test additions, confirmed by row count
in BUG_TESTS.csv, see below).

### Coverage checked when relevant
PASS (qualitative). Money-adjacent code (gift-card issuance, webhook state transitions) and the
isolation layer (core/managers.py, core/admin.py, core/formsets.py) both carry the highest
test density in the repo per the Safety Agent's audit and the BUG_TESTS.csv proof-cycle
methodology (revert-the-fix-and-watch-it-fail on every row, not just "test exists"). No
separate coverage percentage tool was run in this pass; not flagged as a gap given the
proof-cycle discipline already in place.

### Spec Reviewer approved
PASS, with one verification gap noted. Per the orchestrator's gate state, all three product
areas (page versions, gift cards, badges) were APPROVED on re-verification, with three named
residuals: live-JS preview descoped in an ADR docstring (page versions), an unspent-void audit
note documenting a gap (gift cards), and a midnight-flake test pinned with an honest note that
the originally-observed failure was never reproduced/matched (likely time-zone-boundary test
flakiness, not a functional defect). **Gap:** unlike `docs/releases/phase-1/SPEC_REVIEW_APPROVAL.md`,
no equivalent persisted spec-review document was found under `docs/` for wave 3 — this pass could
not independently re-open and inspect the Spec Reviewer's own written verdict, only corroborate
it indirectly (the SEO Agent's and Safety Agent's own re-verification documents are consistent
with "implementation matches ADR," and the code read above matches the ADRs' descriptions
feature-for-feature). Recommend the Spec Reviewer produce a persisted `SPEC_REVIEW_APPROVAL.md`
for this release to close this gap, matching the phase-1 precedent.

### Safety Agent approved when relevant
PASS. `docs/security/WAVE3_AUDIT.md` — final verdict **APPROVED** (all areas), re-verified
2026-07-11 against current code (not just fix reports): W3-1 (REFUNDED-transition whitelist)
and W3-2 (beacon ingest coercion) both independently confirmed present in code by this pass
(see "Implementation complete" above); the former Release-Manager dependency on the M2M
read-path IsolationError blocking the `DiscountCode`/`GiftCardCampaign` admin change views is
explicitly lifted in the audit's own re-verification note, now that the ADR-031 chain has
closed the three isolation rings. Carried LOWs W3-5 through W3-13 plus two deploy-time items
are listed in `KNOWN_RISKS.md`.

### SEO Agent approved when relevant
PASS. `docs/seo/PAGE_VERSIONS_SEO_REVIEW.md` — verdict **PASS-WITH-FIXES**; all three required
fixes (FIX-1 JSON-LD `offers.url`→primary, FIX-2 fr-prefix canonical HTML test, FIX-3 no-robots-meta
test) are marked "APPLIED 2026-07-11" in the document, and FIX-1's code change was independently
re-read by this pass at `storefront/views.py:563-592` and confirmed present. Gift-card campaigns
and security badges have no public/indexable surface of their own (badges render inside existing
already-reviewed product/cart/checkout slots) — SEO Agent review was correctly scoped to page
versions only.

### Designer approved when relevant
N/A for this pass — no UI-polish ticket was in scope for wave 3; the badge preset gallery and
page-version admin forms are functional-implementation UI, not a themed design pass. Not
flagged as a gap: the standard pipeline routes Designer review only when UI polish is the
work item, which it was not here.

### Migrations reviewed
PASS — read directly, not summarized from a report:
- `catalog/migrations/0013_productpageversion_productpageversionimage.py` — two new tables
  (`ProductPageVersion`, `ProductPageVersionImage`), both `StoreOwnedModel` with `PROTECT` on
  `store`, `CASCADE` on their natural parents. Purely additive.
- `catalog/migrations/0014_alter_product_tags.py` — `AlterField` adding `blank=True` to
  `Product.tags` (validation-only, no column type/SQL change).
- `discounts/migrations/0005_gift_card_campaigns.py` — new `GiftCardCampaign` table plus two
  new nullable/defaulted fields on `DiscountCode` (`refund_flagged`, `refund_flagged_reason`).
  Additive.
- `discounts/migrations/0006_campaignreward_campaign_id_pattern_index.py` — index only.
- `badges/migrations/0001_initial.py` — new app, new tables only.
- `python3 manage.py makemigrations --check --dry-run` → **"No changes detected"** (re-run
  myself). No destructive `AlterField`/`RemoveField`/column-type-narrowing operations found in
  any migration touched by this release.

### Settings documented
PASS.
- `ANALYTICS_PAGE_VERSION_TOP_N = 500` documented in `webecom/settings/base.py` with an inline
  comment tying it explicitly to W3-3's cardinality-bound requirement.
- `PLATFORM_APEX_DOMAIN`, `CACHE_URL`, `ANTHROPIC_API_KEY`/`CHAT_KILL_SWITCH` production-only
  required settings all fail loudly (`ImproperlyConfigured`) at boot per `webecom/settings/production.py`
  and `webecom/settings/base.py` — confirmed by triggering each guard during this pass's
  `check --deploy` run (see below).
- No new required environment variable was introduced by this release beyond what the existing
  guards already cover; `GIFT_CARD_FR_MIN_VALIDITY_DAYS` (1826) is a code constant per the
  Safety Agent's audit (§1.8), not a settings-file entry — acceptable, since it is a statutory
  floor, not an environment-specific tunable.

### Deployment checklist ready
PASS — see `ROLLBACK_PLAN.md` and the deploy-time items enumerated in `KNOWN_RISKS.md`
(media `nosniff`, frequency-cap `EXPLAIN` on Postgres, plus all items carried from prior
releases). `check --deploy` re-run myself with production settings and the four required
environment variables set (`PLATFORM_APEX_DOMAIN`, `SECRET_KEY`, `DATABASE_URL`, `CACHE_URL`,
`CHAT_KILL_SWITCH=1`): **1 warning only** (`security.W009`, expected — it fired only because a
placeholder `SECRET_KEY` was used for this triage; a real, properly-generated production
`SECRET_KEY` will not trigger it). No wave-3-specific deploy check issues found.

### Rollback plan ready
PASS — see `ROLLBACK_PLAN.md`. The isolation-hardening chain (TICKET-050/051/052/053) carries an
explicit **do-not-roll-back** recommendation with the reasoning spelled out, since rolling it
back reopens the exact silent-cross-store-write hole it closed — this is flagged prominently
rather than presented as an ordinary reversible change.

### Known risks listed
PASS — see `KNOWN_RISKS.md`. Carries all LOW/INFO items W3-5 through W3-13, the two deploy-time
checklist items, the legal/brand carried items, the parent `.gitignore` bare `tests` pattern
finding (independently re-verified by this pass, see below), the JSON-LD scheme inconsistency
(independently re-verified by this pass, see below), and the CsvListWidget residual display note.

### Human decisions listed
PASS — see `KNOWN_RISKS.md` "Human/legal decisions" section. Payment-network SVG placeholder
legal/brand check, the legal track (beacon exemption, named social-proof modes, DE opt-in), and
the ADR-029/ADR-030 human-approved defaults are all listed with owners.

---

## Independent verification performed by this pass (not delegated)

1. **Full test suite**, re-run directly: 3463 tests, OK, 0 failures/errors (matches claim).
2. **Migrations**: `makemigrations --check --dry-run` → No changes detected (matches claim).
   Additionally read all five new/changed migration files directly — confirmed additive-only.
3. **`check --deploy`** against `webecom.settings.production` with the four required
   environment variables populated: 1 expected warning only (placeholder `SECRET_KEY`).
4. **ADR table** (`specs/ecommerce_engine/09_architecture_decisions.md`): confirmed ADR-028,
   ADR-029, ADR-030, ADR-032 as standalone ACCEPTED rows, and the three ADR-031 amendments
   dated 2026-07-11 against the ADR-001 §4 row.
5. **BUG_TESTS.csv**: confirmed all 8 named isolation-chain rows
   (M2M-READ-PATH-ISOLATION, INLINE-FORMSET-PK-ISOLATION, TERMINAL-METHODS-ISOLATION,
   VALIDATION-WINDOW-ISOLATION, ADMIN-UNIQUE-VALIDATION, RAW-ID-WIDGET-ISOLATION,
   DISCOUNTS-INLINE-ISOLATION, CAMPAIGN-STEP-INLINE-ISOLATION) are present with **STATUS=PROVEN**,
   plus W3-1-REFUND-ORDERING and W3-2-BEACON-COERCION also PROVEN.
6. **Two code spot-checks** (per the orchestrator's instruction):
   - `core/managers.py::_RaisingQuerySet.update()` — confirmed it unconditionally raises
     `IsolationError` (the closed "SILENT CROSS-STORE WRITE" hole), with `update.alters_data = True`
     preserved for the Django admin's changed-fields tracking.
   - `core/widgets.py::CsvListFormField` — confirmed `prepare_value`/`to_python`/`bound_data`
     all overridden as ADR-032 D5 describes; confirmed `catalog/models.py:115`
     (`tags = models.JSONField(default=list, blank=True)`) carries the paired migration.
7. **Parent `.gitignore` bare `tests` pattern** — confirmed independently:
   `git check-ignore -v WebsiteEcom/discounts/tests/test_discount_admin_inline_isolation.py`
   from the parent repo root returns a match on `.gitignore:2:tests`. This is real and live
   today, not a stale carried note.
8. **JSON-LD http/https scheme inconsistency** — confirmed independently by reading
   `permalinks/resolver.py::base_url()` (hardcodes `https://`) against
   `storefront/views.py:567-570` (primary-page JSON-LD uses `request.build_absolute_uri()`,
   which reflects the actual incoming request scheme). If a production deployment's TLS-terminating
   proxy does not correctly set `SECURE_PROXY_SSL_HEADER`, the primary page's JSON-LD could
   emit `http://` while its own canonical tag, hreflang, and sitemap all emit `https://` (which
   use `base_url()`). Confirmed as a real, reachable LOW-severity backlog item, not a
   fabricated one.

---

## Final Verdict

**READY** — see `RELEASE_NOTES.md` for the per-area verdict table (with exact conditions/owners)
and the overall project position.
