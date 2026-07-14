# 04_admin_flows — Step-by-step Admin Flows

> Status: DRAFTED by Spec Agent 2026-07-02.
> Two separate Django admin sites: store admin at `/admin/`, super-admin at `/superadmin/`.
> Same Django User model with role flags (`is_store_admin`, `is_super_admin`).
> PENDING markers cite uncertainty numbers from `11_uncertainties_to_validate.md`.

---

## Store Admin Flows (`/admin/`)

These flows are performed by a Store Admin or a permitted Employee inside the per-store administration interface.

---

### AF-001: Log In / Log Out

**Actor:** Any user with `is_store_admin` or `is_super_admin` flag, or an Employee with at least one module permission  
**Admin site:** `/admin/` (login page); redirects to `/superadmin/` if the user has only `is_super_admin`  
**Preconditions:** User account exists; user is not currently authenticated

**Steps:**

1. User navigates to `/admin/user/login`.
2. System displays the login card: Email input, Password input (masked), "Remember me" checkbox (checked by default), "Forgot password?" link, and a full-width "Login" button.
3. User enters email and password; optionally unchecks "Remember me".
4. User clicks "Login".
5. IF credentials are invalid:
   - System shows an inline error. User remains on the login page.
   - ELSE (credentials valid):
6. System resolves role:
   - IF `is_super_admin` AND NOT `is_store_admin`: redirect to `/superadmin/` dashboard.
   - IF `is_store_admin` (with or without super-admin): redirect to `/admin/` dashboard.
   - IF Employee only: redirect to `/admin/` dashboard of the assigned store(s).
7. "Remember me" checked → system sets a long-lived session cookie (30 days, implementer decides exact value). Unchecked → session cookie only.

**Log out:**

1. User clicks their avatar/initials in the top-right corner of any admin page.
2. System displays a dropdown with a "Log out" option.
3. User clicks "Log out".
4. System invalidates the session and redirects to `/admin/user/login`.

**Postconditions:** User is authenticated (or logged out) and redirected appropriately.

**Edge cases:**
- Rate-limiting / lockout after N failed attempts — PENDING (low-priority; implementer sets a safe default, e.g., 10 failures → 15-minute lock).
- 2FA/MFA — PENDING (out of scope for v1; add later).
- Password reset: user clicks "Forgot password?" → system sends a time-limited reset link to the entered email (token expiry and page sequence TBD by implementer; standard Django password-reset flow is acceptable).

---

### AF-002: Invite Employee + Set 18-Module Permission Matrix

**Actor:** Store Admin (or Employee with Settings — Full access)  
**Admin site:** `/admin/`  
**Preconditions:** Actor has permission to manage Employee Accounts module; an email server is configured

**Steps:**

1. Actor navigates to **Employee Accounts** via the main menu.
2. System lists all active employee accounts with name, email, and last-login timestamp.
3. Actor clicks "Invite New User" (green button, top right).
4. System opens the employee detail panel (master-detail layout):
   - Fields: Full name, Email, Phone (informational only — PENDING whether used for 2FA).
   - Access mode: "Full Access" or "Limited Access" (segmented selector).
5. IF Actor selects "Full Access":
   - Per-module grid is hidden (or greyed out). The employee receives unrestricted access to all modules on this store.
   - ELSE (Limited Access):
6. System shows the 18-module permission grid. For each module, actor sets one of the available radio options:

   | Module | Options |
   |---|---|
   | Apps | Full access / No access |
   | CMS | Full access / No access |
   | Customers | Full access / No access |
   | Dashboard | Full access / No access |
   | Domains | Full access / No access |
   | Gift cards | Full access / No access |
   | Inventory | Full access / No access |
   | Orders | Full access / **Limited access** / No access |
   | Products & Collections | Full access / No access |
   | Reports | Full access / No access |
   | Settings | Full access / No access |
   | Themes | Full access / No access |
   | Up-sell campaigns | Full access / No access |
   | Abandoned campaigns | Full access / No access |
   | Pixels | Full access / No access |
   | Reviews | Full access / No access |
   | Currency Converter | Full access / No access |
   | Security Badge | Full access / No access |

   Note: "Invoice Orders", "CSV Templates", "Files", and "Zapier" modules are **removed** from the engine (per owner annotations).

7. Actor clicks "Save".
8. System sends a "Store invitation" email to the new employee (transactional template — see AF-108).
9. System adds the employee to the list with a green status dot (active) and records creation timestamp.

**Postconditions:** Employee account created; invitation email sent; employee can log in with the assigned permissions.

**Edge cases:**
- PENDING (CRITICAL): Exact semantics of Orders "Limited access" — recommended interpretation: employee can view and update fulfillment status but cannot issue refunds or export CSV. Must be confirmed.
- PENDING (CRITICAL): Whether "Settings" permission grants access to the Employee Accounts module itself (potential privilege escalation). Recommended: "Settings" does NOT implicitly grant employee management; a separate "Employee Accounts" sub-permission should exist. Must be confirmed.
- Employee management permission: currently no "Employees" row in the module grid. Recommended: access to this screen derives from the "Settings — Full access" flag, but this needs confirmation.
- Editing an existing employee: actor clicks the chevron `>` on an existing row. Same panel opens pre-filled. Actor can toggle the account on/off (active/inactive) independently of deletion. "Delete Account" (red outline) hard-deletes the record after a confirmation prompt.
- An employee can be assigned to multiple stores — managed from the super-admin (see AF-112).

---

### AF-003: Create Product

**Actor:** Store Admin or Employee with Products & Collections — Full access  
**Admin site:** `/admin/`  
**Preconditions:** Store is configured; at least one collection exists (optional — can assign later)

**Steps:**

1. Actor navigates to **Add a Product** from the main menu (or clicks "Add a product" from the product list).
2. System opens the product edit form.
3. Actor sets **variant mode**: "Single Variant" or "Multi Variant (size, color, etc.)".
4. Actor fills in **basic fields**:
   - Type (dropdown — enabled only if Product Page setting "Type" is on).
   - Collections (multi-select chip, zero or more).
   - Product Title (required).
   - Description (rich-text editor with tabs; tabs are configured globally in Product Page Settings — see AF-018).
5. Actor fills in **logistics fields**:
   - Shipping weight + unit (enabled only if "Shipping weight" setting is on).
   - "Does this product require shipping?" dropdown (Yes / No).
   - Inventory policy (dropdown — options: "Don't track inventory", "Fixed inventory", "Fake server-assigned inventory", "Always SOLD OUT", "Pre-order (with day to receive)", "Ask when available", "Ask for quotation") — PENDING (exact vocabulary from extra-spec; implementer to enumerate).
   - IF inventory policy is "Fixed inventory": per-variant quantity fields appear.
   - IF inventory policy is "Pre-order": date-to-receive field appears.
   - IF inventory policy is "Ask for quotation": "Starting at $" price field appears (optional).
   - Vendor (combobox — enabled only if "Vendor" setting is on).
   - Tags (chip input — enabled only if "Tags" setting is on).
6. **FBA sync checkbox** (enabled only if FBA integration is configured in super-admin):
   - Checkbox "Sync with FBA inventory if available" — checked by default when FBA is configured. PENDING (Amazon FBA integration scope).
7. Actor configures **variants** (Multi Variant mode):
   - For each option: Option Title (e.g., "Size"), optionally tick "Changes Product Look" (links values to images).
   - Option Values: chip editor (add/remove size chips).
   - Display mode: "Text Dropdown" or "Thumbnail selector" (shown on storefront).
   - Additional options added via "Add another option".
   - System auto-generates variant rows (cartesian product of all option values).
   - For each variant row: Price (required), Compare at price (optional), SKU (optional), availability toggle.
   - PENDING (CRITICAL): Availability toggle per variant = sold-out marker. "SOLD OUT settings (erase inventory if checked)" annotation means toggling this marks the variant as sold-out and clears any tracked inventory count. Must be confirmed.
8. Actor uploads **images** via the drag-drop zone ("Upload All Variant Images"). Images are numbered and can be reordered. IF "Changes Product Look" was ticked on an option, actor assigns specific images to specific option values.
9. **Product videos**: Actor can add one or more video URLs (position: first by default). PENDING (video hosting: upload vs embed URL; confirm before implementing).
10. Actor fills in **SEO fields**:
    - Page Title (SEO) — defaults to Product Title.
    - Meta Description.
    - URL & Handle (slug) — auto-generated from title; editable.
    - Social sharing metadata (Open Graph / Pinterest) — PENDING (requested in annotation; fields to be defined).
11. Actor configures **upsell/bump** settings (optional — "MARKETING" section):
    - Upsell products: search and select products for the post-purchase upsell page.
    - Bumps on Product Page: search and select bump products.
    - Bumps on Checkout Page: search and select bump products.
    - Bumps on Float Cart: search and select bump products.
    - Related Products: search and select.
    - Custom Fields: "Add A Customization Field" for customer personalization (text or image upload).
    - Product Bundles: "+ New Bundle" → select one companion product → set discount %.
12. **Multi-variant page A/B test toggle** (experimental feature): Actor can enable "Create multiple URL variants of this product page" where only images differ — basic A/B impression/conversion statistics tracked. ~~PENDING (page A/B test data model)~~ **RESOLVED — ADR-028 (`ProductPageVersion`, TICKET-042), ACCEPTED (human, 2026-07-11)**: the toggle is the presence of active `ProductPageVersion` rows on the product (no separate store-level feature flag). Each version owns an ordered subset of the product's existing images only (title/price/description/variants/CTA are identical across versions, per UF-005); versions get their own URL (`<product-slug>-<suffix>`, one per language with an active product permalink, cap 10 per product) canonicalized to the primary product URL, excluded from sitemap/hreflang; basic v1 statistics = views/add-to-carts/orders/conversion-rate per version, no significance testing. See `09_architecture_decisions.md` (ADR-028 index row) and `docs/adr/ADR-028-ab-variant-pages.md`.
13. **Timer promo** (optional): Actor can schedule a countdown timer promotion:
    - Select days of the week/month for display.
    - Set discount type (% / fixed / free product).
    - Localized per domain language and country. PENDING (AI job integration).
14. **Pre-order / Quotation mode** (if inventory policy set above): no additional step needed; the policy selection in step 5 drives storefront rendering.
15. Actor clicks **"Publish"** (blue) or **"Save draft"** (grey).
16. IF Publish:
    - System validates required fields (title, at least one price).
    - System creates the product record, generates the slug, and makes the page live.
    - ELSE (Save draft): product saved in draft state, not visible on storefront.

**Postconditions:** Product exists in the database with the chosen publish state; if published, storefront page is accessible at the handle URL.

**Edge cases:**
- Slug collision: system appends a numeric suffix (e.g., `-2`) automatically.
- If "Changes Product Look" is enabled but no images are assigned to values: system shows a warning on save (not a hard block).
- Bundle "Discount Name" is auto-generated from "Product A + Product B — N% off". PENDING (confirm or expose name field).
- "Buy on Amazon" button: IF FBA sync is on AND product has a valid ASIN AND super-admin "Add buy on Amazon button" is enabled → storefront shows an affiliate "Buy on Amazon" link alongside the Add to Cart button. PENDING.

---

### AF-004: Edit Published Product Slug (Auto-creates Redirect)

**Actor:** Store Admin or Employee with Products & Collections — Full access  
**Admin site:** `/admin/`  
**Preconditions:** Product exists and has been published at least once (has an active slug)

