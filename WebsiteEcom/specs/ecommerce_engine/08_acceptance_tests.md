# 08_acceptance_tests

> Acceptance criteria — behavioral specifications for the Pradize Django ecommerce engine.
> Every criterion is black-box testable against rendered HTML, HTTP responses, database state, email content, or API behavior.
> Django code or test implementation is NOT specified here.

**Status: FIRST PASS COMPLETE (2026-07-02) — Spec Agent.**

**SEO acceptance criteria** are already specified in `07_multilingual_seo.md` section 12 (`TST-*` IDs).
Cross-references to those criteria are noted per area below; duplication is intentional only where the coverage angle differs.

---

## Conventions

Each criterion follows this structure:

### AC-NNN: Title
**Area:** Feature area from feature matrix
**Type:** unit / integration / permission / regression / SEO / security / edge-case
**Given:** Precondition
**When:** Action
**Then:** Expected result (specific and measurable)
**Dangerous data / edge case:** What makes this interesting

---

## Area 1 — Authentication & Permissions

### AC-001: Employee limited to assigned modules
**Area:** Authentication & Permissions
**Type:** permission
**Given:** An employee account with Orders = Full access, Reports = No access, all other modules = No access
**When:** The employee logs in and attempts to navigate to `/admin/reports/`
**Then:** The system returns HTTP 403 or redirects to an "Access Denied" page; the Reports link is absent from the sidebar menu
**Dangerous data / edge case:** Employee who manually types the URL, bypassing the menu — URL-level enforcement must match menu-level enforcement.

### AC-002: Full Access master mode bypasses per-module grid
**Area:** Authentication & Permissions
**Type:** permission
**Given:** An employee created with "Full Access" mode selected (per-module grid hidden)
**When:** The employee logs in and navigates to any of the 18 modules including Settings, Orders, Gift cards
**Then:** All modules are accessible without restriction; no module returns 403
**Dangerous data / edge case:** Switching an existing "Limited Access" employee to "Full Access" mid-session — the change must take effect on the next request, not require a new login.

