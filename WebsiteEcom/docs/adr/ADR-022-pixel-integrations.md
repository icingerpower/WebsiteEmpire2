# ADR-022: Third-party pixel integrations (TICKET-030)

**Status: ACCEPTED (human, 2026-07-11; proposed 2026-07-10) — recommended defaults throughout; one product-level
item PENDING APPROVAL (per-provider event mapping table + GA4-only, tracked in
`specs/ecommerce_engine/11_uncertainties_to_validate.md` Part C). Extends ADR-004
(analytics pipeline, FiredPixel placement), ADR-012 D9 (SlotProvider registry),
ADR-015 §8 Phase 4 (thank-you purchase guard). Complies with design-pattern-ideas §X
(insert-before-fire), §IX (no call-site special-casing), §XV-1 (no invisible failures).
Resolves security audit MEDIUM-1 wiring (`docs/security/CHECKOUT_BATCH_2_AUDIT.md`).**

## Decision

A new `pixels` Django app provides five client-side pixel providers (Facebook/Meta,
Google GA4, TikTok, Snapchat, Pinterest) behind a `PixelProvider` registry, configured
per store by one `Pixel` row per provider, rendered exclusively through the existing
`slot.pixels` storefront slot via a single bridging `SlotProvider`. Five canonical
events (`page_view`, `view_content`, `add_to_cart`, `initiate_checkout`, `purchase`)
map to native events per the mapping table below. The Purchase event fires exactly once
per (order, provider) via per-provider `FiredPixel` insert-before-fire claims, gated on
`payment_status == PAID`; the `__purchase_guard__` sentinel is retired by data
migration. Server-side conversion APIs (Meta CAPI, TikTok Events API) and GDPR consent
gating are named follow-ups, deliberately out of scope for v1.

### D1 — Scope: five providers, client-side snippets only

Day-1 providers and their frozen registry keys (must match `FiredPixel.pixel_type`
values and the `Pixel.provider` choices — schema spec 05 §7):

| key | Provider | Snippet family | ID format (validated) |
|---|---|---|---|
| `facebook` | Meta / Facebook Standard Pixel | `fbq` | `^\d{5,20}$` |
| `ga` | Google Analytics 4 | `gtag.js` | `^G-[A-Z0-9]{4,16}$` |
| `tiktok` | TikTok Pixel | `ttq` | `^[A-Z0-9]{10,30}$` |
| `snapchat` | Snapchat Pixel | `snaptr` | UUID |
| `pinterest` | Pinterest Tag | `pintrk` | `^\d{5,20}$` |

- This is exactly the admin-018 screen list (spec 06 §9) and the TICKET-030 list.
- **GA4 only — no Universal Analytics** (RECOMMENDED, part of the PENDING sign-off):
  UA stopped processing hits in 2023/2024; accepting `UA-*` IDs would be an invisible
  failure (§XV-1 — snippet renders, nothing is recorded). The admin field label says
  "GA4 Measurement ID (G-XXXXXXXX)".
- **Client-side snippets only in v1.** Meta CAPI / TikTok Events API are additive
  later because the purchase `event_id` convention (D5) is chosen to be the browser/server
  dedup key those APIs expect. See Risks for why CAPI is the named follow-up, not scope creep.

### D2 — Configuration: `Pixel` model, one row per (store, provider)

New model `pixels.Pixel(StoreOwnedModel)` — ratifies schema spec 05 §7:

- `provider` — CharField with choices = the five keys above.
- `pixel_id` — CharField(64), validated against the provider's regex in `clean()`
  **and** escaped again at render time (see D3). Reject anything outside the strict
  charset — the ID is injected into an inline `<script>`, so validation is a security
  boundary, not cosmetics.
- `is_active` — install/uninstall toggle (admin-018 "Installed / Not Installed" badges).
- `config_json` — JSONField, default `{}`, **reserved** (future: CAPI token, consent
  category). No v1 code reads it.
- `unique_together (store, provider)` — no multi-pixel-per-provider (spec 06 §9:
  "one ID per platform").

Rejected alternative: JSON blob on `Store`. A dedicated table gives DB-level
uniqueness, per-field validation, a natural admin changelist, and the launch-checklist
query ("which providers are installed") without JSON introspection. It also mirrors how
every other connector family is modeled (ProcessorAccount, CatalogFeed).

