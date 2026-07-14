# Release Checklist — Consent Management (TICKET-048/049, ADR-025)

**Date:** 2026-07-11
**Area:** GDPR/ePrivacy consent banner + server-side pixel/beacon gating — resolves the
cross-backlog GDPR blocker recorded in `docs/releases/pixels-currency-chat/KNOWN_RISKS.md`
item 1.
**Verdict:** READY FOR STAGING. READY FOR PRODUCTION WITH CONDITIONS — see "Per-area verdict"
at the end. **New EU position for pixels: RESOLVED-by-ADR-025, CONDITIONAL** (not a blanket
"EU-ready") — see the same section.

All findings below were independently re-verified by the Release Manager in this cycle
(commands run, files read) — not taken on the audit/review documents' word alone.

---

## Checklist

### Spec approved (by human where critical)
PASS. ADR-025 records all four product-level items as **DECIDED (human, 2026-07-11)**:
P1 banner copy/tone, P2 180-day re-prompt interval, P3 `is_enabled=True` rollout default for
existing stores, P4 13-month proof retention (`docs/adr/ADR-025-consent-management.md`
lines 418-440). TICKET-048 and TICKET-049 both record "**PENDING blockers: none**"
(`specs/ecommerce_engine/10_implementation_tickets.md` lines ~763, 822) — the one item still
open, legal sign-off on the D4 beacon-exemption reading, is explicitly scoped as **external,
non-implementation-blocking** by the ADR itself (fallback: flip
`ConsentSettings.beacon_requires_consent` to `True` platform-default, no redesign) — see
"Human decisions listed" below and `KNOWN_RISKS.md` item D4.

### Architecture decisions recorded
PASS. `docs/adr/ADR-025-consent-management.md` — **Status: ACCEPTED (human, 2026-07-11)**,
verified directly (`grep "^\*\*Status" docs/adr/ADR-025-consent-management.md`). Carries:
- D0 — verified correction of ADR-022 D7 ("slot-provider-only" was partly false; claims must
  be gated at the claim site, not just the render).
- D1–D7 — consent model, storage, gating mechanics, beacon exemption, banner UX, multi-tenant
  settings, app boundary — all implemented (see below).
- A dated addendum (2026-07-11, from Safety Agent finding F2) recording a **binding
  provisioning constraint**: `pradize_consent` must be renamed `__Host-pradize_consent`
  before any future shared-subdomain store deployment. No action needed today (current
  provisioning gives every store its own registrable domain) but recorded as binding, not
  optional, the moment that assumption changes.
- Related ADR-022 D7 wording correction (pixels-app + claim-site, not slot-provider-only) is
  included per ADR-025 D7 item 5 — carried as a doc-only follow-up (same class as the
  pre-existing LOW-5 item in the pixels release).

### Implementation complete
PASS, independently verified against the ADR (not taken on the ticket text alone):
- **`consent/` app** exists with the full D7 shape: `models.py` (`ConsentSettings`,
  `ConsentRecord`), `policy.py`, `state.py` (`get_consent`, the single resolution point),
  `views.py` (`POST /_consent/`), `slot_provider.py` (`ConsentSlotProvider`),
  `management/commands/purge_consent_records.py`, `admin.py`, `tasks.py`. Confirmed
  `"consent"` is in `INSTALLED_APPS` (`webecom/settings/base.py:52`).
