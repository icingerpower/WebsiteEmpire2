# Rollback Plan — Consent Management (TICKET-048/049, ADR-025)

**Date:** 2026-07-11

**Important correction to the assumed premise, verified against code in this cycle:**
"`ConsentSettings.is_enabled=False` or app removal restores pre-consent DOM per slot
contract" is **true for the banner** (the `slot.consent` wrapper renders empty again) but is
**NOT true for pixel-firing behavior**. `pixels/slot_provider.py::PixelsSlotProvider.render()`
never reads `ConsentSettings` at all — it gates every `Pixel` row purely on
`consent.state.get_consent(request)` (the cookie). Once the banner cannot render (either
because a store disabled it, or because the whole `consent` app is rolled back), **no shopper
can ever set the consent cookie again**, so `get_consent()` returns `UNDECIDED_STATE` forever,
so **pixels go dark, not unconditional** — the opposite of "restoring the pre-consent DOM" for
that slot. This is the fail-closed direction the ADR intends (§XV-1: never fire unconsented),
but it means neither rollback lever below, on its own, is a live wire back to pre-ADR-025
unconditional pixel firing. See "Consequence for EU pixels" at the end for the precise chain.

---

## What was added / changed

**New app:** `consent/` — `ConsentSettings`, `ConsentRecord` models; `policy.py`
(`CONSENT_POLICY_VERSION`, category constants); `state.py` (`get_consent`, the single
resolution point + cookie parser/serializer); `views.py` (`POST /_consent/`);
`slot_provider.py` (`ConsentSlotProvider`, slot `"consent"` — already declared, no new slot);
`admin.py` (store-admin "Cookie consent" settings card + read-only `ConsentRecord`
changelist); `management/commands/purge_consent_records.py` (13-month rolling purge);
`templatetags/consent_tags.py`.

**Touched existing apps (the gating touch points, ADR-025 D7):**
- `pixels/registry.py` — `PixelProvider.consent_category` class attribute (`"analytics"` for
  GA4, `"marketing"` for the four ad providers).
- `pixels/slot_provider.py` — `_resolve_consent()` + the `consent.allows(provider.
  consent_category)` gate inside `render()`.
- `pixels/service.py::claim_purchase_pixels` — gained a required `allowed_categories:
  frozenset[str]` parameter (the D0 correction: claims, not only renders, are consent-gated).
- `pixels/templates/pixels/ga_base.html` — Consent Mode v2 default line.
- `storefront/views_checkout.py::order_thank_you` — passes allowed categories into
  `claim_purchase_pixels`.
- `storefront/templatetags/storefront_tags.py` — analytics beacon tag no-ops on
  `am_objected` / `beacon_requires_consent`.
- `storefront/templates/storefront/partials/footer.html` — the "Cookie preferences" manage
  button.
- `sitemaps/views.py` — `robots.txt` now disallows `/_consent/` and `/_analytics/` (SEO
  review F1).

**Database schema (migrations):** `consent/migrations/0001_initial` (and any that follow) —
`ConsentSettings`, `ConsentRecord` tables. Entirely additive; no existing table's columns
were changed by this release (contrast the currency/pixels releases, which added columns to
their own new apps but never touched pre-existing tables either).

---

## Rollback levers, in order of safety/speed, with their real effect verified against code

### Lever 1 (fastest, narrowest, no deploy): per-store banner disable

`ConsentSettings.is_enabled=False` via `/admin/` (requires the `non_eu_ack_confirm`
checkbox per `consent/admin.py`'s form — this is a deliberate one-way acknowledgment gate,
not a plain toggle).

**What this actually does, verified:**
- `ConsentSlotProvider.is_enabled(store)` returns `False` → `render_slot "consent"` skips
  calling `.render()` for this provider (`storefront_tags.py:136`) → the banner and the
  "Cookie preferences" footer button disappear for that store. This is the "pre-consent DOM"
  restoration, and it is real and immediate — no deploy needed.
- **It does NOT restore pixel firing.** `PixelsSlotProvider.render()` is completely unaware
  of `ConsentSettings.is_enabled` (verified: zero references to `ConsentSettings` anywhere
  under `pixels/`). With no banner, no shopper can ever set `pradize_consent`, so
  `get_consent()` returns `UNDECIDED_STATE` in perpetuity for that store, and
  `UNDECIDED_STATE.allows("analytics"/"marketing")` is `False` by construction
  (`consent/state.py`). **Net effect: that store's pixels go dark, permanently, not
  unconditional.** This is a known implementation gap (RM-1, `KNOWN_RISKS.md`) relative to
  ADR-025's own documented intent for this escape hatch — flagged, not silently accepted.
- **Use this lever when:** the banner itself is the problem (broken markup, a legal concern
  specific to the banner copy, a performance issue) and you are willing to accept that
  store's pixels going dark as a side effect until the fix ships. **Do not use this lever
  expecting it to bring pixel volume back** for a non-EU store — today it does the opposite.

### Lever 2 (full rollback): remove `consent` from `INSTALLED_APPS` / git-revert the app

**What this actually does, verified against `pixels/slot_provider.py::_resolve_consent` and
`consent/state.py`'s own module docstring:**
- Every cross-app import of `consent.state.get_consent` (`pixels/slot_provider.py`,
  `storefront/views_checkout.py`, `storefront/templatetags/storefront_tags.py`) is wrapped in
  its own `try/except ImportError`, falling back to an **independent, locally-defined
  fail-closed stand-in** that never imports anything from the `consent` package in the except
  branch (verified: `_FailClosedConsent.allows()` returns `True` only for `"necessary"`).