### D3 — Registry + slot bridge (§IX: no special-casing)

`pixels/registry.py`:

```
class PixelProvider:
    key: str                  # frozen registry key (= Pixel.provider = FiredPixel.pixel_type)
    name: str                 # human-readable (admin card title)
    id_label: str             # "Pixel ID" / "GA4 Measurement ID"
    id_pattern: re.Pattern    # strict pixel_id validation
    def render_base(self, pixel_id) -> str            # loader + init + page_view
    def render_event(self, pixel_id, event, payload) -> str  # '' when event unmapped
    def validate_settings(self, pixel_row) -> list[str]
```

Providers self-register in `PixelsConfig.ready()` into a module-level dict, exactly
like the slot registry. **One** bridging `PixelsSlotProvider(SlotProvider)` with
`slot = "pixels"` registers into `storefront.slots`:

- Its `render(context)` does **one** query
  (`Pixel.objects.for_store(store).filter(is_active=True)`), then for each row looks up
  the `PixelProvider` by key and concatenates `render_base` + the page's event snippets
  (D4/D5).
- Rejected alternative: five separate `SlotProvider`s (one per provider). That costs
  five `is_enabled` store-settings queries per request and duplicates the row-fetch
  logic; the bridge keeps per-request cost at one query.
- Unknown provider key in a `Pixel` row (row exists, code plugin removed): render
  nothing for that row, `logger.error` (not debug — §XV-1), and report it via
  `validate_settings` so it surfaces in the launch checklist. Never emit a broken tag.
- Snippets are rendered from per-provider Django templates
  (`pixels/templates/pixels/<key>_base.html`, `<key>_event.html`) with `|escapejs` on
  every interpolated value — second escaping layer behind `clean()` validation.
- Rendering rides the existing slot guarantees: `{% render_slot "pixels" %}` already
  exists in `base.html` and `base_checkout.html`; providers that raise are caught and
  skipped by `render_slot`; **theme preview never renders pixels**
  (`PREVIEW_SUPPRESSED_SLOTS` already contains `"pixels"` — TH-007 satisfied with zero
  new code); every theme must keep the slot (TH-045 conformance suite).

### D4 — Canonical event taxonomy and per-provider mapping (sign-off artifact)

Canonical events and their trigger points:

| Canonical | Trigger point | Dedup mechanism |
|---|---|---|
| `page_view` | every storefront page (part of `render_base`) | none needed |
| `view_content` | product page render | none needed |
| `add_to_cart` | JS bridge event from cart AJAX (D5) | none needed (each add is real) |
| `initiate_checkout` | first GET of `/checkout/` per checkout session | existing `CheckoutState.initiate_event_fired` conditional-UPDATE claim (ADR-015 §2) |
| `purchase` | **first PAID thank-you render** | per-provider `FiredPixel` claim (D6) + platform-side `event_id` |

Native event mapping (**PENDING APPROVAL** — 11 Part C):

| Canonical | facebook | ga (GA4) | tiktok | snapchat | pinterest |
|---|---|---|---|---|---|
| `page_view` | `PageView` | `page_view` (auto via `gtag('config')`) | `ttq.page()` | `PAGE_VIEW` | `pagevisit` |
| `view_content` | `ViewContent` | `view_item` | `ViewContent` | `VIEW_CONTENT` | `pagevisit` + `line_items` |
| `add_to_cart` | `AddToCart` | `add_to_cart` | `AddToCart` | `ADD_CART` | `addtocart` |
| `initiate_checkout` | `InitiateCheckout` | `begin_checkout` | `InitiateCheckout` | `START_CHECKOUT` | — (no standard event; renders `''`) |
| `purchase` | `Purchase` + `eventID` | `purchase` + `transaction_id` | `CompletePayment` + `event_id` | `PURCHASE` + `client_dedup_id` | `checkout` + `event_id` |

Payload rules (single builder in `pixels/events.py`, provider templates only format):

- `value` — string, 2 decimal places, from `Decimal` (never float — money rule).
  Purchase: `order.total`. ViewContent: displayed product price.