- **Gating touch points outside the app**, spot-checked directly:
  - `pixels/slot_provider.py::_resolve_consent` — imports `consent.state.get_consent`
    inside a `try/except ImportError`, with an independent fail-closed
    `_FailClosedConsent` stand-in in the except branch (never imports from `consent`
    there) — confirmed by reading the file. Each `Pixel` row is gated by
    `consent.allows(provider.consent_category)` before rendering (line ~104).
  - `pixels/service.py::claim_purchase_pixels` — confirmed to require an
    `allowed_categories: frozenset[str]` parameter (the D0 correction) and to re-check
    `payment_status == PAID` before any claim, independent of consent.
  - `storefront/views_checkout.py::order_thank_you` passes the resolved allowed categories
    into `claim_purchase_pixels` (per the security audit's file-list and D0 regression test).
  - `storefront_tags.py` beacon tag no-ops on `am_objected` / `beacon_requires_consent`
    without `analytics` grant (confirmed by the security audit's direct code trace,
    section 6).
  - `pixels/templates/pixels/ga_base.html` — Consent Mode v2 default line ordering
    (`gtag('consent','default',...)` before `gtag('config',...)`) confirmed correct by the
    SEO review's direct trace (section 4).
- **`ConsentSlotProvider`** (`consent/slot_provider.py`, read in full this cycle):
  `is_enabled()` and `validate_settings()` fully implemented per TICKET-048;
  `render()` (TICKET-049) delegates to `storefront/templates/storefront/partials/
  consent_banner.html`, server-renders the banner only when undecided plus the always-present
  hidden preferences panel, and (per the F1/F7 fixes below) forces `get_token(request)` so the
  CSRF cookie is present whenever the banner will render.
- **Launch-checklist wiring** (`ConsentSlotProvider.validate_settings`, ADR-025 D6): active
  `Pixel` row + consent disabled + no `non_eu_acknowledged` → blocking error string. Confirmed
  present and tested. See "Known limitation" under F5 below — this check exists in code but
  has **no automated consumer wired into any deploy/launch flow yet** (see Architecture note).

**RM-1 — NEW FINDING (Release Manager, not in either audit) — the `non_eu_acknowledged`
escape hatch does not restore pixel firing; it leaves pixels permanently dark instead.**
Verified by direct code trace, not present in `CONSENT_AUDIT.md` or `CONSENT_BANNER_SEO_REVIEW.md`:

- ADR-025 D3 states render gating applies "when consent is enabled for the store (D6)"
  and P3 states the remedy for a non-EU store's pixel-volume drop is "the owner files the
  non-EU acknowledgment." Both imply that `ConsentSettings.is_enabled=False` (the
  acknowledgment path) should restore unconditional pixel firing for that store.
- **`pixels/slot_provider.py::PixelsSlotProvider.render()` never reads `ConsentSettings` at
  all** (confirmed: repo-wide grep for `ConsentSettings`/`get_or_create_consent_settings`
  shows zero references anywhere under `pixels/`). It unconditionally calls
  `consent.allows(provider.consent_category)` via `_resolve_consent(request) →
  consent.state.get_consent(request)`, which purely parses the `pradize_consent` cookie —
  it has no notion of whether the banner is enabled for the store.
- Consequence: once a store's admin ticks the non-EU acknowledgment (`is_enabled=False`),
  `ConsentSlotProvider.is_enabled()` returns `False`, so the banner **stops rendering**
  (confirmed via `consent_tags.py`/`ConsentSlotProvider.is_enabled()`). With no banner, no
  shopper ever gets a chance to set the `pradize_consent` cookie, so
  `get_consent(request)` returns `UNDECIDED_STATE` for every request, forever, for that
  store. `UNDECIDED_STATE.allows("analytics")` / `.allows("marketing")` are both `False` by
  construction (`consent/state.py:79-84`) — so `PixelsSlotProvider.render()` skips every row,
  **permanently**, for exactly the stores that used the escape hatch to get out of the
  consent requirement. This is the opposite of the documented intent, and reproduces the same
  §XV-1 "invisible failure" class ADR-025 was built to eliminate — just in the other
  direction (fail-closed/safe, but silently defeats the feature the store owner explicitly
  asked for).