- **Net effect: removing the `consent` app does NOT restore pre-ADR-025 unconditional pixel
  firing either.** It makes every store's pixels behave as if every shopper is permanently
  undecided — i.e. **pixels go dark platform-wide**, not "back to firing for everyone like
  before this release." This is intentional (§XV-1: a botched rollback must never
  accidentally start firing unconsented trackers) but is a real behavioral consequence worth
  stating plainly: **this rollback lever cannot un-ring the pixel-gating bell by itself.**
- **To actually restore true pre-ADR-025 behavior (pixels fire unconditionally for every
  store, exactly as before TICKET-048/049),** you must ALSO revert the pixels-side D0/D3
  touch points listed under "What was added / changed" above — specifically
  `pixels/slot_provider.py`'s consent gate, `pixels/service.py::claim_purchase_pixels`'s
  `allowed_categories` requirement (revert call sites to pass the full category set), and
  `storefront/views_checkout.py::order_thank_you`'s category-passing. This is a larger,
  cross-app revert, not a single-app removal — treat it as a full revert of the ADR-025
  change set, not just of the `consent` app directory.

### Rollback procedure

1. **Prefer Lever 1 first** if the problem is isolated to the banner (UI bug, a specific
   store's legal concern, performance) — no deploy, immediate, and its only real-world side
   effect (that store's pixels go dark) is the safe direction.
2. **If the problem is with the gating/claim logic itself** (e.g. a bug causes legitimate
   consented shoppers to still not see pixels, or a bug in `claim_purchase_pixels`), Lever 2
   (full revert) is appropriate, but plan for **all stores' pixels going dark** during the
   revert window — communicate this to Product/Support before executing, since it is a
   revenue-attribution-visible change, not a silent one.
3. **Migrations:** additive only.
   ```bash
   python3 manage.py migrate consent zero   # drops ConsentSettings/ConsentRecord tables
   ```
   Then remove `"consent"` from `INSTALLED_APPS` and revert the `consent/` directory and the
   D7 touch-point files via git. Determine the exact prior migration state via
   `showmigrations` on the actual deployed environment before running this.
4. **Cookie cleanup:** none needed server-side. `pradize_consent` is a normal HttpOnly cookie
   set only by successful `POST /_consent/` responses; once the endpoint and app are gone, the
   cookie simply stops being read (the fail-closed shim never looks at it) and expires
   naturally at its 180-day `Max-Age`. No proactive clearing mechanism exists or is needed.
5. **Post-rollback smoke check:**
   ```bash
   python3 manage.py test storefront.tests.test_theme_conformance
   ```
   (TH-045/consent slot conformance — confirms `slot.consent` renders its empty wrapper
   cleanly with the provider unregistered, same pattern as the pixels-currency-chat release's
   shared-surface check.)

---

## Data

- `ConsentSettings` / `ConsentRecord` tables are additive; reverse migration drops them
  cleanly — no destructive migration exists in ADR-025 (confirmed, matches the ADR's own
  "Rollback strategy" claim, re-verified here rather than taken on the ADR's word).
- `ConsentRecord` rows are the GDPR demonstrability proof trail (13-month rolling purge
  already running). A rollback that drops the table loses this proof trail entirely — if
  there is any open legal/audit need to retain it, export `ConsentRecord` via the admin
  changelist before running `migrate consent zero`.
- No financial or order state is touched by any rollback lever — consent/pixels are both
  read-only observers of order state; `FiredPixel` claim history (pixels app, pre-existing)
  is unaffected either way.

## What is NOT rolled back by this plan

- Consent decisions already recorded in shoppers' browsers (the `pradize_consent` cookie
  itself) — these simply become unreadable/ignored once the app is gone; they are not
  actively cleared.
- `ConsentRecord` audit history, unless explicitly exported before `migrate consent zero`.
- Real pixel-claim history (`FiredPixel` rows) — unaffected by any consent rollback lever;
  see the pixels release's own rollback plan for that app's specifics.

## Consequence for EU pixels — stated precisely, not assumed

- **Lever 1 (per-store disable) for an EU-facing store:** pixels for that store go dark and
  stay dark (RM-1) — this is *more* conservative than the pre-ADR-025 world, not a reversion
  to it. EU-launch readiness for that specific store is unaffected either way (pixels are not
  firing, so there is nothing non-compliant happening) — it is simply broken in the
  "pointless" direction: consent UI absent, tracking also absent.
- **Lever 2 (full revert) executed completely (consent app AND the pixels-side D0/D3 touch
  points both reverted):** true pre-ADR-025 behavior is restored — pixels fire
  unconditionally for every store again. At that point, **the EU-launch position reverts
  exactly to the pre-ADR-025 posture recorded in `docs/releases/pixels-currency-chat/
  KNOWN_RISKS.md` item 1: pixels are explicitly NOT launch-ready for any EU storefront**,
  because the consent gate that made them EU-ready no longer exists. Any EU-facing store with
  active `Pixel` rows at the moment of this full revert must have those rows deactivated
  (`Pixel.is_active=False`) as part of the same rollback action — the pixels app's own
  rollback plan (`docs/releases/pixels-currency-chat/ROLLBACK_PLAN.md`, "Area 1" step 1)
  already documents this as "the correct first response to the EU/GDPR concern."
- **Lever 2 executed partially (consent app removed, pixels-side gating left in place):**
  pixels go dark for every store, EU and non-EU alike, and stay dark until either the consent
  app is restored or the pixels-side gating is also reverted. This is the safe middle state —
  not compliant-and-firing, not non-compliant-and-firing, just off.