**Steps:**

1. Actor opens the product edit form for an existing published product.
2. Actor locates the **SEO** section and edits the **URL & Handle** field (slug).
3. System shows a notice: "Changing the URL will create a redirect from the old URL."
4. Actor clicks **"Save"**.
5. System:
   - Saves the new slug.
   - Looks up every previously-published slug for this product that does not already have a redirect pointing away from it.
   - For each such slug, creates a new redirect record: `old-slug → new-slug` (301 permanent redirect).
   - The redirect is immediately active (no manual step).

**Postconditions:** Product is accessible at the new slug. All previously-published slugs redirect to the new slug. Redirects appear in the Redirects list (view-only — see AF-017).

**Edge cases:**
- Redirect chain: if the product has had three slugs (A → B → C), the system must ensure A → C directly (not A → B → C chain). Implementer must flatten redirect chains on each slug change.
- Loop detection: saving the same slug as current is a no-op (no redirect created).
- Manually editing a redirect's destination (from the Redirects list) to override an auto-created one is still permitted.
- Collection and CMS page slug changes follow the same auto-redirect logic.

---

### AF-005: Manage Smart Collection (Rule-based)

**Actor:** Store Admin or Employee with Products & Collections — Full access  
**Admin site:** `/admin/`  
**Preconditions:** Store has products; actor has access to Collections

**Steps:**

1. Actor navigates to **Collections List** and clicks "Add a collection" (or opens an existing one).
2. System opens the collection form:
   - Collection Title (required).
   - Collection Description (optional, rich text).
   - Image upload (drag-drop zone).
3. Actor selects the **"Auto add products based on conditions"** tab.
4. Actor sets the **match mode**: "Match all conditions" (AND) or "Match any condition" (OR).
5. Actor configures **Condition #1**:
   - Field dropdown: Product title / Product vendor / Product type / Product price / Product tag / Weight / Variant title.
   - Operator dropdown:
     - For text fields: "Is exactly" / "Starts with" / "Ends with" / "Contains" / "Does not contain".
     - For numeric fields (price, weight): "Is greater than" / "Is less than" / "Is equal to" / "Is between". PENDING (numeric operators not shown in screenshots; safe default assumed).
   - Value text input.
