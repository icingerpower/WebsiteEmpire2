# 11_uncertainties_to_validate

> Uncertainties awaiting human decision. CRITICAL = blocks implementation of its area. MEDIUM = options will be proposed, recommendation pending approval. LOW items stay in the part files as marked assumptions.

**Status: SPEC PHASE COMPLETE (2026-07-02).** All 13 spec files written. 33 decisions recorded. Remaining open items below are non-blocking for Phase 1 implementation start, except where noted.

## DECISIONS FIRST — highest-leverage human choices

1. ~~**CRITICAL — GrooveKart reference screens**~~ **DECIDED 2026-07-02**: Inspiration only. Extract feature logic and archetypes; the Designer Agent designs the UI fresh. GrooveKart screenshots are NOT screen requirements.
2. ~~**CRITICAL — Coupon/gift-card code system**~~ **DECIDED 2026-07-02**: One unified discount-code system. Single `DiscountCode` model with a type field distinguishing coupons, manual gift cards, and auto-issued gift cards.
3. ~~**CRITICAL — Gift card semantics — balance model**~~ **DECIDED 2026-07-02**: Stored-value with partial redemption. A `GiftCardTransaction` table tracks balance per card. Remaining open sub-items, updated 2026-07-11 per ADR-029 (`docs/adr/ADR-029-gift-card-campaigns.md`, human-decided — full set mirrored in `## Items from ADR-028/ADR-029/ADR-030` below): **value modes beyond fixed — still PENDING** (`percent_of_order` / `percent_discount_coupon` remain reserved enum values with no schema/UI in v1); ~~**expiry per jurisdiction**~~ **DECIDED (human, 2026-07-11)**: 5-year French statutory floor (prescription commerciale) enforced as a minimum-validity floor by the campaign form based on the store's markets (a store targeting the FR market cannot publish/issue a campaign with a shorter-than-5-year expiry); non-FR-market stores keep the configurable `relative_days` (default 30) / `absolute_date` expiry as drawn; **currency across sub-stores — DECIDED** (ADR-029 D3, carried over from ADR-023): always the store's own currency at issuance, no cross-currency redemption in v1; ~~**what auto-issued the 765 uniform $5 cards**~~ **RESOLVED (2026-07-11)**: this was the legacy platform's own automated gift-card campaign ("5 USD after purchase", super-admin-12 list: 744 issued / 10 used / 1 outstanding) — ADR-029's `GiftCardCampaign` feature is its replacement, now with the `CampaignReward` + campaign-row-lock idempotency guard the legacy platform lacked (the "765×$5 incident guard"). Mystery closed, no further spec action needed.
4. ~~**CRITICAL — Payment Control Plane "Publish" semantics**~~ **DECIDED 2026-07-02**: **Live-on-save with Decision Log**. Every routing rule change takes effect immediately. Every payment routing decision writes a `DecisionLog` entry `(order_id, rule_ids_evaluated, chosen_processor, reason, timestamp)`. Rollback = edit/delete the rule; no version snapshots needed.
5. ~~**CRITICAL — One-click post-checkout upsell payment model**~~ **DECIDED 2026-07-02**: **Capture-window model**. Original order payment is AUTHORIZED (not captured). Thank-you page shows upsell offer during a configurable capture window (default 10 min). Accept → add item → one combined capture (original + upsell). Decline or window expires → capture original amount only. Safety Agent must review this flow before implementation. Requires Stripe/PayPal delayed-capture support confirmed by connector spec. ~~PayPal delayed-capture confirmation (T019/T028)~~ **DECIDED 2026-07-05**: PayPal supports delayed capture via the Orders API `intent=AUTHORIZE` flow on standard business accounts. Rather than hardcoding a single mode, implement a configurable `paypal_capture_mode` field on `ProcessorAccount` with two choices: `delayed` (default — authorize now, capture later; PayPal orders eligible for the post-purchase upsell funnel) / `immediate` (capture at checkout; PayPal orders skip the funnel and go straight to fulfillment). Default is `delayed` because it enables the full upsell funnel (the most valuable configuration for merchants).
6. ~~**CRITICAL — Taxes**~~ **DECIDED 2026-07-02**: Zero tax config engine-side. Prices are always tax-inclusive. No tax calculation or tax lines. VAT breakdown only available via the French-bookkeeping order-export API.
7. ~~**CRITICAL — Payment processing screen**~~ **DECIDED 2026-07-02 (partial)**: Day-1 processors = **Stripe + PayPal only**. HiPay, Mollie, BTCPay, BitPay added later via connector registry. `admin-XXX` screen placement and per-processor settings content (API keys, capture mode, webhooks, test mode) remain PENDING — Spec Agent to draft with markers.
8. ~~**MEDIUM — Admin menu split**~~ **DECIDED 2026-07-02**: **Two separate Django admin sites**. Store admin at `/admin/` (per-store: orders, products, customers…). Super-admin at `/superadmin/` (cross-org: organizations, processors, routing rules, themes, shipping…). Same Django User model. Role flags: `is_store_admin` / `is_super_admin`. An employee can be store admin of multiple stores.
9. ~~**MEDIUM — Domain/language/country model**~~ **DECIDED 2026-07-03 (ADR-008)**: `StoreDomain` (unique host, one primary per store) → `StoreLanguage` (= the (domain, language) pair; `use_path_prefix=False` ⟺ domain's default language, DB-enforced) → `ShippingCountry` (target countries per language, drives feeds + hreflang). Single shared `resolve_locale(host, path)`; countries never in URLs; Permalink tables unchanged. See `adr_008_multilingual_domains.md`.
10. ~~**MEDIUM — AI chat technology**~~ **DECIDED 2026-07-10 (human, ADR-024)**: all recommendations of `docs/proposals/ai-chat-technology-proposal.md` approved as-is — **build now**; Claude API + read-only tool-use over the live catalog and StaticPage policies (no RAG / no vector DB); short-lived SSE per reply over plain WSGI (no Channels); `claude-haiku-4-5` with prompt caching, model id configurable; spend into `AiJobMetric` (`job_type='sales_chat'`) + `StoreAiQuota`; antispam reuse + session/store caps; **GDPR: per-store owner opt-in, OFF by default — widget hidden until the store owner explicitly accepts the sub-processor terms**. The ADR-003 CLI-runner rule is a batch-cost policy and does not apply to interactive chat (carve-out documented in ADR-024, Decision 1); the optional `chat_store_digest` batch job stays on the CLI runner (later enhancement). Design: `docs/adr/ADR-024-ai-sales-chat.md`; ACs: TICKET-038 (AC-CHAT-01…16). Mandatory Safety Agent gate before release.
11. **MEDIUM — 3 themes** (foods / fashion / general mid-high-end): Designer to propose. PENDING. **UPDATE 2026-07-05**: full product spec written — `12_storefront_theme_system.md` (T029). Structural contracts, page scope, component inventory and slots are now specced; remaining PENDING here = the *visual* proposals only (final names, palettes/presets, typefaces, imagery, homepage compositions). New MEDIUM decisions raised by that spec (recommendations inside, §6): **T029-A** template engine (recommend Jinja2), **T029-B** asset pipeline (recommend plain CSS + vanilla JS, no build step), **T029-C** checkout shape (recommend single-page grouped sections), **T029-D** theme preview mode (recommend yes, signed token), **T029-E** collection pagination (recommend numbered pagination over infinite scroll).
12. **CRITICAL — French bookkeeping order API**: complete VAT rate/regime values for orders and refunds, per-item ship-from country change at shipping validation. Spec Agent must draft for validation.

## New decisions — from spec-writing pass 2 (2026-07-02)

- ~~**AF-C5 — Revenue definition**~~ **DECIDED 2026-07-02**: Gross Sales = sum of order totals at time of payment, before refunds. Net Sales = Gross Sales − Refunds. Shipping and discounts are separate columns. Reports show Gross Sales; "Net Sales" label on state report is a bug/inconsistency to fix.
- ~~**UF-E — Cart session persistence**~~ **DECIDED 2026-07-02**: **30 days**. Cart survives 30 days after last activity. Resume-cart link in abandoned-checkout emails expires at 30 days.
- ~~**UF-J — Post-purchase upsell funnel depth**~~ **DECIDED 2026-07-02**: **Multi-step funnel from day 1**. After accept/decline on offer 1, a second offer can be shown. Each step is a separate campaign node with its own accept/decline branch. Architect must design the state machine (see design-pattern-ideas §XIV).
- ~~**AF-C3 — CSV Fulfillment tracking import**~~ **DECIDED 2026-07-02**: **Three modes**: (1) Manual tracking entry per order, (2) bulk CSV import (order_id + tracking_number + carrier), (3) Amazon FBA API auto-pull when Amazon FBA integration is configured. All three coexist.
- ~~**FM-C1b / storefront**~~ **DECIDED 2026-07-02**: Spec agent designs storefront pages from scratch. Pages: collection page, product page (standard + A/B variant URL), cart/floating cart, checkout (address + payment), thank-you page (with multi-step upsell funnel), utility pages (search, 404, contact/quotation form).
- ~~**AF-C2b / video**~~ **DECIDED 2026-07-02**: Embed URL only (YouTube/Vimeo). No server upload.
- ~~**UF-H — Multiple gift cards**~~ **DECIDED 2026-07-02**: One gift card per order.

## New decisions — from spec-writing pass 1 (2026-07-02)

- ~~**CRITICAL — Checkout identity**~~ **DECIDED 2026-07-02**: **Guest checkout + optional account**. Coupon per-customer limits enforced by email address. Verified-buyer badge uses order-email match.
- ~~**CRITICAL — Discount stacking order**~~ **DECIDED 2026-07-02**: **Coupon applied first, then gift card on remainder.** Coupon (% or $ off) applies to the full original order total; remaining balance is covered by the gift card.

## New CRITICAL uncertainties — from spec-writing pass 1 (2026-07-02)

### Storefront / buyer-facing surface
- ~~**FM-C1 — Storefront screens**~~ **DECIDED 2026-07-02**: Spec agent designs storefront from scratch using product-page feature toggles, checkout globals, analytics funnel, and 3 curated themes as constraints. Storefront pages: collection page, product page (standard + A/B variant), cart/floating cart, checkout, thank-you page, utility pages (search, 404, contact/quotation).
- ~~**FM-C4 — Multi-URL A/B product pages SEO**~~ **DECIDED 2026-07-02**: All variant URLs canonicalize to the primary product URL. Variant URLs are served (so Pinterest crawlers work) but carry a canonical tag pointing to the primary. Not added to sitemap.
- **FM-C1b — CRITICAL: Zero storefront source screens (remaining gap).** All 87 screenshotted screens are admin or super-admin. No screenshots of the actual buyer-facing storefront (product page, collection page, cart, checkout steps, search, 404, FAQ, contact). This gap blocks: theme design (item 11), pixel event mapping, SEO spec for storefront page types, and storefront user flow refinement. Resolution needed: provide screenshots/mockups, or confirm spec agent designs from scratch using product-page config toggles + checkout globals + report funnel steps as constraints.
- **FM-C4 — CRITICAL: Multi-URL A/B product pages vs SEO canonical strategy.** Extra-spec says same product can have multiple URLs where only images change (Pinterest content + A/B testing). Must decide: (a) all variant URLs canonicalize to one primary URL (safe for SEO, no duplicate-content risk), or (b) each variant URL is indexable independently (more Pinterest surface, requires quality thresholds to avoid thin content). Blocks SEO spec and sitemap rules.
- **UF-E — CRITICAL: Cart session persistence duration.** How long does an abandoned cart survive? (Session only? 24h? 7 days? 30 days?) Determines abandoned-checkout trigger window and the resume-cart link token lifetime. Blocks abandoned-checkout campaign spec.

### Products & inventory
- ~~**AF-C2 — Product video**~~ **DECIDED 2026-07-02**: Embed URL only (YouTube / Vimeo). No blob storage for video. Admin pastes embed URL; renders as embedded player on product page.
- ~~**UF-H — Multiple gift cards per order**~~ **DECIDED 2026-07-02**: One gift card per order only.
- **AF-C2b — CRITICAL: Product video** (remaining open): none. When a product has video, is it: (a) uploaded to server (blob storage required), or (b) an embed URL (YouTube/Vimeo — no storage needed)? Affects media storage architecture.
- ~~**FM-C3 — Pre-order payment**~~ **DECIDED 2026-07-02**: **Full charge at order placement**. Pre-order = pay full price now, ships on [date shown]. No deposit, no delayed capture for pre-orders. Ask-for-quotation: no charge until quote is accepted — conversion flow remains PENDING (Spec Agent to draft).
- ~~**Settings-C1 — Email sender domain**~~ **DECIDED 2026-07-02**: **Shared platform sender for v1**. All stores send from `noreply@mail.pradize.com`. Store sets display name + reply-to address only. Single SPF/DKIM record for `*.mail.pradize.com`. Per-store custom domain later via DNS verification flow. Pre-order: full charge now, or deposit only, or no charge until shipped? Quotation: no charge until quote accepted — how does the quote→order conversion work? What triggers payment? Blocks product editor spec and checkout flow.
- **UF-H — CRITICAL: Multiple gift cards per order.** Can a customer apply more than one gift card to a single order? Affects GiftCardTransaction model and checkout UX.
- **UF-K — CRITICAL: Refund method when a gift-card-paid order is refunded.** Refund to: (a) original gift card balance restored, (b) new gift card code issued, (c) refund to original payment method if mixed payment. Decision affects GiftCardTransaction and refund flow.
- **AF-C3 — CRITICAL: CSV Fulfillment round-trip.** The orders screen has a "CSV Fulfillment" bulk export button implying an external fulfillment partner pipeline. Is there also a tracking-number import-back step (upload CSV with order_id + tracking_number + carrier to bulk-mark orders as shipped)? Blocks fulfillment workflow spec.

### Campaigns & jobs
- ~~**AF-C4 — CRITICAL: Abandoned-checkout email send delay.**~~ **DECIDED 2026-07-04**: Send-delay field appears **in the email wizard** alongside type/style/copy. Default: 1 hour after abandonment. Field label: "Send after" with a number field + hours/days dropdown. The field is **required** — a campaign email cannot be saved without a delay value. Stored as `send_delay_hours` (days × 24).
- **UF-J — CRITICAL: Multiple sequential post-purchase upsell offers.** Can a store have multiple post-purchase upsell offers in a funnel (accept/decline → next offer)? Or is it single offer only? Blocks one-click upsell campaign spec.
- **FM-C2 — CRITICAL: Memberships/credits model.** Extra-spec says API-based AI job execution is used only when membership credits remain, with terminal AI runners preferred. No membership or credit entity appears anywhere in the screenshots. What is this model? Blocks AI job orchestration spec.

### Analytics
- **AF-C5 — CRITICAL: Revenue definition.** "Gross Sales" vs "Net Sales" is used inconsistently across the 13 reports (state report uses "Net Sales", all others say "Gross Sales"). Must define: does "Gross Sales" include or exclude refunds, discounts, shipping? Is "Net Sales" = Gross minus refunds? A single canonical definition must be locked before any report is built.

### Payment Control Plane
- ~~**AF-C6 — Stage-2 threshold cap cascade**~~ **DECIDED 2026-07-02**: **Fall through to default organization** (which has no monthly cap). When all routing options are exhausted, the DecisionLog records the reason and routes to the default org. Merchant is responsible for ensuring the default org can absorb overflow.
- ~~**FM-C2 — AI credits/membership model**~~ **DECIDED 2026-07-02**: **Simple monthly token/credit quota per store** (configured in super-admin). Terminal AI runners (Claude Code, Codex, Gemini Terminal) are preferred; they consume from the quota. When credits run low, the system falls back to direct API calls. No formal membership tiers.

### Medium (noted, non-blocking now)
- **UF-A — MEDIUM:** Timer promo interaction with the pricing engine (what happens if a timed discount promo is active when a coupon is also applied — which takes precedence?).
- **UF-I — MEDIUM:** Order placement idempotency strategy (double-submit prevention on checkout).
- **AF-C7 — MEDIUM:** Payment Control Plane "Publish" rollback mechanism if staged versioning is used.
- **AF-C8 — MEDIUM:** Order Bump split-test result reporting location.
- ~~**AF-C1 — MEDIUM:** Per-store vs global employee permission matrix — can an employee have Full access on Store A and Limited on Store B using the same 18-module matrix, or is access always org-wide?~~ **DECIDED (human, 2026-07-11), per ADR-033 D4c: per-store matrix** (see ADR-033 entry below).

## CRITICAL by area (from screen inventory)

### Analytics & reports (Part A)
- Definition of "Revenue"/"Gross Sales" (gross vs net of refunds/discounts/shipping); "Net sales" label on state report only — intentional?
- Definitions/denominators of all NEW analytics columns (collection display/CTR, page display, purchase rate, avg % scroll, % product CTR) — determines the event-tracking data model.
- Multi-collection attribution (double-counting per collection row?); traffic-source→order attribution model (first vs last touch, window).
- Customer-local-hour bucketing for multi-timezone countries (US/CA/AU); exact country list for state-level breakdown.
- Order state machine: canonical payment-status and fulfillment-status enumerations; CSV Fulfillment round-trip (export orders → import tracking?).
- Employee accounts imply roles/permissions but no matrix shown for store admin (super-admin has the 18-module grid).

### Catalog, collections, reviews, currencies (Part B)
- Collection "product price" rule vs multi-currency/sale prices — which value matches.
- Abandoned checkout: definition of "abandoned", attribution rules, GDPR basis for emailing non-purchasers, sender domain SPF/DKIM per store.
- ~~Abandoned checkout: `CODE123` literal vs generated unique coupons~~ **DECIDED 2026-07-05**: **Unique single-use code per recipient** (not a shared fixed promo code). Each email step that includes a coupon generates a distinct `DiscountCode` per recipient via `CampaignIssuedCode` (the attribution/idempotency link built in T027). The `DiscountCode` has `usage_limit=1`, `provenance='campaign'`.
- ~~Abandoned checkout: resume-link token security~~ **DECIDED 2026-07-05**: **HMAC-signed token**. Token = HMAC(cart_id + expiry timestamp) signed with Django `SECRET_KEY`, via `django.core.signing` — same pattern as Django's password-reset links. Stateless (no extra DB row). Forged tokens are rejected; expired tokens (> 30 days) are rejected with "link expired or invalid" (no token-existence leak).
- Customers: PII retention/anonymization/erasure (GDPR).
- ~~Currency: display-only vs transactional conversion (what currency is charged), rate source/frequency, rounding (.99 pricing)~~ **DECIDED 2026-07-10 (ADR-023, `docs/adr/ADR-023-currency-display.md`)**: Display-only, confirmed — checkout/thank-you/order record/emails always use `store.default_currency` (this was already shipped as ML-004 in ADR-015; ADR-023 only confirms product/collection/cart pages convert while checkout does not, and flags that `AC-160`'s wording predates and conflicts with the shipped ML-004 behavior on the "checkout subtotals" clause). Rate source: manual entry always authoritative + optional ECB daily-feed auto-refresh (off by default, stdlib-only fetch, zero new dependencies). Rounding: `.99` psychological rounding via `ceil(raw) − smallest_unit`, platform-global toggle, zero-amount guard for free line items. Remaining PENDING sub-items below (non-blocking for `T031`/`T037` implementation).
  - ~~**PENDING (MEDIUM, product):** exact list of the 3 non-ECB currencies to reach the ticket's "34 currencies" target (ECB natively covers ~31).~~ **DECIDED (human, 2026-07-10):** seed ONLY the ~30 ECB feed currencies (+ EUR anchor) at launch. No extra non-ECB currencies are seeded now — a super-admin can add any additional `CurrencyDefinition` (with a manual `CurrencyRate`) later at `/superadmin/` exactly like the always-available manual path already supports. Does not block `T031`/`T037` implementation.
  - ~~**PENDING (LOW, product):** 0-decimal currency (JPY/KRW…) psychological-rounding convention.~~ **DECIDED (human, 2026-07-10):** 0-decimal currencies (JPY/KRW) round to whole units using the SAME generic psychological rule as 2-decimal currencies (`ceil(raw) − smallest_unit`, smallest unit = 1 whole unit) — no separate per-locale convention (e.g. "98"/"80" endings) implemented in v1.
  - ~~**Code visibility (`show_code`/`code_placement`) dropped between screenshot spec and ADR-023 draft**~~ **DECIDED (human, 2026-07-11):** restored per admin-011 (`screen_inventory_parts/part_B_admin_006-012.md`). `CurrencyDefinition` gains `show_code` (bool, default `False`) and `code_placement` (prefix/suffix, default `suffix`), composed independently of `symbol_placement` in the single `_format_amount()` formatter — e.g. `"45.99 $ CAD"`. Additive migration, no backfill. See ADR-023 §2 addendum.
  - ~~**LOW — `06_settings_requirements.md` §8's "one global enable toggle" framing**~~ **DECIDED (human, 2026-07-11, ADR-023 §3 addendum):** superseded — there is no platform/store-wide on/off switch; enablement is per-currency (`StoreCurrencySetting.is_enabled`), zero enabled rows = "off" (picker does not render).
- AI-generated reviews: legal/compliance implications, provenance flags, customer-only threshold semantics.

### Settings, pixels, coupons, products (Part C)
- Pixels: exact ecommerce events per provider (Purchase/AddToCart, value, currency, dedup on thank-you reload — see design-pattern-ideas §X). ~~**PROPOSED 2026-07-10 — PENDING APPROVAL**~~ **DECIDED (human, 2026-07-10): full bundle approved as recommended** — full per-provider mapping table in `docs/adr/ADR-022-pixel-integrations.md` §D4 (5 providers = admin-018 list; canonical events page_view/view_content/add_to_cart/initiate_checkout/purchase; value/currency rules; `order-<pk>-purchase` event_id; per-provider FiredPixel PAID-gated claims). Bundled recommended defaults to sign off together: (a) GA4-only, no Universal Analytics IDs; (b) Pinterest has no initiate_checkout standard event → renders nothing; (c) client-side snippets only in v1, Meta CAPI/TikTok Events API as follow-up; (d) `catalog_item_id` convention shared with T033 feeds. ~~GDPR consent gating stays out of scope per 06 §9 / TH-141 (cross-backlog GDPR blocker — EU exposure flagged in ADR-022 Risks).~~ **RESOLUTION IN PROGRESS (2026-07-11):** consent gating is now designed in `docs/adr/ADR-025-consent-management.md` (ACCEPTED, human, 2026-07-11) — server-side render gating of `slot.pixels` by consent category plus consent-gated purchase/initiate claims (ADR-025 D0/D3, correcting ADR-022 D7's "slot-provider-only" wording — see the dated addendum on ADR-022 D7). Implementation: TICKET-048 (consent core) + TICKET-049 (banner UI), both required together (10). This closes TH-141 and KNOWN_RISKS item 1 once shipped; see the ADR-025 items below for the four now-DECIDED product choices and the one remaining external (legal) item.
- ~~**PIX:P1 — MEDIUM — PENDING APPROVAL (2026-07-11, Safety Agent PIXELS_AUDIT MEDIUM-2): thank-you-token
  confidentiality vs pixel page_view URLs.**~~ **DECIDED (human, 2026-07-11): strip the token from pixel
  URLs (option b)** — implemented via a `page_url_override` context key (order_thank_you view →
  `PixelsSlotProvider.render()` → `PixelProvider.render_base(pixel_id, page_url=...)`), set to a
  normalized, tokenless, absolute path (`/orders/thank-you/`) on the thank-you page only. Only GA4
  (`pixels/templates/pixels/ga_base.html`) has a documented, supported client-side override for the
  automatically-collected page URL (`gtag`'s `page_location` config parameter); Facebook, TikTok,
  Snapchat and Pinterest have no equivalent per-call override in their base snippets and continue to
  read `document.location.href` (the real, token-bearing URL) for their automatic page_view/PageView/
  pagevisit calls — this is a real, honestly-documented residual exposure for those four providers, not
  a full fix. A URL-rewrite approach (`history.replaceState` before the pixels slot renders, PIXELS_AUDIT
  option b variant) was considered and rejected: it would change the browser's visible address bar,
  breaking a legitimate reload of the thank-you page (no route is registered for the rewritten path) —
  a regression the checkout flow's existing back-button/reload handling (EC-007) explicitly guards
  against elsewhere. See `docs/adr/ADR-022-pixel-integrations.md` dated addendum (2026-07-11) for the
  full disposition, the corrected risk-note wording (confidentiality, not just analytics-integrity),
  and the recommended follow-up (server-side Conversions APIs — Meta CAPI / TikTok Events API /
  Snapchat CAPI / Pinterest Conversions API — where `event_source_url` can be set explicitly
  server-side for all five providers, closing the residual gap; out of scope for this fix, no
  dependency added).
- Coupon "Add rule" rule-type vocabulary (min order, products, collections, segments?).
- Per-variant availability toggle + "erase inventory if checked" — inventory data model (ties into extra-spec inventory modes: always SOLD OUT / no inventory / fixed / fake server-assigned / presale / ask-when-available / quotation).
- Recent-purchase popup: what buyer data is shown publicly (privacy/GDPR). ~~**PROPOSED 2026-07-11 (ADR-027 D6)**~~ **DECIDED-anonymous-gated (human, 2026-07-11):** social proof ships **anonymous-only at launch** — platform default and only enableable mode is `anonymous` ("Someone in France bought X" — no personal data published, no lawful-basis question), widget disabled by default per store (`SocialProofSettings.is_enabled=False`). `first_name` / `first_name_city` modes (never surname/email/order data, coarse time buckets, anonymized customers excluded) exist in schema/admin but **cannot be enabled for any store** until sign-off on the same human/legal track as the ADR-025 beacon item (see that item below, now extended to cover this) — they carry an admin-visible "pending privacy review" warning until then. Fabricated/simulated purchase entries are explicitly rejected (UCPD/FTC exposure — ADR-027 D6); empty window → widget renders nothing. See `## Items from ADR-026/ADR-027` below for the full DECIDED set (D3 double opt-in default, D4 localStorage exemption).
- Store email domain handling (@pradize.com sender, SPF/DKIM); "Delete store" semantics (soft delete/retention).

### Super-admin operations (Part D)
- ~~Employee "Limited access" semantics on Orders (view-only? no refunds?); Full Access master mode vs per-module grid; where employee-management permission itself lives.~~ **DECIDED (human, 2026-07-11), per ADR-033:** Orders "Limited access" = view-only (no refunds, no status changes, no CSV export); Full Access master flag composed on top of the per-module grid; employee-management lives in its own `employees` module row (D1c). See `## Items from ADR-034/ADR-035` below (ADR-033 sub-section) for the full DECIDED set.
- Shipping: zone matching precedence (specific zone vs "All Countries"); exception rule-type list; annotated "Model 1/Model 2" shipping models + per-product override semantics.
- Automated emails: multilingual template model (per-language variants, fallback, retranslation jobs on template update).
- ~~**Up-sell: super-admin vs store campaign precedence/stacking**~~ **DECIDED 2026-07-04**: **Store-admin wins**. Store-admin campaign takes precedence over super-admin campaign on the same product. Store-admin can view but not modify the super-admin campaign. ~~**Buy-X-get-X free-value cap**~~ **DECIDED 2026-07-04**: **No cap** — free item is always 100% free regardless of price. ~~**Storewide-discount next-order constraints**~~ **DECIDED 2026-07-04**: **Single-use + configurable expiry** (default 30 days).
- Automated gift card "% discount off order" as a card value — stored-value vs coupon.

### Payment Control Plane (Part E)
- Simulation / Decision Logs / Overview / Settings pages have no diagrams — content and retention unspecified.
- Org threshold semantics (what counts: auth/capture/refunds; month boundary; behavior at cap; currency conversion for mixed €/$ thresholds).
- Geography rule ordering paradox (broad "EU" rule at priority 1 shadows "FR" at 3); pool-returning matches and evaluation stop.
- Percentage split mechanics (orders are indivisible — use running-counter targeting per design-pattern-ideas §IX).
- Layer-ordering conflict: method-level option list mixes organizations, but the org is already chosen by rules — which layer decides the merchant of record; composition of org-layer % split with method-layer "alternate evenly".
- `vault://` secret backend (storage, access control, rotation); health status source (probes vs manual) and automatic failover trigger.
- Interaction of geography-rule results with org eligibility/coverage (skip within pool vs fall to default vs reject at save).

## Decisions from spec-writing pass 3 (2026-07-02)

- ~~**AC-U2 — Inventory mode scope**~~ **DECIDED**: **Per variant**. Each ProductVariant owns its own `inventory_mode` (sold_out / no_tracking / fixed_qty / fake_server_assigned / presale / ask_when_available / ask_for_quotation) and `quantity`. A single product can have variants in different modes simultaneously.
- ~~**UF-K / AC-U1 — Gift card refund**~~ **DECIDED**: **Restore to original gift card balance**. Refund = negative GiftCardTransaction reversal entry on the original code. Balance increases by refunded amount.
- ~~**Architecture — Store↔Organization scope**~~ **DECIDED**: **Payment-only + loose store grouping**. Organization owns ProcessorAccounts and RoutingRules only. Platform-level super-admin (not Organization) owns Themes, Shipping zones, Campaign templates. ADR-001 updated.
- ~~**Architecture — Analytics volume**~~ **DECIDED**: **500+ orders/day per store (high volume)**. ADR-004 updated: separate analytics DB (TimescaleDB recommended). Analytics writes never compete with transactional Postgres.
- ~~**Settings-C3 — Processor credential storage**~~ **DECIDED**: **Encrypted in Django DB** using django-fernet-fields. FERNET_KEY env var (separate from SECRET_KEY). AES-256 at rest. Custom `__repr__` so key values never appear in logs. `vault://` URN in SVGs resolves to this encrypted DB field.
- ~~**FEED-C1 — Per-country shopping feed currency**~~ **DECIDED**: **Per-country feed uses display currency**. CA-targeting Google Shopping feed shows CAD prices (converted from store default USD using the currency converter's exchange rate). Feed regenerated when rates update. FB Dynamic Ads feed: same rule.
- ~~**AC-U3 — Mixed refund instrument order**~~ **DECIDED 2026-07-02**: **Card refunded first** (up to the card-charged amount), remainder restores the gift card balance. Faster for the customer (card refunds: 3–5 days; gift card: instant).
- **AC-U5 — MEDIUM: Language-identity validation for AI translation output** (no detection mechanism specified — Architect to propose a minimum-confidence gate in ADR-003).
- ~~**AC-U6 — Pixel FiredPixel placement**~~ **DECIDED 2026-07-02**: **FiredPixel in main transactional DB**. Correctness-critical: must insert before firing. Analytics DB availability must not gate purchase event dedup. Table is tiny (one row per order × pixel type). ADR-004 to be updated to reflect this.
- **AC-U8 — MEDIUM: Collection CTR double-counting** when a product belongs to multiple collections. Solution: add `source_collection_id` to the click Event — Architect to update ADR-004 event schema.

## Items from spec 14 — Multilingual content pipeline (2026-07-05, Spec Agent)

- **14:P2 — PENDING APPROVAL: Variant option translation scope.** 07 §1.5 commits to translating option names AND values keyed by stable PK, but options live in `ProductVariant.option_values_json` with no stable identity. Recommended: Architect introduces stable option/option-value entities, then translate both. Alternatives: values-only with a platform glossary for names; or descope to v2 (requires explicit approval — it contradicts 07 §1.5).
- **14:P3 — PENDING APPROVAL: Manual override vs re-translation.** When a translation record was manually edited and the admin explicitly clicks "Re-translate": recommended per-record "manually edited on <date> — overwrite?" confirmation; automatic/bulk passes always skip overridden records. Alternatives: always skip silently; overwrite without warning (rejected — data loss).
- **14:P4 — PENDING APPROVAL: Retranslation trigger granularity.** On source-field change of a published object: recommended auto-requeue jobs for all languages (skipping overridden records); alternative: mark translations `stale` and let the admin trigger from the coverage view.
- **14:G1 — HIGH (implementation divergence, not a spec question):** `catalog/signals.py` auto-creates product permalinks as `products/{default-lang-slug}`; 07 URL-001 requires root-level slugs and MCP-031 requires the translated slug. Developer fix + data migration before storefront/sitemap consumption.
- **14:MCP-012 — ASSUMPTION (LOW):** Translation records use a two-state status (draft/published) with in-flight/error states carried by the linked AiJob, vs ML-041's four-state wording. Admin coverage view unions both. Spec Reviewer to confirm the reconciliation.

## Items from spec 16 — Checkout page (2026-07-06, Spec Agent)

- **U16-1 — LOW: Stale PENDING orders after cart edit.** The idempotency key is a hash of session + (variant, qty) pairs, so a shopper who fails payment, returns to cart, and modifies the cart can legitimately re-submit — but the first PENDING order (with its manual-capture authorization) remains. Recommended: watchdog voids authorizations on PENDING orders whose capture window expired and whose intent was never confirmed.
- **U16-2 — MEDIUM — PENDING APPROVAL: Failed-payment recovery model (16 §OF-007).** After a decline the Cart is already CONVERTED and the idempotency key blocks re-submission of the identical cart. Recommended: a payment-retry view bound to the existing order + PaymentIntent (abandoned-checkout resume links land there too). Alternative: re-activate the CONVERTED cart and void the order (more moving parts, breaks OF-004 guarantees).
- **U16-3 — MEDIUM — PENDING APPROVAL: Checkout language resolution (16 §ML-001).** `/checkout/` is never language-prefixed (07 §2), so on a multi-language domain the URL alone resolves to the root language. Recommended: `resolve_locale` middleware persists the browsing language in the session; checkout/cart/thank-you read it (fallback: domain root language). Alternatives: language-prefixed checkout URLs (contradicts 07 §2); root-language-only checkout (bad UX for `/fr/` shoppers).
- **U16-4 — LOW — PENDING APPROVAL: Returning-customer login UX at checkout (16 §CO-005).** Recommended: login modal that never leaves `/checkout/`. Alternative: redirect to login with return-redirect. Depends on Phase-5 account pages; the link stays hidden until accounts ship.
- **U16-5 — LOW — PENDING APPROVAL: Billing address (16 §SA-007).** v1 copies shipping into `Order.billing_address` (recommended — tax-inclusive pricing removes the jurisdiction need; processor elements collect AVS data). Alternative: "different billing address" toggle. Confirm acceptable for card AVS in target markets.
- **U16-6 — MEDIUM — PENDING APPROVAL: Free-shipping threshold messaging (16 §SM-004).** No minimum-order field exists on FREE ShippingRate rows. Recommended: add `min_order_amount` to ShippingRate so checkout/cart can show "Add $X more for free shipping". Alternatives: descope threshold messaging to the cart page; free shipping via coupons only.
- ~~**U16-7 — MEDIUM: Shipping-zone "service description" translation**~~ **DECIDED (human, 2026-07-10, ADR-019):** field stays on `ShippingZone` per the spec text (`ShippingZone.service_description`, super-admin-edited, shared by all stores shipping to the zone — accepted consequence; copy must stay operational/store-neutral). The Architect's rate-level alternative was considered and rejected by the human. Single-language for v1, tracked on ADR-019's "known multilingual debt" list rather than a one-off translation table; does not block checkout completeness. See `docs/adr/ADR-019-shipping-service-description.md`.
- **U16-8 — LOW: `checkout_language` field on Order** so confirmation emails render in the shopper's checkout language (16 §ML-005). Schema addition for the Architect.
- **16:G1 / 16:G2 / 16:G3 — implementation divergences, not spec questions:** `begin_checkout` omits shipping from `Order.total` and accepts no shipping rate (16 §OF-002); no zero-total branch when discounts cover the full order (16 §PY-022); `bump_checkout` slot missing from the storefront slot registry (16 §PA-004). Developer to fix under the checkout ticket.

## MEDIUM items

~90 medium items are recorded per screen in the part files (part A: report conventions, sorting, normalisation; part B: collection re-evaluation timing, wizard details; part C: feed cadence, bump pricing; part D: KPI formulas, template variables, theme activation model; part E: store profiles, rule precedence tab). The Spec Agent will surface each with a recommended default when writing the detailed area specs — they do not block architecture start except where listed above.

## Items from ADR-018 — Static pages + contact/quotation forms (2026-07-10, Architect)

- ~~**18:P1 — MEDIUM — PENDING APPROVAL: Contact-message notification recipients (ADR-018 D3).**~~
  **DECIDED 2026-07-10 (T029-SP implementation): all active `StoreEmployee` rows with
  `full_access=True`**, notified via `send_transactional_email("contact_message_received")`
  (`storefront/views_pages.py:_notify_full_access_employees`). No per-store "notification
  email" setting exists in v1; that remains a natural later refinement when the
  settings-validation area lands, with no schema impact today.
- **18:A1 — ASSUMPTION (LOW): Static page body is raw HTML rendered with `|safe`** (ADR-018 D2),
  matching the existing product/collection description behavior. The platform-wide rich-text
  sanitizer (NFR-T3, Safety Agent's allowlist) remains an open item now covering three call
  sites: product description, collection description, static page body — one function at the
  persistence boundary when it lands.
- **18:A2 — ASSUMPTION (LOW): `"pages"` as the permission-matrix module key** for StaticPage
  CRUD and the ContactMessage inbox. Final matrix vocabulary lands in TICKET-047; renaming the
  key is a trivial follow-up.

## Items from SEO review of static pages (2026-07-10, SEO Agent) + spec re-review

- ~~**SEO:P1 — MEDIUM — PENDING APPROVAL: Policy-page indexability contradicts CAN-002/§5/SM-030/TST-CAN-002.**~~
  **DECIDED (human, 2026-07-10): spec amended, no code change.** The spec previously required
  alternate-language legal pages to be noindex + sitemap-excluded; the shipped ADR-018 behavior
  keeps translated policy pages fully indexable and sitemapped, suppressing only hreflang
  alternates. SEO Agent assessed the shipped behavior as SEO-safe and arguably better (policies
  here are genuinely translated, not boilerplate duplicates), and the human approved amending the
  spec rather than changing code. `07_multilingual_seo.md` — CAN-002, the §5 legal-pages row,
  SM-030's exclusion list, the §1.5/§2 cross-references, and TST-CAN-002 — now match the ADR-018
  model verbatim. `pages/tests/test_seo_parity.py` already asserts the amended behavior
  (`test_policy_page_hreflang_tags_empty`, `test_policy_page_sitemap_alternates_empty`,
  `test_policy_page_stays_in_sitemap_loc_list`); no new/duplicate test was added. `python3
  manage.py test pages sitemaps` confirmed green post-amendment.
- **18:P2 — LOW — DECIDED (human, 2026-07-10): quantity NOT displayed on quotation forms.**
  Quotation-page `quantity` POST field exceeded ADR-018 D4 (which stated `quantity=1`).
  Resolved the opposite way from the spec reviewer's default suggestion: quantity is removed
  (not ratified) from all customer-facing quotation forms — quotation-mode products are not
  cartable, so a requested quantity is not meaningful at request time. Removed from
  `static_quotation_form.html`, `quotation_form.html`, `storefront/views_pages.py`
  (`_handle_quotation_post`), and `storefront/views_product_forms.py`
  (`quotation_request_view`). `QuotationRequest.quantity` model field retained (default 1)
  for potential admin-side use; no migration.

## Items from ADR-020 — PayPal buyer-approval completion flow (2026-07-10, Architect)

Recommended defaults are designed into ADR-020 so implementation proceeds; the human can
veto either PENDING item with a one-line follow-up ticket — neither requires schema changes.

- ~~**20:P1 — MEDIUM — non-funnel capture delay.**~~ **DECIDED (human, 2026-07-10):
  the refinement — capture immediately on payment confirmation — chosen over the
  original "keep watchdog-at-expiry" default.** Orders without an active funnel
  campaign no longer wait for the watchdog's next `capture_window_expires_at`
  sweep (2 days for PayPal / 7 days for Stripe). `begin_checkout` sets
  `Order.capture_immediately=True` when no funnel campaign applies;
  `campaigns.tasks.capture_original_charge_now(order)` is called from the
  payment-confirmation path for both processors —
  `storefront.views_checkout._finalize_payment_return` (synchronous buyer-return
  tail, shared by Stripe and PayPal) and the async webhook fallback
  (`payments.webhook_views._handle_payment_intent_authorized` for Stripe
  `payment_intent.amount_capturable_updated`;
  `payments.paypal_webhook_views._handle_authorization_created` for PayPal
  `PAYMENT.AUTHORIZATION.CREATED`, split into a DB-write half and a
  network-capture half so no `select_for_update` lock is held across the
  capture call). `capture_original_charge_now` reuses
  `_watchdog_claim_and_capture` verbatim (same atomic claim, same
  `capture:{order_id}` idempotency key) — one capture implementation, not two.
  `capture_window_expires_at` is still computed and saved exactly as before and
  remains `capture_window_watchdog`'s reconciliation fallback if the inline
  capture fails. Funnel orders are unaffected (`capture_immediately` stays
  `False`); zero-total orders never reach this code path and keep the model's
  `False` default. See ADR-020 D8 (updated 2026-07-10) and ADR-011's
  2026-07-10 addendum. Tests: `campaigns/tests/test_capture_original_charge_now.py`,
  `cart/tests/test_checkout_capture_immediately.py`,
  `storefront/tests/test_checkout.py::FinalizePaymentReturnImmediateCaptureTest`,
  `payments/tests/test_immediate_capture_webhooks.py`.
- ~~**20:P2 — MEDIUM — PayPal `shipping_preference='NO_SHIPPING'`.**~~
  **DECIDED (human, 2026-07-10): forward the address — chosen over keeping
  `NO_SHIPPING` for v1.** `PaymentProcessor.create_payment_intent` gains a
  defaulted `shipping_address: dict | None = None` kwarg (Stripe ignores it,
  same untouched-by-default posture as `return_url`/`cancel_url`).
  `cart.checkout.begin_checkout` forwards its own checkout-collected
  `shipping_address` unchanged. `PayPalConnector.create_payment_intent` maps a
  non-empty address onto `purchase_units[0].shipping` (Orders API v2:
  `name.full_name`, `address.address_line_1/2`, `admin_area_2`=city,
  `admin_area_1`=state, `postal_code`, `country_code`; blank keys omitted) and
  switches `experience_context.shipping_preference` to `'SET_PROVIDED_ADDRESS'`.
  `None`, or an address dict with no non-blank fields, preserves the exact
  pre-20:P2 payload (`NO_SHIPPING`, no `shipping` key) — Stripe's payload is
  untouched either way. See ADR-020 D2 (updated 2026-07-10). Tests:
  `payments/tests/test_paypal.py` (shipping-address mapping, blank-field
  omission, `None`/all-blank fallback to `NO_SHIPPING`),
  `payments/tests/test_stripe.py::test_create_payment_intent_ignores_shipping_address`.
- **20:A1 — ASSUMPTION (LOW): `payment_source.paypal.experience_context`** is used for
  return/cancel URLs (not the deprecated `application_context`), and the approval link is
  extracted accepting both `rel='approve'` and `rel='payer-action'` (PayPal switches to
  `payer-action` when `payment_source` is present). Sandbox verification during
  implementation; only the connector's link extraction would need adjusting if the response
  shape differs.
- **20:A2 — ASSUMPTION (LOW): retry re-serves the stored PayPal approval link**
  (`retrieve_payment_intent_client_secret` returns the approve URL for orders still in
  `CREATED`/`PAYER_ACTION_REQUIRED`/`APPROVED`-without-authorization). Best-effort: if
  PayPal has expired the link, the shopper falls back to the decline path and re-submits
  checkout (fast-path void reclaims the stale order).
- **20:A3 — ASSUMPTION (LOW): PayPal `max_capture_delay = timedelta(days=2)`** — the 3-day
  honor period minus 24 h of watchdog/beat-outage headroom. A different margin is a
  one-constant change on the connector.

## Items from ADR-025 — Consent management (2026-07-11, human decision)

All four P-items from `docs/adr/ADR-025-consent-management.md` §P were approved as
recommended by the human on 2026-07-11. ADR-025 status flipped PROPOSED → ACCEPTED.
Implementation: TICKET-048 (consent core) + TICKET-049 (banner UI) — see
`10_implementation_tickets.md`.

- ~~**ADR-025:P1 — Banner copy & tone**~~ **DECIDED (human, 2026-07-11):** approved as
  recommended — neutral/factual EN copy ("We value your privacy" / "We use cookies and
  similar technologies to measure our audience and — with your consent — to measure
  advertising performance. You can accept, refuse, or customize."), buttons "Accept
  all / Refuse all / Customize", platform `.po` strings (French day-1). No per-store
  banner copy in v1.
- ~~**ADR-025:P2 — Re-prompt interval**~~ **DECIDED (human, 2026-07-11):** 180 days
  (`CONSENT_COOKIE_MAX_AGE`, platform-level constant, not per-store).
- ~~**ADR-025:P3 — Rollout default for existing stores with active pixels**~~
  **DECIDED (human, 2026-07-11):** consent `is_enabled=True` for ALL stores at
  migration (safe-by-default), with a `non_eu_acknowledged` escape hatch
  (`ConsentSettings`, D6) as the only path to disabling per store.
- ~~**ADR-025:P4 — Proof retention**~~ **DECIDED (human, 2026-07-11):** 13 months
  rolling purge of `ConsentRecord` via a management command on the existing
  scheduled-purge pattern.
- **ADR-025 — legal sign-off on beacon-exemption reading — PENDING, owner
  human/legal. One legal track, now two consumers (extended 2026-07-11 to also
  cover the ADR-027 items below).** The CNIL audience-measurement exemption
  assessment for the first-party analytics beacon (ADR-025 D4: no cookies, ephemeral
  per-tab `sessionStorage` id only, no IP captured, no third-party disclosure) and the
  "refuse-all does not flip the exempt `am` toggle" default are engineering readings
  of CNIL guidance, not counsel. This is an external legal decision, not a code
  decision — it does not block implementation of TICKET-048/049: the fallback
  (`ConsentSettings.beacon_requires_consent=True` as a platform default) is a
  one-boolean flip with no redesign if legal disagrees. Track resolution here; update
  ADR-025 D4/D6 defaults once legal responds.
  **Same track now also covers (ADR-027, 2026-07-11):** (a) whether the named
  social-proof display modes (`first_name`/`first_name_city`, ADR-027 D6) may ever be
  enabled for a store — until this sign-off, only `anonymous` mode may be enabled,
  platform-wide, regardless of merchant preference; (b) the overlay's localStorage
  frequency-cap/exclusion exemption reading (ADR-027 D4) — accepted as designed
  pending this same review; (c) legal sufficiency of the DE double-opt-in default /
  single-opt-in elsewhere (ADR-027 D3). None of (a)/(b)/(c) blocks T034/T035
  implementation — each has a one-boolean/one-enum fallback already documented in
  ADR-027, matching the ADR-025 beacon item's own no-redesign fallback shape. See
  `## Items from ADR-026/ADR-027` below for the full DECIDED product-level defaults
  this legal item sits alongside.
- **ADR-025:RM-2 — MEDIUM — PENDING APPROVAL (2026-07-11, developer flag during the RM-1 fix):
  beacon override × non-EU escape hatch.** A store with `beacon_requires_consent=True`
  (conservative opt-in) AND `is_enabled=False, non_eu_acknowledged=True` (escape hatch) has
  contradictory config: the beacon gate waits for a consent cookie that can never be set
  (banner disabled), so the beacon stays dark — the same structural inversion as RM-1, but
  for the beacon and only under a self-contradictory store configuration. Fail-closed, low
  practical impact. Options: (a) route `resolve_consent_for_pixels` (or a beacon variant)
  through the same store-settings override, (b) forbid the contradictory combination in
  `ConsentSettings.clean()` (recommended — the config is meaningless), (c) accept as
  documented. Architect to adjudicate in the next consent-adjacent ticket.

## Items from ADR-026/ADR-027 (2026-07-11, human decision)

Both ADRs flipped PROPOSED → ACCEPTED on 2026-07-11. Implementation: TICKET-033
(T033-A engine / T033-B admin) for ADR-026; TICKET-034 (lead capture) + TICKET-035
(social proof) for ADR-027 — see `10_implementation_tickets.md`.

**ADR-026 — Catalog feeds:**
- ~~**ADR-026:D4 — Field-mapping table**~~ **DECIDED (human, 2026-07-11):** signed
  off as designed — shared item dict, frozen `catalog_item_id` for `g:id`,
  `display_amount()` for all pricing, permalink-resolver `g:link`, availability
  matrix per inventory mode (D4b). No changes requested.
- ~~**ADR-026:D4 vs Risks — regular-price/sale-price guard (paper conflict)**~~
  **DECIDED (human, 2026-07-11):** the Risks-section guard wins. Emit `g:sale_price`
  only when `compare_at_price` is set **and** `> variant.price`; in every other case
  (no `compare_at_price`, or a merchant-misconfigured `compare_at_price <= price`)
  emit `g:price = variant.price` alone, never `compare_at_price` as the regular
  price. The D4 table's Price row previously omitted this guard (read "`compare_at_price`
  if set else `variant.price`", contradicting the Risks row below it) and has been
  corrected in `docs/adr/ADR-026-catalog-feeds.md` to match.
- All nine PENDING product items from `docs/adr/ADR-026-catalog-feeds.md`'s P-table
  were approved as recommended:
  - ~~**P-1 — Pinterest feed in v1?**~~ **DECIDED:** deferred — Google + Facebook only in v1.
  - ~~**P-2 — Country-tracking parameter on `g:link`?**~~ **DECIDED:** none in v1.
  - ~~**P-3 — `google_product_category` source?**~~ **DECIDED:** omitted in v1.
  - ~~**P-4 — Dedicated GTIN/barcode field?**~~ **DECIDED:** deferred; `identifier_exists=no` + `mpn=sku` covers v1.
  - ~~**P-5 — `sale_price_effective_date` from timed promos?**~~ **DECIDED:** deferred until the timed-promo entity exposes dates.
  - ~~**P-6 — `g:color`/`g:size` from `VariantOptionAssignment`?**~~ **DECIDED:** deferred; title suffix suffices for v1.
  - ~~**P-7 — Item-level `g:shipping` blocks?**~~ **DECIDED:** deferred to Merchant Center account-level settings.
  - ~~**P-8 — Per-country per-product shippability exclusion?**~~ **DECIDED:** deferred with P-7.
  - ~~**P-9 — Regeneration cadence?**~~ **DECIDED:** confirmed — 30-min debounced beat + nightly full rebuild, both settings-overridable.
- No open items remain on ADR-026; nothing blocks T033-A/T033-B.

**ADR-027 — Conversion overlays (lead capture + social proof):**
- ~~**ADR-027:D6 — Social-proof buyer-data display mode**~~ **DECIDED-anonymous-gated
  (human, 2026-07-11):** ships **anonymous-only at launch** — `anonymous` is the only
  mode any store may enable; `SocialProofSettings.is_enabled=False` by default per
  store. Named modes (`first_name`, `first_name_city`) exist in schema/admin, carry a
  "pending privacy review" warning, and **cannot be enabled for any store** until the
  human/legal sign-off tracked in the extended ADR-025 item above resolves. Never-
  fabricate rule (empty window → widget renders nothing) is DECIDED and absolute, not
  subject to the legal track.
- ~~**ADR-027:D3 — Single vs double opt-in for lead-capture email marketing**~~
  **DECIDED (human, 2026-07-11):** v1 ships single opt-in + `LeadSignup` consent-proof
  rows for all stores, **with double opt-in enabled by default for stores targeting
  Germany (DE)**. Must be live before the first marketing send to captured leads, not
  before TICKET-034 ships (capture ≠ send). Legal sufficiency of both the DE default
  and single opt-in elsewhere is tracked on the extended ADR-025 legal item above
  (non-blocking).
- ~~**ADR-027:D3 — DE-targeting detection basis**~~ **DECIDED (human, 2026-07-11):**
  a store counts as DE-targeting, and therefore gets double opt-in by default, when
  its `de` `StoreLanguage` is enabled **OR** any of its `ShippingCountry` rows targets
  Germany — either condition alone is sufficient (not both required). Basis: BGH
  case-law reasoning applies German consumer-protection expectations based on either
  the language a store markets in or the country it ships to, independently of the
  other. Legal sufficiency of this reading remains on the extended ADR-025 legal item
  above (non-blocking).
- ~~**ADR-027:D4 — localStorage frequency-cap/exclusion exemption reading**~~
  **DECIDED (human, 2026-07-11):** accepted as designed — no consent-category gating
  required for the overlay's visited-page-exclusion or frequency-cap localStorage
  keys. Fallback (gate behind the ADR-025 functional consent category) stays
  documented but inactive, pending the same legal review as the beacon item.
- No item on ADR-027 blocks T034/T035 implementation; the three items above are
  either fully DECIDED product-level defaults or externally-tracked legal
  confirmations with pre-built no-redesign fallbacks (see the extended ADR-025 item
  above).

## Items from ADR-028/ADR-029/ADR-030 (2026-07-11, human decision)

All three ADRs flipped PROPOSED → ACCEPTED on 2026-07-11 (human decision — all
recommended options approved). Implementation: TICKET-042 (ADR-028), TICKET-045
(ADR-029), TICKET-046 (ADR-030) — see `10_implementation_tickets.md` and the
index rows in `09_architecture_decisions.md`.

**ADR-028 — A/B multi-variant product pages (`ProductPageVersion`):**
- ~~**ADR-028:P1 — max page versions per product**~~ **DECIDED:** 10 (permalink-bloat guard).
- ~~**ADR-028:P2 — version URL languages**~~ **DECIDED:** version permalinks are created automatically for **every language** with an active product permalink (suffix appended to that language's translated slug), not per-language opt-in.
- ~~**ADR-028:P3 — activation requires ≥ 1 image**~~ **DECIDED:** yes — an empty image set would render identically to the primary (pure duplicate page).
- ~~**ADR-028:P4 — statistical-significance display**~~ **DECIDED:** none in the v1 report (counts + conversion rate only, per "basic statistics" in 00). Revisit together with AF-109 (order-bump split-test reporting) so A/B result reporting is designed once.
- ~~**ADR-028:P5 — redirect semantics**~~ **DECIDED:** deactivating a version → 302 to primary (reversible); deleting a version → 301 to primary (permanent — Pinterest link equity).
- Resolves AF-12's "page A/B test data model" PENDING marker (`04_admin_flows.md`, updated 2026-07-11). No open items remain on ADR-028; TICKET-042 may proceed (two Developer PRs, 042-A/042-B, per the ADR's implementation split).

**ADR-029 — Automated gift-card campaigns (`GiftCardCampaign`):**
- ~~**ADR-029:D6 — expiry**~~ **DECIDED:** 5-year French statutory floor (prescription commerciale) enforced as a configurable-validity floor by the campaign form, based on the store's markets. Mirrored into decision #3 above.
- ~~**ADR-029:D9 — refund/void policy**~~ **DECIDED:** on full refund of the triggering order, an issued campaign gift card that is **unspent** is deactivated (`is_active=False`, audited); a card that is partially/fully **spent** is left untouched and the order is flagged in the refund admin view for manual review. No automatic clawback; partial refunds never auto-void (v1).
- ~~**ADR-029:D3 — value modes**~~ **DECIDED:** fixed-value only ships in v1; `percent_of_order` / `percent_discount_coupon` remain reserved enum values / PENDING — unchanged, mirrored into decision #3 above.
- ~~**ADR-029:D2 — trigger vocabulary**~~ **DECIDED:** `order_paid` only in v1 (`nth_order` / `spend_threshold` / `win_back` stay reserved, documented values — no new code path).
- ~~**ADR-029:D7 — delivery channel**~~ **DECIDED:** email only in v1 (no thank-you-page display — avoids colliding with the upsell funnel's own thank-you surface, per §XIV).
- ~~**ADR-029:D9 — self-gifting brake**~~ **DECIDED:** two brakes — (1) an order whose processor-charged amount is zero (fully covered by gift card/coupon) never triggers issuance; (2) the per-recipient-email frequency cap (`cap_enabled`/`cap_count`/window), default off but recommended in admin help text.
- ~~**11:#3 remainder — "what auto-issued the 765 uniform $5 cards"**~~ **RESOLVED (2026-07-11):** see decision #3 above — the legacy platform's own automated gift-card campaign is the issuer; ADR-029 is its replacement with the idempotency guard the legacy platform lacked.
- Remaining non-blocking items (unchanged from the ADR, not part of this human decision): multi-store scoping of `owner_scope` (MEDIUM), `GiftCardCampaignTranslation` + AiJob wiring (follow-up), conditions rule builder (hidden in v1 UI), frequency-cap prefix-query index verification. None block TICKET-045.

**ADR-030 — Security badge designer:**
- ~~**ADR-030:D1 — truthful-only built-in badge set**~~ **DECIDED:** approved as designed — no third-party certification marks (McAfee/Norton/TRUSTe/BBB/"24-Hour Surveillance"/"AES-256 BIT") ship as built-in assets; built-in assets are either self-verifying (lock/SSL icons gated on `request.is_secure()`) or derived from the store's actually-enabled `PaymentMethod.method_family` rows; stores may still upload their own image for a genuine third-party certification they hold. **Recorded as a deliberate, human-approved product-integrity decision** — a knowing deviation from the CommerceHQ reference screenshots' four raster "BADGE COMBINATION" images, not an oversight or a missed requirement.
- **ADR-030:D5 — live re-render preview pane**: stays **out of scope, PENDING** — v1 ships the static preset-card grid (already visual) without a live keystroke-driven re-render of the composed badge; a follow-up ticket can add it if merchants ask.
- No other open items on ADR-030; TICKET-046 (complexity S) may proceed.

## Items from ADR-034/ADR-035 (2026-07-11, Architect)

**ADR-034 — Per-store custom email sender domains (`SenderDomain`, TICKET-040):**
- ~~**ADR-034:P1 — `dnspython` dependency approval (CRITICAL, blocks the automated-verification path)**~~ **DECIDED/APPROVED (human, 2026-07-11):** the ticket's "verification polling with explicit status states" can only be delivered by a real DNS TXT lookup — no stdlib DNS resolver exists in Python. `dnspython` (pure Python, no C extension) is approved for the Celery beat task, joining the `requirements.txt` "Approved <date> (reason)" list next to `redis`/`psycopg2-binary`/`anthropic`. The automated DNS-verification path (B1) ships; the manual super-admin-only "Mark verified" fallback (B3) is no longer needed.
- **ADR-034:P2 — re-verification cadence for already-`VERIFIED` domains:** recommended weekly re-check demoting to `FAILED` on record disappearance (prevents a domain that changes DNS ownership after verification from keeping platform trust indefinitely). Exact cadence not yet human-approved — **still PENDING**, not part of the 2026-07-11 decision batch.
- Full design: `../../docs/adr/ADR-034-sender-domains.md`. Mandatory Safety Agent gate before release (domain verification = spoofing surface).

**ADR-035 — TimescaleDB analytics migration (TICKET-044, revises ADR-004):**
- Architect recommendation (not a hedge): **defer the hypertable conversion**, ship the vendor/extension-detection guards now. At zero production traffic there is no load to tune a chunk-management policy against, and ADR-004 already made the schema identical on both backends for exactly this reason.
- No PENDING product decision blocks this — it is a build-now / convert-later posture. Extension install remains an ops/release-checklist item, never automatic.
- **DECIDED (human, 2026-07-11):** ADR-035 was already decisive at proposal time — no PENDING items to resolve; ADR status flipped to ACCEPTED.
- Full design: `../../docs/adr/ADR-035-timescaledb-migration.md`.

**ADR-033 — Employee permission matrix (TICKET-047 + TICKET-054):**
- **Closes 18:A2**: `pages` stays the canonical module key, displayed as "CMS" (freeze
  criterion: shipped keys canonical, AF-002 labels display-only). `analytics` likewise
  stays, displayed as "Reports". Sole key migration: `collections` merged into `products`.
- ~~**ADR-033:P1 — `employees` as a separate 19th grid row (CRITICAL, default set)**~~ **DECIDED (human, 2026-07-11):**
  restates AF-002's edge case — "Settings — Full access" must NOT implicitly grant
  employee management (escalation hole). Shipped as decided: separate "Employee Accounts"
  row (matches code already gating on `"employees"`); the grid renders 19 rows, a
  deliberate deviation from the 18-row screenshot grid.
- ~~**AF-C1 (existing item) — default set by ADR-033 D4c: per-store matrix**~~ **DECIDED (human, 2026-07-11): per-store matrix** (AF-112's own
  recommendation; org-wide degenerates to copying one matrix per row, so nothing is
  foreclosed).
- ~~**Orders "Limited access" (existing Part D item) — default remains view-only**~~ **DECIDED (human, 2026-07-11): view-only**
  (TICKET-002 decision carried; no refunds, no status changes, no CSV export).
- ~~**ADR-033:A1 — module_key mapping ASSUMPTIONS (LOW, one-line edits each)**~~ **DECIDED (human, 2026-07-11) — approved as-is:** campaigns
  admins → any-of `("upsell_campaigns","abandoned_campaigns")`; cart → `orders`; chat →
  `apps`; consent, emails, shipping → `settings`; unified DiscountCode (coupons + gift
  cards, ADR-002) → `gift_cards` (AF-002 has a "Gift cards" row but no "Coupons" row —
  a separate coupons module would be a vocabulary change needing human sign-off).
- ~~**ADR-033:A2 — reserved keys with no admin surface yet (LOW)**~~ **DECIDED (human, 2026-07-11) — approved as-is:** `inventory` (quantities
  stay under `products` inside the product editor until a standalone inventory screen
  exists), `themes` (TICKET-029 area), `customers` (when store-site registered),
  `dashboard` (gates index dashboard widgets via `require_module`, never the index page).
- Full design: `../../docs/adr/ADR-033-permission-matrix.md`. **Mandatory Safety Agent
  gate** before release (permission system: escalation, lockout prevention, invite
  tokens, IDOR on employee rows) — joint gate over TICKET-054 + TICKET-047.
- **ADR-033 status: ACCEPTED (human, 2026-07-11) — full bundle approved** (19-module
  vocabulary with separate `employees` row, coupons→gift_cards mapping, structural
  enforcement, lockout guard, no self-edit). Implementation may proceed without
  restriction.

## Deferred implementation items (non-blocking)

- **Collection noindex threshold — per-store configurability (DEFERRED to T007)**:
  `COLLECTION_INDEXABLE_THRESHOLD = 3` in `catalog/templatetags/catalog_tags.py` is a
  module-level constant shared by the `{% noindex_if_thin %}` template tag and the
  sitemap builder.  Per-store configurability is deferred: when T007 (general settings
  model) lands, this should become a store-level setting so that stores with very large
  catalogues can raise the threshold and stores with a narrow focus can lower it.
  A `# TODO T007` comment marks the constant in code.