- `currency` — ISO 4217; purchase uses `order.currency`, others `store.default_currency`
  (transactions always charge store currency — AC-161; display-only conversion never
  changes pixel currency).
- `content_ids` — **must equal the catalog-feed item IDs** or Meta/TikTok dynamic
  retargeting silently breaks. Convention frozen here for T030 **and** T033: a shared
  helper `catalog_item_id(product, variant=None)` returns `str(product.pk)` for
  product-level and `f"{product.pk}_{variant.pk}"` when a variant is known. TICKET-033
  feeds MUST reuse this helper (cross-ticket constraint — noted in the ticket).
- `event_id` (purchase only) — `f"order-{order.pk}-purchase"`, stable and derivable, so
  even a double-fire that slips past FiredPixel is deduped platform-side (AC-110
  explicitly requires an order-derived event ID), and a future CAPI sends the same ID.
  GA4 uses `transaction_id = str(order.pk)` (native GA4 transaction dedup).
- `num_items`, `content_name` — best-effort optional fields; absence must not break
  rendering.

### D5 — Rendering contract: explicit `pixel_events` context, JS bridge for AJAX

- **Explicit over sniffing:** views place a `pixel_events` context key — a list of
  `(canonical_event, payload_dict)` tuples. `PixelsSlotProvider.render` consumes only
  that key plus `purchase_pixel_providers` (D6). The slot provider never guesses from
  `'product' in context` — implicit sniffing is how events silently stop firing after a
  template refactor (§XV-1).
  - Product view adds `('view_content', payload)`.
  - `checkout_view` adds `('initiate_checkout', payload)` **only when its existing
    conditional UPDATE claim returned 1** — pixel InitiateCheckout thereby inherits the
    exact server-side dedup already shipped; a checkout reload renders nothing. No
    `FiredPixel` row is possible here anyway: `FiredPixel.order` is a non-null FK and no
    order exists yet.
- **`add_to_cart` (AJAX, no page load):** the product page embeds the prebuilt payload
  in a `<script type="application/json" id="pixel-product-payload">` block; on
  successful add, cart JS dispatches
  `document.dispatchEvent(new CustomEvent('pradize:pixels', {detail: {event: 'add_to_cart', payload}}))`.
  Each provider's base snippet attaches one listener translating the bridge event to its
  native call. The bridge event name and payload schema are frozen by this ADR; future
  slots (wishlist, search) reuse the same bridge without touching providers.

### D6 — Purchase idempotency: per-provider FiredPixel claims, PAID-gated

`pixels/service.py::claim_purchase_pixels(store, order) -> set[str]`:

1. **PAID gate (defense in depth):** if `order.payment_status != PaymentStatus.PAID`,
   return `set()` — even though `order_thank_you` also gates (MEDIUM-1: the
   `processing` redirect path renders thank-you on PENDING orders; a PENDING render
   must never consume the claim).
2. For each installed+active `Pixel` row:
   `FiredPixel.objects.for_store(store).get_or_create(store=store, order=order,
   pixel_type=provider_key, event='purchase')`. Include the key in the returned set
   **only when `created=True`**. `get_or_create` + the `unique_together
   (order, pixel_type, event)` constraint make concurrent thank-you reloads safe.
3. `order_thank_you` replaces the `__purchase_guard__` block with this call and puts
   the result in context as `purchase_pixel_providers`. The slot provider renders each
   provider's purchase snippet **only** if its key is in that set; `created=False`
   renders nothing (reload, back button — AC-110).

Properties preserved from §X and the audit:

- **Insert-before-fire:** the row is committed before the response body exists. A crash
  between insert and response loses that provider's event — under-fire is the accepted
  failure mode; double-fire never happens. Platform-side `event_id` (D4) is the second
  net.
- **Never at intent creation** (AC-112): purchase snippets exist only on the thank-you
  render path, and only behind the PAID gate — webhook ordering (`payment_intent.created`
  before `succeeded`) cannot fire anything because no page renders there.
- **Late provider install:** a provider installed after an order's first PAID render
  never retro-fires for old orders (claims are per render of *currently installed*
  providers) — correct: purchase events are only useful near-real-time.

