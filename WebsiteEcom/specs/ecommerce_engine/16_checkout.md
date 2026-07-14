# 16 — Checkout Page Specification

> Storefront checkout: from "Checkout" click on the cart to the thank-you page.
> Written by the Spec Agent, 2026-07-06. Sources: `spec-ecom/` screenshots,
> `extra-spec-ecom.txt`, `design-pattern-ideas.txt`, `03_user_flows.md`
> (UF-007…UF-014), `07_multilingual_seo.md`, `15_storefront_theme_system.md`,
> ADR-006-R (payment routing), ADR-007/ADR-011 (capture window + vault),
> ADR-008 (locale), and the existing backend service `cart/checkout.py`
> (`begin_checkout`), whose decided behavior this spec incorporates.

Tag legend: `[screenshot]` derived from screenshots · `[extra-spec]` from
extra-spec-ecom.txt · `[DECIDED …]` already decided in `11_uncertainties_to_validate.md`
or an accepted ADR · `[ASSUMPTION]` reasonable default chosen by the Spec Agent ·
`[PENDING APPROVAL]` requires a human decision before architecture/implementation.

---

## 1. Overview and user journey

### 1.1 Page model

- **CK-001 — Single-page checkout with progressive sections.** One page at
  `/checkout/` containing, in order: Contact → Shipping address → Shipping
  method → Payment. An order-summary panel (sidebar on desktop, collapsible
  accordion on mobile) is always visible and acts as the review step — there is
  **no separate review page**. `[screenshot: UF-007 describes one checkout page
  with sequential form sections; no multi-page flow appears in any screenshot]`
  `[ASSUMPTION: single-page over multi-step — fewest navigations, matches the
  CommerceHQ reference UI; sections validate-and-collapse as the shopper advances]`
- **CK-002 — Section gating.** A section is editable only when all previous
  sections are valid. Completed sections collapse to a summary line with an
  "Edit" link (e.g. "Contact: jane@example.com — Edit"). Editing an earlier
  section re-opens it without losing later entries, except that changing the
  country or address re-triggers shipping-method lookup (SM-001). `[ASSUMPTION]`
- **CK-003 — Entry conditions.** Reached from the cart page or floating cart
  "Checkout" CTA. On load with an empty or missing cart → 302 redirect to
  `/cart/` (EC-001). Fires `EVT_INITIATE_CHECKOUT` (cart value, item count,
  currency) once per checkout session. `[screenshot: UF-007 step 2]`
- **CK-004 — Return-to-cart link.** A persistent "← Return to cart" link
  (top of page and/or under the summary) navigates back to `/cart/` without
  losing any entered data (form state persisted in the checkout session,
  CO-007). `[ASSUMPTION]`
- **CK-005 — "All taxes included."** The order summary displays the note
  "All taxes are included in the displayed prices" (translated). No tax line,
  no tax calculation, no invoice — email receipt only.
  `[extra-spec]` `[DECIDED #6 — zero tax config engine-side]`
- **CK-006 — Distraction-free layout.** The checkout template hides the full
  store header/footer navigation; it shows only the store logo (linking to the
  home page), the return-to-cart link, and the security badge slot. Recent-purchase
  notifications, lead-capture overlays, and the AI chat widget are suppressed on
  `/checkout/` (overlay suppression consistent with 03_user_flows UF edge case
  "trigger suppressed if a cart or checkout action is in progress"). `[ASSUMPTION]`
- **CK-007 — Guest checkout.** No login is ever required. Identity is the email
  address. `[DECIDED — Checkout identity: guest checkout + optional account]`
- **CK-008 — Abandoned-checkout timer.** The abandonment timer starts when the
  shopper's email is captured (CO-002). The inactivity timeout is the super-admin
  "Abandoned checkout timeout" setting (minutes field, 10 recommended/default).
  `[screenshot: super-admin-07-checkout-page.jpg]` Campaign behavior itself is
  specced in ADR-010 / T021, not here.
- **CK-009 — Order bumps (before-checkout upsell).** If an order-bump campaign
  targets the checkout page, the bump section renders between the cart items
  summary and the payment section, with the campaign's configured heading text,
  multi-select or single-select product cards, country targeting, and split-test
  rotation when several campaigns share a category.
  `[screenshot: super-admin-11-up-sell-campaigns-07-before-checkout-order-bump.jpg]`
  Rendering goes through a `bump_checkout` slot (see §10.4). Adding/removing a
  bump item updates cart items and totals in place without leaving checkout.
- **CK-010 — Security badge slot.** The configured security badge ("Guaranteed
  SAFE Checkout" image block with the store's chosen combination, colors, border,
  width) renders under the Pay button when the badge's "Checkout page" location
  is enabled. `[screenshot: super-admin-06-security-badge.jpg — "Set on Cart and
  Checkout"; super-admin-06-security-badge-02-edit.jpg — per-location enable + width]`
