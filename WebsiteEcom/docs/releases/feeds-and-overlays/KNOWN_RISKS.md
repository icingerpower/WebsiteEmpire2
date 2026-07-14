# Known Risks — Catalog Feeds + Conversion Overlays (TICKET-033/034/035, ADR-026/027)

**Date:** 2026-07-11. All items below were independently verified against current code/docs
by the Release Manager in this review cycle (not carried forward unverified). Sources:
`docs/security/FEEDS_ENGAGEMENT_AUDIT.md` (FINAL VERDICT: APPROVED WITH CONDITIONS), the spec
review's routed-fix list (independently re-verified in `RELEASE_CHECKLIST.md`), direct code
trace, and `manage.py check --deploy` re-run with a full environment.

## Deploy-config risks (block PRODUCTION, not STAGING)

1. **Feed-file serving location — code fix verified, live verification still open.** The
   release-blocking finding (feed XML files reachable under `/media/feeds/…` with no token
   check, nullifying the token gate) is fixed in code: `FEEDS_ROOT` is a dedicated,
   never-web-served directory, confirmed unreferenced by any URLconf. **What remains is a
   deploy-time human check, not a code gap:** on the production VPS, curl both
   `/media/feeds/1/google/us-en.xml` (expect 404) and the real tokened `/feeds/...` URL
   (expect 200), and confirm no nginx `alias`/`root` directive maps `BASE_DIR` broadly. **Owner:
   DevOps.**
2. **`FEEDS_ROOT` env-override discipline.** If an operator ever overrides `FEEDS_ROOT` via
   the environment, it must point to a path the web server does not serve. Not enforced by
   code (by design — the setting is meant to be flexible); must be a documented deployment
   rule. **Owner: DevOps/Architect** (add to deployment docs).
3. **Stale pre-fix `MEDIA_ROOT/feeds/` directories.** Not applicable to this deploy (the
   feature is unreleased, so production has no such directory yet) — but any environment that
   ran an earlier build of this feature (e.g. a staging box that predates the F3 fix) should
   have that directory deleted. **Owner: DevOps.**
4. **Real-client-IP restoration at the edge proxy.** `engagement`'s lead-capture rate limiter
   (5/IP/store/hour) keys on `REMOTE_ADDR` only, by design (no `X-Forwarded-For` parsing, to
   prevent limit-spoofing) — the same pattern already named in the pixels-currency-chat and
   consent-management release checklists. If the proxy does not restore the real client IP,
   every shopper of a store shares one bucket, and legitimate signups start getting 429s after
   5/hour store-wide. **Owner: DevOps** — one physical check now satisfies three releases'
   worth of this same item.
5. **Redis cache backend must be live in production** for the rate limiter's atomicity across
   workers — already a stated requirement from the prior release cycle; re-verified here as
   still true for this release's antispam reuse. **Owner: DevOps.**

## Functional / correctness risks (not launch-blocking, but real)

6. **SEO Agent review named in ADR-026 D8 was never run.** `docs/seo/` contains no feeds
   review. Assessed on its own merits (see `RELEASE_CHECKLIST.md` "SEO Agent approved"):
   feeds are non-indexed (robots-disallowed, token-gated, not HTML), and the one
   link-canonicalization question a review would raise is already proven by
   `feeds/tests/test_adr_named_cases.py::ABVariantSlugNeverInLinkTest`. Judged **non-blocking
   — a checklist line is sufficient, not a full review** — but recommend a short (~15 minute)
   SEO Agent confirmation pass to formally close the letter of ADR-026 D8. **Owner: SEO
   Agent, non-blocking.**
7. **Settings documentation lag.** `FEEDS_ROOT`, `FEEDS_REGENERATE_INTERVAL_MIN`, and
   `NAMED_SOCIAL_PROOF_MODES_ENABLED` are documented at the code/ADR level but not mirrored
   into `specs/ecommerce_engine/06_settings_requirements.md` (the canonical per-store-admin
   settings inventory, dated 2026-07-02, pre-implementation). That document's groups 10
   (Catalog feeds), 12 (Recent purchase notification), 13 (Lead capture overlay — global)
   also still read as pre-implementation drafts (e.g. group 12 still lists the GDPR display
   question as an open CRITICAL uncertainty, which ADR-027 D6 has since resolved
   anonymous-gated). Same structural gap pattern already recorded for `CHAT_KILL_SWITCH` in
   the pixels-currency-chat release. **Owner: Spec Agent/Architect, non-blocking.**
8. **Spec Reviewer re-approval loop not formally closed.** The spec review's verdict was
   REJECTED (narrowly), with nine routed items to fix. All nine were independently
   re-verified present and correct by this Release Manager (see `RELEASE_CHECKLIST.md`
   "Implementation complete"), but no fresh Spec Reviewer pass has re-run and formally
   flipped the verdict to APPROVED. Not blocking, since the underlying evidence was
   independently traced to working code and passing tests, not taken on the fix descriptions'
   word — but recommend closing the loop before the next release cycle, matching the pattern
   already carried in `consent-management/KNOWN_RISKS.md` item 11. **Owner: Spec Reviewer,
   non-blocking.**
