# ADR-025: GDPR/ePrivacy consent management (cookie banner + pixel gating)

**Status: ACCEPTED (human, 2026-07-11; proposed 2026-07-11) — all four product-level
items (§P1–P4) approved as recommended, see decisions inline below. Resolves the
cross-backlog GDPR blocker (10 §cross-backlog "GDPR" pixels row, TH-141, ADR-022 D7,
KNOWN_RISKS item 1 of `docs/releases/pixels-currency-chat/KNOWN_RISKS.md`). Extends
ADR-012 D9 (slot registry — uses the already-declared `slot.consent`, no new slot),
ADR-022 (pixel gating), ADR-004 (first-party beacon exemption assessment). Complies
with design-pattern-ideas §XV-3 (consent state explicit, never inferred from absence),
§XV-4 (single resolution function), §XV-1 (no invisible failures — refusal and
misconfiguration are visible states), §XIII-class atomicity (claims stay untouched
until consent exists).**

**One item remains OPEN, external to this ADR and tracked separately (not a code
decision): legal sign-off on the beacon-exemption reading (D4) — a lawyer must confirm
the CNIL audience-measurement exemption assessment. See
`specs/ecommerce_engine/11_uncertainties_to_validate.md`, item "ADR-025 — legal
sign-off on beacon-exemption reading", owner human/legal. This does not block
implementation of TICKET-048/049 (the engineering fallback,
`beacon_requires_consent=True` platform-default, requires no redesign if legal
disagrees).**

The compliance bar used throughout is CNIL guidance (France is the minimum target
market): prior consent before any non-essential tracker runs, refusing as easy as
accepting, per-purpose granularity, demonstrable proof of consent, consent re-collected
periodically, withdrawal as easy as granting. This ADR designs an engineering artifact,
not legal advice; the PENDING items are the spots where a product/legal owner must sign.

## Decision

A new `consent` Django app provides: a 3-state consent model (undecided / granted /
refused, per category) stored in one first-party cookie plus a server-side
`ConsentRecord` proof row per decision; a server-rendered banner + preferences panel via
the existing `slot.consent`; **server-side render gating** of `slot.pixels` by consent
category, with the pixel **purchase/initiate claims consent-gated at the claim site**
(not only at render — see D0); a CNIL-exemption-configured first-party analytics beacon
that keeps running while undecided but honors an explicit objection toggle; per-store
enable with a non-EU acknowledgment escape hatch wired into the launch checklist.
Platform-provided translated strings via the `.po` catalog; no per-store banner copy in
v1. When this ships, `pixels` flips from "NOT launch-ready for EU storefronts"
(KNOWN_RISKS item 1) to EU-ready.

### D0 — Verification of ADR-022 D7's claim: "gating is a slot-provider-only change" — PARTLY FALSE, corrected here

Verified against `pixels/slot_provider.py`, `pixels/service.py`,
`storefront/views_checkout.py` (2026-07-11):

- **TRUE for stateless events** (`page_view` base snippets, `view_content`, the
  `add_to_cart` JS bridge): all HTML flows through `PixelsSlotProvider.render()`;
  suppressing there gates all five providers with zero provider/template changes. The
  `add_to_cart` bridge degrades safely: the `CustomEvent` fires into no listeners when
  base snippets are suppressed.
- **FALSE for `purchase`**: `order_thank_you` calls `claim_purchase_pixels(store, order)`
  *in the view, before the slot renders* (`storefront/views_checkout.py:1682`). Each
  claim is a one-shot `FiredPixel.get_or_create` — consumed exactly once per
  (order, provider). If the slot merely suppressed the render on an unconsented
  thank-you view, the claims would already be burned (`created=True` returned to a
  render that emits nothing); when the shopper later consents and reloads,
  `created=False` → **the purchase pixel would silently never fire**, the exact §XV-1
  invisible-failure class ADR-022 was built to avoid.
- **FALSE-with-a-twist for `initiate_checkout`**: the one-shot
  `CheckoutState.initiate_event_fired` conditional-UPDATE claim
  (`views_checkout.py:632-642`) is **shared** between the exempt first-party analytics
  event (`record_initiate_checkout`) and the pixel event. It cannot be consent-gated
  wholesale without also suppressing the exempt analytics event.

**Correction adopted (D3):** consent gates the *claims*, not only the render. ADR-022
D7's sentence should be read as "a pixels-app-only change"; the wording correction to
ADR-022 is included in this ADR's rollout notes.

### D1 — Consent model: three categories + one exempt row, per-provider category fixed in code

Categories (frozen keys):

| key | Label (banner) | Contents in v1 | Legal basis |
|---|---|---|---|
| `necessary` | Strictly necessary | `sessionid` (cart, checkout, `sf_lang`, currency), `csrftoken`, the consent cookie itself, `__theme_preview` (admin-only) | Exempt (ePrivacy strictly-necessary) — always on, no toggle |
| `analytics` | Third-party analytics | GA4 (`ga` provider) | Consent |
| `marketing` | Marketing / advertising | `facebook`, `tiktok`, `snapchat`, `pinterest` providers | Consent |
| *(exempt row)* | Audience measurement (first-party) | `analytics/` beacon + `pa_sid` sessionStorage key | CNIL audience-measurement exemption, with objection toggle (D4) |