- **CK-011 — Step sequence summary.**
  1. Contact (§2) → 2. Shipping address (§3) → 3. Shipping method (§4) →
  4. Payment incl. discount codes + order bumps (§5) → Pay → 5. Thank-you (§6).

### 1.2 Order summary panel (always visible)

- **CK-020 — Contents:** line items (thumbnail, product name, variant title,
  quantity, line total — quantities are **not editable** here; "Return to cart"
  is the edit path `[ASSUMPTION]`), pre-order expected-ship date per pre-order
  item (UF-005), coupon line ("Coupon CODE — −$X"), gift-card line ("Gift card
  •••• last4 — −$Y"), shipping line (selected rate name + price, or "—" until
  selected), the tax-inclusive note (CK-005), and the total in the store's
  transactional currency.
- **CK-021 — Live recalculation.** Any change (bump added, coupon applied or
  removed, shipping method changed) recalculates and re-renders the summary
  server-side; totals are never computed in JavaScript. `[design-pattern §XV-5]`
- **CK-022 — Discount stacking order.** Coupon applies first to the subtotal;
  one gift card applies to the remainder; shipping is added after discounts
  unless a free-shipping coupon zeroes it (OF-006).
  `[DECIDED — stacking order; DECIDED UF-H — one gift card per order]`

---

## 2. Contact step (CO-xxx)

- **CO-001 — Email field.** Required. Validated as RFC-shaped email
  (Django EmailValidator); normalized lowercase + trimmed before any lookup
  (matches `Customer.save()` normalization). Used at finalization to
  `get_or_create` the store-scoped Customer row.
- **CO-002 — Email capture beacon.** On blur/change with a valid email, the
  storefront POSTs the email to the cart (`Cart.customer_email`) — this arms
  abandoned-checkout recovery (CK-008) before the order exists.
  `[screenshot: UF-007 step 4]`
- **CO-003 — Phone field.** Displayed per store setting
  `checkout_phone_mode ∈ {hidden, optional, required}`, default `optional`.
  When shown: free-format string, max 50 chars, no country-format validation
  beyond length (matches `Order.phone`). `[ASSUMPTION — no screenshot shows a
  phone setting; a store config is the safest superset of "optional or required"]`
- **CO-004 — Newsletter opt-in.** Unchecked-by-default checkbox
  "Email me with news and offers" (translated). When checked, sets
  `Customer.accepts_marketing = True` at order finalization. Never checked by
  default (GDPR). `[ASSUMPTION — standard practice; field exists on Customer]`
- **CO-005 — Returning customer link.** A low-key line "Already have an
  account? Log in" opens the account login **without abandoning checkout**
  (modal or same-tab return-redirect back to `/checkout/`). On successful
  login: email pre-fills (read-only with a "change" affordance) and the default
  saved address pre-fills SA fields. The link is hidden while the account
  feature (storefront Phase 5 `/account/`) is not yet enabled for the store.
  `[DECIDED — guest + optional account]` `[PENDING APPROVAL — modal vs
  redirect; see U16-4]`
- **CO-006 — Validation errors** render inline under the field, translated
  (ML-002): "Please enter a valid email address."
- **CO-007 — Form-state persistence.** Contact + address + selected shipping
  method are persisted server-side against the cart/checkout session on each
  section submit, so a reload, a return-to-cart round-trip, or an
  abandoned-checkout resume link restores them (resume prefill is DECIDED —
  HMAC resume token, ADR-010).

---

## 3. Shipping address step (SA-xxx)

- **SA-001 — Fields.** First name (required), Last name (required), Address
  line 1 (required), Address line 2 (optional), City (required), State/Province
  (conditional — SA-004), ZIP/Postal code (required — SA-005), Country
  (required dropdown). Stored as the `Order.shipping_address` snapshot dict
  `{name, line1, line2, city, state, postal_code, country}` where `name` is
  "First Last" and `country` is ISO 3166-1 alpha-2. `[screenshot: UF-007 step 5]`
- **SA-002 — Country selector.** Options = the union of countries covered by
  active shipping zones that have at least one active rate for this store
  (a catch-all "All Countries" zone with a rate ⇒ full country list). Default
  preselection: geo-IP country when available, else the store's default
  country. Changing the country resets state/province options, relabels fields
  (ML-003), and re-triggers shipping-method lookup (SM-001).
  `[screenshot: UF-007]` `[ASSUMPTION — offering only shippable countries
  up-front prevents the dead-end error state SM-005 in most cases]`