9. **Pinterest / GTIN / promo-dates / color-size / shipping-block deferrals (ADR-026 P-1,
   P-4, P-5, P-6, P-7, P-8).** All six are named, human-approved future enhancements with
   their own trigger conditions recorded in ADR-026's P-table (e.g. P-5 triggers once the
   timed-promo entity exposes start/end dates; P-7/P-8 trigger once T032's per-product
   shipping-override semantics are decided). None are regressions or launch blockers — listed
   here only so they are not mistaken for gaps in this release. **Owner: Architect** (design
   each when its trigger condition is met), **no action before then.**
10. **T042 (A/B variant URLs) will need to arm a negative-case test.**
    `feeds/tests/test_adr_named_cases.py::ABVariantSlugNeverInLinkTest` proves the only
    reachable case today (canonical URL only, since A/B variant URLs do not exist anywhere in
    this engine yet) and contains an explicit "ACTIVATION NOTE" docstring for whoever
    implements T042: extend the test with the negative case (an A/B variant URL must never
    leak into `g:link`) at that time. **Owner: Developer, when T042 lands — not a condition
    of this release.**
11. **No JS/browser test harness exists** for the overlay's client-side triggers
    (exit-intent, time-delay, localStorage frequency cap) or the `pa:cart-busy`
    suppression window — same standing note carried from the pixels-currency-chat and
    consent-management releases' known-risks lists. All client-side behavior is asserted
    structurally on rendered HTML/Python-side tests only. Residual risk for anything that
    only breaks at the JS/DOM layer. **Owner: Test Agent, non-blocking, platform-wide gap.**
12. **No Designer review artifact exists** for the overlay-theme preview gallery or the feed
    admin card. Both are functional/small additions over existing surfaces, not new
    visual-first features, but a polish pass on the preview-gallery card grid (spacing, focus
    states, responsive breakpoints) is recommended. **Owner: Designer, non-blocking.**

## Human decisions still open

13. **Legal/human sign-off track (one legal item, three consumers)** — owner human/legal.
    Extends the existing ADR-025 beacon-exemption legal track to also cover: (a) whether
    ADR-027's named social-proof display modes may ever be enabled for any store — currently
    hard-blocked in code (`NAMED_SOCIAL_PROOF_MODES_ENABLED = False`), not just by policy;
    (b) the legal sufficiency of the DE-targeting double-opt-in detection basis
    (language-OR-ships-to-DE); (c) the ADR-027 D4 localStorage frequency-cap/exclusion
    exemption reading. **None of (a)/(b)/(c) blocks this release** — each has a no-redesign
    fallback (stay anonymous-only / keep the conservative DE default / keep the exemption as
    designed), exactly matching the ADR-025 beacon item's own fallback shape. Tracked in
    `specs/ecommerce_engine/11_uncertainties_to_validate.md` lines 305-337.
14. **SEO Agent confirmation pass (item 6)** — owner SEO Agent, non-blocking, ~15 minutes.
15. **Settings documentation pass (item 7)** — owner Spec Agent/Architect, non-blocking.
16. **Spec Reviewer re-approval loop (item 8)** — owner Spec Reviewer, non-blocking.
17. **Deploy-time feed-serving verification (items 1-3)** — owner DevOps, must complete
    before/at first production deploy of the feeds ticket.
18. **Whole-project note: `WebsiteEcom` remains untracked in git.** Verified directly this
    cycle: `git status --short` from the parent `WebsiteEmpire2` repository shows
    `?? WebsiteEcom/`, and no `.git` directory exists inside `WebsiteEcom/` — it is a plain
    untracked directory in the parent repo's working tree, not its own repository. Every
    commit-based rollback step described in this release's `ROLLBACK_PLAN.md` (git revert of
    specific files/directories) is available only once this directory is actually committed
    somewhere. **Owner: human** — decide whether to bring `WebsiteEcom` under the parent
    repo's version control or initialize it as its own repository; this is a pre-existing,
    whole-project condition, not something introduced by this release, but it is restated
    here because this is the first release cycle to explicitly verify and record it.

## Cross-reference — prior releases

This release does not change the EU-pixel position recorded in
`docs/releases/consent-management/RELEASE_CHECKLIST.md` ("New EU position for pixels") — feeds
and engagement do not touch the `pixels`/`consent` gating chain. The `pa:cart-busy` emitter
(item 1 of the spec-review-routed list) is a storefront-side UX coordination between the
cart-refresh flow and the overlay's suppression window; it has no consent or pixel-firing
implications and was verified to not alter any existing pixel claim/render path.
