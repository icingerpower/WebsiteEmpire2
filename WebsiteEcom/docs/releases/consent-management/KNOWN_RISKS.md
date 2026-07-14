# Known Risks — Consent Management (TICKET-048/049, ADR-025)

**Date:** 2026-07-11. All items below were independently verified against current code/docs
by the Release Manager in this review cycle (not carried forward unverified). Sources:
`docs/security/CONSENT_AUDIT.md` (Verdict v2 CLEAR-WITH-NOTES),
`docs/seo/CONSENT_BANNER_SEO_REVIEW.md` (PASS-WITH-FIXES), direct code trace, and one
finding (RM-1) discovered by this Release Manager beyond either audit.

## THE headline item — read this first

1. **RM-1 — FIXED (2026-07-11).** ~~The `non_eu_acknowledged` escape hatch does not restore
   pixel firing.~~ `pixels/slot_provider.py::PixelsSlotProvider.render()` never read
   `ConsentSettings` — it unconditionally gated every `Pixel` row on
   `consent.state.get_consent(request).allows(...)`. Once a store's admin
   disabled the banner via the acknowledgment ("this store does not target EU/EEA
   visitors"), the banner stopped rendering, so no shopper ever got to set the
   `pradize_consent` cookie, so `get_consent()` returned `UNDECIDED_STATE` forever for that
   store, so **pixels never fired again** for it — the opposite of what ADR-025 D3 ("when
   consent is enabled for the store...") and P3 ("the owner files the non-EU acknowledgment"
   as the remedy for a pixel-volume drop) both document as the intended behavior. Failed safe
   (no unconsented tracking), but silently defeated the escape hatch the store owner explicitly
   asked for — no error, no warning, pixels were simply, permanently dark. **Not caught by
   `CONSENT_AUDIT.md`** (which verified the fail-closed *security* direction, correctly, but
   did not test the acknowledgment's *business-logic* restore claim) **nor by
   `CONSENT_BANNER_SEO_REVIEW.md`** (out of scope) **nor by any existing test** (verified: no
   test anywhere called `PixelsSlotProvider().render()` against a
   `is_enabled=False, non_eu_acknowledged=True` store — the only tests around the
   acknowledgment asserted the launch-checklist string, not actual pixel rendering). **Scope:
   did not affect the primary EU-gating path** (`is_enabled=True`, the default for all
   stores) — that path was fully tested and correct throughout.

   **Fix (Developer, 2026-07-11):** added `consent/state.py::resolve_consent_for_pixels(request)
   -> ConsentState` — the single resolution point (§XV-4) both `PixelsSlotProvider.render()`
   and the `claim_purchase_pixels()` caller (`storefront/views_checkout.py::
   _resolve_consent_allowed_categories`) now call instead of `get_consent()` directly.
   It returns the cookie-derived state unchanged UNLESS the store's `ConsentSettings` show
   `is_enabled=False` AND `non_eu_acknowledged=True`, in which case it returns an
   all-categories-allowed state (pixels fire unconditionally, matching P3's intent). The
   disabled-WITHOUT-acknowledgment combination (the D6 launch-check-blocked state) is
   deliberately NOT special-cased and stays fail-closed/dark, unchanged — that combination
   should never reach production per the launch check, and if it does anyway, dark is the
   correct posture. Both existing ImportError rollback shims (pixels/slot_provider.py,
   storefront/views_checkout.py) are preserved unchanged, now wrapping the new function name
   instead of `get_consent`. See ADR-025 D3's dated correction (2026-07-11) for the full
   writeup. **Regression tests added:** `consent/tests/test_state.py::
   ResolveConsentForPixelsTest` (unit coverage of the resolution rule, including the
   no-cookie/stale-cookie/enabled-unaffected/no-store-fallback cases),
   `consent/tests/test_single_resolution_point.py::SingleResolutionPointTest` (proves the
   slot-provider render path and the claim path are wired to the exact same function, not two
   independently re-implemented copies), `pixels/tests/test_slot_provider.py::
   NonEuAcknowledgedEscapeHatchTest` (end-to-end render assertions for all three
   `ConsentSettings` combinations), and `storefront/tests/test_checkout.py::
   OrderThankYouTest.test_thank_you_non_eu_acknowledged_claims_all_active_providers_without_cookie`
   / `test_thank_you_disabled_without_acknowledgment_stays_dark_unchanged` (claim-path
   coverage on a PAID order). Full `consent`/`pixels`/`storefront` suites and the full
   project suite are green after the fix (2770 tests).

## Deploy-config risks (block PRODUCTION, not STAGING)

2. **Legal sign-off on the D4 beacon-exemption reading is still OPEN.** The CNIL
   audience-measurement exemption assessment for the first-party analytics beacon (no
   cookies, ephemeral per-tab `sessionStorage` id only, no IP captured, no third-party
   disclosure) and the "refuse-all does not flip the exempt `am` toggle" default are
   engineering readings of CNIL guidance, not counsel — explicitly flagged as such in ADR-025
   itself. **Does not block this release**: the fallback
   (`ConsentSettings.beacon_requires_consent=True` as a platform default) is a one-boolean
   flip with no redesign if legal disagrees. **Owner: Human/Legal.** Tracked in
   `specs/ecommerce_engine/11_uncertainties_to_validate.md` (lines 305-314).
3. **Real-client-IP restoration at the edge proxy is required before `/_consent/` traffic
   reaches Django** (F3, `CONSENT_AUDIT.md`). The 5/hour consent-decision rate limit keys on
   `REMOTE_ADDR` only, by design (no `X-Forwarded-For` parsing, to prevent limit-spoofing). If
   the proxy does not restore the real client IP, every shopper of a store shares one bucket:
   after 5 decisions/hour store-wide, all further shoppers get 429, the banner cannot be
   dismissed, and **pixels stay dark platform-wide for that store** until the window rolls
   over — fail-closed, but a real availability defect. Already an item on
   `docs/releases/pixels-currency-chat/RELEASE_CHECKLIST.md` (item 7, added retroactively) —
   one physical deploy check satisfies both. **Owner: DevOps.**
4. **Production cookie-flag verification is a live check, not just a code guarantee.**
   `webecom/settings/production.py` sets `SESSION_COOKIE_SECURE=True`, and the consent cookie
   inherits `Secure=settings.SESSION_COOKIE_SECURE`; `HttpOnly=True` always. This is verified
   in code and by unit test, but has not been observed against real production TLS
   termination. **Action:** one-time post-deploy browser-devtools check (Application →
   Cookies) confirming `pradize_consent` carries both flags. **Owner: QA/DevOps.**
5. **Fresh-session 204-vs-403 live check has not been performed outside the Django test
   client.** `CONSENT_AUDIT.md`'s F1/F7 fixes are proven under Django's full middleware stack
   in-process, but not against a real browser + real reverse proxy + real TLS. **Action:**
   open a fresh private-browsing session on a deployed single-currency store, click "Accept
   all," confirm 204 (not 403) and the cookie is set; repeat with "Refuse all." **Owner: QA.**

## Functional / correctness risks (not launch-blocking per the audits, but real)

6. **F5 — no automated consumer exists anywhere for `SlotProvider.validate_settings()`.**
   Verified beyond what the security audit checked: grepping every call site of
   `validate_settings`/`get_providers` in the repo shows the only caller of `get_providers()`
   is the `render_slot` template tag (which renders HTML, not a checklist) — there is no
   management command, admin view, or API that iterates providers and calls
   `validate_settings(store)` to produce an actual pre-launch report. References to
   "launch-readiness checklist" / "L11" elsewhere in the codebase document a contract for a
   consumer that does not exist yet. This means ADR-025 D6's "blocking error" (active pixels +
   consent disabled + no acknowledgment) is real, tested code, but **nothing runs it
   automatically before a store launches today** — same pre-existing gap for
   `PixelsSlotProvider.validate_settings()` and currency's equivalent, not introduced or
   worsened by this release. **Owner: Architect** (design the consumer) **→ Developer**
   (implement). Until it exists, the manual check is: run every registered provider's
   `validate_settings(store)` by hand (Django shell) before any store with active pixels goes
   live.
7. **F2 (LOW, hardening) — unsigned, non-`__Host-`-prefixed cookie.** Safe today because every
   store has its own registrable domain (`StoreDomain.host` globally unique). Recorded as a
   **binding** ADR-025 addendum: rename to `__Host-pradize_consent` before any future
   shared-subdomain store provisioning. No action needed now. **Owner: Architect**, tracked
   for the day this provisioning assumption changes.
8. **F7 (already fixed in this release, listed for completeness) — CSRF-cookie refresh on
   repeated manage-panel re-saves.** Verified fixed: `consent_post` now calls `get_token()`
   before every 204, refreshing `csrftoken`'s clock alongside the consent cookie's. No
   further action.
9. **F6 (INFO, cosmetic) — purge-count log drift.** `purge_consent_records()` logs
   `count()` then calls `.delete()`; under concurrent inserts the logged number can drift from
   the actual deleted count. `delete()`'s own return value is correct. No functional impact.
10. **No JS/browser test harness exists** (carried, same standing note as the pixels-
    currency-chat release's item 13). The reload-on-grant behavior, the inline banner JS's
    fetch/CSRF handling, and the manage-panel toggle state are asserted structurally on
    rendered HTML/Python-side tests only, not a real browser harness. Residual risk for
    anything that only breaks at the JS/DOM layer.
11. **No Spec Reviewer artifact exists for TICKET-048/049** specifically (same pattern as the
    pixels-currency-chat release, which also had no saved spec-review doc). The Security Audit
    and SEO Review both independently traced the implementation against every ADR-025
    decision point and found it matching, functioning as an equivalent cross-check for this
    area, but a dedicated Spec Reviewer pass is recommended before the next release cycle
    closes this gap formally. **Owner: Spec Reviewer**, non-blocking.
12. **No Designer review artifact exists for the consent banner**, which is new UI (unlike
    pixels/chat, which reused existing surfaces). The banner is theme-token-only by
    construction (ADR-012 tokens, TH-045 conformance), so it is not visually unreviewed, but a
    dedicated mobile-viewport pass (button/text sizing at the ≤40vh cap) is recommended.
    **Owner: Designer**, non-blocking.

## Human decisions still open (see RELEASE_CHECKLIST.md "Human decisions listed" for full detail)

13. Legal sign-off on the D4 beacon-exemption reading (item 2) — owner human/legal, does not
    block this release.
14. RM-1 disposition (item 1) — **RESOLVED, fixed 2026-07-11** (see item 1 above); the
    non-EU escape hatch may now be recommended to store owners.
15. F5 launch-checklist consumer (item 6) — owner Architect/Developer, pre-existing
    platform-wide gap, not introduced by this release.
16. Deploy-time live checks (items 3-5) — owner DevOps/QA, one-time post-deploy smoke checks.

## Cross-reference — EU position for pixels

This release changes `docs/releases/pixels-currency-chat/KNOWN_RISKS.md` item 1 from a
blanket "NOT launch-ready for any EU storefront" to **RESOLVED-by-ADR-025, CONDITIONAL** — see
that document (updated in this cycle) and `RELEASE_CHECKLIST.md` in this directory for the
exact conditions. The PIX:P1 GA4-only token-stripping residual (Facebook/TikTok/Snapchat/
Pinterest still transmit the token-bearing thank-you URL) is carried unchanged — it is a
confidentiality residual orthogonal to consent legality, not affected by this release either
way.