- **SA-003 — Google Maps address autocomplete.** When the super-admin has
  activated the Google Maps integration (Activate button + API credentials),
  the Address line 1 field offers Places autocomplete; selecting a suggestion
  auto-fills line1, city, state, postal code, country. ZIP auto-fill is
  US-reliable only (per the setting's own description). Autocomplete is
  assistive only: free-text entry always remains possible and no suggestion is
  ever forced (UF-007 edge case). When the integration is not activated, the
  form is plain fields — no degradation. `[screenshot: super-admin-07-checkout-page.jpg]`
- **SA-004 — State/Province.** Required select for countries with mandatory
  subdivisions (at minimum US, CA, AU); optional free-text for others; hidden
  for countries without subdivisions. Label adapts per country (ML-003).
  `[ASSUMPTION — subdivision list per country from a static ISO 3166-2 dataset]`
- **SA-005 — Postal code.** Required; format check per country where a pattern
  is well-known (US 5(+4), CA A1A 1A1, FR 5 digits, …), otherwise non-empty.
  **Format-only validation — no geocoding, no deliverability check.**
- **SA-006 — Save address (logged-in only).** When the shopper is logged in, a
  checkbox "Save this address for next time" (checked by default) creates/updates
  a `CustomerAddress` (first saved address becomes `is_default`). Guest
  checkouts never create `CustomerAddress` rows — the address lives only in the
  Order snapshot (matches `customers/models.py` invariant). `[ASSUMPTION]`
- **SA-007 — Billing address.** v1 does not collect a separate billing
  address: `Order.billing_address` is written as a copy of the shipping
  address. Card verification data (postal code) is handled by the processor's
  own payment element where the processor requires it.
  `[ASSUMPTION — matches current `begin_checkout` behavior (billing snapshot
  unused); tax-inclusive pricing removes the tax-jurisdiction need]`
  `[PENDING APPROVAL — see U16-5 if a "different billing address" toggle is wanted]`

---

## 4. Shipping method step (SM-xxx)

- **SM-001 — Rate lookup.** On address completion (and on every country/address
  edit), the system resolves the shipping zone for the destination country
  (most-specific-first: explicit-country zone beats "All Countries" catch-all —
  `shipping/service.py` rule) and loads the store's active `ShippingRate` rows
  for that zone, filtered by cart weight for WEIGHT_BASED rates and applying
  any `ShippingRateException` (product/collection overrides).
- **SM-002 — Display per rate.** Radio list showing: rate display name
  (e.g. "Standard Shipping"), carrier name when set, estimated delivery window
  ("5–8 business days" from `estimated_days_min/max`; omitted when unset),
  the shipping-zone "shipping service description" when configured (translated
  — this is the customer-facing field flagged in 06_settings §1178), and the
  price ("Free" for zero-price/FREE rates).
- **SM-003 — Default selection.** The cheapest available rate is preselected;
  ties broken by fastest `estimated_days_max`, then name. `[ASSUMPTION —
  cheapest-first maximizes conversion; task offered cheapest-or-first]`
- **SM-004 — Free-shipping threshold display.** When the store has a
  free-shipping incentive configured, checkout shows "You qualify for free
  shipping" / "Add $X more for free shipping". **Blocked on schema**: no
  minimum-order field exists on FREE `ShippingRate` rows today.
  `[PENDING APPROVAL — see U16-6: add `min_order_amount` to ShippingRate vs
  descope threshold messaging to the cart page only]`
- **SM-005 — No shipping available.** If zone resolution or rate filtering
  yields zero rates: the shipping-method section shows a blocking, translated
  error — "We're sorry, we can't ship to this address. Please try a different
  address or contact us." (contact link to the store contact page). The Pay
  section stays disabled. This state is reachable even with SA-002 filtering
  (e.g. weight-filtered zones). `[screenshot: UF-007 edge case]`
- **SM-006 — Shipping price is part of the payable total.** The selected rate's
  id and price are submitted with finalization and re-validated server-side
  against the zone/rate tables inside the checkout transaction (never trusted
  from the client). Free-shipping coupons zero the shipping line (OF-006).
  **Implementation gap note (16:G1):** the current `begin_checkout` accepts no
  shipping parameter and omits `shipping_amount` from `Order.total` — the
  Developer must extend it per OF-002. This spec is the source of truth.

---

## 5. Payment step (PY-xxx)

### 5.1 Methods

- **PY-001 — Method families.** The checkout renders one button/tab per enabled
  platform `PaymentMethod` (`card`, `paypal`, `crypto`), using its
  `displayed_label`. The shopper never sees individual processor accounts
  (ADR-006-R). Day-1 processors: **Stripe (default for credit cards) and PayPal
  (default for PayPal)**. `[screenshot: admin-XXX-payment-processing.jpg]`
  `[DECIDED #7 — Stripe + PayPal day 1; others later via connector registry]`
- **PY-002 — Method availability (AC-134).** Before rendering, each method
  with `hide_when_no_route=True` runs `preview_route()` for the destination
  country / currency / approximate total; methods with no eligible healthy
  processor are hidden. If **all** methods are hidden → EC-005 error state.
- **PY-003 — Card entry.** Card family renders the processor SDK's hosted
  fields (Stripe Payment Element). Card data never touches Pradize servers
  (SAQ-A posture). `EVT_ADD_PAYMENT_INFO` fires when payment details are
  completed. `[screenshot: UF-007 step 8]`
- **PY-004 — Apple Pay / Google Pay.** Offered as express wallet buttons within
  the card family via the Stripe Payment Element / Payment Request Button when
  the device/browser supports them. Wallets ride the `card` method family and
  the same PaymentIntent flow — no separate `PaymentMethod` row.
  `[ASSUMPTION — wallets are a Stripe presentation detail, not a routing family]`
- **PY-005 — PayPal.** PayPal family uses the PayPal SDK button →
  approve-redirect/popup → return to checkout with the authorization. Capture
  mode follows `ProcessorAccount.paypal_capture_mode` (`delayed` default —
  funnel-eligible; `immediate` — skips the post-purchase funnel).
  `[DECIDED #5 — PayPal delayed capture]`

### 5.2 Payment intent lifecycle

- **PY-010 — Single finalization call at "Pay now".** The PaymentIntent is
  created **at Pay-now submit**, not on step entry: card fields mount in the
  processor SDK's deferred mode (amount/currency known client-side, no intent
  yet). Clicking Pay POSTs the finalization request; the server runs
  `begin_checkout` (OF-001) which creates the Order and the PaymentIntent
  (`capture_method='manual'` — authorize only) and returns `client_secret` +
  `processor_type`; the storefront then confirms the payment with the SDK
  (`stripe.confirmPayment`). Rationale: `begin_checkout` is one atomic
  transaction keyed by the cart idempotency fingerprint — creating the intent
  earlier would create Orders for shoppers who never press Pay.
  `[DECIDED — matches the accepted `cart/checkout.py` design, ADR-007 §2]`