- **Provider→category mapping is a class attribute on `PixelProvider`**
  (`consent_category: str`, values `"analytics"` or `"marketing"`), set in code per
  provider — GA4 = `analytics`, the four ad pixels = `marketing`. Rejected alternative:
  per-store mapping via `Pixel.config_json` (ADR-022 D2 reserved a "consent category"
  key there). A store owner reclassifying Meta as "analytics" to dodge the marketing
  toggle is a compliance foot-gun the platform would own; `config_json` stays reserved
  and unread. A future per-store *stricter-only* override can be added without schema
  change.
- Both consent categories are prior-consent categories under CNIL — the split exists so
  a shopper can grant analytics without marketing (granularity requirement), and so the
  GA4 Consent Mode signals (D3) can be derived truthfully.
- **Consent state is tri-state per category** (undecided / granted / refused), never
  inferred from cookie absence alone (§XV-3): absence = undecided (show banner, render
  no consent-requiring tracker); explicit refusal = refused (hide banner, render
  nothing, do not re-prompt until expiry).
- **Chat widget — assessed, no consent category needed:** the shopper-side chat stores
  nothing persistent client-side (verified: `chat_widget.html` holds the session token
  in a JS variable only; it reads the exempt `csrftoken` cookie); a `ChatSession` is
  created only on an explicit user act (opening the widget) — a user-requested service,
  strictly-necessary territory. The ADR-024 sub-processor acceptance is the *store
  owner's* GDPR obligation (controller→processor chain), orthogonal to shopper cookie
  consent. Recommended (owned by ADR-024, not scoped here): one disclosure line inside
  the widget ("conversations are processed by an AI provider — see privacy policy").
- **Frequency-cap storage for future T034/T035** (overlay dismissal, social-proof caps):
  classify as strictly-necessary *preference* storage (it records the user's own
  dismissal to honor it — same nature as the consent cookie). Those tickets must
  confirm this classification at implementation time; the `consent` app exposes
  `get_consent(request)` (D3) if they choose the conservative route instead.

### D2 — Storage: one compact first-party cookie + `ConsentRecord` proof rows

**Cookie** `pradize_consent`, set exclusively by the server on `POST /_consent/` (D5):

- Value: `v<policy_version>:<consent_id>:a=<0|1>,m=<0|1>,am=<0|1>` — e.g.
  `v1:6f9c...e2:a=1,m=0,am=1`. Strict parser: any malformed value, unknown version, or
  `policy_version != CONSENT_POLICY_VERSION` (platform constant in `consent/policy.py`)
  ⇒ treated as **undecided** (fail-closed: re-prompt, render nothing consent-gated).
- `consent_id` = UUID4 hex generated at decision time — the correlation key to the
  `ConsentRecord` proof row (demonstrability chain: cookie in the shopper's browser ↔
  server-side record of what was chosen, when, under which policy version).
- Attributes: `Max-Age=CONSENT_COOKIE_MAX_AGE` (default **180 days** — §P2),
  `SameSite=Lax`, `Secure` in production, **`HttpOnly=True`**, `Path=/`. HttpOnly is
  possible because *no JavaScript ever reads the cookie*: the banner/panel is
  server-rendered with current state, and gating is server-side (D3). This removes the
  cookie from the XSS-exfiltration/forgery surface at zero cost. Not signed: a shopper
  forging their own consent cookie only affects their own browser; there is no
  cross-user or server-trust consequence (documented so nobody adds HMAC "for safety"
  later and breaks nothing but complexity budget).
- Expiry ⇒ cookie gone ⇒ undecided ⇒ banner re-appears — this **is** the re-prompt
  mechanism (CNIL: re-collect periodically; refusal is honored for the same duration —
  a refusal cookie also lives 180 days, so a refuser is not nagged).

**Proof model** `consent.ConsentRecord(StoreOwnedModel)` — one INSERT per decision
(grant, refusal, change via panel, withdrawal), main transactional DB:

| field | type | notes |
|---|---|---|
| `consent_id` | UUIDField, db_index | matches the cookie |
| `analytics` / `marketing` | BooleanField | resulting state |
| `am_objected` | BooleanField, default False | audience-measurement objection (D4) |
| `action` | CharField choices: `accept_all` / `refuse_all` / `custom` / `withdraw` | which UI act produced it |
| `policy_version` | PositiveSmallIntegerField | platform constant at decision time |
| `lang` | CharField(8) | banner language shown |
| `source` | CharField choices: `banner` / `manage_panel` | |
| `user_agent` | CharField(256), truncated | proof context; **no IP address stored** (deliberate: IP adds PII retention burden and is not required for demonstrability; timestamp + consent_id + policy_version + choices suffice) |
| `created_at` | auto | |

