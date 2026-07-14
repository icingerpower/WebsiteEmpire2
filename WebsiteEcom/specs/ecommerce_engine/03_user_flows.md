# 03 — User Flows: Storefront End-Customer Journeys

> **Status:** First draft — Spec Agent 2026-07-02.
> All flows are from the perspective of the **end-customer (shopper)** visiting the storefront.
> PENDING items cite the corresponding uncertainty number from `11_uncertainties_to_validate.md`.

---

## Tracking event legend

The following canonical tracking events are referenced throughout the flows.
Each pixel provider (Facebook, Google Analytics Enhanced Ecommerce, Pinterest, TikTok) fires
the equivalent native event. Implementation details belong to the pixels spec (see `admin-018`).

| Event ID | Description |
|---|---|
| `EVT_PAGE_VIEW` | Any storefront page loaded |
| `EVT_COLLECTION_IMPRESSION` | Product card rendered in viewport on a collection page |
| `EVT_PRODUCT_CTR` | Shopper clicks a product card in a collection |
| `EVT_PRODUCT_VIEW` | Product detail page fully loaded |
| `EVT_VARIANT_SELECT` | Shopper selects a variant option |
| `EVT_ADD_TO_CART` | Item successfully added to cart |
| `EVT_INITIATE_CHECKOUT` | Shopper enters the checkout flow |
| `EVT_ADD_PAYMENT_INFO` | Payment fields entered/selected |
| `EVT_PURCHASE` | Order confirmed (fires once, deduped on thank-you reload) |
| `EVT_LEAD_CAPTURE` | Email collected via overlay |
| `EVT_REVIEW_SUBMITTED` | Customer submits a review |
| `EVT_SCROLL_DEPTH` | Scroll % milestone reached on a page (25 / 50 / 75 / 100 %) |
| `EVT_UPSELL_IMPRESSION` | Post-purchase upsell offer rendered |
| `EVT_UPSELL_ACCEPTED` | Shopper accepts a post-purchase upsell |
| `EVT_UPSELL_DECLINED` | Shopper declines a post-purchase upsell |

---

## UF-001: Browse collection page

**Actor:** end-customer (shopper)
**Preconditions:**
- Shopper navigates to a collection URL (e.g. `/dresses`, `/collection/sale`).
- Collection is published and contains at least one published product.

**Steps:**

1. Browser requests the collection URL.
2. System renders the collection page: hero image (if set), collection title, description, and a grid of product cards.
   Each product card shows: product image, product title, price (and compare-at price if set and higher), star-rating summary (average + count, hidden if zero reviews and `hide widget` is configured).
3. System fires `EVT_PAGE_VIEW` (page = collection URL).
4. As each product card enters the viewport, system fires `EVT_COLLECTION_IMPRESSION` (product_id, collection_id, position_in_grid).
5. System records scroll-depth milestones (25 / 50 / 75 / 100 %) and fires `EVT_SCROLL_DEPTH` (page = collection, depth = milestone %). These feed the *Avg % scroll* metric in the Sales by Collection report.
6. Shopper scrolls, browses, and optionally uses filters or sorting controls (if available on this collection).
   - IF shopper clicks a product card:
     - System records the click for CTR tracking (numerator of Collection CTR and % Product CTR metrics).
     - Browser navigates to the product page → continue to **UF-002** or **UF-003/UF-004** as applicable.
   - IF shopper leaves without clicking any product → session contributes to *Display* count but not to CTR.

**Postconditions:**
- `EVT_COLLECTION_IMPRESSION` events recorded for all products that entered viewport.
- Scroll-depth events recorded.
- If the shopper clicked a product, that click is attributed to this collection for CTR and purchase-rate calculations.

**Edge cases:**
- Collection has no published products → system renders empty-state message (e.g. "No products available").
- Shopper navigates directly via a URL with no referrer → referrer recorded as "Direct".
- Multi-currency enabled: prices displayed in the shopper's selected currency (see UF-020). — PENDING (uncertainty 9: display-only vs transactional conversion).
- Collection URL has changed and old slug has a redirect → system issues HTTP 301 to new URL; shopper lands on collection transparently.
- `EVT_COLLECTION_IMPRESSION` must not fire for cards that are rendered but never scroll into viewport (requires IntersectionObserver or equivalent).

---

## UF-002: View product page — standard

**Actor:** end-customer (shopper)
**Preconditions:**
- Product is published, has at least one variant with inventory mode set to anything other than SOLD OUT.
- Shopper arrives from a collection page, a search result, a direct link, or a social/ad referral.

**Steps:**

1. Browser requests the product URL (e.g. `/product/vivienne-silver-fairycore-ethereal-dress-gown`).
2. System renders the product page with:
   - Image gallery (numbered, swipeable; first image displayed by default).
   - Product title, star-rating summary (average + review count; hidden if zero reviews and admin configured *hide widget*).
   - Price (and compare-at / strikethrough price if applicable).
   - Product description tab(s) (e.g. Description, Shipping, Care — as configured in Product Page Settings).
   - Variant selector(s): each option rendered as text dropdown or thumbnail selector per admin configuration.
   - Quantity selector (default 1).
   - "Add to Cart" CTA button (enabled for in-stock inventory modes).
   - Security badge widget (if enabled in admin).
   - Reviews section (star breakdown, review list with photos, paginated; "Write a Review" button).
   - Related products carousel (if configured — see UF-011).
   - Order bump(s) on the product page (if configured — see UF-006).
3. System fires `EVT_PAGE_VIEW` + `EVT_PRODUCT_VIEW` (product_id, variant_id = default variant, price, currency).
4. System fires `EVT_SCROLL_DEPTH` milestones as shopper scrolls.
5. IF Recent Purchase Notifications are enabled and purchases exist within the configured time window:
   - After the configured delay (default 2 s), system shows a social-proof popup: "[First name], [City] purchased [Product name] [N minutes ago]". — PENDING (uncertainty: what buyer data is publicly shown — first name only? city? — GDPR impact; see Part C `admin-014`).
   - Popup auto-dismisses; next popup fires after the configured interval.
6. Shopper selects a variant (e.g. size "US-6"):
   - System updates the displayed image(s) to match the variant (if *Changes Product Look* is set for this option).
   - System fires `EVT_VARIANT_SELECT` (product_id, option_name, option_value).
   - IF selected variant has a different price → price display updates.
7. Shopper sets quantity and clicks "Add to Cart" → continue to **UF-006**.

**Postconditions:**
- `EVT_PRODUCT_VIEW` recorded for the loaded variant.
- Variant selection recorded.
- Shopper proceeds to cart or continues browsing.