- **PY-011 — Authorize now, capture later.** Checkout only **authorizes**.
  Capture happens after the post-purchase upsell capture window (default
  10 min, per campaign) via the watchdog, or per the 7-day fallback window for
  non-funnel orders. When a one-click-funnel campaign is active, the intent is
  created with `setup_future_usage`/vault so accepted upsells can charge
  off-session. `[DECIDED #5 — capture-window model; ADR-011]`
- **PY-012 — 3DS / SCA.** Handled by the processor SDK during client-side
  confirmation: the Payment Element opens the 3DS challenge (modal/iframe or
  full redirect with `return_url=/checkout/?resume_intent=…`). On redirect
  return, the storefront re-checks intent status and proceeds to the thank-you
  redirect (success) or shows the payment error inline (failure). The order
  and intent already exist (PY-010), so 3DS retries reuse them.
- **PY-013 — Payment error display.** Processor decline codes map to
  translated, user-safe inline messages above the Pay button: card declined /
  insufficient funds / expired card / 3DS failed / generic. The shopper may
  retry on the **same order + intent** (SDK re-confirm) or switch method
  family. Raw processor error strings are never shown verbatim; full detail
  goes to logs and the DecisionLog context. `[screenshot: UF-010 decline branch]`
- **PY-014 — Pay button.** Label "Pay now — $TOTAL" (translated, amount
  included). Disabled until every section is valid; double-click-guarded
  client-side; server-side double-submit is covered by OF-004 idempotency.

### 5.3 Discount codes at checkout

- **PY-020 — Coupon entry.** One code input + "Apply" in the order summary.
  Apply validates advisorily (existence, active, dates, limits, per-email
  limit, min order, product/collection rules, stacking) and stores the code on
  `Cart.discount_code`; atomic consumption happens only inside the checkout
  transaction (OF-005). All UF-008 branches A–H apply with their exact error
  copy; applied coupons show as a removable chip ("CODE ✕") whose removal
  reverts totals.
- **PY-021 — Gift card entry.** A gift card code may be entered in the same
  input; the system detects the type and applies gift-card logic
  (`Cart.gift_card`). One gift card per order; a second gift card is rejected
  with "Only one gift card may be applied per order." Balance display follows
  UF-009 (full or partial redemption; partial shows remaining amount to pay).
  `[DECIDED #2 — unified code system; DECIDED UF-H]`
- **PY-022 — Zero-total orders.** If coupon + gift card cover the entire total
  (UF-009 Branch A), the payment-method section collapses to "No payment
  needed" and Pay-now finalizes without creating a PaymentIntent; the order is
  recorded with a zero-amount charge path. `[screenshot: UF-009 Branch A]`
  **Implementation gap note (16:G2):** current `begin_checkout` always creates
  an intent; Developer must add the zero-total branch.

---