**Sentinel retirement (data migration):** each existing `__purchase_guard__` row is
converted into per-provider `FiredPixel(purchase)` rows for the providers
installed+active on that store at migration time, then deleted. Without this, every
pre-T030 order would late-fire Purchase on its next thank-you revisit (wrong-day
attribution). Reverse migration: recreate one sentinel per order that has any
per-provider purchase row.

### D7 — Consent: explicitly out of scope, honestly flagged

Spec 06 §9 states "GDPR consent integration: out of scope for this version" and TH-141
leaves banner policy PENDING (cross-backlog GDPR blocker). This ADR does **not** design
a consent platform. What it does guarantee:

- All pixel output flows through `slot.pixels`, which TH-141 already declares gated by
  `slot.consent`. When the consent decision lands, gating is a change inside the slot
  provider (server-side suppression until a consent cookie, or the
  `<script type="text/plain" data-consent="marketing">` activation pattern) — zero
  changes to providers or templates.
- **Honest flag:** the platform targets EU stores (France at minimum). Shipping
  marketing pixels without consent gating is a live GDPR/ePrivacy exposure from the
  first EU visitor. This is a product/legal call already tracked in 10 §cross-backlog
  "GDPR"; T030 must not be treated as done-for-EU-launch until it is resolved.

> **Correction (dated addendum, 2026-07-11, per ADR-025 D0/D3 verification):** the
> claim above that consent gating would be "a change inside the slot provider … zero
> changes to providers or templates" is **partly false**. It holds for stateless
> events (`page_view`, `view_content`, `add_to_cart`) — those are slot-provider-only.
> It does **not** hold for the `purchase` claim: `claim_purchase_pixels(store, order)`
> is called in `storefront/views_checkout.py::order_thank_you` — in the view,
> before the slot renders — as a one-shot `FiredPixel.get_or_create`. Gating only the
> render (and not the claim) would burn the claim on an unconsented render, so a
> shopper who later grants consent and reloads would never see the purchase pixel
> fire (`created=False` on the already-consumed claim) — a permanent, silent
> under-report, exactly the §XV-1 invisible-failure class this ADR was built to avoid.
> Read this sentence as "a pixels-app-only change" (slot provider **and** the
> claim site in `pixels/service.py` / `storefront/views_checkout.py`), not
> "slot-provider-only". The corrected design — consent gates the *claims*, not only
> the render — is ADR-025 D0 (verification) and D3 (mechanics); implemented in
> TICKET-048. `initiate_checkout`'s shared exempt-analytics claim has its own
> documented partial exception, also in ADR-025 D3.

### D8 — Settings validation & launch checklist

`PixelsSlotProvider.validate_settings(store)`:

- No `Pixel` rows / all inactive → `[]`. Pixels are optional (checklist R6 is
  informational: "Pixel(s) installed (if ad campaigns are planned)").
- Active row whose `pixel_id` fails its provider regex, or whose provider key is not in
  the registry → one human-readable error each. Non-blocking for launch, visible in the
  checklist (settings-validation architecture, spec 06 §9 item 9).

Admin page = admin-018: card per registry provider (iterate the registry, not a
hardcoded list), Installed/Not-Installed badge from the `Pixel` row, Install → ID form
with regex validation, "..." menu → uninstall (sets `is_active=False`; row kept for
audit).

### D9 — Multi-tenant isolation

`Pixel` extends `StoreOwnedModel`; every read goes through `for_store(request.store)`;
the slot provider receives the store from the request like every other slot. No
platform-global pixel IDs exist. Cross-store leakage is covered by a dedicated test
(store A configured, request on store B's domain → zero occurrences of A's pixel ID).

## Context

- TICKET-030 requires five pixel emitters driven by the same event stream, FiredPixel
  insert-before-fire dedup, and purchase-on-payment-confirmation-only, with the
  per-provider event mapping to be proposed for sign-off (10:460-470).
- Already built and waiting: `FiredPixel` (orders app, main transactional DB per
  AC-U6/ADR-004 §5, unique `(order, pixel_type, event)`); the `slot.pixels` +
  `slot.consent` slots and `SlotProvider` registry (ADR-012 D9, `storefront/slots.py`);
  preview suppression of pixels; the `__purchase_guard__` PAID-gated sentinel in
  `order_thank_you` (the `fire_purchase_pixel` context key was removed as dead context
  pending this ticket); server-side `EVT_INITIATE_CHECKOUT`/`EVT_PURCHASE` analytics
  events with their own dedup; first-touch UTM capture (T012, AC-111).