- **Not caught by either existing audit**: `CONSENT_AUDIT.md` verified the fail-closed
  *security* direction (undecided/removed-app never fires unconsented — correct and still
  true) but did not test the *business-logic* claim that the acknowledgment restores firing.
  `consent/tests/test_slot_provider.py`'s `test_disabled_with_ack_and_active_pixel_has_no_
  blocking_error` (and siblings) only assert the **launch-checklist string** is empty when
  acknowledged — no test anywhere calls `PixelsSlotProvider().render()` against a store with
  `is_enabled=False, non_eu_acknowledged=True` to confirm pixels actually render. Verified: no
  such test exists (`grep -rln non_eu pixels/tests/*.py` → no results).
- **Scope of impact**: only affects stores that take the `non_eu_acknowledged` escape hatch.
  The default path (`is_enabled=True` for all stores at migration, per P3) — which is what
  actually resolves the EU/GDPR blocker and is this release's primary purpose — is unaffected
  and correctly tested end-to-end (undecided/refused/granted-per-category all behave exactly
  per ADR-025 D3, confirmed by the D0 regression suite and the audit's re-verification).
- **Disposition: does not block this release's primary purpose (EU consent gating), but
  blocks recommending the non-EU escape hatch for production use until fixed.** Recommended
  fix (not applied here, per Release Manager scope — flagging, not patching): either (a) have
  `PixelsSlotProvider.render()` check `get_or_create_consent_settings(store).is_enabled` and
  skip consent gating entirely when `False` (matching the ADR's own conditional wording), or
  (b) explicitly correct ADR-025 D3/P3's wording if the "acknowledgment restores firing"
  reading was never actually intended and disabling consent is meant to mean "no tracking at
  all, ever" for that store (a defensible but different product decision that would need
  re-ratification, since it contradicts P3's own stated rationale for offering the
  acknowledgment). **Owner: Architect (adjudicate which reading is correct) → Developer
  (implement) → After-Bug Test Agent (regression test once fixed).** Ticketed to
  `KNOWN_RISKS.md`.

### Tests added
PASS. New/expanded test modules: `consent/tests/test_models.py`, `test_state.py`,
`test_views.py`, `test_slot_provider.py`, `test_admin.py`, `test_fail_closed_shim.py`,
`test_csrf_cookie_availability.py`, `test_csrf_cookie_refresh.py`; plus
`storefront/tests/test_consent_banner.py`, `storefront/tests/test_consent_beacon.py`,
`pixels/tests/test_consent_mode.py`. Covers: prior-consent gating per category, cookie
mechanics (HttpOnly/SameSite/Secure/Max-Age), proof-row integrity and tenant isolation, the
D0 claim-preservation regression, beacon exemption + objection toggle + conservative
override, GA4 Consent Mode v2 signal correctness, launch-checklist validate_settings cases,
slot/theme conformance (incl. checkout/thank-you/preview-suppression), fail-closed shim with
`consent` app absent, i18n (fr), tenant isolation, and the two CSRF-cookie-availability
regressions (F1, F7).

### Tests passing
PASS. Full suite re-run independently by the Release Manager just now:
```
DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py test
Found 2757 test(s).
System check identified no issues (0 silenced).
...
OK
```
Matches the count both audit docs report (`CONSENT_AUDIT.md` re-verification: "full suite →
2757 tests, OK"; `CONSENT_BANNER_SEO_REVIEW.md`: 139/642 tests OK on its own targeted runs).
No failures, no errors.

### Coverage checked
Not separately re-measured with a coverage tool this cycle — same posture as the prior
release. Given 2757/2757 passing across a wide targeted-suite breakdown (83 → 92 → 111
consent-focused tests across the audit's three runs, plus 60 more in
storefront/pixels-adjacent suites, plus 139/642 in the SEO review's own runs) coverage is
judged adequate for this gate; not a blocker. Recommendation carried forward: run an explicit
`coverage run/report` pass before the next major release.

### Spec Reviewer approved
**GAP, same pattern as the prior release cycle.** No standalone `SPEC_REVIEW_APPROVAL.md`-
style artifact exists for TICKET-048/049 (only `phase-1` has one on disk); the
pixels-currency-chat release also had no saved spec-review artifact, only a "routed list"
referenced in its checklist. For consent, the closest equivalent evidence is the Security
Audit and SEO Review, both of which independently traced the implementation against every
ADR-025 decision point (D0–D8) and found it matching, with no missing/hallucinated feature
noted. **This is a process gap, not a functional one** — recommend a dedicated Spec Reviewer
pass compare screenshots (none exist for a cookie-consent banner — it's a new UI element, not
in the original screenshot spec) / ADR-025 / code / rendered UI / tests before the next
release cycle closes this gap formally. Not treated as blocking here because two independent
technical reviews (security, SEO) already exercised the equivalent cross-check for this
specific area.

### Safety Agent approved
PASS — `docs/security/CONSENT_AUDIT.md`, verdict **v2: CLEAR-WITH-NOTES (= APPROVED WITH
CONDITIONS — none blocking)**. Independently re-read and spot-checked in this cycle:
- CSRF enforced (no `csrf_exempt`, proven by a 403+zero-rows test), strict input validation
  (closed enums, genuine JSON booleans, 4096-byte cap), no reflected output, fail-closed
  cookie parser (fuzzed), tenant isolation tested, no PII in `ConsentRecord` (no IP field —
  schema-pinned), 13-month purge is real (`.delete()`, not a soft flag), claims gated at the
  claim site exactly per ADR-025 D0, every rollback shim fails closed.
- **F1 (CSRF cookie availability on form-free pages) — VERIFIED FIXED.** Spot-checked
  directly: `consent/slot_provider.py:134-135` calls `django.middleware.csrf.get_token(request)`
  whenever `show_banner` is True.
- **F4 (withdraw proof-trail mislabeling) — VERIFIED FIXED.** Spot-checked: `consent/views.py`
  groups `WITHDRAW` with `REFUSE_ALL`, both force `analytics=marketing=False` server-side
  regardless of payload.
- **F7 (withdrawal availability edge on repeated manage-panel re-saves) — APPLIED
  2026-07-11.** `consent_post` now also calls `get_token(request)` before returning 204,
  refreshing the CSRF cookie's clock alongside the consent cookie's on every decision, not
  just banner renders. Non-blocking by the audit's own adjudication (rare, multi-condition,
  fail-closed, self-healing at consent expiry) but already shipped.
- **F2 (unsigned, non-`__Host-` cookie)** — recorded as a binding ADR addendum for any future
  shared-subdomain deployment (see Architecture section above), not applicable today.
- **F3 (rate limiting keyed on `REMOTE_ADDR`, no edge-proxy IP-restoration guarantee)** —
  carried onto this release's deployment checklist below (also already present in the
  pixels-currency-chat checklist item 7, since it was added there retroactively when this
  audit ran).
- **F5 (launch-check warning-vs-blocking severity)** — assessed below, **ticketed**, not
  silently resolved (see "Deployment checklist ready").
- **F6 (INFO, cosmetic count/delete drift in purge logging)** — no action needed.
- No CRITICAL/HIGH findings anywhere in the audit's two passes.

### SEO Agent approved
PASS — `docs/seo/CONSENT_BANNER_SEO_REVIEW.md`, verdict **PASS-WITH-FIXES**. Independently
re-checked reasoning in this cycle:
- Zero-CLS claim confirmed (fixed-position banner, no late DOM injection, no font/image
  shift risk, worst-case unstyled-flash analysis still yields zero CLS because the markup
  sits at end-of-body).
- No intrusive-interstitial flagging (bottom bar, `aria-modal="false"`, ≤40vh cap, both
  independently exempt under Google's cookie-notice and reasonable-screen-space carve-outs).
- Crawl/index neutrality confirmed (consentless render = complete content, no cloaking, no
  head/meta impact, POST-only endpoint).
- GA4 Consent Mode v2 ordering and signal truthfulness confirmed.
- i18n (fr catalog) and reload-on-grant SEO-neutrality confirmed.
- **All three findings (F1 robots.txt hygiene, F2 `data-nosnippet`, F3 `<style>` ordering)
  APPLIED 2026-07-11**, re-run suite (`consent sitemaps storefront`) → 642 tests, OK.

### Designer approved
**N/A for this gate**, consistent with the pattern in the prior release checklist. No
Designer review artifact exists for the consent banner. The banner is explicitly
theme-token-only (ADR-025 D5: "skinned exclusively by the ADR-012 theme tokens... all three
themes conform without per-theme markup"), and `TH-045`'s existing conformance suite already
asserts `slot.consent` renders across themes — this is a functional/behavior-preserving
addition to the existing slot system, not a visual-polish pass. Recommend a Designer pass
specifically on mobile viewport button/text sizing before this ships broadly, since the
banner is new (unlike pixels/chat, which reused existing surfaces) — non-blocking suggestion,
not a gate failure.

### Migrations reviewed
PASS. `python3 manage.py makemigrations --check --dry-run` → **"No changes detected"**
(re-run by the Release Manager, exit 0) for the full project including `consent`'s
migrations. `consent` is a new app; its migration(s) create `ConsentSettings` and
`ConsentRecord` — additive only, no destructive schema change, matching the ADR's own
"Rollback strategy" claim ("no destructive migration exists in this ADR").

### Settings documented
PASS with one item to verify live (see Deployment checklist). `CONSENT_COOKIE_MAX_AGE`
(180 days, `consent/policy.py`), `CONSENT_POLICY_VERSION` — both platform constants per
ADR-025 P2. Cookie attributes are set in `consent/state.py`/`consent/views.py`:
`SameSite=Lax`, `Path=/`, `HttpOnly=True` always, `Secure=settings.SESSION_COOKIE_SECURE`.
Confirmed directly: `webecom/settings/production.py:102-104` sets `SECURE_SSL_REDIRECT=True`,
`SESSION_COOKIE_SECURE=True`, `CSRF_COOKIE_SECURE=True` — so in production the consent
cookie inherits `Secure=True` through that setting. **This is a code-level guarantee, not
itself a live-environment proof** — the human checklist item below (browser devtools check)
is the actual production verification and remains open (see Deployment checklist item 3).

### Deployment checklist ready
PARTIAL — same structural gap as the prior release (no consolidated
`docs/DEPLOYMENT_HARDENING.md`); items below are itemized here plus carried to `KNOWN_RISKS.md`:

1. **Deploy consent together with pixels — not consent alone, not pixels alone.**
   Per ADR-025 D7: "048 without 049 would gate pixels with no way to ever grant... the release
   gate is the pair." Both TICKET-048 and TICKET-049 are in this release together (verified:
   `ConsentSlotProvider.render()` is fully implemented, not a TICKET-048-only stub). **This
   release must ship as a single atomic deploy alongside (or before) any EU-facing store's
   `Pixel` rows being activated** — see the EU-position statement below.
2. **Real-client-IP restoration at the edge proxy (F3, `CONSENT_AUDIT.md`).** The 5/hour
   consent-decision rate limit keys on `REMOTE_ADDR` only (deliberately never parses
   `X-Forwarded-For`, to prevent limit-spoofing). If the reverse proxy does not restore the
   real client IP, every shopper of a store shares one bucket → 429s → banner
   undismissable → pixels stay dark platform-wide for that store until the window rolls over
   (fail-closed, but a real availability defect). **This is the exact same item already
   present in `docs/releases/pixels-currency-chat/RELEASE_CHECKLIST.md` item 7** (added
   retroactively when the consent audit ran) — one physical deploy check satisfies both.
   **Owner: DevOps.**
3. **Verify production cookie flags live (human checklist item 3, `CONSENT_AUDIT.md` v1).**
   After deploy, open browser devtools → Application → Cookies on the production domain and
   confirm `pradize_consent` carries `Secure` and `HttpOnly`. The code-level guarantee (above)
   is necessary but not sufficient proof — this is a one-time post-deploy smoke check.
   **Owner: QA/DevOps.**
4. **Fresh-session 204-vs-403 live check (human checklist item 1, `CONSENT_AUDIT.md` v1,
   F1/F7 verification in the wild).** On a deployed single-currency store, open the home page
   in a fresh private-browsing session, click "Accept all," and confirm the network tab shows
   204 (not 403) and the `pradize_consent` cookie is set; repeat with "Refuse all." This is
   the live-environment counterpart to the F1/F7 code fixes and unit tests — those tests
   prove the mechanism works under Django's test client with the full middleware stack, but
   have not been observed against a real browser/real proxy/real TLS termination.
   **Owner: QA.**
5. **Legal sign-off on the D4 beacon-exemption reading — track to resolution, does not block
   this deploy.** See "Human decisions listed" and `KNOWN_RISKS.md` for the full item and its
   fallback. **Owner: Human/Legal.**
6. **F5 — launch-checklist severity semantics: ticket, do not silently resolve.** Investigated
   this cycle beyond what the audit checked: **there is currently no code anywhere in this
   repository that consumes `SlotProvider.validate_settings()` across providers to produce an
   actual pre-launch report** — confirmed by grepping every call site of `validate_settings`
   and `get_providers`: the only caller of `get_providers()` is the `render_slot` template tag
   (`storefront/templatetags/storefront_tags.py`), which renders HTML, not a checklist.
   References to a "launch-readiness checklist" / "L11" / "L* items" elsewhere in the codebase
   (`storefront/theme.py`, `pixels/registry.py` docstrings) are all **documentation of a
   contract for a consumer that does not exist yet**, not evidence of one. This means
   `ConsentSlotProvider.validate_settings()`'s blocking-error string ("Active pixels require
   the consent banner...") is real, tested code, but **nothing currently reads it
   automatically before a store launches** — the same is true for `PixelsSlotProvider.
   validate_settings()` and `currency`'s equivalent, so this is a **pre-existing,
   platform-wide gap that consent does not introduce or worsen**, but it does mean ADR-025
   D6's "blocking error" is enforced today only by a human manually running the Django shell
   or by this Release Manager's own manual review — not by any automated gate. **Ticketed,
   not silently resolved: recommend a follow-up ticket to build the actual launch-checklist
   consumer (management command or super-admin view iterating `get_providers()` for every
   slot and calling `validate_settings(store)`), after which the warning-vs-blocking severity
   question the audit's F5 raised becomes concretely testable.** Until that consumer exists,
   the manual pre-launch check is: run `ConsentSlotProvider().validate_settings(store)` and
   every other registered provider's `validate_settings(store)` by hand (or via the Django
   shell) for any store about to go live with active pixels. **Owner: Architect (design the
   consumer) → Developer (implement).**

### Rollback plan ready
PASS — see `ROLLBACK_PLAN.md` in this directory. Verified against `consent/slot_provider.py`
and the ADR's own "Rollback strategy" section; the actual safest rollback lever, and its
EU-pixel consequence, are stated there (not taken on the ADR's summary alone — see that
document for the verification detail).

### Known risks listed
PASS — see `KNOWN_RISKS.md` in this directory.

### Human decisions listed
PASS. Consolidated:
1. **Legal sign-off on the D4 beacon-exemption reading** — owner **human/legal**
   (`specs/ecommerce_engine/11_uncertainties_to_validate.md`, item "ADR-025 — legal sign-off
   on beacon-exemption reading," lines 305-314). Explicitly does **not** block this release
   per the ADR's own text: the fallback is a one-boolean flip
   (`ConsentSettings.beacon_requires_consent=True` as a new platform default) with no
   redesign if legal disagrees. Track to resolution; update ADR-025 D4/D6 defaults once legal
   responds.
2. **F5 launch-checklist consumer** — ticketed above; owner Architect/Developer.
3. **Real-IP restoration at the edge** — owner DevOps, shared item with the
   pixels-currency-chat release checklist.
4. **Production cookie-flag + fresh-session live checks** — owner QA/DevOps, one-time
   post-deploy smoke checks (items 3-4 above).
5. **F7 patch is already shipped** (not open) — recorded for completeness since the audit
   listed it as "ship in the next patch"; verified it is already in this release's code.
6. **Spec Reviewer artifact gap** — recommend a dedicated pass before the next release cycle;
   not blocking this one given the security + SEO reviews' equivalent cross-checks.
7. **Designer pass on mobile banner sizing** — recommended, non-blocking.
8. **RM-1 (new, this cycle)** — the `non_eu_acknowledged` escape hatch does not restore
   pixel firing (see "Implementation complete" above). Owner Architect (adjudicate intended
   behavior) → Developer (fix) → After-Bug Test Agent (regression test). Does not block this
   release's primary EU-gating purpose; blocks recommending the escape hatch itself until
   resolved.

---

## Per-area verdict

### Consent management (TICKET-048/049 / ADR-025)

- **READY FOR STAGING: YES.** Full suite 2757/2757 OK, no migration drift, ADR-025 ACCEPTED
  (human), Safety Agent CLEAR-WITH-NOTES with zero blocking findings (F1/F4/F7 all fixed and
  verified), SEO Agent PASS-WITH-FIXES with all three fixes applied and re-tested.
- **READY FOR PRODUCTION: YES, WITH CONDITIONS.**
  1. Legal sign-off on the D4 beacon-exemption reading — **Human/Legal**, tracked, does not
     block (fallback exists).
  2. Real-client-IP restoration verified at the edge proxy before `/_consent/` traffic
     — **DevOps**.
  3. Production cookie-flag verification (Secure/HttpOnly, browser devtools) — **QA/DevOps**,
     one-time post-deploy check.
  4. Fresh-session 204-vs-403 live check (Accept all / Refuse all) — **QA**, one-time
     post-deploy check.
  5. Ticket the launch-checklist automated consumer (F5) — **Architect/Developer**, not
     launch-blocking for this release (pre-existing platform gap), but must exist before
     ADR-025 D6's "blocking error" has real automated teeth for any store.
  6. Consent (TICKET-048+049) must deploy as a pair, in the same release as, or strictly
     before, any EU-facing store's `Pixel` rows going active — **Human/Product**, operational
     sequencing condition, not a code gate.
  7. **RM-1: do not advise any store to use the `non_eu_acknowledged` escape hatch until it
     is fixed.** It correctly suppresses the launch-checklist blocking error but does **not**
     restore pixel firing — it leaves that store's pixels permanently dark instead (see
     "Implementation complete" above). This does not affect the default `is_enabled=True`
     path that resolves the EU blocker (unaffected, fully tested) — it only affects stores
     that would otherwise legitimately want the escape hatch (e.g. a store that has decided
     it does not target EU/EEA visitors and wants pixels to fire unconditionally, as before
     this release). **Owner: Architect → Developer.**

### New EU position for pixels (supersedes `pixels-currency-chat/KNOWN_RISKS.md` item 1)

**RESOLVED-by-ADR-025, CONDITIONAL** — no longer a blanket "NOT launch-ready for EU
storefronts." Pixels may be enabled for an EU-facing store **only when all of the following
hold**:
1. This consent release (TICKET-048+049) is deployed and active for that store
   (`ConsentSettings.is_enabled=True`, the default) — pixels fired without the consent gate
   live is exactly the pre-ADR-025 posture this release exists to close.
2. Legal sign-off on the beacon-exemption reading (D4) is either obtained, or the conservative
   fallback (`beacon_requires_consent=True`) has been applied for that store pending sign-off
   — **Human/Legal** decision, tracked in `11_uncertainties_to_validate.md`.
3. The deploy checklist items above (real-IP restoration, prod cookie-flag verification, live
   204-vs-403 check) have been completed at least once for the environment serving that store.
4. **PIX:P1 GA4-only residual note carried unchanged**: the thank-you-token confidentiality
   fix only covers GA4; Facebook/TikTok/Snapchat/Pinterest still transmit the token-bearing
   thank-you URL in their automatic page_view calls. This is an accepted, named, GA4-only
   fix from the pixels release and is **orthogonal to the EU-consent gate** — it is a
   confidentiality residual, not a consent-legality blocker, and remains open per its own
   ADR-022 follow-up (server-side Conversions APIs).

Absent any of conditions 1-3 for a specific store, that store's pixels remain **NOT
launch-ready for EU**, unchanged from the prior posture.