## 6. Thank-you page (TH-xxx)

- **TH-001 — URL and access.** `/orders/<public_order_id>/thank-you/` (per
  `storefront/urls.py` Phase-5 plan). `public_order_id` is the order number
  plus an unguessable component or signed token; access requires the creating
  session or a valid signature — never enumerable by order number alone (page
  contains PII). Direct hits without access → 404. `[ASSUMPTION — signed
  access mirrors the DECIDED HMAC resume-token pattern]`
- **TH-002 — Confirmation content.** "Thank you, {first name}!" + order
  reference number, item list (snapshots: name, variant, qty, line total —
  `PENDING_CAPTURE` upsell items excluded until promoted), shipping address,
  selected shipping method + estimated delivery (or pre-order date), discount
  lines, shipping, total, tax-included note, payment summary (method family +
  card last4 when available).
- **TH-003 — Email note.** "We've sent a confirmation to {email}." The
  confirmation email itself is enqueued on payment confirmation, not on page
  render (T022; order-confirmation triggers on payment confirmed).
- **TH-004 — Upsell funnel slot.** The `thankyou_funnel` slot renders the
  post-purchase one-click funnel when a `CampaignSession` exists for the order
  and `now < capture_window_expires_at`, using the `upsell_token` returned by
  finalization (never logged). Behavior per ADR-011 / spec 13. On decline,
  expiry, or funnel completion the slot collapses to the standard confirmation.
- **TH-005 — Account-creation CTA (guests).** Card: "Create an account to
  track your order" — email pre-filled, password field, one click. Creates the
  account, links it to the Customer row, and converts the order address into a
  default `CustomerAddress`. Hidden when the shopper is already logged in or
  the account feature is disabled. `[DECIDED — optional account post-purchase]`
- **TH-006 — Storewide next-order offer.** When a post-checkout storewide
  discount campaign is active (UF-013), its block (headline, countdown, coupon
  code, "Shop Now") renders below the confirmation; server-side expiry is
  authoritative.
- **TH-007 — Purchase events, exactly once.** `EVT_PURCHASE` and all purchase
  pixels fire on the **first** thank-you render after payment confirmation,
  guarded by the `FiredPixel` insert-before-fire idempotency table — a reload
  never re-fires. `[design-pattern §X; DECIDED AC-U6]`