- Audit `CHECKOUT_BATCH_2_AUDIT.md` MEDIUM-1: the purchase guard must only be consumed
  on PAID renders (fixed in the view; this ADR keeps the gate in the service too) and
  must be wired before T030 ships.
- First-party analytics (beacon, ADR-004) and third-party pixels are separate systems:
  the beacon is inlined first-party JS ingesting into the analytics DB; pixels are
  third-party scripts mounted in their slot. They share trigger semantics, never code
  paths or storage.

## Options considered

1. **Configuration:** dedicated `Pixel` model *(chosen)* vs JSON on `Store` vs generic
   `StoreSetting` key/value rows.
2. **Registry shape:** one bridging SlotProvider over a `PixelProvider` registry
   *(chosen)* vs five independent SlotProviders vs hardcoded template includes.
3. **Purchase guard:** per-provider FiredPixel rows, sentinel retired via migration
   *(chosen)* vs keeping the sentinel as a master gate (loses per-provider audit and
   the ability to add a provider mid-window) vs localStorage/client-side dedup
   (AC-U6 already decided server-side; client storage is trivially cleared).
4. **Event trigger plumbing:** explicit `pixel_events` context contract *(chosen)* vs
   slot provider sniffing context keys vs separate template tags per event.
5. **Server-side APIs:** client-only v1 with CAPI-compatible event IDs *(chosen)* vs
   shipping Meta CAPI now (needs access tokens, PII hashing policy, retry
   infrastructure — a ticket of its own).
6. **InitiateCheckout dedup:** reuse the `CheckoutState.initiate_event_fired` claim
   *(chosen)* vs fire on every checkout GET vs a new pre-order dedup table
   (FiredPixel cannot hold it — non-null order FK).

## Chosen option

Decisions D1–D9 above: `pixels` app, `Pixel` model (one row per store×provider),
`PixelProvider` registry bridged by a single `PixelsSlotProvider` into `slot.pixels`,
five canonical events with the D4 mapping table, per-provider PAID-gated FiredPixel
purchase claims with the sentinel retired by data migration, consent and CAPI as named
follow-ups.

## Why

- Reuses every relevant existing mechanism instead of inventing parallel ones: slot
  registry (rendering, error isolation, preview suppression, theme conformance),
  FiredPixel (dedup), CheckoutState claim (InitiateCheckout), settings-validation
  surface (launch checklist). The Developer ticket is mostly providers + wiring.
- §IX/§XV compliance by construction: adding provider #6 is one class + templates +
  registry entry — no call-site edits; unconfigured providers render nothing;
  misconfigured ones are loud in the checklist, silent on the storefront.
- The purchase path satisfies AC-110/AC-112 and audit MEDIUM-1 with two independent
  layers (DB claim + platform event_id), and the event_id convention makes the CAPI
  follow-up purely additive.

## Risks

- **GDPR/ePrivacy (highest):** v1 fires pixels without consent on EU storefronts.
  Explicitly a product/legal decision tracked in the cross-backlog GDPR blocker; not
  silently accepted here.
- **Client-only purchase loss:** if a shopper never revisits the thank-you page after a
  slow settlement (saw only the PENDING render), the purchase pixel never fires — the
  structural limit of client-side firing. Quantifiable later via FiredPixel coverage
  vs PAID orders; the fix is the CAPI follow-up (fire server-side at the webhook using
  the same event_id).
- **Leaked thank-you token** (audit MEDIUM-1 scenario 1): a leaked link's first PAID
  render consumes the claim in the wrong browser. This has **two dimensions, not one**
  (correction, 2026-07-11 addendum below, PIXELS_AUDIT MEDIUM-2): an analytics-integrity
  angle (double-consumption/dedup, partially mitigated by platform event_id dedup) AND a
  **confidentiality** angle — the thank-you URL itself is a never-expiring signed
  capability token (`storefront/tokens.py`) that renders order details, and every
  pixel provider's automatic page_view historically shipped that full URL to the ad
  platform, landing it durably in third-party logs (visible to anyone with ad-account
  access). The original wording ("analytics-integrity impact only") understated this.
  See the dated addendum for the mitigation shipped for the confidentiality dimension
  and its honestly-documented residual scope.