6. Actor clicks **"Add Rule"** to add more conditions (Condition #2, #3, …).
7. System shows a **live match count**: "This collection matches N products" (displayed after each rule change). PENDING (whether real-time or on-save; real-time via AJAX is the recommended default).
8. Actor fills in the **SEO** section (Page Title, Meta Description, URL & Handle).
9. Actor clicks **"Publish"** or **"Save draft"**.
10. System saves the collection; IF publishing, evaluates all active products against the rules and assigns matching ones to the collection.
11. Going forward, system re-evaluates collection membership whenever a product is created, edited, or deleted. PENDING (re-evaluation timing: real-time on product save vs background cron — recommend real-time for small catalogs, async job for large ones).

**Postconditions:** Smart collection exists; matching products are included; storefront collection page is live (if published).

**Edge cases:**
- "Product price" condition matching: matches the product's base (variant minimum) price in the store's default currency. Sale/compare-at prices are not used. PENDING (must be confirmed; multi-currency interaction TBD).
- Weight condition uses the store's configured weight unit.
- Switching from Auto to Manual mode: existing auto-assigned products remain but auto-update stops. Confirmation prompt recommended.
- Operator case sensitivity: default is case-insensitive string matching.

---

### AF-006: Manage Manual Collection

**Actor:** Store Admin or Employee with Products & Collections — Full access  
**Admin site:** `/admin/`  
**Preconditions:** Collection form open in "Manually Add Products" mode

**Steps:**

1. Actor opens the collection form (create or edit) and selects **"Manually Add Products"** tab.
2. Actor fills in title, description, and image (same as AF-005, steps 2–3).
3. To **add a product**, actor types in a search field within the product list section. System shows a matching product list (live autocomplete). Actor selects a product; it is added to the bottom of the collection's product list.
4. System displays each product row with:
   - Drag handle (manual reordering).
   - Thumbnail, product title, variant count, price.
   - Performance columns (if analytics are available): **Display rate** (collection-page impressions showing this product) | **CTR rate** (clicks on product / impressions) | **Purchase rate** (orders containing product / impressions). PENDING (column definitions; see `11_uncertainties_to_validate.md` analytics section).
   - Trash icon to remove product from collection.
5. Actor **reorders** products by dragging rows.
6. **Auto-sort suggestion**: IF performance columns are populated, the system may display a button "Auto-sort by purchase rate" that reorders products by descending purchase rate. PENDING (auto-sort algorithm — recommend purchase rate as primary, CTR as tiebreaker).
7. Actor fills in SEO section.
8. Actor clicks "Publish" or "Save draft".

**Postconditions:** Manual collection saved; products ordered as specified; storefront renders products in that order.

**Edge cases:**
- Switching to Auto mode from Manual: confirmation prompt warns that manual product list will be cleared and replaced by rule-matching results.
- Duplicate add: system silently ignores adding a product already in the collection.
- Image auto-crop preview (requested annotation): when an image is uploaded, the system shows a preview of how it will appear cropped to the storefront card aspect ratio. PENDING (aspect ratio to be defined in theme specs).

---

### AF-007: Process Order

**Actor:** Store Admin or Employee with Orders — Full access (or Limited access with restricted capabilities)  
**Admin site:** `/admin/`  
**Preconditions:** At least one order exists in the system

**Steps — View Orders List:**

1. Actor navigates to **Orders**.
2. System displays a paginated table (20 per page default): order #, date/time, customer name, total, payment-status badge, fulfillment-status badge.
3. Payment status badges: PAID (green), REFUNDED (yellow), PARTIALLY REFUNDED (yellow), PENDING (grey — PENDING status enumeration, see critical uncertainties).
4. Fulfillment status badges: NOT SENT TO FULFILLMENT, SENT TO FULFILLMENT, SHIPPED, PARTIALLY SENT TO FULFILLMENT & SHIPPED.
5. Actor can **filter** orders using the filter panel (opened from "All Orders ⌄"):
   - Order status, Refund status, Last 4 card digits, Date range, Order number(s), Customer name(s), Customer email(s), Product name keywords (new requirement — matches any order line where product title contains the keyword; AND semantics between multiple keywords; match against title snapshot at order time). PENDING (keyword AND vs OR; match against current vs snapshot title).
   - Buttons: "Refine View" (apply), "Reset", "Cancel". "Save View" is **removed**.

**Steps — View and Change Fulfillment Status:**

6. Actor clicks a row or order number link to open the **order detail** page.
7. System displays order lines, payment details, shipping address, fulfillment status per line item, and action buttons.
8. Actor updates fulfillment status:
   - IF actor marks items as "Sent to fulfillment": system updates fulfillment status badge; records timestamp.
   - IF actor marks as "Shipped" (with optional tracking number + carrier selection): system updates badge and, if shipping-confirmation email is enabled, triggers the automated "Shipping confirmation" email to the customer.
   - Partial fulfillment: actor can mark individual line items separately, resulting in composite statuses (PART. SENT TO FUL. & SHIPPED).

**Steps — CSV Fulfillment Export:**

9. Actor selects one or more orders (checkboxes) from the orders list.
10. Actor clicks **"CSV Fulfillment"** button (top right; also accessible with zero selection for exporting all filtered orders). PENDING (CRITICAL: exact CSV column spec — order ID, product SKU/title, quantity, shipping address fields, etc. Must be defined before implementing).
11. System generates and downloads a CSV file of the selected orders.
12. Import-back (tracking numbers): PENDING (whether a "import tracking CSV" upload step exists to bulk-mark orders as shipped with tracking numbers — this is required for external fulfillment providers; mark PENDING until confirmed).

**Steps — Partial Refund:**

13. Actor opens an order detail page for a paid order.
14. Actor clicks "Issue Refund" (or equivalent refund action).
15. System shows a refund dialog:
    - Line-item checkboxes and quantity inputs to select what to refund.
    - A "Notify customer" checkbox (when checked, triggers the "Refund" automated email).
    - Refund amount calculated automatically; actor can adjust (partial amount override). PENDING (whether actor can refund more than the order total, e.g., to cover return shipping — recommend: No).
16. Actor clicks "Confirm Refund".
17. System calls the payment processor's refund API; updates payment status to PARTIALLY REFUNDED or REFUNDED; records the refund amount.

**Postconditions:** Order status badges updated; customer notified if applicable; refund processed with the payment processor.

**Edge cases:**
- Employee with Orders "Limited access": can view and change fulfillment status; CANNOT issue refunds or trigger CSV fulfillment export. PENDING (confirm scope).
- Orders with RTL customer names (Hebrew, Arabic): display must support Unicode RTL text.
- Order numbers are non-contiguous (gaps are normal; not an error).

---

### AF-008: Issue Manual Gift Card

**Actor:** Store Admin or Employee with Gift cards — Full access  
**Admin site:** `/admin/`  
**Preconditions:** Gift card module is enabled; email sending is configured

**Steps:**

1. Actor navigates to **Issued Gift Cards**.
2. System displays the list of all issued gift cards (code, status badge, generated date, expiry date, value).
3. Actor clicks "Issue a gift card" (green button).
4. System opens the "ISSUE A GIFT CARD" modal:
   - Gift card value (required, in default store currency).
   - Expiration date (optional; if empty → card never expires). PENDING (whether "never expires" is legal in all target jurisdictions).
   - "Send gift to the following email" (optional — if empty, card is created without emailing it).
   - Email subject line (default: "Your gift card code").
   - Message textarea with template variables `{gift_value}` and `{gift_code}`.
5. Actor fills in fields and clicks "Send".
6. System:
   - Auto-generates a unique gift card code (uppercase hex-style, 15–16 characters).
   - Creates a `DiscountCode` record with type = "manual_gift_card", stored value = entered amount, expiry as entered.
   - Creates an initial `GiftCardTransaction` record recording the issued balance.
   - IF email address provided: sends the delivery email using the template.
7. The new card appears in the gift card list with status "NOT USED".

**Postconditions:** Gift card record exists; delivery email sent (if address provided); card is redeemable at checkout.

**Edge cases:**
- Partial redemption: when the card is used at checkout for an order less than the card value, the remaining balance is stored in `GiftCardTransaction` and the card remains usable until balance = 0 or expiry is reached.
- Currency: card value is stored in the store's default currency. Multi-currency display on storefront: PENDING (conversion display-only vs stored per-currency).
- Card type in filter panel: "manual_gift_card" is one type. Other types: "auto_gift_card" (from automated campaigns), "coupon" (from coupon module). PENDING (full type vocabulary).

---

### AF-009: Create Coupon Code

**Actor:** Store Admin or Employee with Gift cards — Full access  
**Admin site:** `/admin/`  
**Preconditions:** Coupon module is accessible

**Steps:**

1. Actor navigates to **Coupon Codes**.
2. System displays the list of coupon campaigns (name, code chip, usage count, "Totalling" figure). PENDING (CRITICAL: "Totalling" = sum of discount amounts given via this code, not revenue — implementer to confirm this is the intended semantics).
3. Actor clicks "Add new campaign" (green button).
4. System opens the coupon create panel (left sidebar + right detail):
   - Campaign Name (not customer-visible; required).
   - Active toggle (on by default).
   - **SETTINGS section:**
     - Limit total uses: "Set the limit" (shows numeric input) / "Unlimited".
     - Limit uses per customer: "Set the limit" (shows numeric input) / "Unlimited". PENDING (identity for guest checkout: by email).
     - "Can be combined with other coupon codes" checkbox (unchecked by default).
     - Valid from date (required). "Never expires" checkbox; when unchecked → expiry date input appears.
   - **DISCOUNT AND CODE section:**
     - Type of discount (dropdown):
       - "$ off from order total"
       - "% off from order total"
       - "Free Shipping" (discount value field hidden)
       - "Free product" — actor selects one product from a product-picker. PENDING (whether free-product variant selection is needed).
     - Discount value (required for $ and % types).
     - Gift card code (auto-generated; actor can override with a custom code).
   - **WHEN THE COUPON CODE CAN BE USED section:**
     - Default: "Your campaign applies to all orders."
     - "Add rule" button: each rule restricts applicability. PENDING (CRITICAL: rule-type vocabulary — minimum order value, specific products, specific collections, customer tag). Implementer to define available rule types.
5. Actor clicks "Save" (blue).
6. System creates a `DiscountCode` record with type = "coupon" and the specified configuration.

**Postconditions:** Coupon is active (if toggle is on); customers can enter the code at checkout; usage counter starts at 0.

**Edge cases:**
- Code uniqueness: system rejects saving a code that already exists (case-insensitive check across all `DiscountCode` records including gift cards).
- When "Set the limit" is chosen but input is blank: system shows validation error — "Limit must be a positive integer".
- PENDING: Are coupons and gift cards in the same code namespace (they share the `DiscountCode` model)? Yes, per the confirmed decision — the UI should prevent duplicate codes across both types.

---

### AF-010: Configure Abandoned-Checkout Campaign

**Actor:** Store Admin or Employee with Abandoned campaigns — Full access  
**Admin site:** `/admin/`  
**Preconditions:** Email sending is configured (sender domain SPF/DKIM set up — see AF-017); abandoned-checkout timeout configured in super-admin (see AF-114)

**Steps:**

1. Actor navigates to **Abandoned Checkouts**.
2. System displays the campaign dashboard: number of active campaigns, campaign list in left sidebar, per-campaign recovery stats (Unique Impressions, Sales recovered, Revenue recovered %) for the selected date range. PENDING (CRITICAL: "Revenue recovered" attribution model — last-touch click-through within N hours; N to be confirmed).
3. Actor clicks "New Campaign" (green button).
4. System opens the campaign form:
   - Campaign name (required).
   - Campaign status toggle (Active, on by default).
   - From name (defaults to store name).
   - From email: local part editable; domain locked to store's sending domain (`@<storedomain.com>`). PENDING (CRITICAL: SPF/DKIM per-store domain setup process — implementer must define the sender verification flow).
5. Actor clicks "Add automated email" to open the **3-step wizard**:

   **Wizard Step 1 — Type:**
   - "Warning" (let them know a product is going out of stock or discount is ending).
   - "Reminder" (cart is still waiting for them).
   - "Incentive" (special limited-time offer with coupon code).
   - Actor selects a type and clicks "Continue".

   **Wizard Step 2 — Style:**
   - Four template options: "Plain Text", "Stylish", "Clean", "Minimal".
   - Live preview panel renders the selected template with sample cart contents, resume-checkout link, and unsubscribe link.
   - Actor selects a template and clicks "Continue".

   **Wizard Step 3 — Copy (accordion, 4 parts):**
   - **1 — Email subject**: curated suggestions (varies by Type selected in step 1) + "Write your own…" free-text.
   - **2 — Email headline**: curated suggestions + free-text.
   - **3 — Body text**: curated suggestions (for Incentive type, suggestions include `CODE123` placeholder) + free-text. PENDING (CRITICAL: `CODE123` must be replaced with a reference to a real `DiscountCode` record, or the system must auto-generate a unique per-customer coupon code — confirm approach).
   - **4 — Call to action button**: curated labels (e.g., "RESUME YOUR CHECKOUT", "TELEPORT TO YOUR CART") + free-text.
   - Each accordion part shows a green "done" state when complete; "Edit" link to re-open.
   - Actor clicks "Continue" after the final part.

6. **DECIDED (AF-C4, 2026-07-04):** The send delay is configured **inside the email wizard**, alongside Type/Style/Copy (no separate schedule step). UI: label **"Send after"** with a number field + a **hours/days dropdown**. The field is **required** — the email cannot be saved without a delay value. Default: **1 hour** after cart abandonment. Stored as `send_delay_hours` (days entered in the UI are stored as hours × 24).
7. The wizard closes and the email appears as a card in the campaign sequence.
8. Actor can add more automated emails (each with its own type, style, copy, and delay).
9. Actor clicks "Save" (or "Edit Settings" on an existing campaign) to save.
10. Active campaign: the abandon detection fires after the configured timeout (AF-114) and queues emails in the sequence according to their delays.

**Resume-checkout link:** The email contains a tokenized deep link. The token uniquely identifies the abandoned cart session without requiring the customer to log in. PENDING (CRITICAL: token scope, expiry window, and whether viewing the link invalidates it — security-critical).

**Unsubscribe link:** Mandatory in every email; unsubscribing removes the customer from all abandoned-checkout campaigns for this store. PENDING (scope of unsubscribe — store-level vs all-marketing).

**Postconditions:** Campaign is active; abandonment detection and email queueing begin.

**Edge cases:**
- "Warning" email type: whether it reads real stock data or is copy-only. PENDING (recommended: copy-only for v1).
- Multiple concurrent campaigns: system sends whichever campaigns are active; emails are sequenced per campaign.
- GDPR basis for emailing non-purchasers: PENDING (CRITICAL: legal basis must be decided — recommending "legitimate interests" with a clear unsubscribe; legal review needed).
- Definition of "abandoned checkout": a checkout session where an email address was captured but no order was completed, after N minutes of inactivity (N = AF-114 setting). PENDING.

---

### AF-011: Manage Reviews

**Actor:** Store Admin or Employee with Reviews — Full access  
**Admin site:** `/admin/`  
**Preconditions:** Reviews module is enabled

**Steps:**

**View Dashboard:**

1. Actor navigates to **Reviews**.
2. System displays:
   - Total review count.
   - Automated Review Request Emails status toggle (enabled/disabled globally).
   - "LATEST REVIEWS" panel with pending count badge and "Manage reviews" button.
   - Last 5 approved reviews (reviewer, product link, timestamp, star rating, status badge).
   - "REVIEW REQUEST STATISTICS" panel: Sent / Accepted / Acceptance rate for a date range.
   - "UP-SELL STATISTICS" panel: Impressions / Clicks / CTR / Orders / Revenue / Order rate for a date range. PENDING (MEDIUM: what the up-sell widget is — assumed a related-product block within review emails or on the review widget on the product page).

**Moderate Reviews:**

3. Actor clicks "Manage reviews" (or the gear icon → "Manage"). System opens the **MANAGE REVIEWS** modal.
4. Tabs: **Pending**, **Approved**, **Hidden**, **All reviews** (default active).
5. Search input: search by reviewer name or email.
6. Each review row shows: reviewer name, product link, star rating, timestamp, full text, photo thumbnails (if any).
7. Actions per review:
   - In **Pending** tab: "Approve" button → moves to Approved.
   - In **Approved** tab: "Hide" button → moves to Hidden.
   - In **Hidden** tab: "Approve" button → moves back to Approved.
   - Reviews can also be hidden from the **All reviews** tab.

**Add Merchant Review:**

8. Actor clicks "Add review" button (header area).
9. System opens the **ADD A REVIEW** modal:
   - Reviewer's name (free text).
   - Select a product (product-picker).
   - Review date (backdating allowed — any past date).
   - Star rating (5 / 4 / 3 / 2 / 1, default 5).
   - Review text (textarea).
   - Photo upload (drop zone; multiple images accepted).
   - "Create another" checkbox (keep modal open for batch entry).
10. Actor fills in fields and clicks "Submit".
11. System creates the review record. IF the auto-publish setting is "Automatically published": status = Approved immediately. ELSE: status = Pending (requires moderation).
12. Merchant-authored reviews are flagged internally as `source = "merchant"` (not displayed publicly but available for filtering and audit). PENDING (whether to expose this flag to store admin UI or keep it internal only).

**AI-generated Reviews (requested feature):**

13. In Settings (gear icon → Review Settings), a toggle "Enable AI-generated reviews (via jobs)" will exist. PENDING (CRITICAL: AI-generated reviews have consumer-protection law implications — provenance flag `source = "ai"` must be stored; the display-threshold setting ("display only customer reviews once N real reviews exist") is incompletely specified — N and per-product vs global scope must be decided).

**Send Review Request:**

14. Actor clicks "Send review request" to manually trigger a review-request email to a customer for a specific order (opens an order-picker or customer-picker). PENDING (exact flow not shown in screenshots).

**Configure Review Request Automation:**

15. Actor clicks the gear icon → "Review settings" (`/admin/apps/app/reviews/settings`):
    - Auto-publish vs moderated pipeline (segmented choice).
    - Notification email address.
    - Widget styling: stars color, button colors, verified-buyer badge style, layout (Tiles/Rows), show/hide widget on products without reviews, number of visible reviews.
    - Review request email: subject, delay (N days after order shipped — requires fulfillment tracking), template editor, test-send.

**Postconditions:** Reviews moderated; automated request emails scheduled; widget configured.

**Edge cases:**
- "Verified buyer badge" appears only on reviews where the reviewer's email matches a completed order containing that product.
- Old orders excluded from automated requests: "Old orders that won't trigger a request" viewer allows actor to see which orders are outside the request window.
- IF automated review request emails are disabled globally: no requests are sent, but manual send (step 14) still works.

---

### AF-012: Configure Pixels

**Actor:** Store Admin or Employee with Pixels — Full access  
**Admin site:** `/admin/`  
**Preconditions:** None

**Steps:**

1. Actor navigates to **Pixels**.
2. System displays a card grid of 5 providers: Facebook, Google Analytics (Enhanced Ecommerce), Snapchat, Pinterest, TikTok.
3. Each card shows install status ("Installed" green badge / "Not Installed").

**Install a pixel:**

4. Actor clicks "Install" on a not-installed provider card (or the "…" overflow menu → "Edit" for an installed one).
5. System opens an install/edit dialog for the provider:
   - Pixel ID / Tracking ID input (required).
   - Provider-specific fields if needed (e.g., API token for server-side events). PENDING (field spec per provider not shown).
6. Actor enters the ID and clicks "Save".
7. System stores the pixel ID; the storefront begins firing events automatically on the next page load.

**Events fired per provider:** PENDING (CRITICAL: exact ecommerce event mapping — at minimum: PageView, ViewContent (product page), AddToCart, InitiateCheckout, Purchase (with order value and currency). Deduplication on thank-you page reload is required for Purchase events (use event ID). Full event spec per provider must be defined separately).

**Verify events fire:**

8. Actor opens the storefront in a browser and uses the provider's native debug tool (e.g., Facebook Pixel Helper, Google Tag Assistant) to verify events are firing. The engine itself provides no built-in event verification UI (implementer may add a "Test events" button — PENDING).

**Uninstall a pixel:**

9. Actor clicks "…" → "Uninstall" (or equivalent).
10. System removes the pixel configuration; storefront stops firing events for that provider.

**Postconditions:** Selected pixels are active; specified events fire on storefront pages.

**Edge cases:**
- PENDING (MEDIUM): Whether multiple pixels per provider are supported (e.g., two Facebook pixels). Current UI shows one per provider; v1 = one per provider.
- Consent management (GDPR): pixel events should be gated by the customer's cookie consent. PENDING (consent mechanism is outside the scope of this screen; must be addressed in the storefront spec).

---

### AF-013: Configure Lead-Capture Overlay

**Actor:** Store Admin or Employee with Apps — Full access  
**Admin site:** `/admin/`  
**Preconditions:** None

**Steps:**

1. Actor navigates to **Lead Capture Overlay** (under Apps).
2. System displays the campaign list with per-campaign metrics: Visitors, Impressions, Conversion Rate, Conversions. Status dots: green (active), red (inactive).
3. Actor clicks "New campaign" (green button).
4. System opens the **Create an Overlay campaign** form:

   **GENERAL SETTINGS section:**
   - Overlay campaign name (required).
   - Status toggle (Active, on by default).
   - Trigger: "Exit" (exit-intent detection on desktop) / "Time" (time-on-page; IF Time → a seconds delay input appears).
   - Mobile and Tablet Trigger: "Disabled" / "Time" (IF Time → a seconds delay input appears for touch devices; no exit-intent on mobile).
   - "Exclude customers that visited pages" input: actor picks one or more store pages; visitors who have viewed those pages are excluded from seeing the overlay.

   **SELECT A THEME section:**
   - Scrollable grid of prebuilt theme cards. Actor selects one (blue border + checkmark).
   - PENDING (MEDIUM: whether theme text/discount copy is editable after selection — i.e., does selecting a theme give an editor for the overlay content, or is the text fixed? Recommend: each theme has editable fields for headline, body, CTA label).

   **UPON A SUCCESSFUL SIGN-UP section:**
   - "Display message" tab: actor writes a thank-you message in a textarea.
   - "Go to Url" tab: actor enters a redirect URL (e.g., a thank-you or offer page).

   **FREQUENCY CAPPING section:**
   - Checkbox "Set per user frequency cap". IF checked → inputs: [N] impressions per [N] [Minutes / Hours / Days (dropdown)].
   - Implemented via a browser cookie/localStorage per-user identifier.

   **TAGS FOR CUSTOMERS THAT SIGNUP section:**
   - Tag input: one or more tags applied to the customer record when they sign up via this overlay.
   - Tags are used for customer segmentation and filtering.

5. Actor clicks "Submit" to save.
6. IF Active: the overlay is immediately live on the storefront for matching visitors.

**Postconditions:** Campaign is saved and active (if toggled). Captures leads and applies tags to resulting customer records.

**Edge cases:**
- PENDING (MEDIUM): Where captured leads are stored — in the Customers list as a new customer record (email = entered email, source = overlay, tag = actor-configured tag). If the email already exists as a customer, the tag is added to the existing record.
- PENDING (MEDIUM): Whether sign-up automatically issues a coupon/gift card (e.g., the "10% OFF" shown in overlay previews). Recommended: a separate step in the form to optionally link an existing coupon code to auto-issue.
- Multiple concurrent campaigns: all active campaigns are eligible to display; frequency capping applies per-campaign. If multiple campaigns are eligible simultaneously, the system shows only one (recommend: the most recently created active campaign).
- "Impressions" = number of times the overlay was displayed to a visitor (not unique visitors).
- "Conversions" = number of times a visitor submitted the email form.

---

### AF-014: Set Up Currency Converter

**Actor:** Store Admin or Employee with Apps — Full access  
**Admin site:** `/admin/`  
**Preconditions:** None

**Steps:**

1. Actor navigates to **Currency Converter** (under Apps).
2. System displays the panel header "N Active Currencies" and a table with columns: Currency name / Symbol / Code / Symbol visibility / Code visibility.
3. Each row shows: enable toggle, currency symbol, ISO code, current visibility settings summary, and an "Edit" button.

**Enable / configure a currency:**

4. Actor clicks "Edit" on a currency row (e.g., Australian Dollar).
5. Row switches to edit mode:
   - Symbol input (editable, e.g., "$").
   - Code display (read-only ISO code, e.g., "AUD").
   - Symbol visibility: Yes/No + Prefix/Suffix (segmented buttons).
   - Code visibility: Yes/No + Prefix/Suffix (segmented buttons).
   - Confirm (✓) and Cancel (×) buttons.
6. Actor sets preferences and clicks ✓.
7. System saves per-currency display format.

**Disable a currency:**

8. Actor turns off the toggle on a currency row. The currency is no longer offered to storefront visitors; prices are not displayed in that currency.

**Storefront behavior:** A currency picker is shown to visitors. Prices are converted from the store's default currency at the current exchange rate. PENDING (CRITICAL: exchange-rate source — recommended: a daily-updated rate from a public API such as openexchangerates.org or ECB; update frequency and whether conversion is display-only vs transactional must be confirmed). PENDING (CRITICAL: rounding rules — recommended: round to 2 decimal places with psychological pricing option (.99 ending)). PENDING (MEDIUM: default currency selection for a visitor — recommend: geo-IP-based country → currency mapping, with manual override via picker).

**Postconditions:** Active currencies are shown in the storefront currency picker; prices displayed in visitor's selected currency.

**Edge cases:**
- Symbols that apply to multiple currencies (e.g., "$" for AUD, CAD, USD): displaying both symbol + code (e.g., "$59.99 AUD") avoids ambiguity — the default configuration for non-USD dollar currencies should include code visibility.
- Decimal/thousand separator localization: system uses the standard locale format for each currency (e.g., French EUR uses space-separator thousands and comma decimal). PENDING (implementer to confirm locale source).

---

### AF-015: View Analytics Reports

**Actor:** Store Admin or Employee with Reports — Full access  
**Admin site:** `/admin/`  
**Preconditions:** Store has traffic and order data

**Steps:**

1. Actor navigates to **Reports**. System displays the "Select a Report" hub with two sections:
   - **SALES**: By Product Title, By Collection, By Month, By Hour, By Country, By State, By Customer, By Traffic Source.
   - **TRAFFIC**: By Referrer, By Device, By Location, By Landing Page.
   - Note: "By Product Variant" is **removed** (owner cross-out).

2. Actor clicks a report card. System opens the report with:
   - Period dropdown (default "All time") — options: Custom period, Today, Yesterday, This week, Last week, This month, Last month, All time. IF "Custom period": a date-range picker appears (from/to). The period selector is available in **all reports**.
   - "Export to CSV" button (always present).
   - Report table (sortable columns — all columns sortable by clicking the header; recommended default sort per report below).
   - Totals footer row (where applicable: Sales by Hour, Traffic by Device).

**Report-specific notes:**

| Report | Default sort | Columns | New columns (owner-requested) |
|---|---|---|---|
| Sales by Product Title | Purchase rate desc | Product title, Type, Orders, Gross Sales | Collection display, Collection CTR, Page display, Purchase rate |
| Sales by Collection | Purchase rate desc | Collection, Type, Orders, Gross Sales | Display, Avg % scroll, % CTR (click ≥1 product), % Purchase (buy ≥1 product) |
| Sales by Month | Month desc (most recent first) | Month, Orders, Gross Sales | — |
| Sales by Hour | Hour asc (00–23) | Hour, Orders, Gross Sales | — |
| Sales by Country | Orders desc | Country, Orders, Gross Sales | — |
| Sales by State | Orders desc | Country/State (hierarchical — see below), Orders, Net Sales† | — |
| Sales by Customer | Lifetime spent desc | Customer name, Customer email, Orders, Gross Sales | — |
| Sales by Traffic Source | Orders desc | Referring URL, Orders, Gross Sales | — |
| Traffic by Referrer | Unique Visitors desc | Referring URL, Unique Visitors | — |
| Traffic by Device | Unique Visitors desc | Device (OS), Unique Visitors | — |
| Traffic by Location | Unique Visitors desc | Location (Country), Unique Visitors | — |
| Traffic by Landing Page | Unique Visitors desc | Landing Page, Unique Visitors | Avg page (avg pages/session), % Product CTR, % Purchase rate |

† "Net Sales" on the state report — PENDING (CRITICAL: whether intentionally net-of-refunds or a labelling error; recommend using the same "Gross Sales" definition as all other reports until confirmed otherwise).

**Sales by State — hierarchical display:**
- For multi-state countries (US, China, Russia, India, Canada, Australia, Brazil — PENDING: exact list): one row per state/province within that country.
- For all other countries: one aggregated row at the country level.
- PENDING (CRITICAL: state/province reference data must be normalised — free-text state values from existing orders are unreliable; a normalised state lookup must be used for new orders).

**Sales by Hour — customer-local hour:**
- Each order is bucketed by the customer's local hour, derived from the delivery country's primary time zone.
- PENDING (CRITICAL: countries with multiple time zones — US, Canada, Australia, Russia — cannot be resolved to one local hour from country alone; recommend storing the state/city from the delivery address and mapping to IANA time zone, or accepting a "majority time zone" approximation per country with a documented limitation).

**New analytics columns definitions (PENDING — CRITICAL: must be confirmed before implementing the event-tracking model):**
- **Collection display** (Sales by Product Title): number of times the product appeared in any collection-page listing visible to a visitor within the period.
- **Collection CTR** (Sales by Product Title): (clicks on product from collection listing) / (collection displays) within the period.
- **Page display** (Sales by Product Title): unique product page views.
- **Purchase rate** (Sales by Product Title): orders containing the product / page displays.
- **Display** (Sales by Collection): collection page views.
- **Avg % scroll** (Sales by Collection): average session scroll depth percentage on collection pages.
- **% CTR** (Sales by Collection): sessions clicking at least one product / collection page views.
- **% Purchase** (Sales by Collection): sessions purchasing at least one product / collection page views.
- **Avg page** (Traffic by Landing Page): average number of pages viewed per session starting on this landing page.
- **% Product CTR** (Traffic by Landing Page): sessions clicking at least one product / sessions with this landing page.
- **% Purchase rate** (Traffic by Landing Page): sessions purchasing / sessions with this landing page.

**Gross Sales definition:** PENDING (CRITICAL: recommend = sum of order totals (including shipping) before refunds; a separate "Net Sales" view = Gross Sales − refunds. Must be confirmed and applied consistently across all reports).

**Revenue / Gross Sales attribution:** Last-touch session referrer at order time. PENDING (CRITICAL: attribution model — recommend last-touch with a 30-day cookie window; must be confirmed).

**CSV export:** clicking "Export to CSV" downloads the full report table (all pages, not just current page) for the selected period.

**Postconditions:** Actor has viewed the desired report; optionally downloaded CSV.

**Edge cases:**
- "By Product Variant" report is removed — if any direct link to it exists, redirect to "By Product Title".
- Reports with zero data: display empty table with a message "No data for the selected period".
- PII in "Sales by Customer" export: the CSV contains names and email addresses; access is gated by Reports permission.

---

### AF-016: Configure Facebook Dynamic Product Feed and Google Shopping Feed

**Actor:** Store Admin or Employee with Apps — Full access  
**Admin site:** `/admin/`  
**Preconditions:** Products exist; feed URL format must match Facebook DPA / Google Merchant Center / Pinterest requirements

**Steps — Facebook DPA Feed:**

1. Actor navigates to **FB Dynamic Product...** (under Apps → Facebook).
2. System displays "Feed settings":
   - "Products to sync" dropdown: "All products from all collections" / "Select specific collections" / …. PENDING (MEDIUM: full option list not shown; recommend: All products, or filtered by one or more collections).
   - "Feed URL" (read-only, e.g., `https://yourdomain.com/fb-dpa/rss.xml`).
   - "Copy" button.
   - Status banner: "Your feed is up to date" (green) / "Feed is being regenerated" (amber) / "Feed is out of date" (red).
3. Actor selects the products-to-sync scope and the system schedules the feed for regeneration.
4. Actor copies the feed URL and pastes it into the Facebook Business Manager → Catalog → Data Source configuration.

**Steps — Google Shopping Feed (requested new feature):**

5. System provides a second feed configuration section (or a separate sub-page) for **Google Shopping**:
   - Same products-to-sync scope selector.
   - Feed URL: `https://yourdomain.com/google-shopping/feed.xml` (Google Merchant Center XML format).
   - "Copy" button.
   - Status banner.
6. Actor copies the URL into Google Merchant Center → Products → Feeds.

**Steps — Pinterest Shopping Feed (requested new feature):**

7. Similar to Google: a Pinterest feed section with its own URL (Pinterest Catalog XML format).

**Feed refresh:** Background job re-generates feed files periodically (recommended: every 6 hours, or triggered on product create/edit/delete). PENDING (MEDIUM: exact cadence and whether product changes immediately invalidate the feed vs batch-update).

**Feed field mapping (PENDING — MEDIUM):** For each provider, the engine maps product fields:
- id → product ID
- title → product title
- description → product description (first 500 chars)
- link → storefront product URL
- image_link → primary product image URL
- price → product price in default currency
- availability → "in stock" / "out of stock" based on inventory policy
- condition → "new" (hardcoded for v1)
- GTIN / MPN: PENDING (whether these fields are required; they may need a per-product data field).

**Postconditions:** Feed URLs are active and reachable; external platforms can import the product catalog.

---

### AF-017: Configure General Store Settings

**Actor:** Store Admin (only — this screen requires Settings — Full access and is sensitive)  
**Admin site:** `/admin/`  
**Preconditions:** Actor is authenticated as a store admin with Settings access

**Steps:**

1. Actor navigates to **General Settings** (`/admin/settings/general`).
2. System displays the Settings page with the following sections:

   **STORE INFORMATION:**
   - Store name (text input; used in email templates and storefront header).
   - Email address: local part (editable) + fixed domain suffix `@<storedomain.com>`. PENDING (CRITICAL: this is the store's outbound sending address; SPF/DKIM verification for custom domains must be defined — does the store use a shared sending domain managed by the platform, or a per-store verified domain?).
   - Logo upload (via a separate media uploader — not shown in this screen; assumed accessible from Themes).

   **PASSWORD PROTECT YOUR ENTIRE STORE:**
   - Toggle (off by default). IF on → a password input appears (visitors must enter this password to view the storefront — useful for staging or "coming soon" mode).

   **STANDARDS AND FORMAT PRESETS:**
   - Unit system (Metric / Imperial).
   - Default weight unit (g / kg / lb / oz).
   - Timezone: "Edit" link enables the timezone dropdown. The store timezone is used for day-boundary calculations (reports, scheduled jobs, etc.).

   **HEADER INCLUDE CODE** and **BODY INCLUDE CODE:**
   - Two large textareas for injecting arbitrary HTML/JavaScript into the `<head>` and end of `<body>` of all storefront pages.
   - Example use: third-party verification tags (Pinterest domain verify), custom analytics scripts, chat widgets.

   **ROBOTS.TXT INCLUDE TEXT:**
   - Textarea for custom robots.txt lines. Platform appends these to a base robots.txt (which allows all store pages by default).

   **ROOT DIRECTORY UPLOAD:**
   - Drag-and-drop zone for uploading static files to the store's root directory.
   - Supported formats: txt, xml, html, json.
   - Use case: Google site verification files, apple-app-site-association, ads.txt.
   - General images/videos use the product/collection image uploaders (the "Files" module is removed).

   **Per-language settings (requested feature):**
   - PENDING (MEDIUM: the owner annotation requests a per-language/per-domain settings table, similar to WebsiteEmpire's PaneSettings. Domain→Language→Country model is being defined separately — see uncertainty #9. This screen should eventually display a tab or sub-table per language/domain, but is PENDING architect design).

3. Actor clicks **"Save"** (at the bottom of the page or as a sticky footer button). System saves all changed settings.

**Redirects (view only):**

4. Actor navigates to **Redirects** (under Apps or via menu).
5. System displays the list of active redirects (source path → destination path/URL, enable toggle, Edit button).
6. Actor can **enable/disable** a redirect via the toggle.
7. Actor can **edit** the destination of an existing redirect (the source path is read-only once created — it was set by the system at the time of slug change).
8. Actor can **delete** a redirect (trash icon).
9. "Add a Redirect" button is **absent** — manual creation is not supported. Redirects are created automatically when product/collection/page slugs are changed (see AF-004).
10. Redirect HTTP status: 301 (permanent redirect). PENDING (MEDIUM: whether 302 is ever used; recommend 301 for all auto-created redirects).

**Domain settings (merged with General Settings, per owner annotation):**

11. The standalone "Domains" screen is merged into this General Settings page.
12. A "Change subdomain" button opens a dialog to rename the store's subdomain. PENDING (whether custom domain mapping is also managed here — likely yes, but flow not captured in screenshots).
13. **Delete store** button (red outline, top-right):
   - Clicking opens a confirmation dialog requiring the actor to type the store name.
   - IF confirmed: store is soft-deleted (data retained for N days for recovery — PENDING CRITICAL: retention period and whether customer PII is anonymized immediately or after retention window).

**Postconditions:** Store settings applied; storefront reflects changes on next request.

---

### AF-018: Configure Product-Page Feature Toggles

**Actor:** Store Admin or Employee with Settings — Full access  
**Admin site:** `/admin/`  
**Preconditions:** None

**Steps:**

1. Actor navigates to **Product Page** settings (`/admin/settings/product`).
2. System displays a list of toggleable features:

   | Toggle | Effect when on | Effect when off |
   |---|---|---|
   | Type | "Type" field visible on product edit form and on storefront product page | Field hidden (existing data preserved) |
   | Description Tabs | Multiple named tabs appear in the product description section; a "+" button and draggable tab chip "Description" are shown | Only a single description editor, no tabs |
   | Compare at price | "Compare at" price field visible per variant | Field hidden |
   | Shipping weight | "Shipping weight" field visible on product edit form | Field hidden (weight still stored; used for shipping calculation if entered) |
   | Vendor | "Vendor" field visible on product edit form and storefront | Field hidden |
   | SKU | "SKU" field visible per variant | Field hidden |
   | Tags | "Tags" field visible on product edit form | Field hidden (collection rules based on tags still evaluate existing tags) |

3. **Description Tabs** sub-widget: when enabled, actor sees the draggable tab chip "Description" with a drag handle and a "+" button to add more tabs (e.g., "Shipping", "Care Instructions", "Size Guide"). Tabs are reordered by dragging. Tab names defined here appear as named tabs in the product edit form for all products.
4. Toggles save instantly (no "Save" button required — changes apply immediately via AJAX).

**Amazon FBA (requested feature):**

5. "Sync with amazon FBA inventory" toggle (when enabled globally in super-admin, this toggle appears):
   - When on: product-level "Sync with FBA inventory if available" checkbox is visible on the product edit form (checked by default for new products).
   - PENDING (Amazon FBA integration design — including ASIN field on the product model and inventory polling mechanism).
6. "Add buy on Amazon button when FBA inventory is available" toggle:
   - When on: storefront displays an affiliate "Buy on Amazon" link on product pages where FBA inventory is detected and an ASIN is configured.
   - PENDING (affiliate link format; commission tracking).

**Postconditions:** Feature toggles applied; product edit form and storefront reflect the enabled fields.

**Edge cases:**
- Disabling a toggle does not delete existing data — it only hides the field from the UI and storefront.
- Description tabs: if tabs are disabled after products have multi-tab descriptions, the storefront renders all tab content concatenated or only the first tab. PENDING (recommend: render only the "Description" (first) tab's content when tabs are disabled).

---

## Super-Admin Flows (`/superadmin/`)

These flows are performed by a Super-Admin user inside the shared control plane that governs all stores of the organization.

The super-admin interface is identified as the **"Payment Control Plane"** for the payment section and extends to cover shared settings (themes, shipping, emails, upsells, employee management across stores).

Common chrome: dark top bar with logo ("Pradize Control Plane"), global search ("Search organizations, processors, rules…"), environment label ("Production"), "Publish" button, avatar. Left sidebar (dark): Overview, Organizations, Processor Accounts, Organization Rules, Payment Methods, Simulation, Decision Logs, Settings. Footer: "Super-admin / shared across stores".

---

### AF-101: Create Organization

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Organizations  
**Preconditions:** Super-admin is authenticated; at least one organization with the "Default organization" flag must exist at all times (enforced by an amber warning badge if missing)

**Steps:**

1. Super-admin navigates to **Organizations** in the left sidebar.
2. System displays the organization list table: Priority, Default, Organization (display name + registration country), Country/area coverage, Status, Current month (volume), Threshold.
3. Super-admin can filter by Country/Area, Store group, Status; free-text search.
4. Super-admin clicks "Add org" (primary button).
5. System opens the **Create / edit organization** panel (right side):
   - **Display name** (required, e.g., "Cedric SASU / FR" — internal label).
   - **Legal name** (required, e.g., "Pradize SASU" — used on statements).
   - **Default organization** checkbox: if checked, this org is the routing fallback when no geography rule matches. EXACTLY ONE org must have this flag. System enforces uniqueness — checking this on a new org unchecks it on the previously-default org (with a confirmation prompt).
   - **Registration country** (select, ISO country list).
   - **Settlement currencies** (multi-value; e.g., EUR, USD — the currencies in which this org settles with its processors).
   - **Buyer country / area coverage** (multi-value; accepts ISO country codes and area aliases: EU, ROW, CH, UK, etc.).
   - **Monthly threshold** (number) + **Threshold currency** (select). Leave blank = no threshold (unlimited processing). PENDING (CRITICAL: threshold semantics — what triggers the threshold: authorized amount, captured amount, or net-of-refunds? Monthly reset boundary: calendar month in which timezone? What happens when threshold is reached: org is excluded from routing candidates until month resets? Must be confirmed).
   - **Support / statement descriptor** (text, e.g., "PRADIZE FR" — appears on customer bank statements).
   - **Eligibility** checkboxes: Eligible for card payments / Eligible for PayPal payments / Eligible for crypto payments.
   - **Status**: Active / Draft (draft orgs are excluded from routing).
6. Super-admin clicks "Save org".
7. System saves; the new org appears in the list. Priority is set to the next available integer; priority order can be adjusted by drag-and-drop on the list.

**Cross-navigate to processors:**

8. "Open processors" button in the panel navigates to Processor Accounts filtered to this org.

**Postconditions:** Organization exists; appears in the routing candidate pool (if Active); current-month volume tracking begins (€0).

**Edge cases:**
- PENDING (CRITICAL): Org-level Priority column vs Stage-1 geography rules — these two ordering mechanisms must not conflict. Decision: org Priority is informational/display only; routing authority rests entirely with Organization Rules (AF-103). PENDING confirmation.
- PENDING (MEDIUM): "Tax profile" mentioned in the diagram caption but no fields appear — mark as PENDING for a future tax profile section.
- PENDING (MEDIUM): "Store group" filter implies store-group management exists somewhere. Location TBD.
- Currency conversion for threshold accounting: PENDING (CRITICAL: if org threshold is EUR but an order is charged in USD, the USD amount must be converted to EUR using a defined rate to track against the threshold — rate source and timing unspecified).

---

### AF-102: Create Processor Account

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Processor Accounts  
**Preconditions:** At least one Organization exists (AF-101 must be completed first)

**Steps:**

1. Super-admin navigates to **Processor Accounts**.
2. System displays the processor accounts table grouped by method family tabs (Card / PayPal / Crypto). Columns: Priority, Processor account (label), Org, Method, Strategy (Primary/Backup badge), Health.
3. Super-admin can filter by Organization, Method family, Processor type, Status; checkbox to show/hide backup accounts.
4. Super-admin clicks "Add account".
5. System opens the **Create / edit processor account** panel:
   - **Organization** (select — must exist; required FK).
   - **Account label** (text, e.g., "Stripe FR Main" — internal identifier).
   - **Processor type** (select: Stripe, PayPal — **day-1 only**; HiPay, Mollie, BTCPay, BitPay added later via connector registry). PENDING (MEDIUM: connector registry pattern for adding new processor types without Django code changes).
   - **Method family** (select: Card / PayPal / Crypto — must match the processor type).
   - **Supports buyer countries** (multi-value: country codes and area aliases).
   - **Settlement currency** (select — single currency; must be in the org's settlement currencies list).
   - **Secret / credential reference** (text; format `vault://payments/<account-label>` — credentials are NEVER stored inline). PENDING (CRITICAL: vault backend specification — recommended: HashiCorp Vault or AWS Secrets Manager; access control and rotation policy must be defined by the infrastructure team).
   - **Role flags**:
     - "May be used as primary" (checkbox).
     - "May be used as backup only" (checkbox — mutually exclusive with primary; system should enforce only one can be checked). PENDING (recommend: replace two checkboxes with a single role enum: Primary / Backup / Both).
     - "Block new traffic manually" (checkbox — kill-switch for this account without deleting it).
     - "Shared across multiple stores" (checkbox).
   - **Runtime status** (read-only display): Healthy / Ready / Blocked.
6. Super-admin clicks "Test creds" to verify credentials: system calls the processor's test endpoint using the vault-referenced credentials and shows a pass/fail result.
7. Super-admin clicks "Save account".
8. System saves; account appears in the list with health status "Ready" (health = Healthy requires at least one successful live transaction or a periodic health probe). PENDING (CRITICAL: health monitoring mechanism — recommend: scheduled health-check job every N minutes; alert when consecutive failures exceed threshold; automatic failover only via the backup-chain strategy — no automatic health-flag flip without human confirmation).
9. Super-admin can drag-and-drop the account within its method-family group to set priority order.

**Postconditions:** Processor account is configured; credentials are referenced from the vault; account participates in routing once rules are configured.

**Edge cases:**
- Day-1 processors: Stripe (Card) and PayPal (PayPal) only. The "Processor type" dropdown shows only these two initially.
- PENDING (MEDIUM): Both "May be used as primary" and "May be used as backup only" unchecked simultaneously renders the account unusable; system should warn or block save.

---

### AF-103: Create Geography Routing Rule (Stage 1)

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Organization Rules → "1. Country / area rules" tab  
**Preconditions:** Organizations exist (AF-101); Stage-1 rules are evaluated in priority order (first match wins)

**Steps:**

1. Super-admin navigates to **Organization Rules**. System shows the two-tab interface: "1. Country / area rules" (active) and "2. Split / threshold rules", plus "Rule precedence" tab. A green badge shows the current default org ("Default org: [Name]").
2. Current Stage-1 rules table: Order, Rule name, Match (country/area), Result (one org or pool name), Status.
3. Super-admin clicks "Add rule".
4. System opens the **Stage-1 rule editor**:
   - **Rule name** (text, required).
   - **Priority** (number — determines evaluation order; lower number = evaluated first; system can auto-assign next available).
   - **When buyer country / area is** (multi-value selector: ISO country codes or area aliases EU / ROW / CH / UK / etc.).
   - **Result cardinality**:
     - "Return one organization" → select exactly one org from a dropdown.
     - "Return an organization pool for stage 2" → select 2+ orgs (multi-select). This requires a corresponding Stage-2 allocation rule (AF-104).
5. **Status**: Active / Draft. Draft rules have no effect on routing.
6. Super-admin clicks "Save rule".
7. Rule appears in the ordered list. Super-admin can drag to reorder (changes the `Order` integer).

**PENDING (CRITICAL): Geography rule ordering paradox.** A broad area rule (e.g., "Country in EU" at order 1) will shadow a more-specific country rule (e.g., "Country in FR" at order 3) because first-match wins. Resolution options:
- Option A: More-specific rules (smaller country set) automatically outrank broader area rules regardless of drag order.
- Option B: Strict first-match; super-admin is responsible for ordering specific rules above broad ones (system warns if a rule is shadowed).
- PENDING (recommended: Option B with a shadowing-detection warning in the UI).

**Postconditions:** Rule is active; incoming orders from matching countries are routed to the specified org or pool.

**Edge cases:**
- Pool-returning rule: "Return an organization pool" without a corresponding Stage-2 rule causes routing to fall to the default org. System should warn on save if no Stage-2 rule exists for this pool.
- If a Stage-1 rule returns an org that is ineligible for the chosen payment method: the org is skipped and routing falls to default. PENDING (CRITICAL: confirm skip-to-default vs reject-at-config-time).

---

### AF-104: Create Allocation Rule for Org Pool (Stage 2)

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Organization Rules → "2. Split / threshold rules" tab  
**Preconditions:** A Stage-1 rule exists that returns an organization pool (AF-103)

**Steps:**

1. Super-admin navigates to **Organization Rules** → "2. Split / threshold rules" tab.
2. Super-admin clicks "Add rule" (or "Add split / threshold rule").
3. System opens the **Stage-2 rule editor**:
   - **Applies to geography result** (select — dropdown lists all Stage-1 rules that return a pool).
   - **Mode** (exactly one per pool):
     - **"Break down income by percentage"**: percentage split between pool members. Super-admin enters a % for each org in the pool (must sum to 100%).
       - PENDING (CRITICAL: split mechanics — orders are indivisible. Recommended implementation: maintain a running-counter per org per pool. For each incoming order, select the org furthest below its target % based on volume-to-date. This ensures the split converges over time without splitting individual orders. Must be confirmed before implementing).
     - **"Switch organization once threshold is reached"**: ordered org sequence + threshold amount per org. E.g., "Use Cedric until €100,000, then Sister".
       - PENDING (CRITICAL: threshold-switch counter — does this use the org's global monthly threshold (from AF-101) or a separate per-rule counter? Recommended: a separate per-rule counter, allowing fine-grained routing independent of the global threshold. Must be confirmed).
       - When the first org hits its threshold, traffic switches to the second. If the second also hits a threshold: fall to the default org.
4. Super-admin clicks "Save rule".

**Postconditions:** Stage-2 rule governs allocation within the specified pool; together with the Stage-1 rule, determines which org receives each order.

**Edge cases:**
- Exactly one Stage-2 rule allowed per pool (system enforces uniqueness; "Save" is blocked if a Stage-2 rule already exists for this pool — offer an "Edit" action instead).
- Changing a Stage-2 rule mid-month: PENDING (whether the running counter resets or continues — recommend: continues without reset).

---

### AF-105: Configure Payment Method

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Payment Methods  
**Preconditions:** Processor accounts exist (AF-102)

**Steps:**

1. Super-admin navigates to **Payment Methods**.
2. System displays three method cards side by side: **Card**, **PayPal**, **Crypto**.
3. For each method card, super-admin can:
   - Toggle **"Enabled at checkout"** (shows/hides this method in the storefront checkout).
   - Edit **"Displayed label"** (customer-facing label, e.g., "Card", "PayPal", "Crypto").
   - Manage the **processor options list**: a draggable ordered list of processor accounts assigned to this method. Each item shows: drag handle (☰), account label, org name, Primary/Backup badge.
     - To add an account: click "Add processor option" → pick from accounts of the matching method family.
     - To remove: trash icon.
     - To reorder: drag handle. Reordering changes both priority and Primary/Backup badge assignment (first position = Primary, subsequent = Backup). PENDING (MEDIUM: confirm whether badge derives from list position or from the account's own role flags set in AF-102).
   - Set **strategy**:
     - **"Use as backup chain"**: first option is always used; subsequent options only if the first is blocked/unhealthy.
     - **"Alternate evenly"**: rotate traffic across all options; skip blocked ones. PENDING (CRITICAL: "alternate evenly" scope — per store, per method globally, or per buyer session? And how does this interact with the org-level percentage split from AF-104? These two distribution mechanisms must be reconciled. Recommended: "alternate evenly" is a per-order round-robin at the processor account level AFTER the org has been determined by rules — not an org-selection mechanism. PENDING confirmation).
   - Toggle **"Hide method when no processor route is available"** (recommended: on by default).
   - Toggle **"Allow stores to locally disable this method"** (if on, store admins can disable the method for their store via the Payment Processing screen in `/admin/`).
   - For PayPal: additional toggle **"Show when enabled even if only one route exists"**.
4. Super-admin clicks "Save" on each method card.
5. Super-admin clicks the global **"Publish"** button in the top bar to atomically push the new routing configuration live. PENDING (CRITICAL: "Publish" semantics — is config staged and deployed atomically, or saved live immediately? Recommend: draft state with explicit Publish to avoid mid-configuration routing errors. Must be confirmed).

**Preview:**

6. Super-admin clicks "Preview" on a method card to see a read-only rendering of the checkout method selector as a buyer would see it. PENDING (preview fidelity level).

**Postconditions:** Payment methods are configured and published; the checkout displays the enabled methods in order; the payment routing pipeline is fully defined.

---

### AF-106: Configure Shipping Zone

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Shipping  
**Preconditions:** None

**Steps:**

1. Super-admin navigates to **Shipping** (`/admin/settings/shipping`).
2. System shows a left sidebar with existing zones (e.g., "West", "All Countries") and a "New Zone" entry, plus "Add a New Zone" button.
3. Super-admin clicks "Add a New Zone" or selects "New Zone".
4. System opens the **zone editor panel** (right side):
   - **Zone Name** (text, not customer-visible; required).
   - **Shipping Rate** (money, default $0 for free shipping).
   - **Countries** (multi-select chip list; supports adding individual countries from a dropdown).
   - **States and Provinces** (auto-populated table for countries that have subdivisions): for each country with subdivisions, a row shows "Country — X/Y [selected/total]" with a blue link to open a subdivision picker (check/uncheck individual states, provinces, prefectures, etc.).
   - **Shipping service description** (text, customer-visible; e.g., "Free 3-5 Weeks Shipping").
   - **RULE BASED EXCEPTIONS section**: "Add an exception" button. Each exception row:
     - Type dropdown: Weight (shown) / Price / Quantity / **FBA inventory available** (requested annotation). PENDING (CRITICAL: full exception type list; "FBA inventory available" exception is a requested feature requiring FBA integration).
     - From / To inputs (numeric range).
     - Rate (money).
     - Per-exception customer-visible shipping service description (e.g., "Free 1-2 Weeks Shipping (PROMO)").
     - Trash icon.
   - PENDING (MEDIUM): Whether exceptions may overlap in range; recommended: first-match wins.
   - **ADDITIONAL SHIPPING OPTIONS FOR CUSTOMERS section**: "Add a service" button. Each service = a named paid option (e.g., "Express Shipping — $19.99") offered as an alternative at checkout.
   - **Shipping Model** (requested annotation — PENDING CRITICAL): "Model 1 (default)" and "Model 2" are sketched in annotations. Multiple shipping price models per zone, with per-product model override on the product edit form. This feature is PENDING design — implementer must define the shipping model entity and override mechanism before implementing.
5. Super-admin clicks "Save" (blue) or "Cancel". "Delete Shipping Zone" (red) with a confirmation prompt.

**Zone matching precedence:**

PENDING (CRITICAL): When a delivery address matches multiple zones (e.g., a US address matches "West" (US listed) and "All Countries"), which zone's rates apply? Recommended: most-specific zone wins (fewer countries in zone → higher priority over "All Countries"). Must be confirmed before implementing.

**Postconditions:** Zone is active; orders with delivery addresses matching the zone's countries/states are rated using the zone's base rate and exceptions.

---

### AF-107: Manage Shipping Carriers

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Shipping Carriers  
**Preconditions:** None

**Steps:**

1. Super-admin navigates to **Shipping Carriers** (`/admin/settings/carriers`).
2. System displays the carrier list (name, tracking URL template, Edit button, delete icon).
3. **Add carrier**: super-admin clicks "Add Carrier" (green button). System opens the "ADD SHIPPING CARRIER" modal:
   - Carrier name (text).
   - Tracking URL (text; the tracking number is appended to this URL, e.g., `https://tools.usps.com/go/TrackConfirmAction_input?qtc_tLabels1=<TRACKING_NUMBER>`).
   - "Add another" checkbox (keeps modal open for batch creation).
   - "Add" (blue) / "Cancel" buttons.
4. Super-admin fills in fields and clicks "Add".
5. The carrier appears in the list immediately.

**Edit carrier:** super-admin clicks "Edit" on an existing row. An inline edit row or modal opens with the same fields (carrier name + tracking URL). Super-admin saves changes.

**Delete carrier:** super-admin clicks the trash icon. PENDING (MEDIUM: what happens to orders referencing this carrier — recommend: block deletion if any order references this carrier and is in SHIPPED status; warn and allow deletion if only historical orders reference it).

**Tracking URL convention:** Most URLs use append-style (`https://example.com/track?number=` + tracking number). Free-form URLs are also supported. PENDING (MEDIUM: whether a `{tracking_number}` placeholder token should be introduced for URLs where the number is not at the end — recommend adding this for clarity).

**Postconditions:** Carrier is available in the carrier dropdown on order fulfillment and in shipping-confirmation email templates.

---

### AF-108: Configure Automated Email Templates

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Automated Email  
**Preconditions:** Email infrastructure is configured (SMTP or transactional email provider)

**Steps:**

1. Super-admin navigates to **Automated Email** (`/admin/emails`).
2. System displays the 7 email templates list:
   - Order notification (sent to store owner for every order, when enabled)
   - Order confirmation (sent to customer after purchase)
   - Refund (sent to customer when a refund is issued and "Notify customer" is checked)
   - Shipping confirmation (sent when a tracking number is added or items marked shipped)
   - Store invitation (sent when inviting a new staff member)
   - Gift card (sent when a gift card is issued)
   - *(Note: "Invoice" email is **removed** — the Invoice Orders feature is crossed out in the menu)*

3. Super-admin clicks a template chevron `>` to open the **template editor** (master-detail panel):
   - **Enable toggle** (green dot / active).
   - Template name + trigger description.
   - **From Name** (text, e.g., "Pradize").
   - **From Email** (e.g., `contact@pradize.com`) with "Edit" link. PENDING (MEDIUM: "Edit" gating — likely requires domain verification; flow not captured; recommend: opening a domain-verification sub-flow similar to the DKIM/SPF provisioning step).
   - **Reply-to Address** (text) with "Edit" link.
   - **Subject** (text, required).
   - **Email format**: "Plain Text" / "HTML" (segmented selector).
     - Plain Text: simple textarea with Jinja2-style template syntax.
     - HTML: code editor with line numbers; same template syntax.
   - **Email body** area with a "View all available email variables" link (opens a reference modal listing all available variables for this template, e.g., `{{ store_name }}`, `{{ order.full_name }}`, `{{ order.order_date|date }}`, `{% for item in order.items %}…{% endfor %}`, `{{ address.shipping_street }}`, etc.).
   - **"Send a test"** button: sends a rendered version of the template to the super-admin's own email address using sample data.
   - **"Save"** button (blue).

4. Super-admin edits the template body and clicks "Save".

**Multi-language template variants (requested feature — PENDING):**

5. PENDING (CRITICAL): The owner annotation states that templates "should support variables +- translation assigning them in jobs to do (handling update also)". This implies:
   - Each template has one variant per language (e.g., EN, FR, DE, JA).
   - When a template is updated in the source language, a re-translation job is created in the jobs system (see Jobs spec — TBD).
   - The language of the variant sent to a customer is determined by the store's language setting (or the customer's preferred language — PENDING domain/language model, uncertainty #9).
   - This feature is PENDING architect design; the initial implementation supports only a single-language (EN) template per email type.

**Postconditions:** Template is saved; email is sent when its trigger fires.

**Edge cases:**
- Disabled templates (toggle off): no email is sent for that trigger (except "Order notification" which is explicitly conditional on being activated).
- "Send a test" renders template variables with sample/stub data; it does not create real orders or customer records.
- Plain-text vs HTML: both bodies are stored per template; the format selection determines which is sent (HTML-capable clients receive HTML; plain-text fallback is standard email practice — implementer should send multipart/alternative).

---

### AF-109: Manage Up-Sell Campaigns

**Actor:** Super-Admin (campaign creation); Store Admin (read-only view + store-level additions)  
**Admin site:** `/superadmin/` → Up-Sell Campaigns; `/admin/` → Up-Sell Campaigns  
**Preconditions:** None

**Steps — Super-Admin creates a campaign:**

1. Super-admin navigates to **Up-Sell Campaigns** (`/admin/upsells`).
2. System displays the campaign list with columns: Campaign Name and Type, Unique impressions, Conversion rate (%), Conversions. Status badges: Active / Inactive.
3. Super-admin clicks "Create campaign" (green button).
4. System displays the **campaign type chooser** (5 archetype cards):

   | Archetype | Stage | Description |
   |---|---|---|
   | Related Products Campaign | Before checkout | Product recommendations on the product page |
   | Buy X Items Get X Items Free or Get % Off | Before checkout | Quantity-based promotion |
   | Storewide discount with timer | After checkout | Post-purchase whole-site discount with countdown |
   | One Click Upsell Funnel | After checkout | Post-purchase one-click charge on saved payment |
   | Order Bumps | Before checkout | Add-on offers embedded in checkout |

5. Super-admin selects an archetype and clicks to proceed. System opens the archetype-specific form.

**Archetype: Related Products Campaign:**

6. Form fields:
   - Campaign name; enable toggle.
   - Targeting: "All Products" / "Manually" (product picker) / "Conditions" (rule builder: field / operator / value, AND/ANY combinator).
   - Matching Method (ordered, drag-to-reorder priority cascade):
     - Level 1: "Products most commonly bought together" (requires order-history co-purchase analysis).
     - Level 2: "Products with the same tags".
     - Level 3: "Products from the same collection".
7. Super-admin clicks "Submit".

**Archetype: Buy X Items Get X Items Free or Get % Off:**

6. Form fields:
   - Campaign name; enable toggle; targeting; country targeting.
   - Discount mode: "Buy X items get X items for free" or "Buy X items get % off".
   - Offer tiers: "Buy [N] items and get [N] items free". Multiple tiers via "Add Another Offer".
   - ~~"Maximum value of free items" toggle + amount cap~~ **REMOVED (DECIDED 2026-07-04): no free-value cap — the free item is always 100% free regardless of price.** (Screenshot element intentionally excluded; recorded in `12_out_of_scope.md`.)
   - "Allow stacking" toggle (buy 1 get 1 → buy 2 get 2, etc.).
   - Note: cheapest items in cart are set to free.
   - PENDING (MEDIUM, narrowed 2026-07-04: value cap resolved as "no cap"; remaining is the multi-tier + stacking pricing algorithm — implementer must define it precisely before implementing. Recommend: evaluate tiers from highest to lowest; apply best-matching tier; stacking multiplies the lowest qualifying tier).

**Archetype: Storewide Discount with Timer:**

6. Form fields:
   - Campaign name; enable toggle; targeting; country targeting.
   - Discount amount and unit ($ off or % off).
   - Discount expires after (N minutes — countdown timer starts when the thank-you page loads).
   - Timer bar: checkbox "Top timer bar"; bar message template (with `[$0.00]` and `[00:00:00]` token pills); bar background color + text color.
   - Up-sell message on thank-you page: "Text with button" (CTA text + button text + button colors) or "Image" (upload + link). PENDING (Image variant fields — implement as URL + image upload).
   - Frequency cap per user.
   - **"Discount code expires after N days"** field (default **30**) — governs the server-side validity of the generated code (DECIDED 2026-07-04).
   - **DECIDED (2026-07-04): next-order application model.** Each trigger generates a unique **single-use** `CampaignCode`; `expires_at` = creation + the "Discount code expires after N days" setting (default 30 days). The code is emailed to the customer and/or shown on the thank-you page. Redemption marks the code used (`used_at` + redeeming-order FK); reuse and expired codes are rejected server-side. ASSUMPTION (LOW): the thank-you timer bar (minutes) is the promotional urgency display; the code's actual validity is governed solely by the days field.

**Archetype: One Click Upsell Funnel:**

6. Form fields:
   - Campaign name; enable toggle; targeting; shipping policy (free or apply store shipping rules); country targeting; frequency cap.
   - "Continue to build funnel pages" button proceeds to a **funnel page builder** (not captured in screenshots). PENDING (CRITICAL: funnel page builder UI and one-click payment model — reusing the original order's payment token requires specific PSP API support; this is a security-critical and money-critical design decision. See uncertainty #5 in `11_uncertainties_to_validate.md`).

**Archetype: Order Bumps:**

6. Form fields:
   - Campaign name; enable toggle.
   - Type: "Multi Select" (customers can add multiple bump products) or "Single Select" (e.g., warranty with year options).
   - Category: heading text shown above the bump section at checkout. If multiple campaigns share the same category, they A/B-rotate automatically (split test).
   - Targeting; country targeting.
   - Order Bump Products: "Add items" button opens a product picker.
   - Frequency cap per user.
7. Super-admin clicks "Submit".

**Scheduled / Timed campaigns (requested annotation — PENDING):**

Each campaign archetype can optionally be enabled only on specific days of the week or month, with a countdown timer. When enabled, an AI job can add/remove/measure the campaign. Localized to the primary country of each domain language. PENDING (this is a future feature; mark as PENDING in the campaign form).

**Store-admin view:**

8. Store admin navigates to **Up-Sell Campaigns** in `/admin/`. System displays:
   - Super-admin-created campaigns: listed with a "Super-admin" badge; all fields are **read-only** for the store admin.
   - Store-admin-created campaigns: listed below; full edit access.
9. Store admin can click "Create campaign" to add a store-level campaign using the same archetype chooser.

**DECIDED (2026-07-04): Precedence of super-admin vs store-level campaigns — store-admin wins.** When a product has both a super-admin campaign and a store-admin campaign, the **store-admin campaign takes precedence at runtime** (the super-admin campaign does not fire for that product). If only a super-admin campaign exists for a product, it applies. Super-admin campaigns remain visible to store-admins but are **read-only** in the store-admin view. Impressions and conversions are tracked per campaign as before.

**Postconditions:** Campaign is active; impressions and conversions are tracked per campaign.

---

### AF-110: Configure Automated Gift Card Campaigns

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Automated Gift Cards  
**Preconditions:** Gift card module is configured; email infrastructure is operational

**Steps:**

1. Super-admin navigates to **Automated Gift Card Campaigns** (`/admin/settings/gift-cards`).
2. System displays the list: campaign name, "N Issued gift cards", "N Used gift cards", "N Outstanding gift cards". PENDING (MEDIUM: "Outstanding" = issued cards that are still valid and have remaining balance, i.e., not expired and balance > 0. Must be confirmed).
3. Super-admin can filter and use bulk selection.
4. Super-admin clicks "Add a campaign" (green button).
5. System opens the **campaign form**:
   - **Campaign name** (required).
   - **Gift card value** (radio group):
     - "Set value" → fixed $ amount.
     - "Percentage of order total" → a % of the order value is issued as a gift card (e.g., 5% of a $100 order = $5 card). Computed at time of order. PENDING (whether pre- or post-shipping/discount).
     - "Set % discount off order" → PENDING (CRITICAL: this option is semantically ambiguous — is this a stored-value gift card for X% of the order, or a percentage-off coupon issued as a code? Given the confirmed `DiscountCode` model with a type field, this option should issue a `DiscountCode` of type `pct_off_coupon` with value = the %. Must be confirmed).
   - **Product triggers** (segmented tabs): "All Products" / "Manually Add Products" (product picker) / "Products Based on Conditions" (rule builder).
   - **Gift card expiry date** (radio group):
     - "Set number of days" → relative expiry from issuance (input: number of days; note: default 1 day is a placeholder — recommend 30 days as a sensible default).
     - "Specific date" → absolute calendar date.
   - **Frequency cap**: checkbox "Set per user frequency cap"; N gift cards per N [Minutes / Hours / Days].
   - **Delivery section**:
     - "Email subject line" (default: "Here is your gift card code").
     - Message editor with token variables: `[DISCOUNT]` (resolved to the gift card value), `[GENERATED GIFT CARD CODE]` (resolved to the auto-generated code).
     - PENDING (MEDIUM: whether this delivery email is distinct from the "Gift card" automated email template configured in AF-108. Recommend: this form overrides the default template for this campaign; the AF-108 "Gift card" template is the system default when no campaign-level template exists).
6. Super-admin clicks "Save as draft" or "Publish".
7. IF Published: the campaign is immediately active; whenever a qualifying order is placed (matching the product trigger), the system:
   - Generates a unique `DiscountCode` record.
   - Creates a `GiftCardTransaction` record for the initial balance.
   - Sends the delivery email to the customer.
   - Increments the "Issued gift cards" counter.

**Postconditions:** Campaign is live; gift cards are issued automatically on qualifying purchases.

---

### AF-111: Manage Org Theme Library

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Themes  
**Preconditions:** None

**Steps:**

1. Super-admin navigates to **Themes** (`/admin/themes`).
2. System displays "N Themes in the Library" with a grid of theme cards: thumbnail, name, last-edited timestamp. The active/default theme is marked with a blue checkmark badge.
3. Sort control: "Sort by last edited (Newest first)" dropdown.

**Upload / build a theme:**

4. Super-admin clicks "Build new theme": system opens a dialog to choose between:
   - **Twig theme**: code-based template engine. Opens a code editor environment (files: layout.html, product.html, collection.html, cart.html, etc.).
   - **Visual builder theme**: drag-and-drop page builder.
   - PENDING (MEDIUM: what "Build new theme" opens — a scaffold generator? A code editor? The visual builder? Flow not captured).
5. Super-admin can also click "Theme Store" to browse and import third-party themes.

**Set org default theme:**

6. Super-admin clicks on a theme card (or a "Set as default" button on hover).
7. System marks this theme as the org default (blue checkmark badge). The checkmark moves from the previously-default theme.
8. Store admins see this theme as the pre-selected option in their Themes screen.

**Store-admin picks theme:**

9. Store admin navigates to **Themes** in `/admin/`.
10. System displays the org theme library (same grid, read-only cards for Twig and Visual builder themes).
11. Store admin clicks a theme card to set it as active for their store.
12. System marks the theme as active for this store (may differ from the org default).

**Three suggested starter themes (requested feature — PENDING):**

13. The initial library should include 3 complete starter themes designed for different verticals:
    - **Food theme** (colors/fonts/images for food & beverage stores).
    - **Fashion theme** (colors/fonts/images for apparel/accessories).
    - **General mid–high-end theme** (colors/fonts/images for general products at an average to high price point).
    - PENDING (Designer Agent to propose these themes; implementer builds them as minimal Twig theme variants — different fonts, colors, and sample images only — to reduce bugs and simplify testing).

**Postconditions:** Theme library is populated; org default theme is set; store admins can select from available themes.

---

### AF-112: Invite Employee + Assign Stores (Cross-org)

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Employee Accounts  
**Preconditions:** At least one store exists

**Steps:**

1. Super-admin navigates to **Employee Accounts**.
2. System displays all active employee accounts with name, email, last-login timestamp, and their assigned stores/roles.
3. Super-admin clicks "Invite New User" (green button).
4. System opens the employee detail panel:
   - Full name, Email, Phone (same fields as AF-002).
   - Access mode: "Full Access" (across all stores they are assigned to) or "Limited Access" (per-module matrix).
   - **Store assignments**: multi-select of stores the employee can access. Each store assignment can carry its own permission matrix (e.g., employee has Orders — Full access on Store A but Orders — Limited access on Store B).
   - PENDING (MEDIUM: whether the super-admin employee invitation panel has a unified permission matrix (one set applying to all assigned stores) or a per-store permission matrix. Recommend: per-store matrix for flexibility. To be confirmed).
5. Super-admin clicks "Save" → invitation email sent.

**Postconditions:** Employee can log in and access all assigned stores with the configured permissions.

---

### AF-113: Configure Security Badge

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Security Badge  
**Preconditions:** None

**Steps:**

1. Super-admin navigates to **Security Badge** (`/admin/apps/app/security-badge`).
2. System displays "N Active Badge(s)" with a list (badge name, placement summary, Edit, Delete).
3. Super-admin clicks "Create a new Badge" (green button).
4. System opens the **badge configuration page**:

   **GENERAL SETTINGS:**
   - Badge name (text, e.g., "Safe checkout").
   - Status toggle (Active).

   **BADGE COMBINATION:**
   - "Upload your own" button: actor can upload a custom badge image.
   - Scrollable gallery of preset badge combinations (payment logos + security seals). Actor selects one (blue border + checkmark).

   **REFINE STYLE:**
   - Preview width dropdown (200–500 px in steps).
   - "Show Guaranteed Safe Checkout text" checkbox. IF checked:
     - Three-part color inputs: "Guaranteed" color / "Safe" color / "Checkout" color (each with hex swatch).
   - "Show Border" checkbox + border color + border width (px).
   - Live preview pane rendering the badge.

   **LOCATION:**
   - For each placement location (Product page / Under buy button; Cart page; Checkout page):
     - On/Off toggle.
     - IF on: size selector strip (200 / 250 / 300 / 350 / 400 / 450 / 500 px).
   - PENDING (MEDIUM: full list of placement locations — at minimum product page, cart, and checkout are confirmed; a footer location may exist).

5. Super-admin clicks "Save". Badge is active on the specified locations with the specified width.

**Postconditions:** Badge is displayed on the selected storefront pages at the configured width.

---

### AF-114: Configure Checkout Globals

**Actor:** Super-Admin  
**Admin site:** `/superadmin/` → Checkout Page  
**Preconditions:** Google Cloud account for Maps API (optional)

**Steps:**

1. Super-admin navigates to **Checkout Page** settings (`/admin/settings/checkout`).
2. System displays two sections:

   **GOOGLE MAPS ADDRESS AUTOCOMPLETE:**
   - Help text explaining the feature (address autocomplete as customers type; US-only zipcode autofill).
   - "Activate" button. IF not yet activated → clicking opens a Google Cloud API key entry dialog. IF already activated → shows the current status (enabled) with a "Deactivate" option.
   - PENDING (MEDIUM: exact API key entry flow — recommend: a text field for the API key, saved to secure config; displayed as masked once saved. Actor can test by attempting an address lookup in a preview modal).

   **ABANDONED CHECKOUT TIMEOUT:**
   - Label: "After how many minutes of inactivity is a checkout considered abandoned".
   - Numeric input (default 10, unit: Minutes).
   - Validation: must be a positive integer; recommended range 5–60 minutes.

3. Super-admin clicks "Save" (blue button).

**Postconditions:** Checkout autocomplete is enabled (if API key provided); abandoned-checkout timeout is set org-wide and used by all stores' abandoned-checkout detection.

---

## New CRITICAL Uncertainties Discovered During Flow Writing

The following uncertainties were identified while drafting these flows that are not already enumerated in `11_uncertainties_to_validate.md`:

1. **AF-002 / AF-112 — Employee permission matrix scope**: When a super-admin invites an employee and assigns multiple stores, it is unclear whether a single permission matrix applies to all stores or a per-store matrix is needed. **CRITICAL** for security model implementation.

2. **AF-003 — Product video hosting**: The owner annotation requests product video support with position settings. Whether videos are hosted (upload to platform storage) or embedded (YouTube/Vimeo URL) is undefined. **CRITICAL** for storage architecture.

3. **AF-007 — CSV Fulfillment round-trip**: The import-back step (uploading tracking numbers to bulk-update orders to SHIPPED status) is assumed required for external fulfillment partners but not captured in screenshots. **CRITICAL** for fulfillment workflow.

4. ~~**AF-010 — Abandoned-checkout email delay step**~~ **RESOLVED (AF-C4, 2026-07-04)**: the send-delay field lives inside the email wizard alongside Type/Style/Copy — "Send after" number field + hours/days dropdown, required, default 1 hour. Stored as `send_delay_hours` on the email record (see AF-010 flow step 6).

5. **AF-015 — "Gross Sales" vs "Net Sales" definitions**: The inconsistency between the state report ("Net sales") and all other reports ("Gross Sales") is flagged in `11_uncertainties_to_validate.md` but the recommended resolution (use "Gross Sales" = pre-refund order total everywhere; offer a "Net Sales" filter that subtracts refunds) must be confirmed before any report is built.

6. **AF-104 — Stage-2 threshold switch with multiple orgs exceeding caps**: If org A hits its threshold and routing switches to org B, and org B also hits its threshold within the same period, the fallback (default org) may receive unexpected volume. This edge case needs an explicit business rule. **CRITICAL** for routing correctness.

7. **AF-105 — "Publish" as a config-versioning mechanism**: If "Publish" is implemented as staged/versioned config, a rollback mechanism is implied. Rollback behavior (revert to previous published version) is undefined. **MEDIUM** for operational safety.

8. **AF-109 — Order Bump category split-test reporting**: The automatic A/B rotation between campaigns sharing the same category implies split-test result reporting somewhere (which campaign wins). The reporting location and promotion criteria are undefined. **MEDIUM** for analytics model.
