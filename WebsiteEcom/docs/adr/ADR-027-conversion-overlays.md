# ADR-027: Conversion overlays — lead capture (T034) + recent-purchase social proof (T035)

**Status:** ACCEPTED (human, 2026-07-11) — D6 social proof ships **anonymous-only at
launch** (named modes `first_name`/`first_name_city` remain gated behind the human/legal
sign-off track, the same track as ADR-025's beacon-exemption item); D3 ships **single
opt-in + consent proof for v1, with double opt-in enabled by default for stores
targeting Germany**; D4's localStorage frequency-cap/exclusion exemption reading is
accepted as designed. T034/T035 are cleared for implementation.
**Tickets:** TICKET-034 (lead capture overlay), TICKET-035 (recent-purchase notification).
**Related:** ADR-002 §4/§5 (atomic coupon counters, `CampaignReward` idempotency),
ADR-004 (analytics DB, closed event vocabulary, no-IP invariant), ADR-012 (slot registry,
theme tokens, preview suppression), ADR-014/ADR-021 (translation pipeline),
ADR-018 D3 (antispam primitives), ADR-025 (consent categories, no-IP proof rows,
beacon-exemption legal track).
**Spec sources:** 02 §14, 03 UF-019, 04 AF-013, 06 §12/§13,
`screen_inventory_parts/part_C_admin_013-021.md` (admin-014, admin-015 ×3),
15 TH-045 / C-31, 16 §checkout suppression, 11 Part C (buyer-data uncertainty).

One ADR covers both tickets because they share the same delivery infrastructure
(the ADR-012 slot registry: `slot.overlay` and `slot.social_proof`), the same
storefront constraints (zero-CLS, checkout/preview suppression, theme-token styling,
vanilla JS), and adjacent privacy reasoning. They remain **two independently
implementable tickets**; the shared pieces already exist in `storefront/slots.py`.

---

## Decision

### D1 — App boundary: one new `engagement` app hosting both features

Both features live in a single new Django app `engagement/` (spec Area 14
"Lead capture & notifications" groups them), mirroring the `consent/` app shape:
`models.py`, `service.py`, `slot_providers.py` (two `SlotProvider` subclasses),
`views.py` + `urls.py` (public endpoints, T034 only), `admin` screens under the
existing Apps section, `tests/`.

- `LeadCaptureOverlayProvider(SlotProvider)` — `slot = "overlay"`, `key = "lead_capture"`.
- `SocialProofProvider(SlotProvider)` — `slot = "social_proof"`, `key = "recent_purchases"`.
- Both registered in `EngagementConfig.ready()` via `storefront.slots.register()`.

**Verified infrastructure (no changes needed):** `{% render_slot "overlay" %}` and
`{% render_slot "social_proof" %}` are already emitted by
`storefront/templates/storefront/base.html` (lines 72–73) inside overridable blocks;
`base_checkout.html` empties both blocks (16 §checkout suppression); both slot names
are in `PREVIEW_SUPPRESSED_SLOTS` (`storefront/templatetags/storefront_tags.py:59`),
so theme preview renders the empty wrapper only. The `<div data-slot="…">` wrapper is
always present (TH-045) — the T034/T035 JS may rely on it.

T034 creates the app and the lead-capture half; T035 adds the social-proof half
(models + provider + template only — no public endpoint, see D7). No cross-ticket
code dependency beyond the app skeleton.

### D2 — Lead capture data model: campaigns, not a settings singleton

The admin screens (admin-015 ×3) show a **campaign list with per-campaign stats and
lifecycle**, not a single settings page. The model is therefore campaign-shaped:

**`LeadCaptureCampaign(StoreOwnedModel)`**
- `name` (required), `is_active` (bool, default True per AF-013).
- `trigger`: choices `exit` / `time` (desktop). `trigger_delay_seconds`
  (PositiveInteger, used when `trigger=time`; default 15 — ASSUMPTION, screenshot
  hides the input until "Time" is selected).
- `mobile_trigger`: choices `disabled` / `time` (no exit-intent on touch, per spec);
  `mobile_trigger_delay_seconds` (default 15 — same ASSUMPTION).
- `excluded_pages`: M2M to `permalinks.Permalink` ("Exclude customers that visited
  pages" picker). Stable against slug edits; resolved to a path list at render time
  (D4) — never store raw path strings.
- `overlay_theme_key`: CharField validated against the overlay-theme registry (D10).
- Editable content (per the AF-013 MEDIUM recommendation — theme text IS editable
  after selection): `headline`, `body`, `cta_label`, `dismiss_label`,
  `success_message` (supports a `{code}` placeholder), all with sensible defaults
  matching the screenshot mockups ("SIGN-UP FOR OUR NEWSLETTER", …).
- Post-signup action: `post_signup_action` choices `display_message` / `go_to_url`;
  `redirect_url` (URLField, blank; `clean()` requires it when action is `go_to_url`).
- Frequency cap: `cap_enabled` (bool), `cap_impressions` (PositiveInteger),
  `cap_window_value` (PositiveInteger), `cap_window_unit` choices
  `minutes`/`hours`/`days` (exact screenshot shape: "[N] impressions per [N] [unit]").
- `signup_tags`: JSONField list of strings ("TAGS FOR CUSTOMERS THAT SIGNUP").
- Reward: `reward_discount_code` — nullable FK to `discounts.DiscountCode`
  (PROTECT). v1 links an **existing** code (the AF-013 recommendation); per-lead
  generated unique codes are a later enhancement, NOT in these tickets.
- Denormalized counters (D5): `visitors_count`, `impressions_count`,
  `conversions_count` — PositiveIntegers, **only ever updated via `F()` expressions**
  (same discipline as `DiscountCode.times_used`, ADR-002 §4).
- Name field on the overlay form: always rendered, always optional (ASSUMPTION,
  resolving UF-019's PENDING "name required or optional" — the theme mockups show a
  NAME input, and requiring it measurably kills conversion; no per-campaign mode
  in v1).

**`LeadSignup(StoreOwnedModel)`** — one row per successful signup; doubles as the
**marketing-consent proof record** (Art. 7(1) GDPR: who consented, when, via what):
- `campaign` FK (PROTECT — the signup is the audit trail), `email` (lowercased),
  `customer` FK (SET_NULL), `lang` (language the overlay was shown in),
  `created_at`.
- `unique_together (store, campaign, email)` — the idempotency guard for
  double-submits (D3).
- **Deliberately NO IP address field** — same invariant as `consent.ConsentRecord`
  and `analytics.Event` (ADR-025 D2 / ADR-004). IP is used transiently in the
  antispam cache key only.

**`LeadCaptureCampaignTranslation(StoreOwnedModel)`** — mirrors
`campaigns.CampaignStepTranslation` exactly (D9): `campaign` FK, `lang_code`,
translated `headline`/`body`/`cta_label`/`dismiss_label`/`success_message`,
`status` draft/published, `ai_job` FK (SET_NULL),
`unique_together (store, campaign, lang_code)`. Produced by the AiJob CLI runner —
never direct API (memory: feedback_translation_cli).

**No global lead-capture toggle in v1** (resolves 06 §13's PENDING as "not needed"):
the screenshot shows none, and per-campaign `is_active` already covers the
requirement. Adding one later is additive. ASSUMPTION, consistent with the settings
spec's own note.

### D3 — Signup path: one rate-limited POST, existing-customer merge, idempotent coupon, single opt-in

`POST /overlay/signup/` (store-scoped storefront URL, `engagement/views.py`):

1. **Antispam (reuse `pages/antispam.py`, ADR-018 D3 — nothing new invented):**
   `honeypot_triggered(request)` → return the normal success response (silent drop,
   info log — never reveal detection); `rate_limit_exceeded(request,
   scope="lead_capture")` with the default 5/IP/store/hour → HTTP 429 with a plain
   message. CSRF enforced (storefront sessions already carry tokens — the consent
   banner uses `get_token`).
2. **Validate:** `campaign_id` must be an active campaign of `request.store`
  (404 otherwise); email via Django's EmailValidator; name optional, capped at 255.
3. **Customer:** `customers.service.get_or_create_customer(store, email,
   first_name=name)` — the existing merge-never-overwrite service. Then set
   `accepts_marketing=True` and append `campaign.signup_tags` to `customer.tags`
   (dedup, preserve existing — the TICKET-011 tags contract).
4. **Consent proof:** create `LeadSignup`. On `IntegrityError` from
   `(store, campaign, email)` → this email already signed up for this campaign:
   return the same success payload (including the same code) **without**
   incrementing `conversions_count`. Idempotent by construction.
5. **Coupon (idempotent, ADR-002 §5):** if `reward_discount_code` is set, write
   `discounts.CampaignReward(campaign_id=f"lead_capture:{campaign.pk}",
   recipient_email=email, discount_code=campaign.reward_discount_code)` inside the
   same transaction; a duplicate INSERT hits the unique constraint and the caller
   returns the already-issued code. `campaign_id` uses the pk, not the name — stable
   across renames (the `CampaignReward` contract requires a stable opaque ref).
   The linked code keeps `provenance=LEAD_CAPTURE` (the enum value already exists in
   `discounts.ProvenanceType`); abuse of the shared code is bounded at redemption by
   the existing `per_email_limit` machinery (ADR-002 §4) — signup-time farming of
   the code string is harmless because redemption, not possession, is limited.

   **Adjudication (dated 2026-07-11, Architect):** a paper conflict was flagged
   between this reuse and `05_database_schema.md` §3 / `ADR-002` §5's wording,
   which described `CampaignReward` as carrying a literal `campaign` FK +
   `customer_email`. Verified directly against the shipped model
   (`discounts/models.py`): `CampaignReward` already stores `campaign_id` as an
   opaque `CharField` (not an FK) plus `recipient_email`, `discount_code` FK,
   `issued_at`, unique on `(store, campaign_id, recipient_email)` — an exact
   match for this D3 usage as written above. **Decision: reuse
   `discounts.CampaignReward` directly, as designed — do not introduce a
   dedicated `LeadReward` model.** The conflict existed only on paper; the two
   stale spec rows (`05_database_schema.md` §3 and the `ADR-002` §5 prose) have
   been corrected to match the code, and both now cross-reference the unrelated
   `campaigns.CampaignIssuedCode` (FK-bound to `CampaignSession`/`CampaignStep`,
   ADR-009/ADR-010 funnel/abandonment scope) to prevent the two models from
   being confused again.
6. **Response:** JSON `{ok, action, message_html | redirect_url, code?}`. The view
   renders `success_message` with `{code}` substituted server-side.
7. `conversions_count` incremented via `F()` in the same transaction as the
   `LeadSignup` INSERT.

**Email marketing legal basis — honest assessment.** Single opt-in (checkbox-free:
the form's sole purpose is newsletter signup, stated in the overlay copy) plus the
`LeadSignup` proof row satisfies GDPR Art. 6(1)(a)/7 in most EU member states for
v1. Germany (BGH case law) and some DPAs effectively require **double opt-in**
(confirmation email before any marketing send). Decision: **v1 ships single opt-in
with proof rows.** **DECIDED (human, 2026-07-11): double opt-in enabled by default
for stores selling into Germany (DE-targeting), single opt-in elsewhere** — this
product-level default is approved as recommended; it must be implemented and active
**before the first marketing campaign is actually sent to captured leads**, not
before this ticket ships (capture ≠ send). The fallback mechanism is additive: a
`confirmed_at` field on `LeadSignup` + one confirmation email template + one
signed-token confirm view; no redesign.
**Legal sufficiency of the DE default (and of single opt-in elsewhere) remains an
external legal-review item on the same human/legal track as the ADR-025
beacon-exemption sign-off — tracked in `11_uncertainties_to_validate.md`,
non-blocking for implementation.**

**DE-targeting detection basis — DECIDED (human, 2026-07-11):** a store counts as
DE-targeting, and therefore gets double opt-in by default, when its `de`
`StoreLanguage` is enabled **OR** any of its `ShippingCountry` rows targets
Germany — either condition alone is sufficient, both are not required. Basis: BGH
(German Federal Court of Justice) case law applies German consumer-protection
expectations based on either the language a store markets in or the country it
ships to, independently of the other, so an OR (not an AND) is the correct
reading. (The safety audit's `is_de_targeting_store` note — "active `de`
`StoreLanguage`" only, over-approximating by also catching AT/CH German
storefronts — describes the language half only; the shipping-country half of
this OR is confirmed here as part of the same decision, both directions being
the safe/over-inclusive side of a consent control.)

**`overlay_confirm` delivery timing — noted 2026-07-11.** D3 above states the
double opt-in confirm view is required *before the first marketing send*, not
before T034 ships (capture ≠ send) — it was explicitly allowed to be deferred
past T034. In practice it was delivered early, within T034's own implementation,
and was therefore in scope for the pre-release safety audit alongside the rest of
T034: reviewed as `FEEDS_ENGAGEMENT_AUDIT` finding F4 (cross-store token binding
gap on `overlay_confirm`), **APPLIED/fixed 2026-07-11** — see
`docs/security/FEEDS_ENGAGEMENT_AUDIT.md` F4.

**No automatic welcome email in v1.** `emails/signals.py:send_welcome_email`
(template `welcome`) exists and is trivially wireable, but UF-019 specifies the
in-overlay confirmation message only — wiring a send is a product decision, not an
architecture default (LOW, noted for the product backlog; do not implement in T034).

### D4 — Overlay delivery and UX: server-rendered hidden markup, client-side triggers, localStorage state

- **Campaign selection (server, per request):** among active campaigns, render
  **exactly one** — the most recently created active campaign (the AF-013
  recommendation for concurrent campaigns). Rendering means: the provider emits the
  full overlay markup `hidden` (inline `display:none` on the root, revealed only by
  JS) plus one embedded JSON config block (`<script type="application/json">`) with:
  campaign id, trigger config, cap config, excluded paths (resolved from the
  `excluded_pages` permalinks for the current store, all active languages), CSRF
  token endpoint usage notes, and the localized strings already baked into the HTML.
- **Client (vanilla ES module in `static/storefront/`, no build step — ADR-012):**
  - Exit-intent = `mouseout` toward the viewport top with `relatedTarget == null`,
    desktop pointer only (`matchMedia('(pointer: fine)')`); touch devices use the
    mobile trigger config (`disabled` → never fires; `time` → `setTimeout`).
  - **Visited-page exclusion is client-side:** the module appends the current path
    to a capped (50-entry) localStorage list on every page view; before firing, it
    intersects that list with the excluded-path list from the config and suppresses
    on a match. No server-side browsing-history tracking is introduced (deliberate:
    keeps PII out of the server and needs no consent category).
  - **Frequency cap:** localStorage per campaign — an array of impression epoch
    timestamps, pruned to the cap window; the trigger is suppressed once
    `cap_impressions` is reached within the window. Dismissal (X or the dismiss
    link) writes an impression timestamp too, so a dismissed overlay respects the
    cap rather than re-firing on the next page.
  - **Cart/checkout interaction suppression (UF-019 edge case, PENDING resolved
    with a default):** the trigger is suppressed while an add-to-cart request is
    in flight and for 10 s after (a module-level flag the existing cart JS sets);
    checkout pages never render the slot at all (base_checkout).
- **Inline-JS location — noted 2026-07-11 (deviation, accepted):** the "Client"
  bullet above is stated as a pure static ES module, but the widget-chrome
  strings that must go through gettext (D9: platform-owned copy, `{% trans %}`
  in the slot templates, collected by `makemessages`) cannot live inside a
  static `.js` file — Django's template engine never processes `static/`.
  Those translated strings, and the small bootstrap that reads the embedded
  JSON config block, are therefore rendered inline in
  `storefront/templates/storefront/partials/overlay_themes/_base.html`
  (confirmed at `_base.html:101`, `FEEDS_ENGAGEMENT_AUDIT.md` area 8/finding
  F6), with the static module consuming them from the DOM/config rather than
  containing translated literals itself. This is a deviation from "static
  module" as the sole JS location, accepted as designed — no redesign
  required.
- **Zero CLS:** the overlay root is `position:fixed` + backdrop; it displaces no
  layout (consistent with the T041 fixed-overlay rule in
  10_implementation_tickets.md).
- **No-JS behavior:** the markup ships `display:none` and is only revealed by JS —
  without JS the overlay simply never shows. Accepted and stated: a lead-capture
  popup is a progressive enhancement, not content.
- **Accessibility:** `role="dialog"` `aria-modal="true"`, focus moved into the
  dialog on open, focus trap, `Escape` closes, focus returned to the prior element.
- **Storage/consent classification:** the localStorage keys (visited paths capped
  list, impression timestamps) contain no identifier and no PII, are first-party,
  and exist to *reduce* intrusion (cap re-prompting). We classify them as exempt
  functional storage — the same engineering-reading caveat as ADR-025 D4's beacon
  exemption. **DECIDED (human, 2026-07-11): the localStorage frequency-cap/exclusion
  exemption reading is accepted as designed** — no consent-category gating required.
  Fallback retained as documented, not activated: gating overlay JS behind the
  ADR-025 functional category remains a one-line `is_enabled` change if this reading
  is ever revisited. The signup form itself is user-initiated first-party data entry
  and needs no cookie-consent gating.

### D5 — Lead-capture stats: main-DB `F()` counters; analytics-DB events deferred

The admin list needs three numbers per campaign (admin-015). Definitions (resolving
the 02 MEDIUM "visitors vs impressions"; the screenshot's own arithmetic — 1
conversion / 6 impressions = 17% — pins the rate denominator):

- **Visitors** = unique storefront sessions to which the campaign was served
  (provider rendered it). Incremented server-side once per session per campaign via
  a session flag — no beacon needed.
- **Impressions** = actual displays (the trigger fired and the overlay became
  visible), reported by JS: `POST /overlay/event/` `{campaign_id}` → rate-limited
  (`scope="lead_capture_event"`, generous limit e.g. 60/hour), `F()` increment.
  Client-reported, therefore best-effort — acceptable for merchant-facing stats.
- **Conversions** = successful first-time signups (D3 step 7, server-side truth).
- **Conversion rate** = conversions / impressions (computed at read time, never stored).

Counters live on `LeadCaptureCampaign` in the **main DB**, not the analytics DB:
ADR-004 §2 declares a **closed event vocabulary** and these three numbers need no
sessionized event stream. 03_user_flows' `EVT_LEAD_CAPTURE` is therefore **deferred**
— emitting it requires an ADR-004 amendment adding the event type; do that only when
a real aggregate consumer exists (deliberate, documented deviation from the flows
spec's event list, not an oversight).

### D6 — Social proof: what buyer data is shown publicly (resolves the 11 Part C item)

**Display modes** (`SocialProofSettings.display_mode`, D8):

| Mode | Rendered example | Personal data published? |
|---|---|---|
| `anonymous` (**platform default**) | "Someone in France bought *Linen Shirt* — 2 hours ago" | No (country alone, from an order, is not identifiable) |
| `first_name` | "Marie bought *Linen Shirt* — 2 hours ago" | Yes |
| `first_name_city` (**platform-allowed maximum**) | "Marie from Lyon, France bought…" | Yes |

Hard rules in every mode: **NEVER** surname, full name, email, email-derived
strings, street/postal data, order number, or amounts. First name is taken from
`customer.first_name`, falling back to the first token of the
`shipping_address['name']` snapshot; city/country from the `shipping_address`
snapshot (`city`, `country` keys — verified present on `orders.Order`). Orders
whose customer has `anonymized_at` set are **excluded from the named modes**
(still usable anonymously). Blank first name → that entry renders as `anonymous`.

**Lawful-basis reasoning (recorded honestly):** `anonymous` mode publishes no
personal data and needs no basis — this is why it is the default. The named modes
publish a real buyer's first name (+ city) on a public page: the arguable basis is
legitimate interest (Art. 6(1)(f)) with data minimisation — short rolling window,
no linkage, coarse time buckets — plus transparency (a sentence in the store's
privacy policy). That balancing test is an engineering reading, not counsel, and
buyers are never asked. **DECIDED (human, 2026-07-11): social proof ships
anonymous-only at launch** — `anonymous` is the only mode any store may enable
(platform default, `SocialProofSettings.is_enabled=False` per store). The named
modes (`first_name`, `first_name_city`) remain built into the schema and admin UI but
stay gated behind human/legal sign-off on the same track as the ADR-025
beacon-exemption item (11 Part C already flagged exactly this — one legal track, two
consumers); until that sign-off, they cannot be enabled for any store and carry an
admin-visible "pending privacy review" warning. This matches the ticket's own
blocker ("needs human sign-off before enabling by default"). Fallback if legal says
no: delete two enum choices — no redesign.

**Never fabricate.** Verified: the extra-spec's "fake inventory (server
assignment)" philosophy (`extra-spec-ecom.txt` line 40) is **inventory-only**;
no fabricated social proof appears anywhere in the specs — the screen inventory's
MEDIUM "hide widget? show fabricated data?" is an open question, which this ADR
answers: **when no PAID orders exist in the window, the widget renders nothing.
No seed data, no fabricated purchases, ever.** Beyond ethics, fabricated social
proof is a misleading commercial practice under the UCPD (and FTC endorsement
rules) — legal exposure, not a product knob. If the product owner ever wants
simulated notifications, that is an explicit human ethics/product/legal decision
requiring its own ADR; this architecture deliberately provides no hook for it.

### D7 — Social proof delivery: server-embedded JSON, cached; NO public buyer-data endpoint in v1

Options were (a) a polled JSON endpoint and (b) inline JSON embedded at page render.
**Chosen: (b) inline embed.** The provider renders the toast container (hidden,
`position:fixed`, zero CLS) plus a `<script type="application/json">` payload of up
to `max_items` sanitized entries; the JS module cycles through them with
`first_delay_seconds` / `interval_seconds` (screenshot fields). Rationale:

- **Smaller attack surface:** no new public endpoint serving buyer-derived data —
  the data rides the already-rendered page, with the same cache lifecycle. This is
  the decisive safety argument.
- **Freshness is sufficient:** entries are fresh at page load; a shopper's page
  lifetime is short relative to the rolling window. Live refresh via an endpoint is
  a later enhancement if ever needed (would then need its own Safety review).

**Feed computation** (`engagement/service.py`): orders of the store with
`payment_status=PAID` and `placed_at >= now − window_hours`, newest first, capped at
`max_items`; one entry per order (first item's product), product title/URL resolved
through the existing translation + PermalinkResolver chain for the current language
(§ single-permalink-resolver lesson — never hand-build URLs; entries whose product
has no active permalink in the current language are skipped, ML-011 rule).
**Implementation bound — ASSUMPTION, noted 2026-07-11:** the underlying order
query is capped at `_SOCIAL_PROOF_ORDER_SCAN_LIMIT = 200` — at most this many
of the store's most-recent PAID orders within `window_hours` are scanned before
selecting the (permalink-filtered) top `max_items` for display. This is not
spec'd in the ADR text above or in the screenshots; it bounds worst-case
DB/CPU cost per cache-miss render for stores with high order volume within the
window, at the cost of a theoretical (and here, accepted) edge case: a store
placing more than 200 orders inside `window_hours` could have its very newest
orders excluded from the scan if they sort after the 200th by the query's
ordering. Given `window_hours` defaults to 1 and `max_items` maxes at 20, 200
is a generous multiple; revisit only if a real store's order volume approaches
this bound.
Timestamps are **coarsened server-side into localized relative buckets** ("a few
minutes ago", "2 hours ago") — the raw datetime never reaches the client, so
precise order timing cannot be inferred. All display-mode filtering happens
server-side; the client receives only final display strings.

The computed feed is cached (`django.core.cache`) for 60 s per
`(store, lang, display_mode)` — one cheap cache get per page render, bounded DB
load, TTL-only invalidation (no signals; 60 s staleness is irrelevant here).

**Consent gating: none needed.** The widget sets no cookies/storage, reads nothing
client-side, and tracks nothing — the GDPR question is entirely about the data
*shown* (D6), not about the viewer.

### D8 — `SocialProofSettings(StoreOwnedModel)`: per-store singleton

Mirrors `ConsentSettings` + `get_or_create_social_proof_settings(store)`:

- `is_enabled` — **default False** (ticket blocker: not enabled by default before
  human sign-off; even `anonymous` mode stays opt-in per store).
- `display_mode` — choices per D6, default `anonymous`.
- `window_hours` — default 1 (screenshot value), validators 1–168.
- `first_delay_seconds` — default 2 (screenshot), validators 1–300.
- `interval_seconds` — default 2 (screenshot; aggressive but spec'd), validators 1–300.
- `max_items` — default 10, validators 1–20 (ASSUMPTION, not on the screenshot).
- `position` — choices `bottom_left` (default) / `bottom_right` (ASSUMPTION; the
  screenshot shows no position field — kept because themes may want either corner;
  top positions excluded to avoid header collisions).

The global enable toggle requested by the owner annotation on admin-014 is
`is_enabled` — IN_SPEC, satisfied. Neither settings model participates in
launch-readiness (06 §12/§13: "Required for launch? no" on every field), so
`validate_settings()` on both providers returns `[]` unless enabled-but-misconfigured
(e.g. campaign active with `go_to_url` and empty URL).

### D9 — i18n: platform strings via `.po`, merchant content via translation rows

Two established patterns, applied by ownership of the string:

- **Widget chrome** (social-proof sentence scaffolding "Someone", "bought",
  time buckets; overlay form labels EMAIL/NAME placeholders' defaults, error
  messages): platform-owned → `{% trans %}` in the slot templates, collected into
  the storefront `.po` catalog — exactly the ADR-025 D6 / consent-banner decision,
  including its template-location reasoning (templates live under
  `storefront/templates/storefront/partials/` so one `makemessages` pass collects
  them).
- **Merchant-authored campaign content** (headline/body/CTA/dismiss/success):
  store-owned → `LeadCaptureCampaignTranslation` (D2), produced by the AiJob CLI
  runner with draft→published lifecycle, resolution = published translation for the
  request language else the campaign's base fields (the `CampaignStepTranslation`
  fallback rule verbatim). Social proof has no merchant-authored text in v1.

### D10 — Overlay themes and store-theme conformance

- **Overlay theme gallery** (admin-015 "SELECT A THEME" grid) = a code registry, not
  DB rows: `OVERLAY_THEMES` in `engagement/` mapping `key → {template, preview_png,
  name}` with 4–6 layout templates (photo-left dark, centered light, photo-right
  accent, top banner — matching the screenshot set). Registry pattern per the
  system prompt; adding a theme = adding a template + registry entry.
  `overlay_theme_key` is validated against the registry in `clean()` (loud at admin
  save, §XV-1); if a stored key disappears from the registry later, `render()` falls
  back to the default layout **with an error log** — the storefront must never 500
  over a removed overlay skin (slot contract: `render()` never raises).
- **Styling:** both widgets consume ADR-012 design tokens only (`--p-color-*`,
  `--p-font-*`) so they inherit each of the 3 store themes without per-theme CSS;
  overlay theme templates control *layout*, tokens control *skin*.
- **Conformance:** the existing theme conformance suite already fails a theme that
  drops either slot (TH-045). T034/T035 add render assertions under all 3 themes.

### Permissions and gates

- Admin screens: Store Admin or Employee with **Apps — Full access** (AF-013 actor),
  enforced with the existing employee-permission checks; store-scoped querysets via
  `.for_store()` everywhere (both models are `StoreOwnedModel`).
- **Safety Agent gate is MANDATORY before release for both tickets** (named here per
  the pipeline): T034 exposes a public unauthenticated POST pair
  (signup + impression event) — review spam/enumeration/CSRF/429 behavior and the
  coupon-issuance path; T035 publishes buyer-derived data on every storefront page —
  review display-mode enforcement, anonymized-customer exclusion, and that no raw
  order fields reach the client.
- SEO Agent review is NOT required: both widgets are `position:fixed`,
  JS-revealed, non-content overlays with no crawlable links added (the social-proof
  product links duplicate existing catalog links); no indexable page structure
  changes. (Google's interstitial guidance is a design constraint, not an SEO
  architecture change — the overlay must not fire immediately on landing; the
  default 15 s time trigger and exit-intent respect this.)

---

## Context

Two GrooveKart/CommerceHQ-parity conversion features (screens admin-014, admin-015)
were specced but not designed: an email-capture popup with campaign lifecycle,
triggers, frequency caps, tags and a promised coupon; and a social-proof toast
showing recent real purchases. The slot infrastructure they render into was built
ahead of time in TICKET-029/ADR-012 and verified present. Blocking uncertainty:
11 Part C "what buyer data is shown publicly (privacy/GDPR)" — this ADR resolves it
with a shipping-safe default (D6). Adjacent lessons applied: atomic counters and
idempotent reward issuance (ADR-002), no-IP proof rows and the legal-track pattern
(ADR-025), antispam reuse (ADR-018 D3), translation via AiJob CLI (ADR-014/021).

## Options considered

1. **App boundary:** separate `leadcapture` + `socialproof` apps · one `engagement`
   app (chosen) · folding into `campaigns` (rejected: `campaigns` is upsell/email
   machinery with its own lifecycle; Area 14 is a distinct admin surface).
2. **Lead-capture config:** settings singleton (as the ticket brief sketched) ·
   campaign rows (chosen — the screenshots unambiguously show a campaign list with
   per-campaign stats and lifecycle).
3. **Stats:** analytics-DB events (rejected: closed vocabulary, needs ADR-004
   amendment, overkill for three admin numbers) · main-DB `F()` counters (chosen).
4. **Coupon reward:** per-lead generated unique codes (deferred) · shared linked
   `DiscountCode` + `CampaignReward` idempotency row (chosen; redemption-side
   `per_email_limit` already bounds abuse).
5. **Social-proof delivery:** polled public JSON endpoint (rejected for v1: new
   public buyer-data surface, caching + rate-limit machinery, no freshness need) ·
   server-embedded JSON with 60 s cached feed (chosen).
6. **Buyer display:** first-name+city default (rejected: publishes personal data by
   default ahead of legal sign-off) · anonymous default with named modes gated
   behind the legal track (chosen) · fabricated entries when the window is empty
   (rejected outright — UCPD/FTC exposure; explicitly not designed in).
7. **Visited-page exclusion:** server-side browsing-history tracking (rejected:
   creates a PII trail and a consent question) · client-side localStorage path list
   (chosen).

## Chosen option

One `engagement` app; campaign-shaped lead capture with `LeadSignup` consent-proof
rows, antispam-reused POST, `CampaignReward`-idempotent shared-coupon issuance,
client-side triggers/caps/exclusions, `F()` counters with pinned metric definitions;
social proof from PAID orders in a rolling window, server-embedded sanitized +
cached feed, no public endpoint, `anonymous` display default with named modes
pending legal sign-off, and an absolute no-fabrication rule. Both widgets are
token-skinned slot providers already suppressed on checkout and in preview.

## Why

Every risky sub-decision reuses a pattern that already survived a Safety audit or
human sign-off in this codebase (antispam, CampaignReward, no-IP proof rows, the
legal-track fallback structure, CampaignStepTranslation, slot providers), so the
Developer implements against known contracts rather than inventing architecture.
The two genuinely product/legal-level questions (named-mode buyer display, double
opt-in) are shipped *around* — safe defaults now, one-boolean/enum fallbacks later —
instead of blocking the tickets or silently deciding law by code.

## Risks

- **Client-side impression counting is spoofable/lossy** — merchant stats only,
  never billing; rate-limited; accepted as best-effort (same stance as antispam
  LocMem).
- **Shared coupon code becomes public knowledge** once one lead shares it —
  bounded by `per_email_limit` at redemption; residual margin erosion is a merchant
  configuration choice (they pick the linked code). Per-lead unique codes are the
  future fix.
- **localStorage exemption reading could be rejected by legal** — fallback: gate
  the overlay module behind the ADR-025 functional consent category (small change,
  documented in D4).
- **Named social-proof modes rejected by legal** — fallback: remove two enum
  choices; `anonymous` default means no shipped store regresses.
- **interval_seconds=2 default is aggressive UX** — kept because it is the spec'd
  screenshot value; merchants can raise it; Designer pass may propose a new default
  (requires spec note, not an ADR change).
- **First name parsed from `shipping_address['name']` can misfire** (single-token
  company names, honorifics) — mitigated: `customer.first_name` preferred, blank →
  anonymous rendering for that entry.
- **Most-recent-active campaign selection** may surprise merchants running two
  campaigns intentionally — documented in the admin UI helptext; per-campaign page
  exclusions still apply.

## Rollback strategy

Both features are additive and default-quiet: social proof ships `is_enabled=False`;
lead capture shows nothing until a merchant activates a campaign. Emergency
disable = deactivate campaigns / flip `is_enabled` per store (no deploy), or
unregister the two providers in `EngagementConfig.ready()` (one-line deploy) — the
slots then render their empty wrappers exactly as today. Schema rollback: the
`engagement` migrations touch no existing tables except the `CampaignReward` rows
they insert (PROTECT-linked; reversing requires deleting reward rows first, which is
the intended audit friction). Public endpoints are app-local URLs — removing the
app's `urls.py` include removes the surface.

## Tests required

**T034 (lead capture) — AC-150…154 plus:**
- Provider: renders only for the most-recent active campaign; nothing when none
  active; nothing on checkout pages (base_checkout block empty) and in theme
  preview (regression pin on `PREVIEW_SUPPRESSED_SLOTS`, mirroring
  `test_consent_banner.py` §10d); wrapper `<div data-slot="overlay">` always present.
- Signup POST: creates Customer with `accepts_marketing=True` + merged deduped tags;
  existing-customer fields never overwritten (service contract); `LeadSignup`
  created; duplicate submit → success, same code, `conversions_count` unchanged;
  invalid campaign/store mismatch → 404; malformed email → 400.
- Coupon: reward issued once per email under concurrent duplicate POSTs
  (`CampaignReward` IntegrityError path); no reward configured → success without
  code; issued code has `provenance=lead_capture`.
- Antispam: honeypot filled → normal success response, no rows written; 6th
  submission in an hour → 429; message/name length caps enforced.
- Counters: visitors incremented once per session across repeated renders;
  impression endpoint increments via `F()` (assert no read-modify-write, concurrent
  increments sum correctly); conversion rate = conversions/impressions in the admin
  list (17% fixture reproducing the screenshot arithmetic).
- Content: `{code}` substitution; `go_to_url` action requires URL at clean();
  unknown `overlay_theme_key` rejected at save, removed-key render falls back +
  logs; translation fallback (published FR row shown on /fr, draft ignored, base
  fields otherwise).
- Permissions: employee without Apps Full access cannot list/edit campaigns;
  cross-store campaign access denied.
- Rendering: all 3 themes render the overlay from tokens (conformance additions);
  fixed-position root (zero CLS); markup hidden without JS.

**T035 (social proof) — no dedicated ACs (manual QA + privacy review) plus:**
- Feed: only PAID orders inside `window_hours`; empty window → provider renders
  nothing (assert NO fabricated entries — pin the no-fabrication rule as a test);
  `max_items` cap; store-scoped (other stores' orders never appear).
- Privacy: `anonymous` mode output contains no first name/city; `first_name_city`
  never emits surname/email/order number/amount (assert on serialized payload);
  anonymized customer (`anonymized_at` set) excluded from named modes but present
  anonymously; blank first name renders anonymous entry; raw datetimes absent from
  payload (buckets only).
- Settings: defaults (`is_enabled=False`, `display_mode=anonymous`, window 1 h,
  delays 2 s); validator bounds; disabled store → provider silent.
- Delivery: feed cached 60 s per (store, lang, display_mode) — second render hits
  cache (assertNumQueries); product URL via permalink resolution, entries without an
  active permalink in the request language skipped; localized product title used.
- Suppression: checkout + preview render nothing (same regression pins as T034);
  wrapper always present; all 3 themes token-render.