### AC-003: Store admin cannot see another store's data
**Area:** Authentication & Permissions
**Type:** permission
**Given:** Two stores A and B exist; a store-A admin is authenticated
**When:** The store-A admin attempts to access `/admin/` (store B's admin) or directly requests store B's order list endpoint
**Then:** The system returns HTTP 403 or redirects to store A's context; no store B data is rendered
**Dangerous data / edge case:** IDOR via direct `?store_id=B` query parameter substitution on any endpoint that accepts a store identifier.

### AC-004: Super-admin can access all stores
**Area:** Authentication & Permissions
**Type:** permission
**Given:** A user with `is_super_admin=True` flag authenticated at `/superadmin/`
**When:** The super-admin views the organization and stores list, then drills into store A's orders and store B's orders
**Then:** Both stores' data are returned with HTTP 200; data from both stores is visible without re-authentication
**Dangerous data / edge case:** A store that was soft-deleted — super-admin should still see it with a "deleted" status indicator; soft-deleted store's storefront must remain inaccessible to buyers.

### AC-005: Orders "Limited access" restricts refunds
**Area:** Authentication & Permissions
**Type:** permission
**Given:** An employee with Orders = Limited access
**When:** The employee views an order detail page and attempts to click "Issue Refund" or POST to the refund endpoint
**Then:** The "Issue Refund" button is absent from the UI; a direct POST to the refund endpoint returns HTTP 403
**Dangerous data / edge case:** An employee who had Full access, was downgraded to Limited, and still has an active browser session with a cached page showing the "Issue Refund" button — clicking it must be rejected server-side.

### AC-006: Employee account invite email and first login
**Area:** Authentication & Permissions
**Type:** integration
**Given:** A store admin invites a new employee via AF-002; the invitation email is sent
**When:** The new employee clicks the invitation link and sets a password
**Then:** The employee can log in; their permission matrix matches what the store admin configured; last-login timestamp in the employee list is updated after login
**Dangerous data / edge case:** Invitation link reused after account is already activated — system must reject the second use with a "link expired or already used" error.

### AC-007: Rate limiting on failed login attempts
**Area:** Authentication & Permissions
**Type:** security
**Given:** A login endpoint at `/admin/user/login`
**When:** An automated client submits 10 consecutive failed login attempts for the same email address
**Then:** The 11th attempt within the lockout window is rejected with HTTP 429 or an explicit "account temporarily locked" message regardless of credentials correctness; a legitimate user from a different IP is unaffected
**Dangerous data / edge case:** Enumeration attack via timing difference between "no account found" and "wrong password" responses — both must return the same HTTP status and take the same response time.

---

## Area 2 — Product Catalog

### AC-010: Product with zero variants is rejected
**Area:** Product Catalog
**Type:** unit
**Given:** An actor fills in the product title and all required fields but configures Multi Variant mode with one option that has zero values added
**When:** The actor clicks "Publish"
**Then:** The system blocks the save with a validation error: "You must add at least one variant value"; no product record is created
**Dangerous data / edge case:** Single-Variant mode with the price field left empty — also blocked.

### AC-011: Product with 500 variants is accepted
**Area:** Product Catalog
**Type:** integration
**Given:** A product in Multi Variant mode with 3 options: 10 sizes × 10 colors × 5 materials = 500 variants, each with a price set
**When:** The actor clicks "Publish"
**Then:** The system saves the product with all 500 variant rows; the product page loads on the storefront with all 500 variant combinations selectable; response time for the admin save is under 30 seconds
**Dangerous data / edge case:** Cartesian product explosion — 10 options each with 10 values = 10^10 combinations, which must be blocked or bounded by a configurable variant ceiling.

### AC-012: Duplicate SKU within a store is rejected
**Area:** Product Catalog
**Type:** unit
**Given:** Product A in store X has variant with SKU "WIDGET-001"
**When:** An actor creates product B in store X and sets a variant's SKU to "WIDGET-001"
**Then:** The system blocks the save with a validation error: "This SKU is already in use in this store"; the duplicate check is case-insensitive
**Dangerous data / edge case:** Same SKU in store X and store Y — this must be allowed (SKU uniqueness is per-store, not global). A blank SKU is always allowed and never treated as a duplicate.

### AC-013: Slug NFD-normalization for accented characters
**Area:** Product Catalog
**Type:** unit
**Given:** A product titled "Santé mentale"
**When:** The slug is auto-generated
**Then:** The slug equals `sante-mentale`, not `sant-mentale` (the e is preserved, not stripped); `GET /sante-mentale/` returns HTTP 200
**Dangerous data / edge case:** "Müller Größe" → `muller-grosse`; "São João" → `sao-joao`; "Αθήνα" (Greek) → slug generation must not silently emit an empty string (see SLUG-003). See also `TST-SLUG-001` in `07_multilingual_seo.md`.

### AC-014: Slug change on published product auto-creates 301
**Area:** Product Catalog
**Type:** regression
**Given:** A product published with slug `old-dress` that has been live for more than 7 days
**When:** An actor changes the slug to `new-dress` and saves (with the confirmation step acknowledged)
**Then:** `GET /old-dress/` returns HTTP 301 to `/new-dress/`; `GET /new-dress/` returns HTTP 200; the redirect appears in the Redirects list with origin = `auto_slug_change`
**Dangerous data / edge case:** Slug changed via AI translation job (not the admin form) — the pre-save hook must fire and create the redirect regardless of the update path (API, admin, AI job). See also `TST-SLUG-030`.

### AC-015: Empty product title is rejected
**Area:** Product Catalog
**Type:** unit
**Given:** An actor opens a new product form
**When:** The actor leaves the Product Title blank and clicks "Publish" or "Save draft"
**Then:** The system shows a validation error: "Product title is required"; no product record is created
**Dangerous data / edge case:** Title composed entirely of whitespace or Unicode zero-width spaces — must be treated as empty after trimming.

### AC-016: Video embed URL validation (YouTube/Vimeo only)
**Area:** Product Catalog
**Type:** unit
**Given:** An actor adds a video URL to a product
**When:** The actor enters a non-YouTube/Vimeo URL (e.g. `https://vimeo.evil.com/123` or `ftp://example.com/video.mp4`) and saves
**Then:** The system rejects the URL with an error: "Video URL must be a valid YouTube or Vimeo embed URL"; a valid `https://www.youtube.com/watch?v=abc123` is accepted; a valid `https://vimeo.com/123456` is accepted
**Dangerous data / edge case:** A crafted URL that starts with `https://youtube.com.evil.com/` — the validator must parse the host, not use substring matching.

### AC-017: A/B variant URL returns 200 with canonical to primary
**Area:** Product Catalog
**Type:** SEO
**Given:** A product with primary URL `/dress/` and an A/B variant configured at `/dress-v2/`
**When:** A crawler fetches `GET /dress-v2/`
**Then:** HTTP 200; the page contains `<link rel="canonical" href="https://example.com/dress/">` pointing to the primary; the page is not in `/sitemap.xml`; no hreflang tags are emitted for the variant URL
**Dangerous data / edge case:** The A/B variant URL must carry the primary URL's canonical even if the variant has different images, to prevent duplicate-content penalties. See also `TST-CAN-020`.

---

## Area 3 — Inventory Modes

### AC-020: "Always SOLD OUT" mode blocks add-to-cart
**Area:** Inventory
**Type:** integration
**Given:** A published product with inventory mode = Always SOLD OUT
**When:** A shopper views the product page
**Then:** The CTA button renders as "Sold Out" (disabled, not clickable); `POST /cart/add/` for this product variant returns HTTP 400 or 403; no `EVT_ADD_TO_CART` is fired
**Dangerous data / edge case:** A direct API POST to the cart endpoint bypassing the UI button — must be rejected server-side.

### AC-021: "No inventory tracking" mode allows unlimited add-to-cart
**Area:** Inventory
**Type:** integration
**Given:** A published product with inventory mode = No inventory tracking
**When:** 1000 concurrent shoppers add the product to their cart
**Then:** All 1000 requests succeed; inventory is never decremented; no "out of stock" message is shown
**Dangerous data / edge case:** Switching from "Fixed inventory" to "No tracking" mid-day — existing cart reservations should be honored or cleared gracefully.

### AC-022: "Fixed quantity" mode decrements on purchase and blocks overselling
**Area:** Inventory
**Type:** integration
**Given:** A product with inventory mode = Fixed quantity and quantity = 3
**When:** Three different shoppers each complete a purchase of 1 unit; a fourth shopper attempts to complete a purchase of 1 unit of the same product
**Then:** The first three purchases succeed; the fourth purchase attempt fails with "Item no longer available" at payment time; inventory displayed on the product page shows 0; the CTA changes to "Sold Out"
**Dangerous data / edge case:** Concurrent "Place Order" requests from two shoppers when quantity = 1 — only one must succeed (atomic decrement; the other gets a meaningful error, not a server crash or double-sell).

### AC-023: "Fake/server-assigned" inventory shows same quantity to all sessions
**Area:** Inventory
**Type:** integration
**Given:** A product with inventory mode = Fake/server-assigned inventory with server-assigned quantity = 7
**When:** Two simultaneous shoppers on different browsers view the product page
**Then:** Both see the same quantity label (e.g., "7 left in stock"); adding to cart does not decrement this display quantity; the displayed number is consistent across page reloads
**Dangerous data / edge case:** A shopper who views the page, adds to cart, and then reloads — the quantity display must not change.

### AC-024: "Presale" mode shows pre-order CTA and expected date
**Area:** Inventory
**Type:** integration
**Given:** A product with inventory mode = Presale and expected-receipt date = 2026-09-01
**When:** A shopper views the product page
**Then:** The CTA button reads "Pre-Order Now"; a notice reads "Expected to ship by September 1, 2026"; the shopper can add to cart; the cart and order confirmation both show the pre-order date beside the item; `EVT_ADD_TO_CART` includes `is_preorder=true`
**Dangerous data / edge case:** Mixed cart with one pre-order and one in-stock item — checkout must not be blocked; the confirmation email must distinguish item types.

### AC-025: "Ask When Available" mode shows notification form
**Area:** Inventory
**Type:** integration
**Given:** A product with inventory mode = Ask When Available
**When:** A shopper views the product page and clicks "Notify Me When Available", enters email `test@example.com`, and submits
**Then:** The CTA is replaced with the "Notify Me" button; no add-to-cart is possible; on submission, a waitlist record is stored for (product, variant, email); a confirmation message is displayed; the same email submitted again for the same variant produces no error and no duplicate record
**Dangerous data / edge case:** Email submitted without JavaScript (form POST must work). Submitting an invalid email format returns an inline validation error, not a 500.

### AC-026: "Ask for Quotation" mode shows quotation form
**Area:** Inventory
**Type:** integration
**Given:** A product with inventory mode = Ask for Quotation and "Starting at $149" configured
**When:** A shopper views the product page
**Then:** "Starting from $149" is displayed; the CTA reads "Request a Quotation"; clicking opens a form with Name, Email, ~~Quantity,~~ Message fields (**DECIDED, human, 2026-07-10, 18:P2: quantity removed — quotation-mode products are not cartable, so a requested quantity is not meaningful at request time**); submitting a valid form stores the request and shows a confirmation; the store admin receives a notification email; no charge is made
**Dangerous data / edge case:** Required fields (Name, Email) left blank — inline field-level errors shown, form not submitted.

### AC-027: Storefront CTA matrix — all 7 modes covered
**Area:** Inventory
**Type:** regression
**Given:** Seven products each with a different inventory mode (Always SOLD OUT, No Tracking, Fixed Qty with stock, Fake/Server-assigned, Presale, Ask When Available, Ask for Quotation)
**When:** A shopper visits each product page
**Then:** CTAs and behaviors are exactly: (1) Sold Out button (disabled), (2) Add to Cart (functional), (3) Add to Cart (functional, limited qty), (4) Add to Cart (unlimited, same displayed qty), (5) Pre-Order Now + date notice, (6) Notify Me, (7) Request Quotation + starting price
**Dangerous data / edge case:** Variant-level mode override — a product where variant A is in-stock and variant B is always sold out must show the correct CTA per selected variant.

---

## Area 4 — Collections

### AC-030: Smart collection auto-matches product by title rule
**Area:** Collections
**Type:** integration
**Given:** A smart collection with rule: Product title contains "dress" (case-insensitive)
**When:** A product titled "Red Summer Dress" is published and a product titled "Blue Shirt" is also published
**Then:** The dress product is included in the collection; the shirt product is not; if the shirt's title is later changed to "Dress Shirt", it joins the collection automatically on save
**Dangerous data / edge case:** A product title with leading/trailing whitespace — matching must work after normalization.

### AC-031: Smart collection matches on price range
**Area:** Collections
**Type:** integration
**Given:** A smart collection with rule: Product price is between $10 and $50
**When:** A product with base price $25 is published; another with base price $75 is published
**Then:** The $25 product is in the collection; the $75 product is not; if the $25 product's price is later raised to $60, it is removed from the collection on next save
**Dangerous data / edge case:** A multi-variant product where variant A is $25 and variant B is $75 — the matching uses the product's minimum variant price; confirm expected behavior.

### AC-032: Empty auto-collection returns 404, not a blank page
**Area:** Collections
**Type:** SEO
**Given:** A smart collection whose rules match zero active products
**When:** A shopper requests the collection's storefront URL
**Then:** The server returns HTTP 404 (not HTTP 200 with an empty product grid); the collection URL is absent from `/sitemap.xml`; no hreflang points to this URL
**Dangerous data / edge case:** A collection that previously had 3 products but had 2 products deleted — the transition from indexable to 404 must happen within the sitemap debounce window (default 15 min). See `TST-IDX-collection`.

### AC-033: Collection with 1–2 products is noindex
**Area:** Collections
**Type:** SEO
**Given:** A smart collection currently matching 2 products (below the threshold of 3)
**When:** A shopper fetches the collection's storefront URL
**Then:** HTTP 200; the page contains `<meta name="robots" content="noindex">`; the collection URL does not appear in `/sitemap.xml`
**Dangerous data / edge case:** A product is unpublished, reducing the collection from 3 to 2 products — the noindex transition must happen without manual intervention.

### AC-034: Manual collection ordering gap after product deletion
**Area:** Collections
**Type:** regression
**Given:** A manual collection with products in positions 1, 2, 3; product at position 2 is deleted
**When:** A shopper views the collection page
**Then:** The page renders products in positions 1 and 3 with no gap; the ordering is 1 → 3 visually; the remaining products' ordering integers are compacted or position 2 is simply absent (no blank card)
**Dangerous data / edge case:** Concurrent deletion of two products — no orphaned position values should cause a duplicate-key error.

### AC-035: Collection CTR and purchase-rate columns are accurate
**Area:** Collections
**Type:** integration
**Given:** A collection page viewed 100 times; 40 sessions clicked at least one product; 10 sessions resulted in a purchase from that collection
**When:** An actor views the Collection report for the period covering those events
**Then:** % CTR = 40.0%; % Purchase = 10.0%; Display = 100; data is accurate after the analytics pipeline completes (within the event-processing SLA)
**Dangerous data / edge case:** A session that clicks a product but from a different collection (not the current one) — the click must be attributed only to the originating collection.

---

## Area 5 — Discount Engine

### AC-040: Coupon $ off applied first, then gift card on remainder
**Area:** Discount Engine
**Type:** integration
**Given:** An order subtotal of $100; a $20-off coupon applied; a $30 gift card applied
**When:** The shopper proceeds to checkout with both applied
**Then:** Coupon reduces subtotal to $80 first; gift card reduces $80 to $50; payment processor is charged $50; `GiftCardTransaction` records a deduction of $30 against the gift card balance; the order summary shows coupon discount = −$20, gift card = −$30, total = $50
**Dangerous data / edge case:** Coupon is percentage-based (20% off $100 = $20); then gift card covers the $80 remainder partially — same computation path, correct order of application.

### AC-041: Gift card full redemption — zero-charge order
**Area:** Discount Engine
**Type:** integration
**Given:** An order total of $45; a gift card with $60 balance applied
**When:** The shopper clicks "Place Order"
**Then:** No charge is submitted to the payment processor; order status = PAID; `GiftCardTransaction` records balance_before = $60, amount_deducted = $45, balance_after = $15; the gift card remains usable with $15 remaining
**Dangerous data / edge case:** The order total changes between gift card application and "Place Order" (e.g., shipping recalculated) — the system must re-validate the deduction amount.

### AC-042: Gift card partial redemption — correct remainder charged
**Area:** Discount Engine
**Type:** integration
**Given:** An order total of $80; a gift card with $30 balance applied
**When:** The shopper enters a payment method and places the order
**Then:** Gift card deducts $30; processor is charged $50; `GiftCardTransaction` records balance_after = 0; order summary shows gift card = −$30, charged = $50
**Dangerous data / edge case:** Processor declines the $50 charge — the gift card deduction must be rolled back atomically; the gift card balance must be restored to $30; no order is created.

### AC-043: Single gift card per order enforced
**Area:** Discount Engine
**Type:** unit
**Given:** A shopper on the checkout page who has already applied gift card GC-001
**When:** The shopper attempts to apply a second gift card GC-002
**Then:** The system displays an error: "Only one gift card can be applied per order"; GC-002 is not applied; GC-001 remains active
**Dangerous data / edge case:** Two simultaneous checkout sessions for the same customer, each applying a different gift card — server-side enforcement must block the second application regardless of UI state.

### AC-044: Coupon use-limit atomic decrement under concurrent requests
**Area:** Discount Engine
**Type:** integration
**Given:** A coupon with total limit = 1 (use count = 0)
**When:** Two shoppers simultaneously click "Place Order" with this coupon applied
**Then:** Exactly one order is created with the coupon applied; the other order either fails with "coupon no longer available" or completes without the discount applied; the coupon's use count is exactly 1 after both requests complete
**Dangerous data / edge case:** Race condition without atomic DB-level locking — must use `SELECT FOR UPDATE` or an equivalent atomic decrement that prevents both orders from being created with the coupon.

### AC-045: Coupon per-email limit enforced for guest checkout
**Area:** Discount Engine
**Type:** integration
**Given:** A coupon with per-customer limit = 1; a previous order with email `buyer@example.com` used this coupon
**When:** A guest with email `buyer@example.com` (same email, no account) attempts to apply the same coupon on a new order
**Then:** The system identifies the previous use by email, rejects the application with "You have already used this coupon code", and does not apply the discount
**Dangerous data / edge case:** Email entered with different casing (`Buyer@EXAMPLE.com` vs `buyer@example.com`) — comparison must be case-insensitive.

### AC-046: Expired coupon is rejected
**Area:** Discount Engine
**Type:** unit
**Given:** A coupon with `valid_until` = yesterday's date
**When:** A shopper attempts to apply this coupon at checkout
**Then:** The system returns an inline error "This coupon code has expired"; the order total is not reduced; the coupon use counter is not incremented
**Dangerous data / edge case:** Coupon that expires at midnight UTC while a shopper in UTC+14 is still "on the same calendar day" — expiry must be enforced by UTC timestamp, not by displayed date string.

### AC-047: Wrong-product coupon is rejected
**Area:** Discount Engine
**Type:** unit
**Given:** A coupon restricted to products in collection "Shoes"; the cart contains only a product from collection "Dresses"
**When:** The shopper applies this coupon at checkout
**Then:** The system returns an inline error "This coupon does not apply to the items in your cart"; the discount is not applied
**Dangerous data / edge case:** A cart with one eligible (Shoes) and one ineligible (Dresses) item — if the coupon applies only to eligible items, the discount is partial and the error message must reflect this.

### AC-048: Minimum order coupon rejected below threshold
**Area:** Discount Engine
**Type:** unit
**Given:** A coupon requiring minimum order of $100; cart subtotal = $85
**When:** The shopper applies this coupon
**Then:** Error: "A minimum order of $100 is required for this coupon"; no discount applied; adding more items to bring the cart above $100 allows the coupon to apply
**Dangerous data / edge case:** Minimum order comparison after applying a previous discount — minimum must be checked against the post-discount (or pre-discount?) subtotal; the spec must clarify which; recommend pre-discount comparison.

### AC-049: "Free product" coupon adds product to cart
**Area:** Discount Engine
**Type:** integration
**Given:** A coupon of type "Free product" configured with Product X as the free item; cart contains at least one paid item
**When:** The shopper applies the coupon
**Then:** Product X is added to the cart at $0.00; the order summary shows it as a free item; the coupon use counter increments on order placement; if Product X is out of stock, the free product coupon cannot be applied
**Dangerous data / edge case:** The free product is also the only item already in the cart — the coupon should add a second unit for free, not discount the existing item (or the spec must define this explicitly).

### AC-050: Free shipping coupon removes shipping cost
**Area:** Discount Engine
**Type:** unit
**Given:** An order with a $9.99 shipping cost; a free-shipping coupon applied
**When:** The shopper views the order summary
**Then:** Shipping line shows $0.00; the coupon discount line shows "−$9.99 (Free Shipping)"; the order total is reduced by exactly $9.99; if a different shipping method is subsequently selected at a higher cost, the coupon zeroes that cost too
**Dangerous data / edge case:** A coupon that is both free shipping AND percentage off — behavior depends on stacking rules (AC-044 pattern).

---

## Area 6 — Orders & Fulfillment

### AC-060: Order state transition: placed → sent to fulfillment → shipped
**Area:** Orders & Fulfillment
**Type:** integration
**Given:** A paid order in state (PAID, NOT SENT TO FULFILLMENT)
**When:** An actor marks it as "Sent to fulfillment", then later marks it as "Shipped" with tracking number "1Z999AA10123456784" and carrier "UPS"
**Then:** Payment status remains PAID; fulfillment status transitions to SENT TO FULFILLMENT, then to SHIPPED; the shipping-confirmation email is sent to the customer containing the tracking number and a carrier tracking link (UPS URL with the tracking number appended); timestamps for each status change are recorded
**Dangerous data / edge case:** Status set backwards (SHIPPED → NOT SENT) — the system must either block illegal transitions or require an explicit override confirmation.

### AC-061: Partial refund updates payment status
**Area:** Orders & Fulfillment
**Type:** integration
**Given:** An order with total $120 in PAID state
**When:** An actor issues a partial refund of $30 with "Notify customer" checked
**Then:** The processor refund API is called for $30; payment status changes to PARTIALLY REFUNDED; the refund amount ($30) is recorded on the order; a "Refund" automated email is sent to the customer; the remaining balance due is shown as $90; Net Sales for analytics = Gross Sales − $30
**Dangerous data / edge case:** Attempting to refund more than the order total — the system must reject with "Refund amount cannot exceed the order total."

### AC-062: CSV tracking import bulk-marks orders as shipped
**Area:** Orders & Fulfillment
**Type:** integration
**Given:** 50 orders in SENT TO FULFILLMENT state; a CSV file with columns `order_id,tracking_number,carrier` containing 50 rows
**When:** An actor uploads the CSV via the tracking import function
**Then:** All 50 orders transition to SHIPPED; each gets the tracking number and carrier from the CSV row; shipping-confirmation emails are enqueued for all 50 customers; rows with unrecognized order_id are reported as errors (not silently skipped)
**Dangerous data / edge case:** CSV with duplicate order_id rows — the last row wins, or the system rejects the file; either behavior must be documented. A malformed row (missing carrier) must produce a per-row error, not abort the whole import.

### AC-063: Amazon FBA tracking auto-pull — marks PENDING if not confirmed
**Area:** Orders & Fulfillment
**Type:** integration
**Given:** An order for a product linked to Amazon FBA; the FBA API integration is configured; the FBA API returns no tracking data for this order ID
**When:** The automated FBA tracking poll runs
**Then:** The order's fulfillment status is set to a PENDING tracking state (not SHIPPED); the admin sees an indicator that FBA tracking was requested but not yet received; once the FBA API returns a tracking number, the order transitions to SHIPPED automatically
**Dangerous data / edge case:** FBA API timeout or HTTP 5xx — the system must not mark the order as error; it must retry on the next scheduled poll.

### AC-064: Order placement idempotency — double-submit prevented
**Area:** Orders & Fulfillment
**Type:** security
**Given:** A shopper who clicks "Place Order" and whose browser sends the request twice (network retry or double-click)
**When:** Both requests arrive at the server within milliseconds
**Then:** Exactly one order is created; exactly one payment charge is submitted to the processor; the shopper sees the thank-you page for the single order; no duplicate order appears in the order list
**Dangerous data / edge case:** The first request succeeds but the HTTP response is lost in transit — the shopper's browser retries; the retry must be idempotent (use Stripe idempotency key or session-scoped order lock).

### AC-065: Product-name keyword filter on orders
**Area:** Orders & Fulfillment
**Type:** integration
**Given:** 10 orders; orders #1, #3, #7 contain a product titled "Blue Dress"; order #5 contains "Red Dress"; orders #2, #4, #6, #8, #9, #10 contain other products
**When:** An actor filters orders by product keyword "Blue Dress"
**Then:** Orders #1, #3, #7 are returned; order #5 is not (does not contain "Blue Dress"); filters are applied against the product title snapshot at order time, not the current product title
**Dangerous data / edge case:** The product was renamed after the order was placed — the filter must still find the old order using the snapshot title. A SQL injection attempt in the keyword field must be sanitized.

---

## Area 7 — Abandoned Checkout

### AC-070: Cart persists 30 days after last activity
**Area:** Abandoned Checkout
**Type:** integration
**Given:** A shopper adds items to their cart and does not complete checkout; 29 days pass with no activity
**When:** The shopper revisits the store
**Then:** The cart is restored with all original items, quantities, and applied discounts; the shopper can proceed to checkout without re-adding items
**Dangerous data / edge case:** A product in the cart becomes out-of-stock during the 29 days — on cart restoration, the out-of-stock item is shown with an availability warning; checkout is blocked until the shopper removes or resolves the item.

### AC-071: Abandonment triggered after configurable send delay (DECIDED AF-C4, 2026-07-04)
**Area:** Abandoned Checkout
**Type:** integration
**Given:** A shopper enters their email at checkout but does not complete payment; the first email step's `send_delay_hours` is 1 (the default); an active campaign is configured
**When:** The Celery beat scan finds the cart older than `send_delay_hours` and not converted
**Then:** The cart is flagged as abandoned; the first email in the campaign sequence is enqueued; if the shopper completes a purchase before the window elapses, the flag is cleared and no email is sent
**And:** The email wizard rejects saving an email without a delay value (`send_delay_hours` is required; UI "Send after" number field + hours/days dropdown; a value entered in days is stored as hours × 24)
**And:** A cart is never re-triggered for a step once an email was already sent for that session (repeat beat scans and worker retries send nothing)
**Dangerous data / edge case:** The shopper reaches the address step but does not enter an email — abandonment tracking must not trigger (no email address captured = no abandonment record).

### AC-072: Resume-cart link restores exact cart state
**Area:** Abandoned Checkout
**Type:** integration
**Given:** An abandoned-checkout email with a tokenized resume-cart link is sent; the cart at abandonment contained 2 items with specific variants and quantities; a coupon was applied
**When:** The shopper clicks the resume-cart link
**Then:** Browser navigates to the checkout page; cart is pre-loaded with the same 2 items at the same quantities and variants; if the coupon is still valid, it is re-applied; if any item is now out of stock, an error is shown (not a silent removal)
**Dangerous data / edge case:** A cart item whose price changed since abandonment — the resumed cart must reflect the current price, not the price at abandonment.

### AC-073: Resume-cart link expires after 30 days
**Area:** Abandoned Checkout
**Type:** security
**Given:** A resume-cart link generated at time T
**When:** The link is clicked at T + 31 days
**Then:** The server returns an error page: "This link has expired. Please visit our store to start a new order."; no cart is restored; no checkout is pre-populated
**Dangerous data / edge case:** A forged or tampered token (e.g., incrementing the token ID by 1) — must be rejected with the same "link expired or invalid" message without leaking whether the original token existed.

### AC-074: Abandoned-checkout email not sent when order is completed
**Area:** Abandoned Checkout
**Type:** regression
**Given:** A shopper enters their email at checkout; 30 minutes later they complete the purchase; the abandonment timeout is 60 minutes
**When:** 60 minutes elapse from email capture
**Then:** No abandoned-checkout email is sent; the campaign's "Unique Impressions" counter is not incremented; the abandoned-checkout record is marked as recovered
**Dangerous data / edge case:** Race condition where the order-completion event and the email-send job fire simultaneously — the email must not be sent even if the job had already started processing.

### AC-075: Campaign recovery revenue attributed correctly
**Area:** Abandoned Checkout
**Type:** integration
**Given:** A shopper clicks a resume-cart link, completes the purchase for $89.99
**When:** An actor views the campaign statistics
**Then:** "Sales recovered" increments by 1; "Revenue recovered" increments by $89.99; the recovery is attributed to the specific campaign whose email link was clicked (not other active campaigns)
**Dangerous data / edge case:** A shopper who clicks the link but completes a different order (different items, different total) — the full new order total is attributed, not the original cart value.

---

## Area 8 — Analytics

### AC-080: Gross Sales = order totals at payment time, before refunds
**Area:** Analytics
**Type:** unit
**Given:** 3 orders: $100 placed and paid; $150 placed, paid, and then $30 refunded; $200 placed and paid
**When:** An actor views the Sales by Month report for the period
**Then:** Gross Sales = $450 (sum of order totals at payment time); Net Sales = $420 (Gross − $30 refund); the report clearly labels which is Gross and which is Net
**Dangerous data / edge case:** An order whose total was modified post-placement (e.g., manual adjustment by admin) — Gross Sales must use the total at the moment of payment confirmation, captured as a snapshot.

### AC-081: Period selector filters correctly across all report types
**Area:** Analytics
**Type:** regression
**Given:** Orders spanning multiple months; the actor selects period "This month"
**When:** Each of the 12 report types is viewed with the "This month" selector
**Then:** All reports show only data from the current calendar month (in the store's configured timezone); no data from prior months appears; "This month" uses the store timezone's midnight as the boundary
**Dangerous data / edge case:** An order placed at 23:59 store-time on the last day of the month — it must appear in "This month" and not in "Last month".

### AC-082: Collection CTR = clicks / impressions for that collection
**Area:** Analytics
**Type:** unit
**Given:** Collection "Dresses": 200 product-card impressions recorded; 80 product clicks recorded from that collection
**When:** An actor views the Sales by Collection report
**Then:** % CTR for "Dresses" = 40.0% (80 / 200); the metric is not contaminated by clicks from other collections or from direct product page visits
**Dangerous data / edge case:** A product appearing in two collections — its click from collection A is attributed only to collection A, not to both; double-counting must be prevented.

### AC-083: Customer-local-hour uses delivery-country timezone
**Area:** Analytics
**Type:** integration
**Given:** An order placed at 14:00 UTC for delivery to France (UTC+2); another order placed at 14:00 UTC for delivery to Japan (UTC+9)
**When:** An actor views the Sales by Hour report
**Then:** The France order is bucketed at hour 16 (2 PM local); the Japan order is bucketed at hour 23 (11 PM local); neither is bucketed at the UTC hour of 14
**Dangerous data / edge case:** An order with delivery to the United States (multiple timezones) — the system uses the delivery address state to resolve the IANA timezone; for states where a single timezone cannot be determined, document the approximation used.

### AC-084: State report shows state-level breakdown for US/CN/RU/IN
**Area:** Analytics
**Type:** integration
**Given:** Orders from New York (US), California (US), Bavaria (DE), and Osaka Prefecture (JP)
**When:** An actor views the Sales by State report
**Then:** US orders appear as two rows (New York, California) nested under a US group; Japan appears as a single country-level row (not per-prefecture); Germany appears as a single country-level row; the hierarchical view is unambiguous
**Dangerous data / edge case:** A US order where the state field contains a misspelling or abbreviation (e.g., "Calfornia" or "CA") — normalization must map it to a canonical state name; unrecognizable state values appear in an "Unknown" bucket, not as a new state row.

### AC-085: Dashboard "today" KPIs are timezone-aware
**Area:** Analytics
**Type:** integration
**Given:** A store with timezone = Europe/Paris; an order placed at 23:30 Paris time (01:30 UTC next day)
**When:** An actor in Paris views the dashboard at 23:45 Paris time on the same day
**Then:** The order appears in "Today" KPIs; it does not appear in tomorrow's KPIs when the actor views the dashboard the next day
**Dangerous data / edge case:** Daylight saving time transitions — the "today" boundary must use the IANA-aware timezone library, not a fixed UTC offset.

---

## Area 9 — SEO

> The primary SEO acceptance criteria are in `07_multilingual_seo.md` section 12 (`TST-*` IDs). The criteria below cover angles not addressed by those tests.

### AC-090: Product page has self-referencing canonical
**Area:** SEO
**Type:** SEO
**Given:** A product at URL `https://example.com/blue-dress/`
**When:** The page is rendered
**Then:** Exactly one `<link rel="canonical" href="https://example.com/blue-dress/">` is present; the canonical uses HTTPS, includes the trailing slash, and matches the page's own URL exactly (no query strings, no fragments)
**Dangerous data / edge case:** A shopper arriving via `?utm_source=facebook` — the canonical must still point to the clean URL without UTM parameters. See `TST-CAN-003`.

### AC-091: Translated product hreflang targets only translated-languages ∩ available-countries
**Area:** SEO
**Type:** SEO
**Given:** A product published in EN and FR; the store ships to US, CA, FR; the EN domain targets US+CA; the FR domain targets FR
**When:** The FR product page is rendered
**Then:** hreflang tags are exactly {fr: FR URL, en: EN URL, x-default: EN URL}; no DE or other language entry exists even if DE is a configured language but without a published translation for this product
**Dangerous data / edge case:** A product translated to FR but the FR translation status = pending (not published) — no FR hreflang must be emitted. See `TST-CAN-004`.

### AC-092: Collection with 0 products returns 404
**Area:** SEO
**Type:** SEO
**Given:** A smart collection whose rules match 0 published products
**When:** A crawler or shopper requests the collection URL
**Then:** HTTP 404; the collection URL appears in no sitemap and no hreflang group; a user-friendly 404 page is rendered (not a blank response)
**Dangerous data / edge case:** A collection that was indexable (3+ products) but all products were unpublished — the transition to 404 must propagate to the sitemap within the debounce window. See `TST-IDX-collection`.

### AC-093: Duplicate canonical URLs never emitted
**Area:** SEO
**Type:** SEO
**Given:** A product at its primary URL `/dress/` and its A/B variant at `/dress-v2/`
**When:** Both pages are rendered
**Then:** `/dress/` has canonical = `/dress/`; `/dress-v2/` has canonical = `/dress/`; the sitemap contains exactly one entry for this product (the primary URL); no two distinct URLs share the same canonical value unless one is an A/B variant of the other
**Dangerous data / edge case:** A manual redirect from `/dress-old/` to `/dress/` — the redirect source must not appear in the sitemap or as a canonical value.

### AC-094: Slug with accented characters normalizes correctly
**Area:** SEO
**Type:** SEO
**Given:** A collection titled "Été Chic" (French for "Summer Chic")
**When:** The slug is auto-generated
**Then:** Slug = `ete-chic` (É → e, accent stripped via NFD normalization); `GET /collections/ete-chic/` returns HTTP 200; no URL under `/collections/` contains raw accented characters
**Dangerous data / edge case:** Manual slug input with a raw accented character via the admin form — the system must normalize on save, not reject the input. See `TST-SLUG-001`.

### AC-095: Redirect chain A→B→C collapsed to A→C at write time
**Area:** SEO
**Type:** regression
**Given:** A product that had slug A, then was changed to B (creating redirect A→B), then changed to C
**When:** The slug changes to C and the system creates redirect B→C
**Then:** The existing A→B redirect is rewritten to A→C; `GET /a/` returns 301 → `/c/` in a single hop; `GET /b/` returns 301 → `/c/`; no redirect chain requires more than one hop
**Dangerous data / edge case:** A cycle: slug changed from A to B, then back to A — the system must detect the loop and not create a redirect from A to itself or restore a deleted redirect. See `TST-SLUG-032`.

---

## Area 10 — Multilingual

### AC-100: Product page in FR has translated slug, title, and meta
**Area:** Multilingual
**Type:** integration
**Given:** A product with EN slug `blue-dress`, EN title "Blue Dress", and a published FR translation with slug `robe-bleue`, title "Robe Bleue", meta_title ~~"Robe Bleue — Pradize"~~ "Robe Bleue | Pradize" (**DECIDED, human, 2026-07-10, separator amendment — see 07_multilingual_seo.md META-001**)
**When:** A request is made to `https://pradize.fr/robe-bleue/`
**Then:** HTTP 200; page title is "Robe Bleue | Pradize"; `<html lang="fr">`; all internal links (breadcrumbs, navigation, collection cards) resolve to FR URLs; the EN URL `https://pradize.com/blue-dress/` returns HTTP 200 with its own EN content; cross-contamination of languages is zero
**Dangerous data / edge case:** An FR page whose collection breadcrumb links to the EN collection URL (wrong language) — the single resolver function must prevent this.

### AC-101: Changing language resolves to correct translated URL
**Area:** Multilingual
**Type:** integration
**Given:** The `resolve_permalink` function is called for a product object with target_lang = "fr"
**When:** The function is invoked from 5 different rendering contexts: navigation menu, product card on collection page, breadcrumb, hreflang tag, sitemap entry
**Then:** All 5 contexts return the same FR URL (`https://pradize.fr/robe-bleue/`); no context independently derives the URL; the resolver is called once per page render context and cached
**Dangerous data / edge case:** A product with a FR translation in "pending" status (not published) — `resolve_permalink(..., "fr")` must return None or raise, never return the EN URL as a fallback, and the calling context must handle the None case by omitting the link.

### AC-102: Menu items resolve to current-language URL at render time
**Area:** Multilingual
**Type:** regression
**Given:** A navigation menu item that references a product object by PK (not by a stored URL)
**When:** The menu is rendered in EN and then in FR
**Then:** EN rendering produces the EN product URL; FR rendering produces the FR product URL; no stored URL string in the menu record; changing the product's FR slug updates the FR menu link automatically on next render without re-saving the menu
**Dangerous data / edge case:** A menu item where the referenced product has been deleted — the menu item must render as invisible or with a safe fallback, not as a 500 error.

### AC-103: hreflang never points to a URL that 404s
**Area:** Multilingual
**Type:** integration
**Given:** A product published in EN only (FR translation status = pending)
**When:** The EN product page is rendered
**Then:** No FR hreflang is present; the EN hreflang group contains only {en, x-default}; a script that fetches every URL referenced in any hreflang attribute across the whole site must find zero 404 responses
**Dangerous data / edge case:** A translation was published, then the translation record was deleted without unpublishing — the system must detect this and remove the hreflang before the next page render.

### AC-104: Unpublished translation results in 404 on translated URL
**Area:** Multilingual
**Type:** integration
**Given:** A product whose FR translation status = pending
**When:** A request is made to the FR product URL `/fr/robe-bleue/`
**Then:** HTTP 404 (not a soft 200 with partial EN content); the 404 page is rendered in French (localized text); the product does not appear in the FR sitemap
**Dangerous data / edge case:** A crawler that saw the FR URL in hreflang before the translation was removed — the 404 must be real (not soft) so crawlers deindex it. See `TST-ML-011`.

---

## Area 11 — Pixels & Tracking

### AC-110: Facebook Pixel "Purchase" event fires only once per order
**Area:** Pixels & Tracking
**Type:** regression
**Given:** A Facebook pixel installed; a shopper completes an order and lands on the thank-you page; the shopper reloads the thank-you page
**When:** The thank-you page loads the first time and again on reload
**Then:** The "Purchase" pixel event fires exactly once (on first load); the second load (reload) does not fire the Purchase event again; deduplication uses an event ID derived from the order ID so that even if the pixel fires twice, Facebook's dedup mechanism can deduplicate server-side
**Dangerous data / edge case:** A shopper who navigates away from the thank-you page and then clicks the browser back button — must not re-fire the Purchase event.

### AC-111: UTM parameters captured at first page view, not at checkout referrer
**Area:** Pixels & Tracking
**Type:** integration
**Given:** A shopper arrives at the store via a URL with `?utm_source=instagram&utm_campaign=spring_sale`; the shopper browses for 20 minutes and then checks out
**When:** The order is created
**Then:** The order's traffic source attribution records `instagram / spring_sale` (from the first page view); the referrer at checkout (which may be an internal checkout page) does NOT overwrite the attribution; the traffic-source report shows this order attributed to `instagram`
**Dangerous data / edge case:** A shopper who visits from Instagram, leaves, then returns directly and completes a purchase — the attribution is last-touch within the 30-day attribution window; the direct return does NOT reset attribution if the Instagram visit was within 30 days.

### AC-112: "Purchase" pixel event not fired at payment-intent creation
**Area:** Pixels & Tracking
**Type:** security
**Given:** A shopper whose payment fails after the payment intent is created (e.g., card declined)
**When:** The failed payment event is processed
**Then:** No "Purchase" pixel event (Facebook, Google, TikTok, Pinterest, Snapchat) is fired; the `EVT_PURCHASE` tracking event is not recorded; only a confirmed payment triggers the purchase event
**Dangerous data / edge case:** A Stripe webhook that fires `payment_intent.created` before `payment_intent.succeeded` — the system must only fire the purchase event on the confirmed payment webhook, not on intent creation.

---

## Area 12 — Reviews

### AC-120: AI-generated review marked with provenance flag
**Area:** Reviews
**Type:** unit
**Given:** An AI review-generation job completes and creates a review record
**When:** The review is saved to the database
**Then:** The review record has `source = "ai"`; this flag is stored even if the review is auto-published; the review is excluded from structured data JSON-LD (never in `Review` items in the product page's schema); the admin review list shows a visual indicator distinguishing AI reviews from customer and merchant reviews
**Dangerous data / edge case:** An AI-generated review that passes the admin's manual approval — approval does not change the `source` flag.

### AC-121: Customer-only display threshold applied on storefront
**Area:** Reviews
**Type:** integration
**Given:** A product with threshold N = 5; the product has 3 published customer reviews and 10 published AI reviews
**When:** A shopper views the product page
**Then:** All 13 reviews are shown (3 customer + 10 AI) because the threshold of 5 customer reviews has not been reached; after 2 more customer reviews are published (total = 5), only the 5 customer reviews are shown and the 10 AI reviews are hidden from the storefront
**Dangerous data / edge case:** An AI review that is hidden by a moderator — it must not count toward the customer-review threshold even if subsequently re-approved.

### AC-122: Review request email sent N days after shipment, not after order
**Area:** Reviews
**Type:** regression
**Given:** An order placed on Day 1; the order is marked shipped on Day 10; the review-request delay is configured as 45 days
**When:** Time elapses
**Then:** The review-request email is sent on Day 55 (10 + 45), not on Day 46 (1 + 45); the trigger is the shipment timestamp, not the order creation timestamp; if the order is never shipped, the email is never sent
**Dangerous data / edge case:** An order that is partially shipped (some items shipped, others not) — the review request triggers N days after the first shipment date.

### AC-123: Verified-buyer badge requires order-email match
**Area:** Reviews
**Type:** unit
**Given:** A review submitted by `buyer@example.com`; an order exists for `buyer@example.com` containing the reviewed product; another review submitted by `organic@example.com` with no matching order
**When:** Both reviews are displayed on the product page
**Then:** The review from `buyer@example.com` displays a "Verified Buyer" badge; the review from `organic@example.com` does not display a badge; the badge check is performed server-side on render (not client-side)
**Dangerous data / edge case:** A reviewer who submits with a slightly different email (`Buyer@EXAMPLE.com`) — the match is case-insensitive.

---

## Area 13 — Payments

### AC-130: Geography rule returns correct organization
**Area:** Payments
**Type:** integration
**Given:** Stage-1 routing rules: Rule 1 (priority 1): "Country in EU → Org-FR"; Rule 2 (priority 2): "Country in All → Org-US (default)"; a buyer with a France delivery address
**When:** An order is placed
**Then:** The routing engine matches Rule 1 (France is in EU) and selects Org-FR; Org-US is not used; the order record stores the selected org for audit purposes
**Dangerous data / edge case:** A buyer with a delivery address in Switzerland (not in EU, not explicitly listed) — falls through to Rule 2 (catch-all) and uses Org-US (or the configured default org).

### AC-131: Allocation rule distributes income using running counter
**Area:** Payments
**Type:** integration
**Given:** A Stage-2 allocation rule for a pool of Org-A (60%) and Org-B (40%); 10 sequential orders are placed
**When:** The 10 orders are routed
**Then:** Over 10 orders, Org-A receives 6 orders and Org-B receives 4 orders; the distribution converges toward 60/40 using a running counter (not random per order); the running counter state persists across server restarts
**Dangerous data / edge case:** Concurrent orders arriving at exactly the same time — the running counter must be updated atomically (no two orders select the same org based on the same counter snapshot).

### AC-132: Backup processor failover when primary health = RED
**Area:** Payments
**Type:** integration
**Given:** A payment method "Card" configured with strategy = backup_chain; Stripe (primary) and PayPal (backup); Stripe's health status is manually set to RED (blocked)
**When:** A buyer attempts to pay by card
**Then:** The checkout's Card method routes the charge to PayPal (backup); Stripe receives no charge attempt; the buyer's card is charged via PayPal's card-processing capability; the order record notes the actual processor used
**Dangerous data / edge case:** Both the primary and backup are RED — the card payment method must display "unavailable" (if "hide when no route available" is enabled) and the buyer cannot proceed with that payment method.

### AC-133: alternate_evenly strategy uses round-robin running counter
**Area:** Payments
**Type:** integration
**Given:** A payment method configured with strategy = alternate_evenly; 3 processor accounts in the list (P1, P2, P3); 6 consecutive orders are placed
**When:** The 6 orders are processed
**Then:** Orders 1, 4 → P1; orders 2, 5 → P2; orders 3, 6 → P3 (strict round-robin); if P2 becomes blocked mid-sequence, the counter skips P2 and the next order goes to P3
**Dangerous data / edge case:** A processor account that is removed from the list mid-rotation — the counter must not produce an index-out-of-bounds error; it wraps around the remaining active accounts.

### AC-134: Payment method hidden when no route is available
**Area:** Payments
**Type:** integration
**Given:** A payment method with "Hide method when no processor route is available" = ON; all processor accounts for that method are blocked or unhealthy
**When:** A buyer reaches the payment step of checkout
**Then:** That payment method card/button is not rendered in the checkout UI; no "unavailable" greyed-out button is shown (it is fully absent); if at least one route becomes available (health restored), the method reappears on the next checkout load
**Dangerous data / edge case:** A buyer who has the checkout page open in a cached tab when the route becomes unavailable — placing the order must be rejected server-side, not rely on client-side hiding.

---

## Area 14 — Up-sell & Conversion

### AC-140: Order bump shown at correct placement
**Area:** Up-sell & Conversion
**Type:** integration
**Given:** A bump product configured for "Floating Cart" placement; a second bump product configured for "Product Page" placement; neither is configured for Checkout placement
**When:** A shopper adds the main product to cart (floating cart opens) and then navigates to the checkout page
**Then:** The Floating Cart popup shows the first bump; the product page shows the second bump; the checkout page shows no bumps; each bump appearance fires an impression event
**Dangerous data / edge case:** The bump product is out of stock — no bump card is shown even if the placement is configured; no error is surfaced to the shopper.

### AC-141: Post-purchase funnel — offer accepted leads to next offer
**Area:** Up-sell & Conversion
**Type:** integration
**Given:** A 2-step upsell funnel: step 1 = Product X ($29); step 2 = Product Y ($15); if step 1 accepted, show step 2; if step 1 declined, show step 1's decline-branch offer (Product Z at $9)
**When:** A shopper completes the base purchase, is shown offer 1 (Product X) and accepts it
**Then:** Product X is charged immediately using the stored payment token; the shopper is then shown offer 2 (Product Y); if accepted, Product Y is also charged; `EVT_UPSELL_ACCEPTED` fires for each accepted offer
**Dangerous data / edge case:** The payment token used for one-click charges must be the one from the base order; if the token is expired, the system must skip the upsell and show the thank-you page normally (no error to the shopper).

### AC-142: Post-purchase funnel — offer declined leads to decline-branch offer
**Area:** Up-sell & Conversion
**Type:** integration
**Given:** The same 2-step funnel as AC-141; offer 1 = Product X
**When:** The shopper declines offer 1 (clicks "No thanks")
**Then:** `EVT_UPSELL_DECLINED` fires for Product X; the decline-branch offer (Product Z at $9) is shown; the shopper can accept or decline Product Z; accepting Product Z charges it via the stored token
**Dangerous data / edge case:** A funnel where the "decline branch" is not configured — declining offer 1 takes the shopper directly to the standard thank-you page without a second offer.

### AC-143: Capture window expiry — no upsell button shown
**Area:** Up-sell & Conversion
**Type:** regression
**Given:** A one-click upsell offer whose capture window (post-purchase charge window) has expired (e.g., 30 minutes after the base purchase)
**When:** A shopper opens the thank-you page after the capture window has closed (e.g., by using the order confirmation email link)
**Then:** No upsell CTA button is rendered; the thank-you page shows only the standard order confirmation; no charge is attempted
**Dangerous data / edge case:** A shopper who keeps the thank-you page tab open past the capture window and then clicks "Yes, add to my order" — the server-side charge attempt must be rejected with a "window expired" error, not charged.

### AC-144: Storewide timed discount shows timer and creates correct single-use discount on next order (updated per DECIDED 2026-07-04)
**Area:** Up-sell & Conversion
**Type:** integration
**Given:** A storewide discount campaign: 15% off; "Discount code expires after N days" left at the default (30 days); a shopper completes a purchase
**When:** The shopper views the thank-you page and sees the timer + a unique generated `CampaignCode`; later, within the expiry window, uses the code on a new order
**Then:** The timer on the thank-you page counts down; the code is valid and applies 15% to the new order; the code's `used_at` and redeeming-order FK are set; after `expires_at` (creation + 30 days by default, per the campaign's "Discount code expires after N days" setting), the code is rejected with "expired"; the timer is cosmetic but the expiry is enforced server-side; the code is also emailed to the customer (email and thank-you page carry the same code)
**And:** The code is **single-use**: a second redemption attempt with the same code — by anyone — is rejected, and no second discount is applied
**Dangerous data / edge case:** A shopper who screenshots the coupon code and shares it — the single-use constraint means only the first redemption succeeds; concurrent simultaneous redemptions must not both succeed (atomic `used_at` claim).

### AC-145: Thank-you page access is token-gated (Security)
**Area:** Up-sell & Conversion
**Type:** security
**Given:** A completed guest order
**When:** An anonymous user accesses the thank-you URL without a valid `thankyou-view` token
**Then:** The page returns HTTP 403 (or redirects to the home page); no order details or PII are disclosed
**And:** The accept/decline endpoints reject POST requests without a valid single-use `upsell-act` token (HTTP 403)
**And:** The `upsell-act` token cannot be reused after one successful use — a subsequent use of the same token returns HTTP 403 and no charge is attempted
**And:** The accept/decline endpoints are POST-only, CSRF-protected, and rate-limited (3 attempts per order; after 3 failed attempts the session is auto-declined and no further charge is possible)
**Dangerous data / edge case:** An attacker who enumerates order IDs — opaque (non-sequential) order IDs in URLs provide defense in depth, but the token is the actual access control; a guessable order ID without a valid token must be rejected. A leaked confirmation email link (which carries only `thankyou-view`) must never be able to trigger charges — only the single-use `upsell-act` token embedded in the page DOM can authorize a charge.

### AC-146: Store-admin campaign takes precedence over super-admin campaign (DECIDED 2026-07-04)
**Area:** Up-sell & Conversion
**Type:** integration
**Given:** Product P has an active super-admin campaign A and an active store-admin campaign B of the same type
**When:** A shopper triggers the campaign slot for product P
**Then:** Campaign B (store-admin) fires; campaign A does not fire for product P; B's impression counter increments, A's does not
**And:** In the store-admin campaign list, campaign A is visible with a "Super-admin" badge and every field is read-only (edit/save attempts are rejected server-side, not just hidden in the UI)
**And:** For a product Q that has only super-admin campaign A (no store campaign), campaign A fires normally
**Dangerous data / edge case:** The store-admin campaign B is paused or draft — an inactive store campaign does not suppress the super-admin campaign; A fires for product P while B is not active.

---

## Area 15 — Lead Capture

### AC-150: Overlay shows on pages not in exclusion list
**Area:** Lead Capture
**Type:** integration
**Given:** An active lead-capture campaign with exit-intent trigger; page `/checkout/` is in the exclusion list
**When:** A shopper browses the home page (trigger fires) and the checkout page (trigger fires)
**Then:** Overlay appears on the home page; overlay does NOT appear on the checkout page regardless of exit-intent detection; `EVT_LEAD_CAPTURE` fires only for the home page impression
**Dangerous data / edge case:** A page that is added to the exclusion list after a shopper has already loaded it — the exclusion must be checked at trigger time (server-side), not only at page load.

### AC-151: Frequency cap prevents double-show in same session
**Area:** Lead Capture
**Type:** integration
**Given:** A campaign with frequency cap = 1 impression per 7 days
**When:** A shopper sees the overlay on Day 1; the same shopper (same browser cookie) visits again on Day 3
**Then:** No overlay is shown on Day 3; the cookie/localStorage entry is present and checked; on Day 8, the shopper sees the overlay again (cap resets after 7 days)
**Dangerous data / edge case:** A shopper in private/incognito mode has no cookie — the overlay may show again; this is acceptable behavior since the session is isolated.

### AC-152: Mobile/tablet trigger fires on exit-intent disabled; time trigger fires instead
**Area:** Lead Capture
**Type:** integration
**Given:** A campaign with Desktop trigger = Exit-intent; Mobile/Tablet trigger = Time (10 seconds)
**When:** A mobile shopper visits the page and stays for 10 seconds; a desktop shopper moves the cursor to the top of the browser window (exit-intent)
**Then:** The mobile shopper sees the overlay after 10 seconds; the desktop shopper sees the overlay on exit-intent; if Mobile/Tablet trigger = Disabled, no overlay fires on mobile regardless of time spent
**Dangerous data / edge case:** A tablet in desktop mode (user-agent reports desktop) — the trigger must be based on the canonical device detection (UA + screen width), not solely user-agent.

### AC-153: Customer tagged on signup
**Area:** Lead Capture
**Type:** integration
**Given:** A campaign configured with tag "newsletter_subscriber"
**When:** A shopper submits the overlay form with email `new@example.com`
**Then:** A customer record is created (or updated if email exists) with tag "newsletter_subscriber"; the tag appears in the Customers list for this email; if the email already exists as a customer, the tag is additive (existing tags are not removed)
**Dangerous data / edge case:** A shopper who signs up via two different campaigns with different tags — both tags must be added to the customer record.

### AC-154: Coupon issued automatically on overlay signup
**Area:** Lead Capture
**Type:** integration
**Given:** A campaign configured to auto-issue a coupon with code "WELCOME10"; the overlay text promises "10% OFF"
**When:** A shopper successfully submits the overlay form
**Then:** The coupon code "WELCOME10" is displayed in the confirmation message (or a unique generated code if per-subscriber unique codes are configured); the code is valid and applies 10% off on the shopper's next order; if the code is displayed in plain text, it is the actual redeemable code (not a placeholder)
**Dangerous data / edge case:** Two shoppers sign up simultaneously and both receive the same shared code — the code's total-use limit must accommodate this; if unique-per-subscriber codes are used, each shopper gets a distinct code.

---

## Area 16 — Currency

### AC-160: Displayed prices converted from store default to display currency
**Area:** Currency
**Type:** integration
**Given:** Store default currency = USD; EUR is enabled as a display currency with exchange rate = 0.92; a product priced at $100 USD
**When:** A shopper selects EUR in the currency picker
**Then:** The product page shows "€92.00" (with the configured .99 psychological rounding: "€91.99"); all collection page prices, cart totals, and checkout subtotals display in EUR; the price shown uses the configured symbol/code visibility format
**Dangerous data / edge case:** Stale exchange rate (rate is 2 days old): the displayed price uses the cached rate without surfacing staleness to the shopper; the product's base USD price is not modified.

### AC-161: Transaction currency is the store default (display-only conversion)
**Area:** Currency
**Type:** integration
**Given:** A shopper with EUR display currency selected; the store's default currency is USD; the shopper places an order
**When:** The payment is processed
**Then:** The processor is charged in USD (the store default); the processor charge does not use the EUR display amount; the order record stores the USD amount charged; the order confirmation email shows the USD amount
**Dangerous data / edge case:** A shopper who disputes a charge because their bank shows a different amount after currency conversion — the order record must clearly document that the charge was in USD and the EUR display was informational only.

### AC-162: Rounding applied consistently with .99 psychological pricing
**Area:** Currency
**Type:** unit
**Given:** A product priced at $49.99 USD; EUR exchange rate = 0.92
**When:** Prices are displayed in EUR
**Then:** $49.99 × 0.92 = $45.99 → displayed as "€45.99" (not "€45.9908"); if the .99 rounding mode is configured, "€46.00" would round down to "€45.99" (nearest .99 ending); rounding is consistent: the cart total, the checkout summary, and the collection card all show the same EUR amount
**Dangerous data / edge case:** A product with a price that converts to exactly X.00 — with .99 rounding, this should display as (X-1).99, not X.00.

---

## Area 17 — Shipping

### AC-170: Most-specific zone matched over "All Countries"
**Area:** Shipping
**Type:** integration
**Given:** Two zones: Zone A = "All Countries" with base rate $15; Zone B = "France" with base rate $8
**When:** A shopper with a France delivery address proceeds to shipping selection
**Then:** Zone B's rate ($8) is applied, not Zone A's ($15); the "France" zone matches before the "All Countries" zone regardless of the order in which zones were created
**Dangerous data / edge case:** A delivery address in "Réunion" (FR overseas territory, ISO code RE) — if Zone B explicitly lists "France (FR)" but not "Réunion (RE)", the catch-all Zone A applies.

### AC-171: Weight-range exception applies to correct weight bracket
**Area:** Shipping
**Type:** unit
**Given:** A zone with base rate $10; an exception: Weight from 0 to 2 kg → $5 (free promo rate); a cart with a single product weighing 1.5 kg
**When:** The shipping rates are calculated for this cart
**Then:** The exception applies; shipping cost = $5; a cart with a 3 kg product uses the base rate of $10; the exception boundary is inclusive on the lower end and exclusive on the upper end (or as documented — the boundary rule must be specified)
**Dangerous data / edge case:** A cart where two products together weigh 2.0 kg — the exception boundary (0 to 2 kg) must clarify whether 2.0 exactly is included or excluded.

### AC-172: FBA availability exception overrides standard rate
**Area:** Shipping
**Type:** integration
**Given:** A zone with base rate $12; an exception of type "FBA inventory available" with rate $0 (free because FBA ships it)
**When:** A cart contains a product with FBA sync enabled and the FBA API confirms inventory is available
**Then:** Shipping cost = $0 (FBA exception applies); if the FBA API is unavailable or returns "no inventory", the base rate $12 applies; the checkout shows the shipping method description from the FBA exception row
**Dangerous data / edge case:** A mixed cart with one FBA product and one non-FBA product — the FBA exception applies only to the FBA product portion; the non-FBA product uses the base rate (or the exception applies to the whole cart if cart-level matching is used — this must be specified).

### AC-173: Carrier tracking URL generates correct URL
**Area:** Shipping
**Type:** unit
**Given:** A carrier "UPS" with tracking URL `https://www.ups.com/track?tracknum=` (append style); an order with tracking number "1Z999AA10123456784"
**When:** The order detail page renders the tracking link
**Then:** The link href = `https://www.ups.com/track?tracknum=1Z999AA10123456784`; clicking the link opens the carrier's tracking page in a new tab; a different carrier "DHL" with URL `https://www.dhl.com/track/{tracking_number}` produces `https://www.dhl.com/track/1Z999AA10123456784`
**Dangerous data / edge case:** A tracking number containing special characters or spaces — must be URL-encoded in the appended link.

---

## Area 18 — Email

### AC-180: Order confirmation sent after payment confirmed, not after checkout start
**Area:** Email
**Type:** regression
**Given:** A shopper who starts checkout, enters their email, but whose payment is declined
**When:** The payment failure event is processed
**Then:** No order confirmation email is sent; the "Order confirmation" email is triggered only after the processor returns a successful payment confirmation (not after payment intent creation or checkout start)
**Dangerous data / edge case:** Stripe's `payment_intent.created` webhook fires before `payment_intent.succeeded` — the confirmation email must be tied to the confirmed webhook, not the creation webhook.

### AC-181: Review-request email sent after shipment, not after order
**Area:** Email
**Type:** regression
**Given:** An order placed on Day 1; shipment marked on Day 10; review-request delay = 30 days
**When:** Days elapse
**Then:** The review-request email is sent on Day 40 (10 + 30); no email is sent on Day 31 (1 + 30); if the order is never marked shipped, no email is ever sent
**Dangerous data / edge case:** An order with multiple shipments (partial fulfillment) — the review-request timer starts from the first shipment date, not the final.

### AC-182: Abandoned-checkout email not sent if order was completed
**Area:** Email
**Type:** regression
**Given:** A shopper who abandons checkout (email captured, no payment); the abandonment timeout is 1 hour; 45 minutes later, the shopper returns and completes the purchase
**When:** The 1-hour timeout elapses
**Then:** No abandoned-checkout email is sent; the campaign's "Unique Impressions" counter is not incremented; the abandoned-checkout record has status = "recovered"; any queued but unsent emails in the sequence are cancelled
**Dangerous data / edge case:** A shopper who completes a purchase using a different browser session than the abandonment — the system must match by email address, not by browser session, to correctly clear the abandonment flag.

### AC-183: Email template variable rendering is correct
**Area:** Email
**Type:** unit
**Given:** An order confirmation email template using variables: `{{ order.order_number }}`, `{% for item in order.items %}{{ item.title }}{% endfor %}`, `{{ address.shipping_city }}`
**When:** A confirmation email is sent for order #3112 with 2 items (Blue Dress, Red Scarf) shipping to Paris
**Then:** The rendered email contains "3112", "Blue Dress", "Red Scarf", and "Paris" in the correct positions; no raw template variable syntax (`{{` or `{%`) appears in the rendered output
**Dangerous data / edge case:** A variable that references a None value (e.g., `{{ customer.phone }}` when no phone is set) — must render as empty string, not "None" or raise an exception.

---

## Area 19 — Redirects

### AC-190: Slug change auto-creates 301; old slug redirects to new
**Area:** Redirects
**Type:** regression
**Given:** A published product with slug `old-slug`; actor changes the slug to `new-slug`
**When:** `GET /old-slug/` is requested after the save
**Then:** HTTP 301 → `/new-slug/`; the redirect is in the Redirects list with origin = "auto_slug_change"; `GET /new-slug/` returns HTTP 200 with the product page content
**Dangerous data / edge case:** A slug change performed via the API (not the admin form) — the pre-save hook must fire and create the redirect regardless of update path. See `TST-SLUG-030`.

### AC-191: Manual redirect creation blocked in store admin
**Area:** Redirects
**Type:** permission
**Given:** An actor with Settings access in the store admin visits the Redirects screen
**When:** The actor looks for an "Add a Redirect" button or sends a POST to the redirect creation endpoint
**Then:** No "Add a Redirect" button is present in the UI; a direct POST to the redirect creation endpoint returns HTTP 403 or 404; only auto-generated redirects appear in the list
**Dangerous data / edge case:** A creative actor who tries to create a redirect via the Django REST API or directly in the Django admin (if exposed) — the creation endpoint must be blocked at the view/permission level.

### AC-192: Redirect chain collapsed to single hop
**Area:** Redirects
**Type:** regression
**Given:** Redirect A→B exists; the slug changes again creating B→C
**When:** `GET /a/` is requested
**Then:** HTTP 301 → `/c/` in a single hop (no intermediate redirect to `/b/`); `GET /b/` returns HTTP 301 → `/c/`; the system rewrites A→B to A→C at write time, not at request time
**Dangerous data / edge case:** A third slug change creating C→D: A must now point to D directly (chain: A→C→D collapsed to A→D). See `TST-SLUG-032`.

---

## Area 20 — Multi-Tenant Isolation

### AC-200: Store A data not accessible from store B's admin
**Area:** Multi-Tenant Isolation
**Type:** security
**Given:** Store A has orders, products, and customers; Store B's admin user is authenticated
**When:** Store B's admin accesses any list endpoint (orders, products, customers) using store B's session
**Then:** Only store B's data is returned; store A's records are never included; the record count matches store B's actual record count (not the combined total of A+B)
**Dangerous data / edge case:** A URL-based store selector (e.g., `?store_id=A`) — store B's session must not be able to switch context to store A by URL manipulation.

### AC-201: Employee with store-A access cannot access store B
**Area:** Multi-Tenant Isolation
**Type:** permission
**Given:** An employee assigned to store A only; the employee is authenticated
**When:** The employee attempts to navigate to store B's admin or sends a request to a store-B endpoint
**Then:** HTTP 403 or redirect to store A's context; store B's data is never returned; the employee's session cannot be elevated by adding `?store_id=B`
**Dangerous data / edge case:** An employee whose store-A role is revoked while they have an active session — the next request must return 403 (session-based permission must be re-evaluated on each request, not cached for the session lifetime).

### AC-202: Super-admin can access all stores without restriction
**Area:** Multi-Tenant Isolation
**Type:** permission
**Given:** 5 stores exist with distinct products and orders; a super-admin is authenticated
**When:** The super-admin views the organization list, drills into each store's order list and product list
**Then:** Each store's data is returned correctly; no permission error; the super-admin can see all 5 stores' orders in separate views; the total record counts match the sum across all stores
**Dangerous data / edge case:** A super-admin who modifies data in store A — the change is scoped to store A and does not affect store B's data.

### AC-203: API endpoints require org authentication
**Area:** Multi-Tenant Isolation
**Type:** security
**Given:** An API endpoint that returns an organization's orders
**When:** An unauthenticated client requests the endpoint; an authenticated client from a different org requests the endpoint
**Then:** Unauthenticated request returns HTTP 401; cross-org request returns HTTP 403; only the authenticated org's own orders are returned on a valid authenticated request
**Dangerous data / edge case:** A JWT or session token that has been tampered with to include a different org's ID — the server must verify the token cryptographically and reject the tampered request.

---

## Area 21 — Catalog Feeds

### AC-210: Facebook Dynamic Product Feed contains required fields
**Area:** Catalog Feeds
**Type:** integration
**Given:** A store with 5 published products; the FB DPA feed is configured to sync "All Products"
**When:** The feed is fetched at its feed URL
**Then:** The XML response is valid XML; each product entry contains: `id`, `title`, `description`, `link` (primary product URL), `image_link`, `price`, `availability`, `condition`; the `id` field is PK-based (not slug-based); a product whose slug was recently changed has the same `id` in the feed
**Dangerous data / edge case:** A product with a special character in the title (e.g., "&", "<", ">") — must be XML-escaped in the feed; raw unescaped HTML/XML characters would break the feed parser.

### AC-211: Products without images excluded from feed
**Area:** Catalog Feeds
**Type:** unit
**Given:** A published product with no images uploaded; all other products have images
**When:** The feed is generated
**Then:** The image-less product does not appear in any feed (FB, Google Shopping, Pinterest); other products appear normally; the feed status page shows the product exclusion reason = "no images"
**Dangerous data / edge case:** A product whose image URL returns a 404 (broken image link) — should the product be excluded or included with the broken link? Recommend: exclude after N consecutive failed image fetches.

### AC-212: Out-of-stock products in feed with correct availability
**Area:** Catalog Feeds
**Type:** unit
**Given:** A product with Fixed Quantity inventory mode and current stock = 0
**When:** The feed is generated
**Then:** The product appears in the feed with `availability: out of stock`; it is NOT excluded from the feed (keeps ad item history); a product in "Always SOLD OUT" mode also appears with `availability: out of stock`; a product in "Ask for Quotation" mode is excluded from the feed entirely (no fixed price)
**Dangerous data / edge case:** A product transitioning from in-stock to out-of-stock between two feed generations — the next feed generation (triggered by the inventory change event) must reflect the new availability.

### AC-213: Feed URL is token-protected
**Area:** Catalog Feeds
**Type:** security
**Given:** A feed URL of the form `/feeds/facebook/us-en.xml?token=<store_secret_token>`
**When:** A request arrives without the token or with an incorrect token
**Then:** HTTP 403 is returned; the feed XML is not served; the token is per-store and rotatable from the admin; the feed URL is listed in `robots.txt` under `Disallow: /feeds/` to prevent search engines from indexing it directly
**Dangerous data / edge case:** A token that appears in public URLs (e.g., shared in a screengrab) — the admin must be able to rotate the token and update the URL in Facebook Business Manager without losing historical ad data.

---

## Area 22 — AI Job System

### AC-220: Translation job fails validation — record marked ERROR, not saved
**Area:** AI Job System
**Type:** unit
**Given:** A translation job for a product's FR title; the AI returns an empty string as the translation
**When:** The job result is saved
**Then:** The translation record is marked `status = error` with a human-readable error message (e.g., "Translation result was empty"); no empty translation is saved to the database; the FR product URL continues to return 404 (the pending/error status is treated as unpublished); the job appears in the admin job log with ERROR status
**Dangerous data / edge case:** An AI response that returns the source-language text instead of translating it — the validation must detect language identity (e.g., if FR source was requested and the output is in EN, flag as error).

### AC-221: Chunk-based translation — failed chunk B preserved from chunk A
**Area:** AI Job System
**Type:** integration
**Given:** A product description too long to translate in a single API call; it is split into chunks A and B; chunk A is translated successfully; chunk B fails (API timeout)
**When:** The job processes the failure
**Then:** Chunk A's translated result is persisted; chunk B is marked as failed and queued for retry; the product translation status = in_progress (not error); a retry of the job processes only chunk B, not chunk A; after chunk B succeeds, the full translation is assembled and the status becomes published
**Dangerous data / edge case:** A retry that re-processes chunk A — chunk A's result must not be duplicated or overwritten with a different translation that creates inconsistency.

### AC-222: Job heartbeat updated while processing
**Area:** AI Job System
**Type:** integration
**Given:** A long-running translation job (processing a large product catalog)
**When:** The job runner processes the job over 10 minutes
**Then:** The job's `heartbeat_at` timestamp is updated at least every N minutes (e.g., every 2 minutes) while the job is running; an admin viewing the job list can see it is still active; the heartbeat timestamp advances monotonically
**Dangerous data / edge case:** A job runner that dies mid-job — heartbeat stops updating; the reclaim mechanism detects this.

### AC-223: Dead job (no heartbeat > threshold) is reclaimed
**Area:** AI Job System
**Type:** integration
**Given:** A job with `status = in_progress`; the job runner processing it crashes; no heartbeat update has occurred for > 10 minutes
**When:** The job reclaim process runs (e.g., a periodic cron or health-check)
**Then:** The job status is reset to `queued` (or `error` if max retries exceeded); it becomes available for a different runner to pick up; the failed runner's partial output is either discarded or preserved (depending on chunk completion state per AC-221)
**Dangerous data / edge case:** A job that appears dead but the runner is just very slow (e.g., waiting on a large API response) — the heartbeat threshold must be generous enough to avoid reclaiming active jobs.

### AC-224: Idempotent re-run skips already-done steps
**Area:** AI Job System
**Type:** regression
**Given:** A translation job that has successfully completed chunks 1 and 2; chunk 3 failed; the job is re-run
**When:** The job runner processes the re-run
**Then:** Chunks 1 and 2 are not re-processed (their completed status is detected); only chunk 3 is retried; the final assembled translation contains chunks 1, 2 (from prior run), and 3 (from retry) without duplicates or gaps
**Dangerous data / edge case:** A code deploy between the original run and the retry that changes the chunk boundaries — the re-run must handle the boundary mismatch gracefully (recommend: if boundaries change, discard and rerun from scratch).

---

## New Critical Uncertainties Discovered While Writing

The following uncertainties were identified while authoring these acceptance criteria. They are not yet recorded in `11_uncertainties_to_validate.md` and should be reviewed:

| New ID | Area | Description | Impact |
|---|---|---|---|
| AC-U1 | Discount Engine | **Refund method for gift-card-paid orders** (UF-K): when an order paid partly by gift card is refunded, does the credit go back to the gift card balance, to the processor, or as a new gift card? This is unresolved and blocks AC-042/AC-061 implementation. | GiftCardTransaction model, refund flow |
| AC-U2 | Inventory / Variants | **Per-variant inventory mode override**: the spec describes inventory mode at the product level, but B2B quotation and mixed-availability variants (some in stock, some SOLD OUT) imply a per-variant mode. Must clarify whether inventory mode is per-product or per-variant. | Product model schema |
| AC-U3 | Orders | **Partial refund on a gift-card-plus-card order**: if an order was paid 50% by gift card and 50% by Stripe card, and a partial refund is issued, which payment instrument is refunded first? | Refund logic, GiftCardTransaction |
| AC-U4 | Feeds | **Per-country feed currency**: an EN domain shipping US+CA — does the CA feed show USD prices or CAD prices? The current spec says "display-only conversion" but feed consumers (Google Merchant) expect the charge currency; if USD is charged for CA buyers, the feed must declare USD with a note. Escalate as FEED-C1 per `07_multilingual_seo.md`. | FEED-003, currency model |
| AC-U5 | AI Jobs | **Validation of AI translation language identity**: when a translation job returns output in the wrong language (e.g., English when French was requested), how does the system detect this? No language-detection mechanism is specified. | AC-220, translation job spec |
| AC-U6 | Pixels | **Pixel deduplication mechanism**: the spec states "Purchase event deduped on thank-you page reload" but does not specify the dedup mechanism (localStorage flag, server-side event ID, Stripe event ID). Must be defined before implementing. | AC-110 |
| AC-U7 | Upsells | **One-click upsell payment token security**: the spec marks this CRITICAL (uncertainty #5). AC-141/AC-143 depend on a confirmed token reuse model. This blocks upsell funnel implementation. | Post-purchase upsell |
| AC-U8 | Analytics | **Collection CTR double-counting for multi-collection products**: a product in 3 collections that is clicked from collection B — the click must be attributed only to collection B. The event schema must capture `source_collection_id` at click time, not at session level. | AC-082, EVT_PRODUCT_CTR event spec |

---

## Summary

| Area | AC IDs | Count |
|---|---|---|
| 1. Authentication & Permissions | AC-001 – AC-007 | 7 |
| 2. Product Catalog | AC-010 – AC-017 | 8 |
| 3. Inventory Modes | AC-020 – AC-027 | 8 |
| 4. Collections | AC-030 – AC-035 | 6 |
| 5. Discount Engine | AC-040 – AC-050 | 11 |
| 6. Orders & Fulfillment | AC-060 – AC-065 | 6 |
| 7. Abandoned Checkout | AC-070 – AC-075 | 6 |
| 8. Analytics | AC-080 – AC-085 | 6 |
| 9. SEO | AC-090 – AC-095 | 6 |
| 10. Multilingual | AC-100 – AC-104 | 5 |
| 11. Pixels & Tracking | AC-110 – AC-112 | 3 |
| 12. Reviews | AC-120 – AC-123 | 4 |
| 13. Payments | AC-130 – AC-134 | 5 |
| 14. Up-sell & Conversion | AC-140 – AC-146 | 7 |
| 15. Lead Capture | AC-150 – AC-154 | 5 |
| 16. Currency | AC-160 – AC-162 | 3 |
| 17. Shipping | AC-170 – AC-173 | 4 |
| 18. Email | AC-180 – AC-183 | 4 |
| 19. Redirects | AC-190 – AC-192 | 3 |
| 20. Multi-Tenant Isolation | AC-200 – AC-203 | 4 |
| 21. Catalog Feeds | AC-210 – AC-213 | 4 |
| 22. AI Job System | AC-220 – AC-224 | 5 |
| **Total** | | **120** |

**SEO cross-reference:** 30 additional `TST-*` criteria in `07_multilingual_seo.md` section 12 cover URL routing, slugs, canonicals, hreflang, sitemaps, robots.txt, structured data, meta, feeds, and Amazon links. These are not duplicated here; the combined test surface is 120 + 30 = 150 acceptance criteria.