Retention: rolling purge of records older than **13 months** (§P4) via a management
command on the existing scheduled-purge pattern (same shape as chat's 30-day purge).
Superseded records within the window are kept (they prove the *history* of choices,
including that a refusal was honored until the user changed it). Volume is bounded:
≤ a handful of rows per shopper per 6 months.

Rejected alternatives: (a) consent state in the Django session — sessions expire on
browser close for guests and the consent must outlive the session (180 days); also ties
consent to cart mechanics. (b) localStorage — invisible to the server at render time,
which D3's server-side gating requires. (c) JSON cookie — bigger, and invites schema
creep; the compact form is parsed by one function with one test surface.

**Addendum (2026-07-11, Safety Agent audit `docs/security/CONSENT_AUDIT.md` finding
F2) — binding provisioning constraint for any future shared-subdomain deployment:**
`pradize_consent` is unsigned, not store-bound, and set host-only with no `Domain`
attribute. This is safe **only** because current provisioning gives every store its
own registrable domain (`StoreDomain.host` globally unique, ADR-008). **If a store is
ever provisioned as a sibling subdomain of a shared registrable parent domain**
(e.g. `store1.pradize.com` / `store2.pradize.com`), a script on one subdomain could
plant `Domain=pradize.com; pradize_consent=v1:...:a=1,m=1,am=0`, which the browser
would then send to sibling stores — cookie tossing that fabricates consent the
shopper never gave on that store (compliance impact, no data exposure). **Before
any such deployment ships, the cookie MUST be renamed to `__Host-pradize_consent`**
(its current attributes — `Secure` in production, `Path=/`, no `Domain` — already
satisfy the `__Host-` prefix rules; the browser then refuses to set/accept the
cookie for any request lacking those exact attributes, making parent-domain
injection impossible by browser enforcement, not just by convention).
**Why the prefix is not applied now:** `__Host-` requires `Secure` on every request
that sets or reads the cookie, which breaks the cookie in non-HTTPS local/dev
environments (`webecom/settings/production.py` is the only place
`SESSION_COOKIE_SECURE`/this cookie's `secure=` flag is `True`; development runs over
plain HTTP). Applying the prefix today would make consent un-grantable in dev with no
compliance benefit, since each store already has its own registrable domain. This
constraint is binding, not optional, the moment that provisioning assumption changes.

### D3 — Gating mechanics: server-side render gating + consent-gated claims + reload-on-grant

**Single resolution function** (§XV-4): `consent/state.py::get_consent(request) ->
ConsentState` — frozen dataclass `(decided: bool, analytics: bool, marketing: bool,
am_objected: bool)` with `allows(category: str) -> bool` (`necessary` always True;
undecided ⇒ False for both consent categories). Every consumer calls this; nobody
parses the cookie independently.

**1. Render gating in `PixelsSlotProvider.render()`** (the ADR-022 D7 slot-provider
change, plus consent-app awareness): when `consent` is enabled for the store (D6),
each `Pixel` row renders only if `get_consent(request).allows(provider.consent_category)`.
Undecided and refused render **nothing** for that row — no inert markup, no loader.

**Dated correction (2026-07-11, RM-1 — Release Manager finding, fixed):** the
paragraph above under-specified the disabled+acknowledged case. As originally
implemented, `PixelsSlotProvider.render()` and the `claim_purchase_pixels()` caller
gated exclusively on `get_consent(request)` — the shopper's cookie — with no
awareness of `ConsentSettings` at all. On a store that legitimately disables the
banner via the non-EU acknowledgment (`is_enabled=False, non_eu_acknowledged=True`,
D6), no shopper is ever shown the banner, so no shopper ever sets the
`pradize_consent` cookie, so `get_consent()` resolves UNDECIDED forever and pixels
were **permanently dark** — the opposite of this D3 paragraph's and P3's documented
intent ("when consent is enabled for the store..." implies the converse holds when
it is not). Not caught by `CONSENT_AUDIT.md` (fail-closed direction only) nor any
existing test. Fixed by `consent/state.py::resolve_consent_for_pixels(request) ->
ConsentState` — the single resolution point (§XV-4) both `PixelsSlotProvider.render()`
and the `claim_purchase_pixels()` caller now call instead of `get_consent()` directly:
it returns the cookie-derived state unchanged UNLESS `ConsentSettings.is_enabled=False`
AND `non_eu_acknowledged=True`, in which case it returns an all-categories-allowed
state (pixels fire unconditionally, the pre-consent-feature behaviour P3 intended).
The disabled-WITHOUT-acknowledgment combination (the D6 launch-check-blocked state)
is deliberately NOT special-cased and stays fail-closed/dark, unchanged. See
`docs/releases/consent-management/KNOWN_RISKS.md` item RM-1 (now FIXED) for the full
writeup and the regression tests (`consent/tests/test_state.py`,
`consent/tests/test_single_resolution_point.py`, `pixels/tests/test_slot_provider.py`,
`storefront/tests/test_checkout.py`).

- **Rejected: inert-script activation** (`<script type="text/plain" data-consent=…>`
  swapped live on grant — the pattern ADR-022 D7 name-checked). It requires a second,
  client-side implementation of the category logic (violates single-resolution), ships
  marketing script bodies to shoppers who may be refusing, and still cannot solve the
  purchase-claim problem (D0) — the claim is server-side. Once the claims are gated
  server-side anyway, inert scripts buy only "fire on the very page where consent was
  granted", which reload-on-grant (below) buys more simply.
- **Reload-on-grant:** when the banner POST results in at least one category newly
  granted, the banner JS calls `location.reload()` (query string — and therefore UTM
  params on a landing page — survives reload, so first-touch attribution is preserved
  and the landing `page_view` fires post-reload). On refuse-all or no-new-grant saves,
  no reload — the banner just closes (nothing new would render). Cost: one reload per
  shopper per 180 days, only on grant. Edge accepted: granting mid-checkout via the
  panel reloads the checkout page and loses unsaved form input — rare (the banner is
  shown from the first page), documented, not mitigated in v1.
- **Caching:** verified 2026-07-11 — no full-page caching exists on storefront pages
  (`cache_page` appears only on the sitemap view). Server-side consent-dependent HTML is
  therefore safe today. **Binding constraint recorded:** any future full-page/CDN cache
  of storefront HTML MUST either vary on `pradize_consent` or move pixel injection out
  of the cached body; this is called out in Risks and must be re-checked by any caching
  ADR.

**2. Purchase claims consent-gated at the claim site** (the D0 correction):
`claim_purchase_pixels(store, order, allowed_categories: frozenset[str])` gains a
required parameter; it claims only for `Pixel` rows whose provider
`consent_category ∈ allowed_categories`. `order_thank_you` passes the categories from
`get_consent(request)`. Consequences, all desirable:

- Undecided/refused at first PAID render ⇒ that provider's claim is **not consumed**;
  if the shopper grants consent and revisits/reloads the (bookmarkable) thank-you page,
  the claim is taken then and the purchase fires — late but real, platform-side
  `event_id` still dedupes.
- Granular consent works per provider: analytics-only grant ⇒ GA4's purchase claim is
  consumed and fires; the four marketing claims stay available.
- A FiredPixel `purchase` row now *implies* consent existed at claim time — this is the
  forwardable consent signal the future CAPI follow-up needs (see Scope boundaries).

**3. `initiate_checkout` — accepted loss, documented:** the shared
`initiate_event_fired` claim keeps firing the exempt server-side analytics event
unconditionally (correct — it is beacon-class, exempt). The pixel `initiate_checkout`
context entry is added as today, but the slot's per-row category gate means: if the
matching category is not granted at the *first* checkout GET, that provider's
InitiateCheckout never fires for this checkout (claim consumed by analytics).
Rejected: splitting the claim into per-consumer flags (migration + more state for an
optimization-signal event with no money attached). Recorded honestly as a known,
bounded under-fire.

**4. Google Consent Mode v2 — honest assessment:** under server-side gating the GA4
snippet only exists post-consent, so Consent Mode's "advanced" cookieless-ping mode is
structurally out (and deliberately so — pinging Google pre-consent is itself contested
under CNIL). What v1 *does* need: when GA4 renders with analytics granted but marketing
refused, `ga_base.html` emits `gtag('consent','default', {analytics_storage:'granted',
ad_storage:'denied', ad_user_data:'denied', ad_personalization:'denied'})` before
`gtag('config')`; all-granted emits all-granted. This is cheap, truthful, and is what
Google's EEA enforcement checks for when merchants link GA4 to Google Ads. The four
other providers get no consent-signal parameters in v1 (their snippets only exist when
their category is granted — the signal is implicit).

**5. First-party beacon:** see D4. **6. Theme preview:** `consent` is already in
`PREVIEW_SUPPRESSED_SLOTS` (verified, `storefront/templatetags/storefront_tags.py:59`)
— no banner in preview, zero new code, regression-pinned in tests.

### D4 — First-party analytics beacon: exempt-by-configuration, with an objection toggle

Precise assessment (verified against `analytics/static/analytics/beacon.js`, the inline
tag in `storefront_tags.py:230-245`, `analytics/ingest.py`, `analytics/models.py`):

- What it stores/reads on the terminal: **one sessionStorage key `pa_sid`** — a random,
  per-tab, per-browsing-session identifier. No cookies, no localStorage, no persistent
  identifier: cross-visit tracking of a person is impossible by construction.
  sessionStorage **is** within ePrivacy art. 5(3) scope ("storing of information … in
  the terminal equipment"), so an exemption is required, not assumed.
- Server side: `Event` rows carry `session_id` (the ephemeral pa_sid), soft
  product/order ids, UTM fields, referrer, truncated context — **no IP address field
  exists on the model and none is captured at ingest**. First-party endpoint
  (`/_analytics/beacon/`), data used exclusively for the store's own dashboards, no
  third-party transfer, no cross-site joins.
- Conclusion: this configuration fits the **CNIL audience-measurement exemption**
  (trackers strictly scoped to first-party audience/performance measurement for the
  publisher's exclusive use, no cross-site tracking, no third-party disclosure, no
  persistent identifier — our per-tab ID is stricter than CNIL's 13-month ceiling).
  Named conditions that MUST keep holding, recorded as invariants: (1) no IP or other
  durable identifier added to `Event`; (2) no third-party export of raw events; (3) no
  cross-store/cross-site session joining; (4) the cookie-policy page and the consent
  panel disclose it; (5) **an objection mechanism exists**. Any future change violating
  one of these re-opens the consent question for the beacon (flagged for the Safety and
  SEO/analytics reviewers of such a change).
- **Objection toggle (`am`):** the preferences panel lists "Audience measurement
  (first-party, exempt)" defaulted ON with an explanation, toggleable OFF. The inline
  beacon tag and `beacon.js` include become no-ops when `get_consent(request).am_objected`
  (server-side: the tag renders nothing — consistent with all other gating). "Refuse
  all" does **not** flip `am` (it refuses the consent-requiring categories; the exempt
  row is governed by its own toggle, with wording in the panel making that explicit) —
  recommended default, rationale: matches the legal semantics; conflating them would
  destroy the store's exempt analytics on every refuse-all click for no legal gain.
- Per-store conservative override `ConsentSettings.beacon_requires_consent`
  (default False): a store's DPO can demote the beacon to the `analytics` consent
  category if they disagree with the exemption reading. When True, the beacon tag
  requires `allows("analytics")`.

### D5 — Banner UX: server-rendered in the existing `slot.consent`, bottom bar + panel, reload-on-grant

- **Slot:** the already-declared `slot.consent` (ADR-012 D9; present in `base.html:71`
  and inherited by `base_checkout.html`, whose header comment already promises "Keeps:
  … pixels, consent"). **No new slot.** One `ConsentSlotProvider(SlotProvider)` in the
  `consent` app registers at `ready()`, exactly like `PixelsSlotProvider`.
  `is_enabled(store)` = consent feature enabled for the store (D6). Renders: (a) the
  banner (only when state is undecided), (b) the hidden preferences panel markup
  (always, so the footer "manage" control works after decision), (c) one inline
  `<style>` + one inline vanilla-JS block (T029-B: no build step, no dependency).
- **Banner form:** fixed bottom bar (`position:fixed; bottom:0`), never a full-screen
  modal and never a scroll/cookie wall — content stays reachable and Google's intrusive-
  interstitial guidance is respected. Three controls of **equal visual weight**:
  `Accept all` · `Refuse all` · `Customize` (CNIL: refusing as easy as accepting — same
  tier, same size, no dark-pattern color demotion; the Designer pass may restyle within
  that constraint). `Customize` expands the panel in place: per-category rows
  (necessary: locked ON; analytics, marketing: toggles default **OFF** — pre-ticked
  boxes are invalid consent; audience-measurement exempt row per D4) + `Save choices`.
  Body text links to the store's privacy/cookie `StaticPage` when configured (D6).
- **Actions → `POST /_consent/`** (underscore-prefixed path, same convention as
  `/_analytics/beacon/` — no permalink/slug namespace collision by construction).
  CSRF-protected (banner JS reads `csrftoken` exactly like the chat widget), rate-limited
  via the existing antispam helpers (public unauthenticated POST). Handler: validate →
  INSERT `ConsentRecord` → `Set-Cookie` on the 204 response (insert-before-cookie:
  if the proof write fails, no cookie is set and the banner persists — fail-closed,
  §XIII spirit). JS: on grant-anything → `location.reload()`; otherwise close banner.
- **Re-open ("manage cookies"):** `footer.html` gains, when the feature is enabled, a
  `<button type="button" data-consent-manage>` ("Cookie preferences") that unhides the
  panel. Withdrawal is therefore as easy as granting, from every page with a footer.
  Known gap, accepted: `base_checkout.html` suppresses the footer (CK-006
  distraction-free checkout), so mid-checkout withdrawal requires leaving checkout —
  acceptable (the obligation is "easily accessible", not "omnipresent"), documented.
  No StaticPage nav change needed: the control is a live widget, not a page; stores
  SHOULD additionally have a cookie-policy `StaticPage` (kind=POLICY, hreflang-exempt
  per ADR-018) — recommended content, not enforced.
- **CLS/LCP — the layout-shift question, concretely:** the banner is (1) rendered
  server-side in the initial HTML (no late JS injection — the entire CLS risk class of
  third-party CMPs comes from post-load DOM insertion, which we structurally avoid);
  (2) `position:fixed`, so it overlays rather than displaces content — fixed overlays
  do not move layout and score zero CLS; (3) compact (max-height capped ~40vh mobile,
  panel scrolls internally) so it is never the largest contentful paint candidate and
  never reads as an interstitial; (4) its `<style>` is inline in the same HTML response
  (no extra render-blocking request). Banner strings are visible to crawlers on every
  page — harmless boilerplate; no cloaking (bots get the same HTML, they simply never
  consent, so pixels never render for them — also correct behavior). SEO Agent gate
  required (D8).
- **Accessibility:** `role="dialog"`, `aria-modal="false"` (non-blocking),
  `aria-live="polite"`, focus-visible toggles, Escape closes the panel. Theme
  conformance: markup is skinned exclusively by the ADR-012 theme tokens
  (`--color-surface`, `--color-text`, …), so all three themes conform without
  per-theme markup; TH-045's conformance suite already asserts `slot.consent` renders.

### D6 — Multi-tenant + i18n: `ConsentSettings(StoreOwnedModel)`, platform strings via `.po`

`consent.ConsentSettings(StoreOwnedModel)` — one row per store, created lazily with
defaults:

- `is_enabled` (default **True** — safe-by-default; §P3 for existing-store rollout),
- `non_eu_acknowledged` (default False) + `non_eu_acknowledged_at/by` — the only path
  to disabling: the store owner explicitly acknowledges "this store does not target
  EU/EEA visitors; I am responsible for tracker compliance". Rejected: geo-IP-based
  banner targeting (unreliable, adds a geo dependency, and CNIL applies to targeting,
  not visitor IP);
- `beacon_requires_consent` (default False — D4),
- `policy_page` (nullable FK → `pages.StaticPage`, the store's cookie/privacy policy
  link target).

**Admin (settings spec 06 pattern):** a "Cookie consent" settings card — enable toggle
(with acknowledgment flow when disabling), policy-page picker, read-only view of the
category→provider mapping, and a link to a filtered `ConsentRecord` changelist
(read-only, superuser + store owner; it is an audit table — no edit/delete in admin
beyond the retention purge).

**Launch checklist wiring** (`ConsentSlotProvider.validate_settings(store)`):

- consent disabled AND no `non_eu_acknowledged` AND ≥1 active `Pixel` row → **blocking
  error**: "Active pixels require the consent banner (or an explicit non-EU
  acknowledgment)." This is the single check that flips the KNOWN_RISKS posture: the
  platform can no longer launch a store that fires pixels unconditionally by omission.
- consent enabled AND no `policy_page` set → non-blocking warning (banner still legal,
  link just absent).
- The existing `PixelsSlotProvider.validate_settings` is unchanged (§IX: no
  call-site special-casing — the consent provider owns consent errors).

**i18n:** all banner/panel strings are platform-owned, wrapped in `{% trans %}` /
`gettext` and shipped in the `.po` catalog (ADR-021 wiring; `locale/fr/` exists today —
French day-1, which is exactly the CNIL-driven minimum). Other store languages fall
back to English until translated — added to the multilingual debt list. **No per-store
banner copy in v1** (rejected for v1: it drags in the multilingual content pipeline and
lets a store owner write non-compliant copy the platform then serves; revisit only with
a compliance-reviewed template mechanism). The only store-specific interpolations:
store name and the policy-page URL.

### D7 — App boundary and pieces (implementation shape, two tickets)

New app `consent/`: `models.py` (`ConsentSettings`, `ConsentRecord`), `policy.py`
(`CONSENT_POLICY_VERSION`, category constants), `state.py` (`get_consent`, cookie
parser/serializer — the single resolution point), `views.py` (`POST /_consent/`),
`slot_provider.py` (`ConsentSlotProvider`), `management/commands/purge_consent_records.py`,
templates + inline CSS/JS. Changes outside the app, exhaustively:

1. `pixels/registry.py` — `PixelProvider.consent_category` attribute (+ per-provider
   values); `pixels/slot_provider.py` — category filter via `get_consent`;
   `pixels/service.py` — `allowed_categories` parameter; `pixels/templates/pixels/ga_base.html`
   — Consent Mode v2 default line.
2. `storefront/views_checkout.py::order_thank_you` — pass allowed categories into
   `claim_purchase_pixels`.
3. `storefront/templatetags/storefront_tags.py` (analytics beacon tag) — no-op on
   `am_objected` / `beacon_requires_consent` without `analytics` grant.
4. `storefront/templates/storefront/partials/footer.html` — the manage button.
5. ADR-022 D7 wording correction (dated addendum): "slot-provider-only" → "pixels-app +
   claim-site change; claims must be consent-gated (ADR-025 D0)".

**Ticket split (per the one-or-two constraint): TICKET-048 — consent core** (app,
models, migration, cookie/state, endpoint, all gating in §1–3 above, checklist
validation, purge command, tests) and **TICKET-049 — banner UI** (slot provider
markup/CSS/JS, panel, footer button, fr translations, a11y, theme-token skinning,
Designer + SEO passes). 049 depends on 048; both must ship in the same release — 048
without 049 would gate pixels with no way to ever grant (pixels dark everywhere),
which is safe but pointless; the release gate is the pair.

### P — DECIDED (human, 2026-07-11) — all four approved as recommended

- **P1 — Banner copy & tone — DECIDED (human, 2026-07-11): approved as recommended.**
  Default (EN): title "We value your privacy"; body "We use cookies and similar
  technologies to measure our audience and — with your consent — to measure
  advertising performance. You can accept, refuse, or customize. See our
  {policy page}."; buttons "Accept all / Refuse all / Customize". Neutral/factual tone
  ratified as the platform copy for v1 (FR+EN, `.po` catalog, D6). No further
  product/legal copy sign-off round required to ship TICKET-049; a future copy
  refinement is a `.po` edit, not a re-decision.
- **P2 — Re-prompt interval — DECIDED (human, 2026-07-11): 180 days.** Recommended
  default ratified: `CONSENT_COOKIE_MAX_AGE = 180 days` (CNIL's 6-month
  recommendation), platform-level constant (not per-store — per-store intervals invite
  a race to 13 months).
- **P3 — Rollout default for existing stores with active pixels — DECIDED (human,
  2026-07-11): `is_enabled=True` for ALL stores at migration** (privacy-safe default),
  with the `non_eu_acknowledged` escape hatch (D6) as the only path to disabling.
  Accepted consequence: existing non-EU stores with active pixels will see pixel volume
  drop until shoppers consent or the owner files the non-EU acknowledgment. (Per
  KNOWN_RISKS item 1, EU stores should have no active pixels today, so no EU store loses
  anything.)
- **P4 — Proof retention — DECIDED (human, 2026-07-11): 13 months rolling purge**
  of `ConsentRecord`, ratified as recommended.

## Context

- `docs/releases/pixels-currency-chat/KNOWN_RISKS.md` item 1: pixels are "explicitly
  NOT launch-ready for any EU storefront" until this feature ships — the blocker this
  ADR removes. ADR-022 D7 + Risks flagged the same, honestly.
- Specs: 06 §9 descoped consent from pixels v1; TH-141 requires only that `slot.consent`
  exists and gates `slot.pixels` (satisfied: slot declared in ADR-012 D9, present in
  `base.html`, in `PREVIEW_SUPPRESSED_SLOTS`, in the TH-045 conformance list; banner
  policy was left PENDING — resolved here). 16 §505-510 lists `consent` + `pixels` on
  checkout. No spec text prescribes banner mechanics — this ADR is the design of record.
- Existing infra reused: slot registry (rendering, error isolation, preview
  suppression, theme conformance), `StoreOwnedModel` + `for_store` scoping, antispam
  rate limiting, scheduled-purge pattern, `.po` i18n wiring (ADR-021), StaticPage POLICY
  kind (ADR-018), the `/_prefix/` endpoint naming convention.
- Adjacent GDPR items explicitly NOT solved here (still open in 11 Part B/C): customer
  PII retention/erasure, abandoned-checkout email legal basis, social-proof buyer-data
  display. This ADR is the tracker-consent slice only.

## Options considered

1. **Gating point:** server-side render gating + consent-gated claims + reload-on-grant
   *(chosen)* vs client-side inert-script activation (double logic, ships refused code,
   cannot fix the claim problem) vs middleware that strips `<script>` from responses
   (response rewriting is fragile and violates the slot architecture).
2. **Consent storage:** HttpOnly first-party cookie + server proof rows *(chosen)* vs
   session (lifetime wrong) vs localStorage (invisible server-side) vs third-party CMP
   (new dependency, TCF weight, no-build rule violation).
3. **Category mapping:** fixed per-provider in code *(chosen)* vs store-configurable via
   `config_json` (compliance foot-gun) vs single "tracking" category (fails CNIL
   granularity).
4. **Beacon:** CNIL-exemption configuration + objection toggle + per-store conservative
   override *(chosen)* vs consent-gating the beacon (destroys the store analytics the
   whole ADR-004 pipeline exists for, without legal necessity) vs ignoring ePrivacy for
   sessionStorage (wrong reading of art. 5(3)).
5. **Banner delivery:** server-rendered in existing `slot.consent` *(chosen)* vs new
   slot (TH-141 already declared this one) vs JS-injected overlay (CLS, flicker, the
   exact CMP failure mode).
6. **Proof:** ConsentRecord rows without IP *(chosen)* vs cookie-only (not demonstrable
   server-side) vs full request fingerprint (PII burden exceeds proof value).

## Chosen option

Decisions D0–D7 above: `consent` app; tri-state per-category consent in one HttpOnly
cookie + `ConsentRecord` proof rows; server-side render gating with consent-gated
purchase claims (D0 correction to ADR-022 D7); reload-on-grant; CNIL-exempt beacon with
objection toggle; banner + panel server-rendered into the existing `slot.consent`;
per-store enable with non-EU acknowledgment wired as a blocking launch-checklist error;
platform `.po` strings; tickets 048/049.

## Why

- It is the smallest design that is actually CNIL-conformant: prior consent (nothing
  consent-gated renders pre-decision), refuse-as-easy (equal-weight buttons, refusal
  remembered 180 days), granularity (two toggles + exempt row), withdrawal (footer
  button), demonstrability (ConsentRecord ↔ consent_id), no cookie wall.
- The D0 verification prevented the one genuinely silent failure mode: burning
  FiredPixel claims on unconsented renders would have permanently under-reported
  purchases — invisible, unrecoverable, and exactly the class §XV-1 exists for.
- Every moving part reuses a proven mechanism (slots, StoreOwnedModel, checklist
  validation, purge pattern, antispam, .po) — the Developer tickets are mostly one new
  small app plus five surgical touch points, enumerated in D7.
- The beacon assessment keeps the store-facing analytics dashboards alive for
  undecided/refusing shoppers *legitimately*, instead of either over-blocking (empty
  dashboards) or hand-waving.

## Risks

- **Legal-reading risk (highest):** the beacon-exemption assessment (D4) and the
  "refuse-all does not flip the exempt toggle" default are engineering readings of CNIL
  guidance, not counsel. Flagged for the human/legal owner alongside P1–P4. Fallback is
  one boolean (`beacon_requires_consent=True` platform-default) — no redesign.
- **Pixel volume drop on rollout** for existing non-EU stores with active pixels (P3) —
  a product communication issue, not a defect; the acknowledgment path is the remedy.
- **Consent-rate UX:** a bottom bar with equal refuse prominence yields lower grant
  rates than dark-pattern CMPs — by design; stated so nobody "optimizes" it into
  non-compliance later without a decision.
- **Future caching:** any full-page cache that ignores `pradize_consent` serves
  granted-state HTML (with live pixels) to undecided shoppers — a compliance breach.
  Recorded as a binding constraint in D3; any caching ADR must cite it.
- **Claim-timing edge:** consent granted *while sitting on* the thank-you page fires the
  purchase only after a revisit/reload of that page (claim was correctly preserved).
  Bounded, honest under-fire — same failure direction ADR-022 D6 chose.
- **`initiate_checkout` under-fire** when consent arrives after the first checkout GET
  (D3 §3) — accepted, bounded, analytics-integrity only.
- **Banner blindness / SEO:** fixed bottom bars can overlap theme footers on small
  viewports; Designer pass must verify per theme; SEO gate must confirm zero CLS and no
  interstitial flagging (D5 gives the concrete mechanism: server-rendered, fixed,
  compact).
- **policy_version bumps** re-prompt every shopper platform-wide — correct but noisy;
  bump only for semantic changes (new category, new exempt reading), never for copy.

## Rollback strategy

- Feature-level: `ConsentSettings.is_enabled=False` (with acknowledgment) restores
  pre-025 behavior per store; removing the `consent` app from `INSTALLED_APPS` makes
  `slot.consent` render its empty wrapper again (slot architecture guarantee), and the
  pixels/beacon call sites fall back via a default `ConsentState(decided=False, …)` →
  **fail-closed**: with the app absent, `get_consent`'s import shim must return
  all-refused-equivalent, so a botched deploy dark-launches pixels rather than firing
  them unconsented (the safe direction).
- Data: `ConsentRecord`/`ConsentSettings` tables are additive; reverse migration drops
  them. The cookie dies by expiry; no cleanup needed. `claim_purchase_pixels`'s new
  parameter reverts by passing the full category set (one-line call-site revert).
- No destructive migration exists in this ADR (contrast ADR-022's sentinel migration) —
  rollback is configuration + code revert only.

## Tests required

Django test client + service/unit tests (no JS harness exists — KNOWN_RISKS item 13;
JS behavior asserted structurally on rendered HTML):

1. **Prior consent:** undecided (no cookie / malformed cookie / stale policy_version) ⇒
   zero pixel snippets in the response body (all five providers configured+active),
   banner markup present; granted-analytics ⇒ GA4 renders, four ad pixels absent;
   granted-marketing-only ⇒ inverse; refused-all ⇒ nothing renders and banner absent
   (panel markup still present for the footer button).
2. **Cookie mechanics:** POST `/_consent/` sets `pradize_consent` with HttpOnly,
   SameSite=Lax, Max-Age=180d (Secure asserted under production settings); response 204;
   CSRF required (403 without token); rate-limit path returns the antispam refusal;
   ConsentRecord row written before cookie (simulate insert failure ⇒ 500 and **no**
   Set-Cookie header).
3. **Proof:** each action (accept_all / refuse_all / custom / withdraw via panel)
   writes one ConsentRecord with matching consent_id, categories, policy_version, lang,
   source; records are store-scoped (tenant isolation: store A's records invisible to
   store B); purge command deletes only rows older than the retention window.
4. **Claim preservation (D0 regression — the critical one):** first PAID thank-you GET
   with no consent ⇒ `claim_purchase_pixels` creates **zero** FiredPixel rows and
   renders nothing; subsequent GET with marketing granted ⇒ rows created once, snippets
   render once; a further reload ⇒ nothing (AC-110 intact). Analytics-only consent ⇒
   only the GA4 row/snippet; marketing claims still claimable later.
5. **claim_purchase_pixels unit:** `allowed_categories=frozenset()` ⇒ empty set, zero
   rows, regardless of PAID; PAID gate from ADR-022 still enforced first.
6. **initiate_checkout:** first checkout GET without consent ⇒ analytics
   `record_initiate_checkout` fired, `initiate_event_fired` consumed, no pixel snippet;
   later consented checkout reload ⇒ still no InitiateCheckout snippet (documented
   under-fire pinned as intended behavior).
7. **Beacon:** undecided ⇒ inline beacon script present (exemption default); cookie
   with `am=0` ⇒ beacon tag renders nothing; `beacon_requires_consent=True` store ⇒
   beacon absent until analytics granted; `Event` model has no IP field
   (schema-pinned invariant test for the D4 exemption conditions).
8. **Consent Mode v2:** GA4 snippet under analytics-only grant contains
   `ad_storage.*denied` and `analytics_storage.*granted`; all-granted contains
   all-granted; no consent-mode line on the four other providers.
9. **Launch checklist:** active Pixel + consent disabled + no acknowledgment ⇒ blocking
   error from `ConsentSlotProvider.validate_settings`; acknowledgment set ⇒ no error;
   enabled without policy_page ⇒ warning only.
10. **Slot/theme conformance:** `slot.consent` renders banner for undecided on every
    page type including checkout (`base_checkout` inherits it) and thank-you; theme
    preview renders no banner (`PREVIEW_SUPPRESSED_SLOTS` regression); footer manage
    button present when enabled, absent when disabled; TH-045 suite passes with the
    provider registered.
11. **Fail-closed shim:** with the consent app not installed, `get_consent` fallback
    yields undecided ⇒ pixels dark (never unconsented firing).
12. **i18n:** banner renders French strings on a `fr` storefront request (locale/fr
    catalog), English fallback otherwise.
13. **Tenant isolation:** consent cookie granted on store A's domain does not affect
    gating on store B (cookie is domain-scoped by the browser; server-side test pins
    that `get_consent` never mixes stores via shared test client cookies).
14. **Security (Safety gate support):** malformed/oversized cookie values are rejected
    to undecided without exception; `/_consent/` rejects non-boolean payloads; no
    user-supplied string from the endpoint is ever rendered into HTML.