- **TH-008 — Continue shopping.** "Continue Shopping" link to the store home
  (in the shopper's checkout language, ML-001).
- **TH-009 — Payment-pending rendering.** The thank-you page renders from
  client-confirm success; if the authorization webhook has not yet landed
  (`payment_status` still PENDING), the page still shows the confirmation
  (order number is already final) — no "pending payment" state is shown for
  the normal webhook lag. `[ASSUMPTION — webhook lag is seconds; blocking the
  page on it harms UX]`

---

## 7. Order finalization flow (technical — OF-xxx)

This section binds the spec to the accepted `begin_checkout` transaction
(`cart/checkout.py`) and extends it where the checkout page needs more.

- **OF-001 — When the Order is created: at Pay-now submit, before payment
  authorization, inside one atomic transaction.** Sequence (all-or-nothing):
  1. Load + validate cart items (active variant, inventory mode, FIXED_QTY
     stock with `select_for_update` + atomic `F()` decrement — oversell
     impossible);
  2. compute subtotal from `CartItem.unit_price` snapshots (stale-price policy
     OF-003);
  3. classify coupon vs gift card (one gift card max);
  4. idempotency pre-check (OF-004);
  5. get-or-create Customer;
  6. pre-flight `preview_route` (fails open on infra errors);
  7. create Order (`payment_status=PENDING`, `fulfillment_status=NOT_SENT`,
     address snapshots, attribution columns from first-touch session UTM);
  8. apply gift card atomically (balance lock + `GiftCardTransaction`);
  9. create OrderItem snapshots;
  10. `route_payment` → processor account (+ DecisionLog);
  11. resolve funnel campaign → vault eligibility → processor customer;
  12. create PaymentIntent (`capture_method='manual'`, post-discount total);
  13. set `capture_window_expires_at` (campaign window, else 7 days);
  14. create `OrderCharge` ORIGINAL/AUTHORIZED;
  15. create `CampaignSession` + upsell token (savepoint — degrades to
      no-funnel on failure);
  16. mark Cart CONVERTED (last step).
  Any raise rolls back everything including inventory and discount decrements.
  `[DECIDED — accepted implementation, ADR-002/006/007/011]`
- **OF-002 — Shipping in finalization (extension).** `begin_checkout` MUST
  accept the selected `shipping_rate_id`, re-resolve zone + rate server-side
  for the submitted country, re-price it (including exceptions and weight
  bands), set `Order.shipping_amount`, and include it in `Order.total` and the
  PaymentIntent amount: `total = max(subtotal − coupon, 0) − gift_card + shipping`
  (free-shipping coupon ⇒ shipping = 0, OF-006). A mismatch between the
  submitted rate and server re-resolution (rate disabled, zone changed,
  address edited) → `CheckoutError` "The shipping method changed. Please review
  and try again." **(16:G1 — not yet implemented.)**
- **OF-003 — Cart→Order copy rules.** Copied: variant snapshots
  (product_name, variant_title, sku, unit_price, quantity, line_total), email,
  phone, address snapshots, currency, coupon/gift-card FKs, attribution
  (`utm_*`, first_referrer, landing_page — first-touch, from session),
  `customer_local_hour`. Re-validated at finalization: variant active,
  inventory, coupon/gift-card validity (atomic), shipping rate (OF-002),
  routing. **Stale price policy:** `CartItem.unit_price` snapshot is honored
  as-is at checkout; catalog price changes between add-to-cart and Pay do not
  reprice the cart. `[ASSUMPTION (LOW) — matches cart model docstring
  ("checkout re-validates stale items" applies to availability, not price);
  repricing mid-checkout is hostile UX]`
- **OF-004 — Idempotency.** Key = SHA-256(session_key | sorted
  (variant_id, qty) pairs), globally unique on `Order.idempotency_key`.
  Duplicate POST (button mash, network retry, back-button re-submit) →
  pre-check or `IntegrityError` savepoint recovery → the request resolves to
  the existing order: the storefront treats "already submitted" for the same
  session as success and redirects to that order's thank-you page rather than
  showing an error. `[DECIDED UF-I mechanism; the redirect-to-thank-you UX on
  duplicate is an ASSUMPTION refining the current error message]`
- **OF-005 — Discount consumption.** Coupon counters decrement atomically
  (conditional UPDATE, rows_affected check — §XIII) inside the transaction;
  gift-card balance decrements under row lock with a `GiftCardTransaction` in
  the same transaction as the order write. Cart-apply (PY-020/021) is advisory
  only. Released/failed authorizations restore counters in the webhook
  transaction (T008 scope).
- **OF-006 — Free-shipping coupon.** A FREE_SHIPPING-type coupon zeroes
  `shipping_amount` at finalization; it does not touch the subtotal.
- **OF-007 — Failed payment.** Client-side confirm failure (decline, 3DS fail)
  leaves: Order `payment_status=PENDING` (webhook may later mark FAILED),
  OrderCharge AUTHORIZED-pending-confirm, Cart CONVERTED. The shopper retries
  on the same order/intent (PY-013). If the shopper abandons after a decline,
  the abandoned-checkout campaign targets them via the captured email; the
  resume link must land them on a payment-retry view for the existing order,
  NOT a fresh cart (the cart is already CONVERTED and the idempotency key
  blocks re-submission). `[PENDING APPROVAL — see U16-2: retry-view vs
  cart-reactivation model]`
- **OF-008 — Cart lifecycle.** `Cart.status=CONVERTED` is set in the same
  transaction as order creation (step 16) — rollback leaves it ACTIVE. A
  CONVERTED cart is never rendered again; a shopper returning to `/cart/`
  post-order gets a fresh empty cart for the session. `[DECIDED — accepted
  implementation]`
- **OF-009 — Order numbering.** `YYYYMMDD-XXXXXXXX` (UUID4-hex suffix),
  unique per store, shown to the customer as the order reference.
  `[DECIDED — accepted implementation]`

---

## 8. Multilingual / locale (ML-xxx)

- **ML-001 — Checkout language.** `/checkout/`, `/cart/` and the thank-you URL
  are never language-prefixed (07 §2). The checkout renders in the shopper's
  **browsing language**: `resolve_locale` middleware records the language of
  each storefront page view in the session; checkout/cart/thank-you read that
  session language, defaulting to the domain's root language
  (`use_path_prefix=False`) when absent. On dedicated single-language domains
  this is a no-op. `[PENDING APPROVAL — see U16-3; without this, a
  `pradize.com/fr/` shopper gets an English checkout]`
- **ML-002 — UI strings.** All checkout/thank-you labels, buttons, section
  titles, validation and payment error messages come from the engine's
  translation catalog for the resolved language (TH-044 mechanism; catalog
  translated via the AiJob CLI pipeline — never direct API). Product/variant
  names in the summary come from published translation records; the shipping
  "service description" customer-facing string requires a per-language variant
  (open item from 06 §1178 — carried as U16-7).
- **ML-003 — Country-adaptive address labels.** Field labels adapt to the
  selected country from a static per-country address-format table: US
  "State"/"ZIP code", CA "Province"/"Postal code", GB "County
  (optional)"/"Postcode", FR/DE no subdivision + "Code postal"/"PLZ", generic
  fallback "Region (optional)"/"Postal code". Labels are translated after
  adaptation (catalog keys per label variant). `[ASSUMPTION — static table,
  no external address-format service]`
- **ML-004 — Currency.** All checkout and thank-you amounts display in the
  store's transactional currency (the `Cart.currency` / default store
  currency). Display-currency conversions available while browsing
  (admin-011 currency module) do **not** apply at checkout; if the shopper was
  browsing in a converted display currency, checkout shows a one-line notice
  "You will be charged in USD" (translated). `[extra-spec + DECIDED FEED-C1
  display-only conversion]` `[ASSUMPTION — the notice]`