- **content_ids divergence with T033 feeds** breaks dynamic retargeting invisibly —
  mitigated by the frozen shared `catalog_item_id` helper; T033 must reuse it (add to
  its ticket notes).
- **Script injection via pixel_id** — mitigated twice (regex `clean()` + `escapejs`);
  Safety Agent must still review (admin-entered strings into inline scripts, third-party
  script loading → safety gate applies per pipeline step 11).
- **Snippet drift:** provider snippet formats (fbq/gtag/ttq/snaptr/pintrk) evolve;
  templates isolate the blast radius to one file per provider.

## Rollback strategy

- Feature is additive: uninstalling rows (or deactivating them) empties the slot; the
  `pixels` app can be removed from `INSTALLED_APPS` and the slot renders its empty
  wrapper, exactly as today.
- The only destructive step is the sentinel→per-provider migration; its reverse
  recreates one `__purchase_guard__` row per order having any per-provider purchase
  row, restoring pre-T030 semantics. FiredPixel rows are bounded (order × provider ×
  event) — no growth risk to unwind.

## Tests required

Template-level via Django test client (no JS harness — asserted on rendered HTML) plus
service unit tests:

1. Configured+active provider → base snippet with the store's pixel ID inside
   `<div data-slot="pixels">` on the home page; each of the five providers covered.
2. No `Pixel` row / `is_active=False` → empty slot wrapper, zero broken/partial tags.
3. **Tenant isolation:** store A configured; request on store B → A's pixel ID absent.
4. **AC-110:** first PAID thank-you GET renders purchase snippets and creates one
   FiredPixel row per active provider; reload and back-button GET render no purchase
   snippet and create no rows; snippets contain `order-<pk>-purchase` (Meta/TikTok/
   Snapchat/Pinterest) and `transaction_id` (GA4).
5. **MEDIUM-1 regression:** thank-you GET on a PENDING order creates no rows and
   renders no purchase snippet; after the order flips to PAID, the next GET fires
   exactly once. `claim_purchase_pixels` on a non-PAID order returns empty even if
   called directly.
6. **AC-112:** processing a failed/created payment intent produces no FiredPixel rows
   and no purchase render (webhook path renders nothing by construction — assert no
   rows after a `payment_intent.created`-then-failed sequence).
7. Concurrency: two parallel claims for the same (order, provider) yield one
   `created=True` (unique-constraint path exercised).
8. `initiate_checkout` renders on first checkout GET only; reload renders nothing;
   Pinterest renders nothing for this event.
9. `view_content` payload on product page: correct `catalog_item_id`, value as 2-dp
   string, store currency; JSON bridge payload block present for `add_to_cart`.
10. Validation: `pixel_id` with `</script>` or wrong format rejected at `clean()`;
    a hostile ID that bypasses clean is neutralized by `escapejs` in output; unknown
    provider key in DB → error in `validate_settings`, nothing rendered, error logged.
11. Preview mode (`?theme_preview=`) renders no pixel output (existing suppression —
    regression-pinned here).
12. Sentinel migration: forward converts sentinel rows to per-provider rows for
    then-active providers and deletes sentinels; post-migration thank-you reload of a
    pre-T030 order fires nothing; reverse migration restores sentinels.
13. AC-111 (first-touch UTM) remains covered by T012 tests — no new pixel-side
    assertions beyond payload currency/value correctness.
14. **PIX:P1 (2026-07-11 addendum):** thank-you GA4 render sets `page_location` to the
    normalized tokenless path and never contains the order token in the rendered HTML;
    a non-thank-you GA4 render (e.g. home page) never contains a `page_location`
    override; Facebook/TikTok/Snapchat/Pinterest base snippets keep rendering
    unchanged (no crash) when `page_url` is passed and simply ignore it.

## Addendum (2026-07-11) — PIX:P1, human decision: strip the token from pixel URLs

