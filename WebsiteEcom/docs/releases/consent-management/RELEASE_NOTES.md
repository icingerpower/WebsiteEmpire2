# Release Notes — Consent Management (TICKET-048/049, ADR-025)

**Date:** 2026-07-11

## Summary

This release adds GDPR/ePrivacy-compliant cookie consent management: a server-rendered
banner and preferences panel, a first-party HttpOnly consent cookie plus a server-side proof
trail, and server-side consent gating of the pixel-tracking feature shipped in the prior
release. This is the cross-backlog fix for the single largest carried risk of the previous
release (`docs/releases/pixels-currency-chat/KNOWN_RISKS.md` item 1: "pixels cannot legally
launch on EU storefronts without a consent gate").

## What's new

- **Cookie consent banner and preferences panel**, server-rendered into the already-declared
  `slot.consent` (no new slot). Three equal-visual-weight controls — Accept all / Refuse all /
  Customize — matching CNIL's "refusal as easy as acceptance" requirement. A "Cookie
  preferences" button in the footer reopens the panel at any time (except mid-checkout, where
  the footer is already suppressed for the distraction-free checkout experience).
- **Tri-state, per-category consent** (undecided / granted / refused) for two categories —
  third-party analytics (GA4) and marketing/advertising (Facebook, TikTok, Snapchat,
  Pinterest) — stored in one compact, HttpOnly, first-party cookie
  (`pradize_consent`, 180-day re-prompt interval) plus a server-side `ConsentRecord` proof
  row per decision (13-month rolling retention, no IP address stored).
- **Server-side render gating of pixels by consent category.** No pixel snippet renders for
  an undecided or refused shopper — not even inert/disabled markup. Purchase and
  initiate-checkout tracking claims are gated at the point they are consumed, not only at
  render time, so a shopper who consents *after* first seeing the thank-you page still gets
  their purchase pixel fired correctly on a later visit, instead of the claim being silently
  burned unconsented.
- **First-party audience-measurement beacon kept exempt by default** (no cookies, no
  persistent identifier, no IP, first-party only — the CNIL audience-measurement exemption),
  with an explicit per-shopper objection toggle and a per-store conservative override
  (`beacon_requires_consent`) for stores whose DPO prefers to require consent for it anyway.
- **Google Consent Mode v2** signals on the GA4 snippet, truthfully reflecting the shopper's
  actual granted/denied state for both `analytics_storage` and `ad_storage`/`ad_user_data`/
  `ad_personalization`.
- **Per-store enable, on by default.** Every store gets the banner at migration time
  (privacy-safe default). A store owner who does not target EU/EEA visitors can acknowledge
  this explicitly to disable the banner — see the important caveat about this path in
  "Known limitations" below.
- **Launch-checklist wiring**: a store with active pixels, the banner disabled, and no
  non-EU acknowledgment is flagged as a blocking configuration error.
- **French translations shipped day one** (`.po` catalog); other languages fall back to
  English until translated.
- **13-month rolling purge** of consent proof records via a scheduled management command,
  matching the existing scheduled-purge pattern used elsewhere in the platform.

## New EU-launch position for pixels

Pixels are no longer a blanket "NOT launch-ready for any EU storefront." The position is now
**RESOLVED-by-ADR-025, CONDITIONAL**: an EU-facing store may enable pixels once (1) this
consent release is deployed and active for that store, (2) the legal sign-off on the
first-party beacon's exemption reading is resolved or the conservative fallback applied, and
(3) a short list of deploy-verification items (real-client-IP restoration at the edge, live
production cookie-flag check) has been completed at least once for the serving environment.
See `RELEASE_CHECKLIST.md` in this directory for the exact, itemized conditions and owners,
and the updated `docs/releases/pixels-currency-chat/KNOWN_RISKS.md` /
`RELEASE_CHECKLIST.md` for the carried-forward cross-reference.

The **PIX:P1 residual** from the prior release is unchanged and unaffected by this release:
the thank-you page's order-access token is stripped from the automatically-collected page URL
for GA4 only; Facebook/TikTok/Snapchat/Pinterest still transmit the real, token-bearing URL.
This is a confidentiality residual, orthogonal to consent legality — its fix is a future
server-side Conversions API follow-up, not part of this release.

## Known limitation found during release validation (see `KNOWN_RISKS.md` item 1, "RM-1")

The "this store does not target EU/EEA visitors" acknowledgment (the only way to disable the
banner) currently does **not** restore unconditional pixel firing for that store, contrary to
what the architecture decision record describes as its intent. Because the pixel-gating code
checks the shopper's consent cookie state and nothing else, and a disabled banner means no
shopper can ever set that cookie, a store that takes this escape hatch will see its pixels go
permanently dark instead of firing unconditionally as before. This fails in the safe direction
(no tracking without consent) but defeats the purpose of the escape hatch. **Do not advise any
store owner that this acknowledgment restores pixel volume — today it does not.** Tracked as
a fix-before-recommending item, owner Architect/Developer.

## What is explicitly NOT in this release

- **Legal counsel sign-off on the CNIL audience-measurement exemption reading.** The
  engineering assessment is documented and testable; a lawyer's confirmation remains an open,
  external item. A one-boolean fallback exists if legal disagrees, so this does not block
  the release.
- **An automated launch-readiness checklist consumer.** `validate_settings()` exists and is
  tested for every slot provider (pixels, currency, consent), but nothing in the codebase yet
  runs it automatically before a store launches — this is a pre-existing, platform-wide gap
  this release does not introduce, but it does mean the "blocking error" this feature adds is
  currently enforced by manual review only.
- **A `__Host-`-prefixed consent cookie.** Safe today (every store has its own registrable
  domain); recorded as a binding constraint to apply before any future shared-subdomain store
  provisioning.
- **A JS/browser test harness** for the banner's client-side behavior (reload-on-grant, fetch/
  CSRF handling) — asserted structurally on rendered HTML and via the Django test client's
  full middleware stack, not a real browser harness, consistent with the rest of the platform.
- **A dedicated Spec Reviewer artifact** for this area — the Security Audit and SEO Review
  both independently cross-checked the implementation against the ADR and found it matching,
  serving an equivalent purpose for this release, but a dedicated pass is recommended before
  the next release cycle.

See `RELEASE_CHECKLIST.md` for full evidence per checklist item, `KNOWN_RISKS.md` for the
consolidated risk list (headlined by the RM-1 escape-hatch finding), and `ROLLBACK_PLAN.md`
for the rollback procedure and — importantly — what each rollback lever actually does and
does not restore.