- **ML-005 — Emails.** Confirmation email language = the order's checkout
  language (stored on the order at finalization). **Implementation note:** an
  order-language field is required — carried as U16-8 (schema addition).

---

## 9. SEO and indexability (SEO-xxx)

Binding rules already fixed in 07_multilingual_seo.md — restated for this page:

- **SEO-001** — `/checkout/` (all states) renders
  `<meta name="robots" content="noindex">` from the base-template SEO block
  (IDX-001) and is `Disallow`ed in robots.txt (§7 of 07). No canonical tag, no
  hreflang (CAN-021), no structured data (SD-007), excluded from sitemaps
  (SM-030).
- **SEO-002** — Thank-you page: `noindex` + `/orders/` Disallow; contains PII;
  never linked from indexable pages.
- ~~**SEO-003** — Title pattern: "`Checkout — <store name>`" / "`Order
  confirmed — <store name>`", translated (07 §8 utility-page pattern). No SEO
  meta description needed.~~
  **SEO-003 — DECIDED (human, 2026-07-10; separator amendment, see 07_multilingual_seo.md
  META-001):** Title pattern: "`Checkout | <store name>`" / "`Order
  confirmed | <store name>`", translated (07 §9 utility-page pattern, pipe
  separator). No SEO meta description needed.
- **SEO-004** — No hreflang alternates are ever emitted **to** or **from**
  checkout/thank-you URLs (CAN-021).

---

## 10. Permissions, access, integrations (PA-xxx)

- **PA-001 — Store scoping.** Every query is scoped to `request.store`
  (StoreOwnedModel managers): cart, variants, rates (store-owned, referencing
  platform zones/carriers), discount codes, campaigns, processor routing.
  A cart/order from another store is a 404, never a leak.
- **PA-002 — Guest access.** No authentication required at any step. Session
  cookie is the cart key. CSRF protection on every POST.
- **PA-003 — No admin preview.** Checkout is live-only; there is no draft/
  preview mode. Admins test with real (test-mode processor) checkouts.
- **PA-004 — Slots on checkout.** Enabled slots: `security_badge` (CK-010),
  `bump_checkout` (CK-009 — **new slot name to add to the registry**, mirroring
  `bump_product`/`bump_cart`), `consent` + `pixels` (InitiateCheckout /
  AddPaymentInfo events only — no third-party scripts inside the payment
  fields' iframes), `thankyou_funnel` (thank-you only). Suppressed on
  checkout: `overlay`, `social_proof`, `amazon_buy`, `reviews`, AI chat.
  `[screenshot: slot registry + super-admin-11/06]`
- **PA-005 — Secrets hygiene.** `client_secret`, `upsell_token`, vault IDs are
  never logged, never placed in URLs (POST bodies / DOM only), and never
  exposed to slot providers.

---

## 11. Edge cases and error states (EC-xxx)

- **EC-001 — Empty cart** on `/checkout/` load (no cart, zero items, or
  CONVERTED cart) → 302 to `/cart/` (which shows its empty state).
- **EC-002 — Item unavailable at finalization** (variant deactivated,
  SOLD_OUT, or FIXED_QTY stock < qty — race between cart and Pay): transaction
  aborts, no charge; checkout shows the blocking message from the service
  ("\"X\" is no longer available…" / "Only N unit(s) of 'X' are available.")
  with a "Return to cart" CTA to adjust. Nothing is decremented (full
  rollback). `[screenshot: UF-010 edge case; accepted implementation]`
- **EC-003 — No shipping zone/rate for address** → SM-005 blocking error;
  shopper must change the address; Pay disabled.
- **EC-004 — Coupon/gift-card became invalid between apply and Pay**
  (exhausted by a concurrent order, expired at midnight): atomic consumption
  fails → transaction aborts → inline error on the discount line ("This coupon
  code is no longer available.") with the code chip removed; shopper retries
  Pay without it.
- **EC-005 — Payment processor offline.** `preview_route` hard-fail or
  `route_payment` returning no account → "Payment is temporarily unavailable.
  Please try again in a few minutes." + a Retry button that re-runs the
  submit; the order is not created (or is fully rolled back). Routing infra
  hiccups fail open at preview and are caught at routing time.
- **EC-006 — Session expiry mid-checkout.** POST with an expired/rotated
  session → checkout re-resolves the cart by session; if gone, show "Your
  session expired — your items are saved in your cart." and 302 to `/cart/`
  (30-day cart persistence usually preserves the cart; DECIDED UF-E). Entered
  form data is restored from CO-007 when the cart survives.
- **EC-007 — Duplicate submit / back-button after thank-you** → OF-004:
  resolved to the existing order, redirect to its thank-you page (which
  re-fires nothing, TH-007).
- **EC-008 — 3DS redirect never returns** (tab closed mid-challenge): order
  PENDING, intent unconfirmed — handled by OF-007 + abandoned-checkout
  recovery; the watchdog never captures an unconfirmed intent.
- **EC-009 — Currency mismatch** (cart currency ≠ store transactional
  currency — should be impossible; defensive): finalization aborts with a
  generic error and error-level log (§XV-1 loud invisible-failure rule).
- **EC-010 — JS disabled / SDK load failure.** Payment element failing to
  mount shows "Payment cannot be loaded. Please disable content blockers or
  try another browser." — never a silently dead Pay button. `[ASSUMPTION]`
- **EC-011 — Bump item goes unavailable** between render and Pay: it is a
  normal cart item by then → EC-002 path.

---

## 12. Acceptance criteria seeds (for 08_acceptance_tests.md)

1. Empty cart → `/checkout/` redirects to `/cart/` (EC-001).
2. Full happy path (guest, coupon + gift card + shipping) produces one Order
   with `total = subtotal − coupon − gift_card + shipping`, one AUTHORIZED
   OrderCharge, cart CONVERTED, correct snapshots (OF-001/002/003).
3. Double POST of the same cart yields exactly one Order; second request
   redirects to the same thank-you (OF-004 / EC-007).
4. Concurrent checkout of the last FIXED_QTY unit: exactly one succeeds
   (OF-001 step 1).
5. Coupon exhausted concurrently → order rolled back, counter not negative
   (EC-004, §XIII).
6. No rate for destination → Pay disabled + SM-005 message.
7. All methods hidden by preview_route → EC-005 state.
8. Thank-you reload does not duplicate EVT_PURCHASE / pixels (TH-007).
9. Thank-you URL without session/signature → 404 (TH-001).
10. `pradize.com/fr/` shopper sees a French checkout; `pradize.fr` shopper
    likewise; labels adapt CA → "Province" (ML-001/ML-003).
11. `noindex` + robots Disallow on `/checkout/` and thank-you (SEO-001/002).
12. Zero-total order (gift card covers all) completes without a PaymentIntent
    (PY-022).
13. Declined card → same order retried successfully on second attempt, no
    duplicate order (PY-013 / OF-007).
14. Store B's discount code / rate / campaign never applies on store A
    (PA-001).

---

## 13. Open questions

Recorded in `11_uncertainties_to_validate.md` as items **U16-1 … U16-8**:

| # | Level | Question |
|---|---|---|
| U16-1 | LOW | Editing the cart after a failed Pay attempt: idempotency key changes with the cart fingerprint, so a *modified* cart can be re-submitted — but the first PENDING order remains. Recommended: void stale PENDING orders' authorizations via the watchdog after the capture window. |
| U16-2 | MEDIUM — PENDING APPROVAL | Failed-payment recovery model (OF-007): payment-retry view on the existing order (recommended) vs re-activating the CONVERTED cart. |
| U16-3 | MEDIUM — PENDING APPROVAL | Checkout language via session-persisted browsing language (ML-001, recommended) vs language-prefixed checkout URLs (contradicts 07 §2) vs root-language-only checkout. |
| U16-4 | LOW — PENDING APPROVAL | Returning-customer login UX at checkout (CO-005): modal (recommended) vs redirect-and-return. Depends on Phase-5 account pages. |
| U16-5 | LOW — PENDING APPROVAL | Separate billing address toggle (SA-007): v1 copies shipping (recommended) — confirm acceptable for card AVS in target markets. |
| U16-6 | MEDIUM — PENDING APPROVAL | Free-shipping threshold messaging (SM-004): add `min_order_amount` to FREE ShippingRate (recommended) vs descope to cart page vs coupon-only free shipping. |
| U16-7 | MEDIUM | Per-language variant of the shipping-zone "service description" customer-facing string (carried from 06 §1178). |
| U16-8 | LOW | Order needs a `checkout_language` field so confirmation emails render in the shopper's language (ML-005) — schema addition for the Architect. |

Implementation gaps flagged for the Developer (spec is source of truth):
**16:G1** shipping not in `begin_checkout` totals (OF-002); **16:G2** no
zero-total branch (PY-022); **16:G3** `bump_checkout` slot missing from the
slot registry (PA-004).