Recorded per the Safety Agent's `docs/security/PIXELS_AUDIT.md` MEDIUM-2 finding and
`specs/ecommerce_engine/11_uncertainties_to_validate.md` PIX:P1. The Risks section
above is corrected in place (the "analytics-integrity impact only" wording is now
"analytics-integrity AND confidentiality"); this addendum records the disposition and
the honest residual-exposure statement.

**Decision:** option (b) from `PIXELS_AUDIT.md`'s MEDIUM-2 fix list — strip/normalize
the URL sent in the pixel base snippet's automatic page_view on the thank-you page,
rather than (a) capping the thank-you token's `max_age` (that remains a separate,
still-open product decision affecting order-detail bookmarkability generally, not
pixel-specific) or (c) accepting the exposure as-is.

**What is stripped, and where:**

- `storefront/views_checkout.order_thank_you` computes `page_url_override =
  request.build_absolute_uri('/orders/thank-you/')` — an absolute, tokenless,
  normalized path — and adds it to the `TemplateResponse` context alongside the
  existing `pixel_events` / `purchase_pixel_providers` keys (same ADR-022 D5 pattern:
  explicit context key set by the view, never sniffed).
- `pixels.slot_provider.PixelsSlotProvider.render()` reads `context.get(
  "page_url_override")` (`None` on every other page — no other view sets it) and
  passes it through to `PixelProvider.render_base(row.pixel_id, page_url=...)`.
- `pixels.registry.PixelProvider.render_base()` gained an optional `page_url`
  parameter (default `None`, fully backward compatible — every pre-existing call site
  and test that omits it is unaffected) and adds it to the base-template context.
- Only `pixels/templates/pixels/ga_base.html` (the GA4 provider) consumes it: when
  `page_url` is present, `gtag('config', ...)` is called with an explicit
  `{'page_location': '<page_url>'}` override. `page_location` is GA4's documented,
  supported config-time parameter for overriding the automatically-collected page
  URL — this is not a hack; it is the platform's own sanctioned mechanism for exactly
  this situation.

**Residual exposure — honest statement, not glossed over:** Facebook (`fbq('track',
'PageView')`), TikTok (`ttq.page()`), Snapchat (`snaptr('track', 'PAGE_VIEW')`), and
Pinterest (`pintrk('track', 'pagevisit')`) have **no documented, supported per-call
parameter** to override the URL their automatic page_view/PageView/pagevisit call
reports — all four read `document.location.href` directly from the browser at call
time, exactly as before this fix. On the thank-you page, `document.location.href` IS
the real, token-bearing URL (the token is in the actual path the browser is on, not a
value the server volunteers separately), so **these four providers still ship the
token to their ad platforms**, unchanged by this fix. This is a genuine, only
partially-closed gap — GA4 is fixed, the other four are not.

**Why we did not pursue a universal fix (`history.replaceState`):** `PIXELS_AUDIT.md`
also names a `history.replaceState`-based rewrite (change the browser's visible URL
to the tokenless path before the pixels slot renders, so every provider's
auto-collection reads the corrected URL) as a cross-provider option. We evaluated and
rejected it for this fix: rewriting the visible address bar means a manual reload (or
a bookmark) of the thank-you page requests `/orders/thank-you/` from the server —
a path with no registered Django route — producing a 404 instead of re-rendering the
order confirmation. The checkout flow already has deliberate, tested back-button/
reload handling for the thank-you page (EC-007, `order_thank_you`'s
PENDING-vs-PAID handling, purchase-claim idempotency); silently breaking reload to
partially harden four ad pixels was judged a worse trade than the narrower, honest
GA4-only fix shipped here.

**Recommended follow-up (not implemented, not scoped into this fix — no new
dependency added):** the structural fix for the remaining four providers is
server-side event forwarding — Meta Conversions API, TikTok Events API, Snapchat
Conversions API, Pinterest Conversions API — where `event_source_url` (or each
platform's equivalent field) is set explicitly by the server to the same normalized
path used here for GA4, with the browser never sending its real URL to those
platforms for this event at all. That is a materially larger change (server-side
HTTP calls to each platform, credential/hashing requirements per ADR-022 D1's
explicit "CAPI is the named follow-up, not scope creep") and is out of scope here;
it should be tracked as the closing step for PIX:P1's residual exposure.