**Edge cases:**
- No variant selected before "Add to Cart" → system highlights the unselected option selector with an error message; does not add to cart.
- Product has only a single variant (no options) → variant selectors are hidden; shopper sees just the price and "Add to Cart".
- Compare-at price equals or is lower than sale price → compare-at is NOT displayed.
- Lead-capture overlay triggers (exit-intent or timer) while shopper is on the product page → **UF-019** branch runs in parallel.
- Product has a timer promo active today → countdown timer displayed with the promotional price/discount. — PENDING (timer promo feature: see `admin-021-edit-product-page-02` annotation; no decision recorded yet — **NEW CRITICAL UNCERTAINTY #A** below).

---

## UF-003: View product page — out of stock / sold out / ask-when-available / ask-for-quotation (B2B)

**Actor:** end-customer (shopper)
**Preconditions:**
- Product is published.
- The active inventory mode for the shopper's requested variant is one of: SOLD OUT / no inventory tracking (displayed as unavailable) / ask-when-available / ask-for-quotation.

**Steps:**

**Branch A — SOLD OUT (hard block, always sold out regardless of real inventory):**

1. System renders the product page as in UF-002 steps 1–5.
2. "Add to Cart" button is replaced with a "Sold Out" badge/button (disabled, not clickable).
3. System fires `EVT_PAGE_VIEW` + `EVT_PRODUCT_VIEW`.
4. Shopper cannot add the product to cart. Shopper may browse other content or leave.

**Branch B — No inventory tracking / fixed quantity exhausted:**

1. System renders the product page.
2. If fixed quantity = 0 (or tracking shows zero stock), system treats this as Branch A.
3. IF the variant has quantity remaining → shopper can add to cart normally (UF-006).
   - IF fake/server-assigned inventory: all sessions see the same displayed quantity (assigned server-side; does not decrement per add-to-cart). — PENDING (exact fake-inventory UX on product page: show "X left in stock" label? **NEW UNCERTAINTY #B** below).

**Branch C — Ask When Available:**

1. System renders the product page; "Add to Cart" replaced with "Notify Me When Available" button.
2. System fires `EVT_PAGE_VIEW` + `EVT_PRODUCT_VIEW`.
3. Shopper clicks "Notify Me When Available":
   - System displays an inline email input form: "Enter your email to be notified".
4. Shopper enters email and submits:
   - System validates the email format.
   - IF valid: system stores (product_id, variant_id, email) in a waitlist table and displays a confirmation message: "You will be notified when this item is back in stock."
   - IF invalid: system shows inline validation error; shopper corrects and resubmits.
5. System does not add the item to cart.

**Branch D — Ask for Quotation (B2B):**

1. System renders the product page; "Add to Cart" replaced with "Request a Quotation" button.
   - If the product has a "Price starting at" value configured, it is displayed (e.g. "Starting from $149").
2. System fires `EVT_PAGE_VIEW` + `EVT_PRODUCT_VIEW`.
3. Shopper clicks "Request a Quotation":
   - System displays a quotation request form: Name, Email, Company (optional), ~~Quantity needed,~~ Message (optional). **DECIDED (human, 2026-07-10, 18:P2):** quantity is NOT displayed or collected on quotation forms — quotation-mode products are not cartable, so a requested quantity is not meaningful at request time. `QuotationRequest.quantity` model field is retained (default 1) for potential admin-side use only.
4. Shopper fills out the form and submits:
   - System validates required fields.
   - IF valid: system stores the quotation request and sends a notification email to the store admin; displays a confirmation message to the shopper.
   - IF invalid: system shows inline field-level errors.
5. System does not add the item to cart.

**Postconditions:**
- For Branch C: waitlist entry created; shopper receives confirmation.
- For Branch D: quotation request stored; admin notified; shopper receives confirmation.
- For Branches A/B (sold out): `EVT_PRODUCT_VIEW` recorded but no `EVT_ADD_TO_CART`.

**Edge cases:**
- Shopper submits the "Notify Me" form with the same email twice for the same variant → system deduplicates silently (no error; same confirmation shown).
- Shopper submits a quotation request without JavaScript (form must still work via standard POST). — PENDING design decision.
- Multi-variant product where some variants are sold out and others are not → sold-out variants are visually marked (e.g. strikethrough or "Sold Out" badge on the thumbnail/option) but the shopper can still select and add available variants.

---

## UF-004: View product page — pre-order (date to receive shown)

**Actor:** end-customer (shopper)
**Preconditions:**
- Product is published with inventory mode set to *Presale* (pre-order) with a configured expected-receipt date.

**Steps:**

1. System renders the product page with:
   - All standard elements from UF-002.
   - A prominent pre-order notice displayed near the CTA: "Pre-order — Expected to ship by [DATE]".
   - "Add to Cart" button label changed to "Pre-Order Now".
2. System fires `EVT_PAGE_VIEW` + `EVT_PRODUCT_VIEW`.
3. Shopper selects variant and quantity.
4. Shopper clicks "Pre-Order Now":
   - System adds the item to the cart with a pre-order flag and the expected-receipt date stored in the line item.
   - System fires `EVT_ADD_TO_CART` (product_id, variant_id, quantity, price, is_preorder=true).
   - Cart display and all subsequent checkout steps show the expected date alongside the pre-ordered item.
5. Shopper proceeds to checkout → UF-007 (checkout flow is identical; the confirmation email notes the pre-order date — see UF-015).

**Postconditions:**
- Pre-order item in cart with expected-receipt date.
- Order placed normally; fulfillment happens when stock is received.

**Edge cases:**
- Cart contains both pre-order and in-stock items → checkout proceeds; confirmation email clearly distinguishes items by availability.
- Expected-receipt date changes after order is placed → admin updates the order; customer notification email is out of scope of this flow (admin-triggered transactional email).
- Pre-order date is in the past (admin error) → system still shows the date as configured; admin must correct. — PENDING: whether engine should warn or block on a past pre-order date (NEW UNCERTAINTY #C).

---

## UF-005: View product page — multi-variant A/B version

**Actor:** end-customer (shopper)
**Preconditions:**
- The product has the *multi-variant page* feature enabled (multiple URL versions with different images).
- At least two page versions are configured for this product (e.g. version A at `/product/dress-blue`, version B at `/product/dress-blue-model2`).
- Basic A/B stat tracking is enabled.

**Steps:**

1. Shopper arrives at one of the product's variant URLs (e.g. via a Pinterest pin targeting version B).
2. System renders the product page for that specific URL version:
   - The images shown are those assigned to this URL version (not the default variant images).
   - All other product content (title, price, description, variants, CTA) is identical across versions.
3. System fires `EVT_PAGE_VIEW` + `EVT_PRODUCT_VIEW`, recording the specific page version/URL in the analytics event so that per-version views can be compared.
4. All shopper interactions (variant selection, add-to-cart) proceed identically to UF-002.
5. IF the shopper completes a purchase originating from this version's URL, the `EVT_PURCHASE` event is also attributed to this version for conversion-rate comparison.

**Postconditions:**
- View, add-to-cart, and purchase events are attributable per page version/URL.
- Admin can compare conversion metrics between versions to determine the winning image set.

**Edge cases:**
- Search engines may index multiple URLs for the same product → canonical `<link rel="canonical">` on all versions should point to the primary version. — PENDING: exact canonical strategy (NEW UNCERTAINTY #D).
- A version URL is shared without the feature being enabled → system falls back to normal product page rendering; no A/B tracking fires.
- The product is out of stock on one version page → sold-out state applies uniformly across all versions (stock is shared).

---

## UF-006: Add to cart / mini cart / floating cart interaction

**Actor:** end-customer (shopper)
**Preconditions:**
- Shopper is on a product page (UF-002 or UF-004) with a valid variant selected.
- The selected variant is not sold out.

**Steps:**

1. Shopper clicks "Add to Cart" (or "Pre-Order Now").
2. System validates the selection (variant selected, quantity ≥ 1).
   - IF validation fails → system highlights the missing field; does not proceed.
3. System adds the item to the cart (session-based; persisted for returning visitors).
4. System fires `EVT_ADD_TO_CART` (product_id, variant_id, quantity, unit_price, currency).
5. System opens the **floating cart popup** (mini-cart):
   - Displays: all cart line items (image thumbnail, title, variant, quantity stepper, line total), cart subtotal, and a "Checkout" CTA.
   - IF **order bumps configured for the Floating Cart** for the just-added product:
     - Bump offer(s) appear inside the floating cart: product image, brief description, bump price, "Add to Order" checkbox or button.
     - IF shopper checks/clicks the bump:
       - System fires `EVT_ADD_TO_CART` for the bump product.
       - Bump is added to the cart; cart totals update.
     - IF shopper ignores the bump → bump is not added.
6. Shopper options from the floating cart:
   - **Continue shopping:** closes the popup; shopper is back on the product page.
   - **Go to cart page:** navigates to `/cart` (full cart view).
   - **Checkout:** skips to UF-007.
7. IF shopper navigates to the cart page:
   - System renders full cart: all line items with quantity edit and remove controls, subtotal.
   - IF **order bumps configured for the Cart/Checkout page**:
     - Bump offer(s) displayed as a highlighted block (e.g. "Customers who bought this also got…") with "Add to Order" CTA.
     - Bump interaction as per step 5 above.
   - Shopper adjusts quantities, removes items, or proceeds to checkout (UF-007).

**Postconditions:**
- Cart updated with the added item(s) and any accepted bumps.
- `EVT_ADD_TO_CART` fired for each item added (including bumps).

**Edge cases:**
- Shopper adds the same variant twice → system increments the quantity of the existing line item (does not create a duplicate line).
- Shopper removes all items from the cart → cart is empty; checkout button disabled or hidden.
- Bump product is out of stock → bump is not displayed.
- Cart is not yet visible (shopper has no session) → system creates a new cart session on first `EVT_ADD_TO_CART`.
- Shopper returns to the store after closing the browser → system restores the cart from the persisted session. — PENDING: cart session persistence duration (NEW UNCERTAINTY #E).

---

## UF-007: Reach checkout — address entry (abandoned-checkout timer starts)

**Actor:** end-customer (shopper)
**Preconditions:**
- Shopper has at least one item in the cart.
- Shopper clicks "Checkout" from cart or floating cart.

**Steps:**

1. System renders the checkout page.
2. System fires `EVT_INITIATE_CHECKOUT` (cart value, item count, currency).
3. **Checkout form — Contact:**
   - Email address input (required). If shopper is a returning customer who previously checked out, the email field may pre-fill. — PENDING: account-based vs guest-only checkout model (NEW UNCERTAINTY #F).
4. **Abandoned-checkout timer starts** at this point (email address captured = abandonment trigger).
   - IF the shopper enters their email and then closes the browser/tab without completing payment, the abandoned-checkout campaign will fire after the configured delay (see UF-016).
5. **Checkout form — Shipping address:**
   - First name, Last name, Address line 1 + 2, City, State/Province (conditional on country), ZIP/Postal code, Country (dropdown, default set by geo-IP or store default).
   - Address autocomplete powered by Google Maps Places API (or equivalent). As shopper types, system suggests matching addresses; shopper selects a suggestion to auto-fill the form fields.
6. Shopper reviews or corrects the auto-filled fields and continues.
7. **Checkout form — Shipping method:**
   - System calculates available shipping options based on the delivery address (country/state/zip) and cart contents (weight if applicable).
   - Shopper selects a shipping method.
8. **Checkout form — Payment:**
   - Payment method options displayed (e.g. Credit Card via Stripe, PayPal).
   - IF Credit Card selected: card number, expiry, CVV inputs rendered by the processor's SDK.
   - IF PayPal selected: shopper redirected to PayPal; on return, checkout continues.
   - System fires `EVT_ADD_PAYMENT_INFO` when payment details are entered.
9. IF **order bumps configured for the Checkout Page** are present:
   - Bump offer(s) displayed in the checkout sidebar or inline before payment confirmation.
   - Shopper interaction as in UF-006 step 5.
10. Shopper reviews the order summary (items, shipping, discount if applied, total) and clicks "Place Order" → **UF-010**, **UF-011**, or **UF-012** depending on upsell configuration.

**Postconditions:**
- Checkout flow initiated; abandoned-checkout record created for this shopper.
- If the shopper completes the order, the abandoned-checkout record is marked as recovered (no email sent).

**Edge cases:**
- Shopper enters an invalid address (not recognized by Google Maps) → system allows free-text entry; no auto-complete forced.
- No shipping methods available for the delivery country → system displays an error message; shopper cannot proceed. — PENDING: exact UX copy (assumption: "We don't ship to this location. Please contact us.").
- Shopper applies a coupon → **UF-008** branch.
- Shopper applies a gift card → **UF-009** branch.
- Cart items include a pre-order product → expected-receipt date shown in order summary.
- Prices shown are always tax-inclusive; no tax line shown (confirmed decision).
- Checkout page is abandoned (no payment submitted) → UF-016 triggers asynchronously.

---

## UF-008: Apply coupon code at checkout

**Actor:** end-customer (shopper)
**Preconditions:**
- Shopper is on the checkout page (UF-007, step 7–10 range).
- A "Coupon code" input and "Apply" button are visible in the order summary panel.

**Steps:**

1. Shopper locates the coupon input field.
2. Shopper types or pastes a coupon code and clicks "Apply".
3. System looks up the `DiscountCode` record by code (case-insensitive).

**Branch A — Valid coupon, all conditions met:**

4. System checks: code exists, status active, within validity dates, usage count < total limit, current shopper usage < per-customer limit, cart meets all configured rules (minimum order value, applicable products/collections).
5. All checks pass.
6. System applies the discount:
   - **$ off from order total:** reduces the cart subtotal by the fixed amount (cannot reduce below $0).
   - **% off from order total:** reduces the cart subtotal by the percentage.
   - **Free shipping:** removes or zeroes the selected shipping cost.
   - **Free product:** (PENDING — not yet confirmed in discount engine; see uncertainty in `admin-019-coupons-03`) — **PENDING**.
7. System displays the discount line in the order summary: e.g. "Coupon CODE123 — −$10.00".
8. Order total updates.
9. Shopper proceeds to payment and places the order.
10. On order placement: system increments the coupon's usage counter (total and per-customer).

**Branch B — Expired coupon:**

4. Code exists but `valid_until` date has passed.
5. System displays inline error: "This coupon code has expired."
6. Discount not applied. Shopper may try another code.

**Branch C — Used-up coupon (total limit exhausted):**

4. Code exists but usage count ≥ total limit.
5. System displays inline error: "This coupon code is no longer available."
6. Discount not applied.

**Branch D — Per-customer limit already reached:**

4. Code valid globally, but this shopper (identified by email) has already used it the maximum number of times.
5. System displays inline error: "You have already used this coupon code."
6. Discount not applied. — PENDING: guest identity enforcement (email-based; reliable only if shopper enters email before applying coupon).

**Branch E — Code does not exist:**

4. System displays inline error: "Invalid coupon code. Please check and try again."

**Branch F — Minimum order value not met:**

4. Code exists and is valid, but the cart subtotal is below the configured minimum.
5. System displays inline error: "A minimum order of $[X] is required for this coupon."

**Branch G — Wrong product/collection applicability rule:**

4. Code exists and is valid, but no item in the cart matches the configured product or collection restriction.
5. System displays inline error: "This coupon does not apply to the items in your cart."

**Branch H — Coupon conflict (stacking not allowed):**

4. Shopper already has another coupon applied and attempts to add a second. The first or second coupon has "Can be combined with other coupon codes" unchecked.
5. System displays inline error: "This coupon cannot be combined with other coupon codes."

**Postconditions:**
- If Branch A: discount applied, usage counter incremented on order placement.
- All other branches: no discount applied; shopper can retry with a different code.

**Edge cases:**
- Shopper removes a coupon code after applying it → discount is removed; order total reverts.
- Coupon is a gift card code entered in the coupon field (shared code namespace) → system identifies it as a gift card and applies gift card logic instead (or prompts shopper to use the gift card field).
- The "free product" discount type — PENDING decision.

---

## UF-009: Apply gift card code at checkout

**Actor:** end-customer (shopper)
**Preconditions:**
- Shopper is on the checkout page.
- Shopper has a gift card code (received by email, as part of a promotion, or purchased as a gift card product).
- A "Gift card code" input and "Apply" button are visible (distinct from the coupon field, or combined — PENDING: whether one field handles both code types).

**Steps:**

1. Shopper enters the gift card code and clicks "Apply".
2. System looks up the `DiscountCode` record (type = gift card).
3. System checks: code exists, status = unused or partially used, not expired, remaining balance > $0.

**Branch A — Full redemption (gift card balance ≥ order total):**

4. System deducts the full order total from the gift card balance.
5. System records a `GiftCardTransaction` (card_id, order_id, amount_redeemed = order total, balance_before, balance_after).
6. System displays in the order summary: "Gift card [last 4 chars] — −$[ORDER TOTAL]".
7. Order total becomes $0.00; no payment method entry required.
8. Shopper clicks "Place Order" → order confirmed with zero charge; gift card balance updated.

**Branch B — Partial redemption (gift card balance < order total):**

4. System deducts the full remaining gift card balance from the order total.
5. System records a `GiftCardTransaction` (balance_after = 0).
6. System displays in the order summary: "Gift card [last 4 chars] — −$[GIFT_CARD_BALANCE]".
7. Remaining amount to pay = order total − gift card balance.
8. System prompts shopper to select a payment method for the remaining amount.
9. Shopper enters payment details (credit card or PayPal) for the outstanding balance.
10. Shopper places the order; payment processor charges only the remaining amount.

**Branch C — Expired or depleted gift card:**

4. Code is expired or balance = $0.
5. System displays inline error: "This gift card has expired" or "This gift card has no remaining balance."

**Branch D — Invalid gift card code:**

4. Code not found.
5. System displays inline error: "Invalid gift card code."

**Postconditions:**
- Gift card transaction recorded.
- Remaining balance available for future use (Branch B).
- Order placed; processor charged for remainder only (Branch B) or zero (Branch A).

**Edge cases:**
- Shopper applies both a coupon code and a gift card → PENDING: order of discount application (coupon first, then gift card applied to the reduced total? — **NEW UNCERTAINTY #G**).
- Shopper applies two gift card codes → PENDING: whether multiple gift cards can be stacked on one order (**NEW UNCERTAINTY #H**).
- Gift card currency differs from store default currency → PENDING (uncertainty 3 in `11_uncertainties_to_validate.md`).
- Gift card balance covers the order but not shipping → Branch B logic applies (gift card covers merchandise, shopper pays shipping separately). — PENDING: exact proration logic.

---

## UF-010: Complete purchase — standard (no upsell)

**Actor:** end-customer (shopper)
**Preconditions:**
- Shopper has completed the checkout form (UF-007): valid address, shipping method selected, payment entered.
- No post-purchase upsell is configured for any item in the cart.

**Steps:**

1. Shopper reviews the final order summary and clicks "Place Order" (or "Pay Now").
2. System submits the payment to the processor (Stripe or PayPal).
3. **IF payment is authorized:**
   a. System creates the order record (status: PAID; fulfillment status: NOT SENT TO FULFILLMENT).
   b. System marks the abandoned-checkout record for this shopper as recovered (suppresses abandonment emails).
   c. System increments coupon usage counter (if a coupon was applied).
   d. System decrements inventory (if inventory mode is Fixed Quantity and tracking is enabled).
   e. System fires `EVT_PURCHASE` (order_id, revenue = order total, items, currency). Event is deduplicated: if the shopper reloads the thank-you page, `EVT_PURCHASE` is NOT fired again.
   f. System redirects shopper to the **Thank-you / Order Confirmation page**.
   g. System enqueues a confirmation email → **UF-015**.
4. The order confirmation page displays: order number, items ordered, shipping address, estimated delivery (or pre-order date if applicable), payment summary, and a "Continue Shopping" link.

**Branch — Payment declined:**

3. Processor returns a decline.
4. System displays an inline error: "Your payment was declined. Please check your card details or try a different payment method."
5. Shopper can re-enter payment details or switch payment method; order is NOT created.

**Postconditions:**
- Order created in PAID state.
- `EVT_PURCHASE` fired once.
- Confirmation email enqueued.
- Abandoned-checkout flag cleared.

**Edge cases:**
- Network timeout between "Place Order" and processor response → system must handle idempotency (do not double-charge). — PENDING: exact idempotency key strategy (NEW UNCERTAINTY #I).
- Shopper navigates back after seeing the thank-you page and clicks "Place Order" again → idempotency must prevent duplicate orders.
- Inventory runs out between add-to-cart and payment submission (race condition on Fixed Quantity mode) → system detects zero stock at payment time; returns error "One or more items are no longer available"; does not charge the shopper.

---

## UF-011: Complete purchase — with before-checkout related product recommendation

**Actor:** end-customer (shopper)
**Preconditions:**
- At least one product in the cart has related products configured.
- The store has the "Related Products" feature enabled and a template that renders a related-products block on the cart or checkout page.

**Steps:**

1. Before shopper reaches the payment step, the checkout (or cart) page displays a "You may also like" or "Frequently bought together" section populated with the related products configured for the cart's items.
2. Each related product card shows: image, title, price, "Add to Cart" button.
3. Shopper reviews the recommendation:
   - **IF shopper adds a related product:**
     - System fires `EVT_ADD_TO_CART` for the related product.
     - Cart totals update.
     - Checkout flow continues with the additional item.
   - **IF shopper ignores the recommendation:** checkout continues as UF-010.
4. Shopper proceeds through UF-007 steps 7–10 and completes the order (UF-010).

**Postconditions:**
- Related-product impression tracked (recommendation was shown).
- `EVT_ADD_TO_CART` fired if the shopper added the related product.
- Order placed with or without the additional item.

**Edge cases:**
- Related product is out of stock → not displayed in the recommendation block.
- Related product is added, bringing the cart over a coupon's minimum order threshold → discount auto-applies if it was previously blocked by the minimum.

---

## UF-012: Complete purchase — with after-checkout one-click upsell (post-purchase offer on thank-you page)

**Actor:** end-customer (shopper)
**Preconditions:**
- An upsell campaign is configured for one or more products in the completed order.
- The store supports one-click post-purchase upsell (payment token reuse). — PENDING: one-click upsell payment model (uncertainty 5 in `11_uncertainties_to_validate.md` — CRITICAL, blocks implementation).
- Shopper has just completed the purchase and landed on the thank-you page.

**Steps:**

1. `EVT_PURCHASE` fires (order confirmed, UF-010 step 3e).
2. Instead of (or in addition to) the standard thank-you content, the thank-you page renders a **post-purchase upsell offer**:
   - Headline (e.g. "Wait — a special offer just for you!").
   - Upsell product: image, title, benefit copy, upsell price.
   - Timer countdown (if configured for this upsell). — PENDING (timer-on-upsell feature: see annotation in `admin-021-edit-product-page-02`).
   - Two CTAs: "Yes, add to my order" and "No thanks, I'll pass".
3. System fires `EVT_UPSELL_IMPRESSION` (upsell_product_id, order_id, upsell_price).

**Branch A — Shopper accepts the upsell:**

4. Shopper clicks "Yes, add to my order".
5. System charges the upsell product price using the stored payment token from the original order (no re-entry of card details). — PENDING (uncertainty 5).
6. System creates a new order line or a separate order for the upsell product.
7. System fires `EVT_UPSELL_ACCEPTED` (upsell_product_id, order_id, revenue = upsell_price).
8. System displays confirmation: "Added to your order! You will receive a separate confirmation."
9. System enqueues a supplementary confirmation email for the upsell item.
10. Thank-you page transitions to standard order confirmation view.

**Branch B — Shopper declines the upsell:**

4. Shopper clicks "No thanks" or the timer expires.
5. System fires `EVT_UPSELL_DECLINED` (upsell_product_id, order_id).
6. Thank-you page transitions to standard order confirmation view.
7. No charge; original order unmodified.

**Postconditions:**
- Upsell accepted: additional charge processed; new order/line created; supplementary email sent.
- Upsell declined: original order complete; no additional charge.

**Edge cases:**
- Upsell product is out of stock by the time the shopper accepts → system displays error; upsell cancelled; original order unaffected.
- Payment token has expired (e.g. card expired between original purchase and thank-you page) → system cannot use one-click upsell; falls back to presenting the upsell item as "add to a new cart" or skips the upsell. — PENDING (uncertainty 5).
- Multiple upsell campaigns configured for the same order → PENDING: whether multiple sequential upsell offers are shown or only one (**NEW UNCERTAINTY #J**).

---

## UF-013: Complete purchase — with storewide timed discount offer after checkout

**Actor:** end-customer (shopper)
**Preconditions:**
- A storewide timed discount offer (post-checkout incentive) is configured (e.g. "10% off your next order, valid for 24 hours").
- This is distinct from a one-click upsell (UF-012) — it does not charge immediately; it issues a coupon for the shopper's next order.

**Steps:**

1. Shopper completes the order and lands on the thank-you page.
2. `EVT_PURCHASE` fires.
3. System displays a timed discount offer block on the thank-you page:
   - Headline: "Thank you! Here's an exclusive offer: 10% off your next order".
   - Countdown timer (e.g. 24 hours).
   - Coupon code displayed prominently (e.g. "THANKYOU10").
   - CTA: "Shop Now".
4. Shopper notes the code (or copies it).
5. Shopper clicks "Shop Now" → navigates to the store (collection page or home).
6. IF shopper uses the code within the validity window → UF-008 Branch A applies on the next order.
7. IF timer expires before the shopper uses the code → the coupon's `valid_until` date prevents redemption (UF-008 Branch B).

**Postconditions:**
- Storewide coupon displayed.
- Coupon redeemable on the shopper's next purchase within the validity window.

**Edge cases:**
- Shopper screenshots the code and uses it after the timer expires → server-side expiry enforced (UI timer is informational only).
- The storewide coupon is also available publicly (e.g. in an email blast) → usage limits apply normally.

---

## UF-014: Complete purchase with gift card payment

**Actor:** end-customer (shopper)
**Preconditions:**
- Shopper has applied a gift card code at checkout (UF-009 Branch A or B).
- Order placement is triggered.

**Steps:**

*This flow is identical to UF-010 except for the payment processing step:*

1. Shopper clicks "Place Order".
2. **IF gift card covers 100% of the order total (Branch A from UF-009):**
   - System creates the order without sending a charge to the payment processor.
   - System records the `GiftCardTransaction` (balance → $0 or residual).
   - Order status: PAID.
   - `EVT_PURCHASE` fires normally.
3. **IF gift card covers a partial amount (Branch B from UF-009):**
   - System splits the charge: records the gift card deduction internally, then submits the remaining balance to the processor (Stripe/PayPal).
   - On successful charge: order status = PAID; both the gift card transaction and the processor charge are recorded.
   - On processor decline: the gift card deduction is rolled back; order is not created; shopper sees a payment error (same as UF-010 decline branch).
4. System redirects to thank-you page.
5. Confirmation email sent (UF-015) — email notes the gift card was used.

**Postconditions:**
- Order created in PAID state.
- Gift card balance decremented.
- Processor charged only for the remainder (if any).

**Edge cases:**
- See UF-009 edge cases.
- Refund of a gift-card-paid order → PENDING: refund back to gift card balance vs original processor vs new gift card issued (CRITICAL money decision — **NEW UNCERTAINTY #K**).

---

## UF-015: Receive order confirmation email

**Actor:** end-customer (shopper)
**Preconditions:**
- An order has been successfully placed (UF-010 through UF-014).
- The confirmation email template is configured and the email infrastructure is operational.

**Steps:**

1. System enqueues a transactional confirmation email immediately after order creation.
2. System sends the email to the shopper's email address used at checkout.
3. Email contains:
   - Subject: e.g. "Your Pradize order #3112 is confirmed".
   - Store name and logo.
   - Order number.
   - List of ordered items: product image, title, variant, quantity, unit price, line total.
   - Shipping address.
   - Shipping method.
   - Payment summary: subtotal, discount (if coupon/gift card applied), shipping cost, **total** (tax-inclusive; no tax breakdown shown — confirmed decision).
   - IF any item is a pre-order: expected-receipt date displayed alongside that item.
   - Unsubscribe link (transactional email — required; scope limited to order notifications). — PENDING: unsubscribe scope for transactional vs marketing emails.
4. Shopper receives and opens the email.
5. Email body is rendered correctly; all links (product links, store link) are valid.

**Postconditions:**
- Shopper has a written record of the order.
- No tracking events fire from this email (tracking events already fired at order placement).

**Edge cases:**
- Shopper's email bounces (invalid address) → system records the bounce; admin is notified. — PENDING: bounce handling workflow.
- Multi-language store: email is sent in the language of the domain the shopper checked out on. — PENDING (uncertainty 9 in `11_uncertainties_to_validate.md`: domain/language/country model).
- Gift card used: email states "Paid with gift card [code last 4 chars] (−$X)" and remaining balance if partial.

---

## UF-016: Receive abandoned-checkout email (with resume-cart link)

**Actor:** end-customer (shopper)
**Preconditions:**
- Shopper entered their email address at checkout (UF-007 step 3) but did not complete the purchase.
- At least one active abandoned-checkout campaign is configured.
- The configured inactivity timeout has elapsed.
- Shopper has not opted out of marketing/abandoned-cart emails.

**Steps:**

1. System detects inactivity: the shopper's checkout session has no order created after the configured delay from email capture. — PENDING: exact definition of "abandoned" (inactivity period, which step triggers capture — see uncertainty in Part B `admin-008`).
2. System enqueues the first automated email in the campaign sequence.
3. System sends the email to the shopper's email:
   - Subject: as configured in the campaign wizard (e.g. "You want to make a deal?").
   - Headline: as configured.
   - Body: as configured (e.g. "Your cart is still waiting for you. Use code CODE123 for 10% off."). — PENDING: whether `CODE123` is replaced with a per-shopper generated code or is the same code for all recipients (see uncertainty in Part B `admin-008-abandonned-checkout-06`).
   - Cart contents block: image thumbnails, product titles, variants, quantities, line totals, cart total.
   - **Resume-checkout CTA button** (e.g. "RESUME YOUR CHECKOUT") — links to a tokenized URL that restores the shopper's cart and pre-fills their checkout information. — PENDING: token security model and expiry (CRITICAL, see Part B `admin-008-abandonned-checkout-04`).
   - Unsubscribe link (mandatory).
4. Shopper opens the email.
5. Shopper clicks the resume-checkout link:
   - System validates the token (not expired, not already used for a completed order).
   - System restores the shopper's cart and pre-fills the checkout fields.
   - Browser navigates to the checkout page at the step where the shopper left off (or the beginning of checkout).
   - The abandoned-checkout campaign session registers a click (contributes to "Unique Impressions" stat).
6. Shopper completes the purchase → UF-010.
   - System marks the abandoned-checkout record as recovered; no further emails in the sequence are sent.
   - Revenue recovered is attributed to this campaign.

**Branch — Shopper does not click the link:**

5. System waits for the next email in the sequence (if configured) and repeats from step 3 with a different template.
6. After the sequence is exhausted, system stops sending.

**Postconditions:**
- IF recovered: campaign's "Sales recovered" and "Revenue recovered" counters incremented.
- IF not recovered: no further emails after sequence completion.

**Edge cases:**
- Token is expired → system renders an error page with a link to the store home (shopper must manually rebuild cart).
- Shopper already placed the order by the time they click the link → system detects the existing order and redirects to the thank-you page; no duplicate order created.
- Shopper unsubscribes → all subsequent campaign emails in the sequence are suppressed.
- GDPR: emailing a shopper who never explicitly consented to marketing (they only entered an email on checkout) → PENDING (legal basis for abandoned-cart emails must be decided — see Part B uncertainty on "abandoned checkout GDPR basis").

---

## UF-017: Receive review-request email (N days after shipment)

**Actor:** end-customer (shopper)
**Preconditions:**
- Shopper's order has been marked as shipped (fulfillment status updated).
- Automated Review Request Emails are enabled in Reviews settings.
- N days (default 45) have elapsed since shipment.
- Shopper has not already submitted a review for the purchased product(s).
- The order is not in the exclusion list ("Old orders that won't trigger a request").

**Steps:**

1. System detects that N days have elapsed since the shipment date for the order.
2. System sends a review-request email to the shopper:
   - Subject: as configured (e.g. "So what did you think of our products?").
   - Body: personalized template referencing the purchased product(s).
   - CTA: "Write a Review" button → links to the product page anchored to the review submission form, or to a dedicated review-submission URL.
3. Shopper opens the email and clicks "Write a Review" → **UF-018**.

**Postconditions:**
- Review-request email sent; "Sent" counter in Review Request Statistics incremented.

**Edge cases:**
- Shopper has multiple items in the order → system may send one email per product or one combined email covering all products. — PENDING: per-product vs per-order review request (**NEW UNCERTAINTY #L**).
- Order was never shipped (still in fulfillment) → timer does not start; email is never sent.
- Shopper has already left a review for this product (e.g. from a previous order) → PENDING: whether the system suppresses the request (**NEW UNCERTAINTY #M**).

---

## UF-018: Submit a product review

**Actor:** end-customer (shopper)
**Preconditions:**
- Shopper is on the product page (arrived via review-request email link or organically).
- The review widget is enabled for this product (or globally).

**Steps:**

1. Shopper clicks "Write a Review" (on the product page) or arrives via the review-request email link.
2. System renders the review submission form inline or in a modal:
   - Star rating selector (1–5 stars; 5 pre-selected).
   - Review text textarea.
   - Photo upload (multiple images allowed).
   - Name field (pre-filled if the shopper is identified by the email link; otherwise blank).
   - Email field (used to link the review to an order for the "verified buyer" badge; not displayed publicly). — PENDING: whether email is required or optional for organic reviews.
3. Shopper selects a star rating, writes the review text, and optionally uploads photos.
4. Shopper clicks "Submit".
5. System validates: star rating selected, review text not empty (minimum length TBD).
   - IF invalid: inline error messages; shopper corrects.
6. System saves the review:
   - IF Reviews settings = "Automatically published": review status = APPROVED immediately; appears on the product page.
   - IF Reviews settings = "Published after approval": review status = PENDING; does not appear until admin approves.
7. System fires `EVT_REVIEW_SUBMITTED` (product_id, rating, has_photos).
8. System sends a notification email to the admin (to the configured review-notification email address).
9. System displays a thank-you confirmation to the shopper: "Thank you for your review! It will appear once approved." (or "Your review has been published." if auto-approved).

**Postconditions:**
- Review stored (pending or approved).
- Admin notified.
- "Accepted" counter in Review Request Statistics incremented if the submission came via an email link.

**Edge cases:**
- Shopper submits without a photo → photos are optional; submission succeeds.
- Shopper submits a review with an offensive or spam-like text → caught in moderation (admin reviews and hides if needed — see UF moderation note).
- Review from a non-buyer (organic, no purchase record for this product) → review saved but "Verified Buyer" badge is NOT shown (badge requires matching purchase record by email).
- AI-generated reviews (requested feature in admin Reviews settings) → separate admin-triggered job; not a shopper flow. — PENDING: legal/compliance provenance flags (see uncertainty in Part B `admin-012-reviews-04`).

---

## UF-019: Subscribe via lead-capture overlay (and receive coupon)

**Actor:** end-customer (shopper)
**Preconditions:**
- At least one lead-capture overlay campaign is Active.
- The overlay trigger condition is met (exit-intent detected on desktop, or timer elapsed, or time-based trigger on mobile/tablet).
- The shopper has not already seen the overlay more than the configured frequency cap allows.
- The shopper is not on a page in the exclusion list.

**Steps:**

1. System detects the trigger condition (exit-intent or timer).
2. System checks the frequency cap (e.g. max 1 impression per 7 days per user via cookie/localStorage).
   - IF cap exceeded → overlay is NOT shown; flow ends.
3. System renders the overlay modal:
   - Theme as configured (dark/light/banner style).
   - Headline: e.g. "SIGN-UP FOR OUR NEWSLETTER".
   - Incentive copy: e.g. "AND GET A 10% OFF COUPON".
   - Name input (optional or required — PENDING: whether name is required or optional for overlay forms).
   - Email input (required).
   - CTA button: e.g. "GET MY 10% OFF NOW".
   - Dismiss link: e.g. "I don't want to save money".
4. System increments "Impressions" counter for this campaign.
5. Shopper enters their name (if shown) and email and clicks the CTA.
6. System validates the email format.
   - IF invalid: inline error; shopper corrects.
7. System records the lead: stores (email, name, tags configured for this campaign) in the customer/leads database.
8. System fires `EVT_LEAD_CAPTURE` (campaign_id, overlay_theme).
9. System increments "Conversions" counter for this campaign.

**Branch A — Post-signup: Display message:**

10. System shows the configured confirmation message inside the overlay (e.g. "Thank you! Your coupon code is WELCOME10.").
11. System issues/displays the coupon code. — PENDING: whether the system auto-generates a unique coupon per subscriber or displays a shared code from the campaign config (see uncertainty in Part C `admin-015`).
12. Overlay dismisses after a delay or on shopper click.

**Branch B — Post-signup: Go to URL:**

10. System redirects the browser to the configured URL (e.g. a dedicated landing page with the coupon or a thank-you page).

**Postconditions:**
- Lead stored with configured tags.
- Coupon code delivered to the shopper.
- Campaign conversion counter incremented.

**Edge cases:**
- Shopper dismisses the overlay without subscribing → "Impressions" incremented but "Conversions" not; frequency cap cookie set to prevent re-show for configured duration.
- Shopper's email already exists in the customer database → system deduplicates silently (no error shown; updates tags if new).
- Overlay fires on a product page while the shopper is in the process of adding to cart → overlay should not appear mid-interaction; the trigger should be suppressed if a cart or checkout action is in progress. — PENDING: exact suppression logic.
- Mobile/tablet trigger is set to "Disabled" → overlay never fires on touch devices regardless of other settings (consistent with admin config).

---

## UF-020: Currency switcher

**Actor:** end-customer (shopper)
**Preconditions:**
- The currency converter module is enabled with at least one additional currency active (besides the store default).

**Steps:**

1. Shopper sees a currency selector in the storefront header or footer (e.g. a dropdown showing the current currency symbol and code, e.g. "$ USD").
2. Shopper opens the currency selector and selects a different currency (e.g. "€ EUR").
3. System sets the shopper's preferred currency (via cookie or localStorage).
4. System re-renders all prices on the current page using the exchange rate for the selected currency, formatted per the admin's symbol/code visibility settings (e.g. "€59.99 EUR" or "59,99 €").
5. On subsequent page navigations, prices are displayed in the selected currency.

**Postconditions:**
- Shopper sees prices in their preferred currency for the remainder of the session.

**Edge cases:**
- Display-only vs transactional currency: PENDING (uncertainty in Part B `admin-011` — CRITICAL: is the shopper charged in the selected currency or always in the store default?). — **PENDING (uncertainty in 11_uncertainties_to_validate.md, Part B).**
- Rounding: converted prices are rounded per the configured rounding rule. — PENDING (uncertainty: rounding to .99 or nearest cent?).
- Exchange rates are stale → system displays prices based on cached rates; rate staleness is not surfaced to the shopper.
- Geo-IP default: if the store auto-selects a currency based on the shopper's detected country, this happens at session start before any shopper interaction. — PENDING (uncertainty: geo-IP default currency — see Part B `admin-011`).

---

## UF-021: AI sales assistant chat interaction

**Actor:** end-customer (shopper)
**Preconditions:**
- AI chat feature is enabled for the store.
- The AI assistant is connected to a knowledge base covering the store's products, policies (shipping, returns), and FAQs.

> **PENDING — Technology not confirmed.** This flow describes the intended behavior; implementation is subject to decision on the AI chat technology stack (uncertainty 10 in `11_uncertainties_to_validate.md`: Claude API + RAG recommended, pending approval).

**Steps:**

1. A chat widget (button or tab) appears on all storefront pages (e.g. bottom-right corner).
2. Shopper clicks the chat widget.
3. Chat panel opens with a greeting: "Hi! How can I help you today? I can help you find products, answer questions about shipping, or suggest the right size."
4. Shopper types a question or request (e.g. "Do you have a blue midi dress under $80?" or "What is your return policy?").
5. System sends the shopper's message to the AI assistant backend (Claude API + RAG or equivalent).
6. AI assistant retrieves relevant context from the knowledge base (product catalog, policies, FAQs) and generates a response.
7. System streams or displays the response in the chat panel.
8. IF the response includes a product recommendation:
   - Response contains a product card inline: image, title, price, "View Product" button.
   - Shopper clicks "View Product" → browser navigates to the product page (UF-002).
9. Conversation continues; shopper can ask follow-up questions.
10. Shopper can close the chat panel at any time.

**Tracking events:**
- Chat session start: internal analytics event (not a pixel event).
- Product recommendation clicked from chat: `EVT_PRODUCT_CTR` (source = "ai_chat").
- Add-to-cart from a product page reached via chat: `EVT_ADD_TO_CART` (as normal).

**Postconditions:**
- Shopper receives answers to their questions.
- Product recommendations may lead to product page views and purchases.

**Edge cases:**
- AI assistant does not know the answer → assistant responds with a graceful fallback ("I'm not sure about that. Please contact us at support@pradize.com.").
- Shopper attempts to extract sensitive data (pricing strategy, admin credentials) via the chat → knowledge base scope is restricted; assistant cannot access admin-side data.
- Chat is unavailable (API outage) → widget shows an error message; chat is hidden or degraded gracefully.
- **PENDING:** Language handling — does the AI chat respond in the language of the storefront domain, the shopper's browser language, or the language the shopper writes in? (NEW UNCERTAINTY #N).

---

## New critical uncertainties discovered

The following uncertainties were identified while writing these flows and are not yet tracked in `11_uncertainties_to_validate.md`:

| ID | Area | Description | Impact |
|---|---|---|---|
| **#A** | Product page / promotions | **Timer promo feature** (per-product countdown promo active on selected days/hours): no design decision recorded; interacts with pricing and upsell; mentioned in `admin-021-edit-product-page-02` annotation. | Pricing engine, UF-002, UF-013 |
| **#B** | Inventory display | **Fake/server-assigned inventory UX**: should the product page display a specific quantity label (e.g. "Only 3 left")? If yes, what number does the server assign per session? Inconsistency across sessions could erode trust. | Product page, UF-003 |
| **#C** | Pre-order | **Past pre-order date**: should the engine warn or block publishing/displaying a pre-order product whose expected-receipt date has passed? | UF-004, admin product edit |
| **#D** | SEO / A/B pages | **Canonical URL strategy for multi-variant (A/B) product pages**: which URL is the canonical? All non-canonical URLs need `rel=canonical` to avoid duplicate-content penalties. | UF-005, SEO spec |
| **#E** | Cart persistence | **Cart session duration**: how long is an anonymous cart persisted (e.g. 7 days? 30 days?)? Affects abandoned-checkout timing and returning-visitor experience. | UF-006, UF-016 |
| **#F** | Checkout | **Guest vs account checkout**: does the engine support customer accounts (login, order history) or is checkout always guest-only? Affects pre-fill, per-customer coupon limits, and review "verified buyer" linking. | UF-007, UF-008, UF-018 |
| **#G** | Discounts | **Coupon + gift card stacking order**: when both a coupon and a gift card are applied, which discount is deducted first? The order determines how the gift card balance is consumed. | UF-008, UF-009 |
| **#H** | Gift cards | **Multiple gift cards per order**: can a shopper apply two or more gift card codes to a single order? | UF-009 |
| **#I** | Payments | **Idempotency strategy for order placement**: what key is used to prevent double-charges on network retries (session token? Stripe idempotency key?)? | UF-010 |
| **#J** | Upsells | **Multiple sequential post-purchase upsells**: if multiple campaigns match the same order, are multiple upsell offers shown sequentially (funnel) or only one (first match wins)? | UF-012 |
| **#K** | Refunds / gift cards | **Refund method for gift-card-paid orders**: when an order paid wholly or partially by gift card is refunded, is the credit returned to the gift card balance, to the original payment method, or as a new gift card code? | UF-014 |
| **#L** | Reviews | **Per-product vs per-order review request email**: if an order contains 3 products, does the system send 3 separate review-request emails (one per product) or one combined email? | UF-017 |
| **#M** | Reviews | **Repeat-buyer review request suppression**: if a shopper has already reviewed a product from a previous order, should the system suppress the review-request email for a repeat purchase of the same product? | UF-017 |
| **#N** | AI chat | **AI chat response language**: does the assistant detect and respond in the shopper's input language, always use the storefront domain's language, or is this configurable? | UF-021 |
