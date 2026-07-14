# Part C — Admin: settings, pixels, coupons, product editing, upsells, payment

### Shipping Feeds — FB Dynamic Product Feed Settings (`admin-013-shipping-feeds.jpg`)
**Purpose:** Configure the Facebook Dynamic Product Ads (DPA) product feed: which products to sync and the public feed URL.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/apps/app/fb-dpa`
- Top bar: store logo, app selector dropdown labelled "FB Dynamic Prod..." with hamburger icon and caret, global Search input, store name "Pradize", account avatar "CB" with dropdown caret
- Page heading: "Feed settings"
- "Products to sync" label + dropdown, current value "All products from all collections"
- "Feed Url" label + read-only/placeholder input showing `https://pradize.com/fb-dpa/rss.xml`
- "Copy" button (blue) next to the feed URL
- Full-width green status banner: "Your feed is up to date"
- Red handwritten-style annotation (owner's spec note, not a UI element): "Add also google + pinterest shopping feeds"
**Implied features:**
- Background job generating/refreshing an RSS/XML product feed; status tracking (up to date vs regenerating/stale)
- Feed scoping by collection (dropdown implies options like "specific collections" besides "all products")
- Public unauthenticated feed endpoint consumable by Facebook
- Per the annotation: Google Shopping and Pinterest feed formats are requested new features
**Uncertainties:**
- MEDIUM: Options in "Products to sync" dropdown are not shown (all products / per-collection / rule-based?).
- MEDIUM: Feed refresh cadence and trigger (cron, on product change, manual) is not visible.
- LOW: Whether the Feed Url is editable or display-only (assume display-only with Copy).
- MEDIUM: Feed field mapping (which product attributes are exported: price, availability, image, GTIN...) is entirely implicit.

### Recent Purchase Notification Settings (`admin-014-recent-purchase-notification.jpg`)
**Purpose:** Configure the storefront social-proof widget that shows visitors recent purchase pop-ups.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/apps/app/recent-purchase-notifications`
- Top bar: store logo, app selector "Recent Purchase ..." with hamburger icon and caret, search bar (partially cropped)
- Section heading: "Settings"
- Red annotation next to Settings: "|_| Enable" (spec note requesting an Enable checkbox/toggle)
- Field: "Rotate all purchase data from last [1] hours" — numeric input, value 1
- Field: "Delay first notification [2] seconds" — numeric input, value 2
- Field: "Time interval between notifications [2] seconds" — numeric input, value 2
- "Save" button (blue)
**Implied features:**
- Storefront JS widget reading real recent-order data (product, buyer, time) within a rolling window
- Rotation/queueing logic through the set of recent purchases
- Per the annotation: a global enable/disable toggle is requested but not present in the current UI
**Uncertainties:**
- CRITICAL: What buyer data is displayed on the storefront (name, city?) — privacy/GDPR impact; not visible here.
- MEDIUM: Behaviour when no purchases exist in the window (hide widget? show fabricated data?).
- LOW: Units/limits validation on the numeric fields (assume positive integers).

### Lead Capture Overlay — Campaign List (`admin-015-lead-capture-overlay.jpg`)
**Purpose:** List email-capture overlay (popup) campaigns with per-campaign performance stats.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/apps/app/lead-capture-overlay`
- Top bar: logo, app selector "Lead Capture Ov..." dropdown, Search, "Pradize", avatar "CB"
- Heading: "No Active Overlay Campaigns" (acts as empty-state header while inactive campaigns exist below)
- "New campaign" button (green, top right)
- Campaign rows (2), each with:
  - Red status dot (inactive/stopped indicator)
  - Campaign name ("Black Friday", "10% on exit")
  - Metrics: "5,416 VISITORS", "1,010 IMPRESSIONS", "0% CONVERSION RATE", "0 CONVERSIONS" (row 1); "11 VISITORS", "6 IMPRESSIONS", "17% CONVERSION RATE", "1 CONVERSIONS" (row 2)
  - Chevron (>) to open/edit the campaign
**Implied features:**
- Per-campaign analytics tracking: visitors, impressions, conversions, computed conversion rate
- Campaign status lifecycle (active vs inactive; green vs red dot presumably)
- Header text switches when at least one campaign is active
**Uncertainties:**
- MEDIUM: Exact definition of "visitors" vs "impressions" (unique sessions vs popup displays?) — affects stats implementation.
- LOW: Whether rows support inline actions (pause/delete) beyond the chevron (assume actions live in the edit view).

### Lead Capture Overlay — Create/Edit Campaign, "Display message" variant (`admin-015-lead-capture-overlay-02-edit.jpg`)
**Purpose:** Create or edit an overlay campaign: general settings, theme, post-signup action, frequency capping, and customer tagging.
**Visible elements:**
- Top bar: logo, app selector "Lead Capture Ov...", Search, "Pradize", avatar "CB"
- Page title: "Create an Overlay campaign"
- Section "GENERAL SETTINGS":
  - "Overlay campaign name" text input
  - "Status" toggle, on, labelled "Active"
  - "Trigger" radio group: "Exit" (selected) / "Time"
  - "Mobile and Tablet Trigger" radio group: "Disabled" (selected) / "Time"
  - "Exclude customers that visited pages" input with placeholder "Choose a page."
- Section "SELECT A THEME": grid of theme preview cards (3 visible fully: dark popup with photo left, centered white popup with photo top, large photo right orange-accent popup; second row partially visible with 3 more themes incl. a light-blue banner "SIGN-UP FOR OUR NEWSLETTER!"), vertical scrollbar. Each theme mock shows: close X, "SIGN-UP FOR OUR NEWSLETTER" heading, "AND GET A 10% OFF COUPON" text, NAME input, ENTER YOUR EMAIL input, "GET MY 10% OFF NOW" button, dismiss link "I don't want to save money"
- Section "UPON A SUCCESSFUL SIGN-UP": two-tab toggle "Display message" (selected, blue) / "Go to Url"; "Message" label + multiline textarea
- Section "FREQUENCY CAPPING": checkbox "Set per user frequency cap"; inputs "[ ] impressions per [ ] [Minutes v]" (number, number, unit dropdown)
- Section "TAGS FOR CUSTOMERS THAT SIGNUP": input with placeholder "Mark customer with a tag..."
**Implied features:**
- Exit-intent detection on desktop; time-based trigger option; separate mobile/tablet trigger config (no exit-intent on touch)
- Page-level exclusion targeting (page picker)
- Prebuilt overlay theme library; presumably theme content/text is editable after selection
- Post-signup flows: message or redirect URL
- Per-user frequency capping via cookie/localStorage
- CRM-style customer tagging on signup (tags reused for segmentation elsewhere)
**Uncertainties:**
- MEDIUM: Whether theme texts/discount are editable per campaign or fixed by theme (no editor visible).
- MEDIUM: Where captured leads are stored/exported (customer list? email marketing integration?) — not shown.
- MEDIUM: Whether signup auto-issues the promised 10% coupon/gift-card code — not shown; interacts with coupons module.
- LOW: Time-trigger seconds value input (assume appears when "Time" is selected).

### Lead Capture Overlay — Create/Edit Campaign, "Go to Url" variant (`admin-015-lead-capture-overlay-03-edit.jpg`)
**Purpose:** Same create/edit screen as above with the "Go to Url" post-signup action selected; reveals the Submit button.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/apps/app/lead-capture-overlay/view`
- All elements of `admin-015-lead-capture-overlay-02-edit.jpg` (GENERAL SETTINGS with name, Status Active toggle, Trigger Exit/Time, Mobile and Tablet Trigger Disabled/Time, Exclude customers that visited pages; SELECT A THEME grid with scrollbar; FREQUENCY CAPPING; TAGS FOR CUSTOMERS THAT SIGNUP)
- Section "UPON A SUCCESSFUL SIGN-UP": "Go to Url" tab selected (blue); "Url to redirect" label + input with placeholder "Url"
- "Submit" button (blue, bottom right)
**Implied features:**
- Redirect-to-URL conversion flow (e.g. to a thank-you or offer page)
**Uncertainties:**
- LOW: URL validation rules (assume absolute or relative URL accepted).

### General Settings (`admin-016-general-settings.jpg`)
**Purpose:** Store-wide settings: identity, access protection, units/timezone, code injection, robots.txt, and root-directory file uploads.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/settings/general`
- Top bar: logo, section selector "General Settings" dropdown, Search, "Pradize", avatar "CB"
- Page heading: "Settings"
- Red annotation: "UPDATE: As websiteEmpire/gui/panes/PaneSettings we need to be able to design different settings / language (in a table)" (spec note: per-language settings table)
- Buttons top-right: "Change subdomain", "Delete store"
- Section "STORE INFORMATION": "Store name" input (value "Pradize"); "Email address" input (placeholder "contact") + fixed suffix "@pradize.com" + a second input to its right
- Section "PASSWORD PROTECT YOUR ENTIRE STORE": toggle (off)
- Section "STANDARDS AND FORMAT PRESETS": "Unit system" dropdown ("Metric"); "Default weight unit" dropdown ("g"); "Timezone" label with "Edit" link + dropdown ("CET", disabled until Edit clicked)
- Section "HEADER INCLUDE CODE": large textarea, placeholder "Paste your script here"
- Section "BODY INCLUDE CODE": large textarea containing `<meta name="p:domain_verify" content="00ad9a6f0f8175e24f45816f8c973b06"/>`
- Section "ROBOTS.TXT INCLUDE TEXT": large textarea, placeholder "Paste your lines here"
- Section "ROOT DIRECTORY UPLOAD": drag-and-drop zone "Drop Files Here to Upload to Root Directory (supported file formats: txt, xml, html, json)"; empty state "No files have been uploaded to the root directory yet."; help text "Please note that this should only be used for files that explicitly need to be added to the root folder. For general images, videos, and other files, please use the Files section instead."
**Implied features:**
- Store hosted on a platform subdomain with subdomain rename and full store deletion
- Storefront-wide HTTP password protection (staging/coming-soon mode)
- Injected header/body code rendered on all storefront pages (verification tags, analytics)
- robots.txt merge (platform base + custom lines)
- Root-directory static file hosting (e.g. google site verification files); a separate "Files" section exists for general assets
- Per the annotation: multi-language/per-locale settings variants are a requested feature
**Uncertainties:**
- CRITICAL: Store email domain handling — input shows placeholder "contact" with suffix "@pradize.com" plus an adjacent second field; is this a send-from address with custom domain, and how is outbound email (SPF/DKIM) handled?
- CRITICAL: "Delete store" semantics (soft delete? data retention?) — destructive, needs definition.
- MEDIUM: Whether "Password protect" reveals a password field when toggled on (assume yes).
- MEDIUM: Scope of unit system / weight unit (affects shipping calculations and product forms).
- LOW: Timezone edit flow (Edit link enables the dropdown — safe assumption).

### Product Page Settings (`admin-017-product-pages.jpg`)
**Purpose:** Enable/disable optional product-level fields and configure description tabs shown on the product form and storefront.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/settings/product`
- Top bar: logo, section selector "Product Page" dropdown, Search, "Pradize", avatar "CB"
- Page heading: "Settings"
- Toggle rows (all on), each with title + help text:
  - "Type" — "This allows you to group products by type. For example shirts, glasses or shoes. This is useful for setting up rule based collections."
  - "Description Tabs" — "This is a text area where you can put detailed description of your product. You can also have multiple description tabs such as shipping and care." Sub-widget: draggable tab chip "Description" (with drag handle "::") + "+" button to add tabs
  - "Compare at price" — "If you have an item for sale you are able to show the original price with compare at price."
  - "Shipping weight" — "Allows you to store the weight of products and use that data to calculate shipping price."
  - "Vendor" — "Vendor is the place you are sourcing your product from."
  - "SKU" — "This is a stock keeping unit that can be a very useful to identify and track inventory at warehouses."
  - "Tags" — "Tags help customers find your products if you have a store search. Tags are also useful for setting up condition based collections."
- Red annotations (spec notes): "ADD: Sync with amazon FBA inventory" and "Add: Add buy on amazon button when FBA inventory is available"
**Implied features:**
- Feature flags controlling which fields appear on product edit form and storefront
- Multiple named, orderable description tabs (drag to reorder)
- Rule/condition-based collections driven by Type and Tags
- Shipping-price calculation using product weight
- Requested: Amazon FBA inventory sync and conditional "Buy on Amazon" button
**Uncertainties:**
- MEDIUM: Effect of disabling a field on existing data (hidden vs deleted) — data-model decision.
- MEDIUM: Are description tab names defined globally here and filled per product, or fully per-product?
- LOW: Whether toggles save instantly or need a Save button (none visible; assume instant).

### Pixels (`admin-018-pixels.jpg`)
**Purpose:** Install and manage third-party ad/analytics tracking pixels for the storefront.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/app/pixels`
- Top bar: logo, section selector "Pixels" dropdown, Search, "Pradize", avatar "CB"
- Page heading: "Pixels"
- Card grid (5 cards):
  - Facebook — badge "Installed" (green), "..." overflow menu, logo, label "Standard Pixel", pixel ID `811285492547861`
  - Google Analytics Enhanced Ecommerce — badge "Installed", "..." menu, logo, label "Tracking ID", ID `65722920`
  - Snapchat — badge "Not Installed", logo, "Install" button (blue)
  - Pinterest — badge "Installed", "..." menu, logo, label "Universal Pinterest Tag", ID `2613108244513`
  - TikTok — badge "Not Installed", logo, "Install" button (blue)
**Implied features:**
- Per-provider install flow (enter pixel/tag ID); edit/uninstall via "..." overflow menu
- Automatic storefront event tracking (page views, ecommerce events — "Enhanced Ecommerce" implies purchase/add-to-cart events)
**Uncertainties:**
- CRITICAL: Which ecommerce events are fired per provider (ViewContent, AddToCart, Purchase, values/currency) — money-adjacent tracking accuracy; not visible.
- MEDIUM: Contents of the "..." menu (edit / uninstall / test?) — not shown.
- MEDIUM: Support for multiple pixels per provider (grid suggests one each).
- LOW: Consent management interaction (no GDPR/consent UI visible; assume out of scope of this screen).

### Coupon Codes — List (`admin-019-coupons.jpg`)
**Purpose:** List coupon campaigns with usage stats and access to create/edit.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/settings/coupons`
- Top bar: logo, section selector "Coupon Codes" dropdown, Search, "Pradize", avatar "CB"
- Page heading: "All Coupon Codes"
- "Add new campaign" button (green)
- Pagination indicator: "Coupons: 1 - 2 of 2" with previous/next chevron buttons (disabled)
- Campaign rows (2), each with:
  - Green status dot (active)
  - Campaign name ("CODE123", "FREENAILS")
  - Coupon code chip/boxed value ("CODE123", "FREENAILS")
  - Usage: "Used 5 times" / "Used 0 times"
  - Revenue-ish figure: "Totalling 37.26" / "Totalling 0"
  - Chevron (>) to open
**Implied features:**
- Campaign vs code distinction (campaign name not customer-visible; code is)
- Usage counting and totals aggregation (order totals or discount totals)
- Active/inactive status
**Uncertainties:**
- CRITICAL: What "Totalling" measures (sum of discounts given vs revenue of orders using the coupon) — money reporting semantics.
- LOW: Sorting/search within list (none visible; assume simple paginated list).

### Coupon Codes — Create/Edit Campaign (`admin-019-coupons-02-edit.jpg`)
**Purpose:** Create/edit a coupon campaign: limits, validity, discount type/value, code, and applicability rules.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/settings/coupons?id=0`
- Page heading: "All Coupon Codes"; left sidebar list: "New Campaign" (selected/highlighted), "CODE123" (green dot), "FREENAILS" (green dot)
- Close "X" (top right of edit panel)
- "Campaign Name (Not visible to customers)" label + input (placeholder "Your super campaign") + active/enabled toggle (on) to its right
- Section "SETTINGS":
  - "Limit the number of users total": radios "Set the limit" / "Unlimited" (selected)
  - "Limit the number of uses per customer": radios "Set the limit" / "Unlimited" (selected)
  - Checkbox "Can be combined with other coupon codes" (unchecked)
  - "Valid from" date input (value 04/12/2026) + checkbox "Never expires" (checked)
- Section "DISCOUNT AND CODE":
  - "Type of discount" dropdown (value "$ off from order total")
  - "Discount" input (placeholder "$")
  - "Gift card code" input (placeholder/value "AOBUWIHOQ" — auto-generated code)
- Section "WHEN THE COUPON CODE CAN BE USED": "Add rule" button (top right); default text "Your campaign applies to all orders."
- "Save" (blue) and "Cancel" buttons
**Implied features:**
- Total-uses and per-customer-uses limits (numeric when "Set the limit" chosen)
- Coupon stacking control
- Validity window (from date; expiry date when "Never expires" unchecked)
- Auto-generated code with presumable manual override ("Gift card code" naming suggests coupons and gift cards share a code system)
- Rule engine restricting applicability (products/collections/order value — via "Add rule")
**Uncertainties:**
- CRITICAL: Shared code namespace between coupons and gift cards (field literally labelled "Gift card code" inside a coupon) — data-model question: are coupons implemented as gift-card codes?
- CRITICAL: Rule types available under "Add rule" (min order value? specific products/collections? customer segments?) — not shown; affects discount engine design.
- MEDIUM: Per-customer limit enforcement identity (email? account? device?) for guest checkout.
- LOW: Date format/locale of "Valid from" (shown 04/12/2026).

### Coupon Codes — Discount Type Dropdown Open (`admin-019-coupons-03-edit.jpg`)
**Purpose:** Same coupon edit screen with the "Type of discount" dropdown expanded, revealing available discount types.
**Visible elements:**
- All elements of `admin-019-coupons-02-edit.jpg` (sidebar with New Campaign/CODE123/FREENAILS, campaign name + toggle, limits radios, combinable checkbox, Valid from + Never expires, Discount input, Gift card code "AOBUWIHOQ", WHEN THE COUPON CODE CAN BE USED + Add rule + "Your campaign applies to all orders.", Save/Cancel)
- "Type of discount" dropdown open with 3 options: "$ off from order total" (highlighted/selected), "% off from order total", "Free Shipping"
- Red annotation: "Free product => We can then select a product" (spec note: requested 4th discount type: free product with product picker)
**Implied features:**
- Three discount types today: fixed amount, percentage, free shipping
- Requested: free-product discount type with product selection
**Uncertainties:**
- MEDIUM: For "Free Shipping", whether the Discount value field is hidden/ignored.
- LOW: Currency of "$ off" follows store default currency (safe assumption).

### Issued Gift Cards — List (`admin-020-issued-gift-cards.jpg`)
**Purpose:** List all issued gift cards with status, dates, value, and pagination; entry point to issue new cards.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/gift-cards`
- Top bar: logo, section selector "Issued Gift Cards" dropdown, Search, "Pradize", avatar "CB"
- Page heading: "All Gift Cards"
- Filter control next to heading: "Add filters to narrow the data you are viewing" dropdown
- "Issue a gift card" button (green)
- Pagination: "Gift cards: 1 - 20 of 765" with previous/next chevrons
- Rows (20 visible), columns per row:
  - Gift card code (e.g. `5820475F2C855E2`, `CCC969B7CA821AA`, ...)
  - Status badge "NOT USED" (grey) on every visible row
  - "Generated MM/DD/YYYY" date (e.g. 04/07/2026 down to 08/10/2025)
  - "Expires MM/DD/YYYY" date (each ~1 month after generation)
  - Value "$5.00" on every row
  - Chevron (>) per row for details
**Implied features:**
- Gift card lifecycle: generated → (used / expired); statuses beyond NOT USED (e.g. USED — confirmed by filter screen)
- High-volume automated issuance (765 cards at uniform $5 with ~30-day expiry suggests programmatic issuance, e.g. from lead-capture or post-purchase flows)
- Per-card detail view
**Uncertainties:**
- CRITICAL: Are these stored-value instruments (partial redemption, remaining balance) or single-use $5-off codes? Money/data-model impact.
- CRITICAL: What issues these cards automatically (lead capture overlay? recent purchase? manual bulk?) — source of the 765 uniform cards is not visible.
- MEDIUM: Whether expired cards get an EXPIRED status or remain NOT USED.
- LOW: Code format (hex-like 15-16 chars, uppercase).

### Issued Gift Cards — Issue Dialog (`admin-020-issued-gift-cards-02-create.jpg`)
**Purpose:** Manually issue a single gift card and email it to a recipient with a templated message.
**Visible elements:**
- Modal title: "ISSUE A GIFT CARD", close "X"
- "Gift card value" input (placeholder "$")
- "Expiration date" input (empty)
- "Send gift to the following email" input
- "Email subject line" input (default value "Your gift card code")
- "Message" multiline textarea with default template: "You have been issued a {gift_value} gift card.\nHere is the gift card code: {gift_code}."
- "Send" (blue) and "Cancel" buttons
- Background: gift card list (as in `admin-020-issued-gift-cards.jpg`) dimmed behind modal
**Implied features:**
- Transactional email sending with template variables `{gift_value}`, `{gift_code}`
- Code auto-generation on issue
- Optional expiration (field can be left empty → never expires?)
**Uncertainties:**
- MEDIUM: Behaviour with empty expiration date (never expires vs default policy).
- MEDIUM: Whether email is mandatory (can a card be issued without sending?).
- LOW: Supported template variables beyond the two shown.

### Issued Gift Cards — Filter Panel (`admin-020-issued-gift-cards-03-filter.jpg`)
**Purpose:** Filter the gift card list by status, type, recipient, code, value, and date ranges; save reusable views.
**Visible elements:**
- Same list page behind, with filter dropdown expanded into a panel containing:
  - Segmented status tabs: "View all" (selected) / "Used" / "Unused"
  - "Type" dropdown (placeholder "Select card type")
  - "Issued for Email" input (placeholder "Email address")
  - "Code" input (placeholder "Issued Card Code")
  - "Amount value" — condition dropdown (placeholder "Select condition") + value input
  - "Generated" dropdown (value "All time")
  - "Expires" dropdown (value "All time")
  - Buttons: "Refine View" (blue), "Save View", "Reset", "Cancel"
- List rows/columns and pagination ("1 - 20 of 765") as in the base list screen
**Implied features:**
- Multiple gift-card types exist ("Select card type" — e.g. manually issued vs auto-issued/promotional)
- Comparison operators on amount (equals/greater/less)
- Date-range presets for generated/expires
- Saved filter views per admin user
**Uncertainties:**
- MEDIUM: The set of card types (manual, promotional, refund-credit?) — impacts data model.
- MEDIUM: Where saved views are stored and whether they are shareable between admins.
- LOW: Date presets contents ("All time", presumably last 7/30 days etc.).

### Edit Product Page — Main Form (`admin-021-edit-product-page.jpg`)
**Purpose:** Edit a product: type, collections, title, rich description, shipping/inventory/vendor/tags, variants with per-variant pricing, and variant images.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/products/view?id=6384`
- Variant-mode segmented toggle at top: "Single Variant" / "Multi Variant (size, color, etc)" (Multi selected, blue)
- "Type" dropdown (value "Dress Long")
- "Collections" multi-select with removable chip "Ethereal Dresses And Fancy Flowy Gowns ×"
- "Product Title" input (value "Vivienne Silver Fairycore Ethereal Dress Gown")
- "Description" section with tab "Description" (blue) and full rich-text editor: Source button, undo/redo, B, I, U, S, subscript, superscript, remove-format, lists (numbered, bulleted), indent/outdent, quote, alignment buttons (left/center/right/justify), direction buttons, link/unlink, anchor, flag/language, image, video?, table, insert-special, Styles dropdown, Format dropdown, Font dropdown, Size dropdown, text color, background color; long English marketing copy in the editor plus an embedded size table (US/CA | 2 | 4 | 6 | 8 | 10)
- "Shipping weight" input with unit suffix "g"
- "Does this product require shipping?" dropdown (value "Yes")
- "Inventory policy" dropdown (value "Don't track inventory")
- "Vendor" dropdown/combobox (placeholder "Select or enter product vendor")
- "Tags" input (placeholder "Enter tags")
- Red annotation: "|_| Sync with FBA inventory if available (checked by default)" (requested checkbox)
- "Variants" section, subtitle "Set the product variations like size or style":
  - "Option Title" input (value "Size") + checkbox "Changes Product Look"
  - "Option Values" chip editor with removable chips: "US-2 | UK/AU-6 | EU/DE-32 | FR/ES-34 ×", "US-4 | UK/AU-8 | EU/DE-34 | FR/ES-36 ×", "US-6 | UK/AU-10 | EU/DE-36 | FR/ES-38 ×", "US-8 | UK/AU-12 | EU/DE-38 | FR/ES-40 ×", "US-10 | UK/AU-14 | EU/DE-40 | FR/ES-42 ×"
  - Display-mode toggle: "Text Dropdown" (selected) / "Thumbnail selector"
  - Trash/delete icon for the option row
  - "Add another option" button
- Variant table: columns Variant (checkbox + name; first row has "DEFAULT" badge), Price ($129.99 on all 5 rows), Compare at ($ placeholder), SKU (empty inputs), unnamed toggle column (first row toggle lighter/off-blue, others on)
- Red annotation on the toggle column: "SOLD OUT settings (erase inventory if checked)" with a red box around the toggles
- Section "Upload All Variant Images": drag-drop zone "Drop Images Here to Upload"; 5 numbered image thumbnails (1-5) of the dress
- Red annotation: "Add product videos (with settings if displayed position, first by default" (requested product video support)
**Implied features:**
- Single-variant vs multi-variant product modes
- Multiple options per product ("Add another option"), option values as chips, "Changes Product Look" linking option values to images
- Thumbnail-selector variant picker on storefront as alternative to dropdown
- Per-variant price, compare-at, SKU, availability toggle; default variant designation
- Ordered image gallery (numbered thumbnails imply drag-ordering)
- Inventory policy alternatives (track / don't track)
- Requested: FBA inventory sync per product; product videos with display-position settings
**Uncertainties:**
- CRITICAL: Meaning of the per-variant toggle column (enabled/available vs sold-out) and the annotation "erase inventory if checked" — inventory data-model impact.
- MEDIUM: Whether per-variant weight/shipping overrides exist (only product-level weight visible).
- MEDIUM: How "Changes Product Look" maps images to option values (per-value image assignment UI not shown).
- LOW: Currency display "$" follows store currency.

### Edit Product Page — Lower Half / SEO (`admin-021-edit-product-page-02.jpg`)
**Purpose:** Lower portion of the product edit form (same page id=6384, mostly blurred) exposing SEO fields and save action; carries spec annotations for a timer-promo feature.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/products/view?id=6384`
- Rich-text editor toolbar (top, partially visible) and blurred description content with highlighted table row
- Several blurred field groups mirroring the main form (shipping weight, requires shipping, inventory policy, vendor, tags), a blurred Variants section with option chips, "Text Dropdown"-style toggle, blurred variant table (5 rows with price/compare-at/SKU/toggles), blurred "Upload All Variant Images" drop zone and 5 thumbnails
- Red annotations (spec notes): "|_| Timer promo: We select days for which timer promo is display (with discount in %, currency or free product offered)"; "=> For each domain, time and day adapt to the main country language"; "=> Available as AI job that will add / remove / measure this feature"
- Section "SEO" with red annotation "Add more settings for social media sharing (facebook, pinterest...)":
  - "Page Title (SEO)" input (value "Vivienne Silver Fairycore Ethereal Dress Gown")
  - "Meta Description" input (value "Experience luxury with worldwide shipping. Your first purchase is risk-free: satisfied or refunded, no return needed. Discover timeless elegance today.")
  - "URL & Handle" input (value "vivienne-silver-fairycore-ethereal-dress-gown")
- "Save" button (blue, bottom right)
**Implied features:**
- Per-product SEO overrides (title, meta description, slug/handle)
- Requested: countdown timer promotions scheduled by day with discount (%/amount/free product), localized per domain/country, managed and measured by AI jobs
- Requested: Open Graph / social sharing metadata settings
**Uncertainties:**
- MEDIUM: Slug change behaviour (auto-redirect from old handle?) — SEO impact, not visible.
- MEDIUM: Timer promo (annotation) interaction with coupons/pricing engine — feature request, needs definition.
- LOW: Meta description length validation.

### Edit Product Page — Upsells, Bumps, Related, Custom Fields, Bundles (`admin-021-edit-product-page-03-upsell-bumps-and-bundles.jpg`)
**Purpose:** Reference screen (from a different platform, GrooveKart) showing per-product marketing add-ons: upsell page, order bumps in three placements, related products, customization fields, and bundle discounts.
**Visible elements:**
- URL: `protecing.groovekart.com/administration/v2/index.php/product/form/8?_token=...` (different engine than pradize screens)
- Header: green logo, "Edit Product" title, "Preview" button (top right)
- Left nav "PRODUCT SETUP": radio-style items "Information", "Images & Videos", "Pricing & Variants", "Publish"
- Left nav "MARKETING (OPTIONAL)": "Product Layout" (selected/checked), "Upsells", "Bumps", "Related Products", "Custom Fields", "Product Bundles", "SEO", "3rd Party Display"
- Section "Upsell Page Template" (red-boxed/highlighted): text "Select a template for Upsells. If you do not have good templates yet - go to Visual Builder and build the most selling one" (with "go to Visual Builder" link); template preview card "Default Upsell Layout"
- Section "Upsell Products": help text "Application will search for all active products except product you are editing right now."; search input "Search and add a related upsell product"; selected item row with thumbnail "Épurateur De Piscine Compatible (ref:filter-piscine-promo)" and remove "×"
- Red annotation (left margin): "Optional: Enabled only some days of the week / month. It then add a timer." / "=> Available as AI job so AI can add / remove / measure performances" / "=> Day time adapt to main country of each domain language"
- Section "Bumps on Product Page" (red-boxed): help "Please be sure Product Template you selected have Bump element"; search input "Search and add Bump products on product page"; 4 selected bump product rows with thumbnails and remove "×": "Bouée Rose Pour Enfants (ref:bouet-rose)", "Bouée Dorée Pour Enfants (ref:bouee-or)", "Grand pistolet à eau 700 ML (ref:pistolet-eau)", "Bouée Licorne Pour Enfants (ref:...)"
- Section "Bumps on Checkout Page": help "These Bumps will be shown on Shopping Cart Page and on Checkout Page"; search input "Search and add a Bump product on checkout page"
- Section "Bumps on Float Cart": help "When visitor will add product in the cart we will show float shopping cart. These Bumps will be shown there"; search input "Search and add a Bump product on pop up cart"
- Section "Related Products": help "Select related products"; search input "Search and add a related product"
- Section "Custom Fields": help "Customers can personalize the product by entering some text or by providing custom image files."; button "Add A Customization Field" (pink)
- Section "Product Bundles" (red-boxed):
  - "Available Bundle Discounts for this product" table — columns: Discount Name, Discount %, Bundle Product, Action; empty state "This product has no bundle discounts."
  - "All Bundle Discounts" table — columns: Discount Name, Discount %, Primary Bundle Product, Secondary Bundle Product, Action; empty state "No bundle discounts found."
  - "+ New Bundle" button (pink)
**Implied features:**
- Post-purchase/upsell page built from templates in a Visual Builder
- Order bumps in three placements: product page, cart/checkout page, floating cart popup (requires theme/template bump slots)
- Product personalization (custom text and image upload fields)
- Two-product bundle discounts (primary + secondary) defined by percentage
- Requested (annotation): scheduled/timed bumps-upsells with timer, AI-managed with performance measurement, localized by domain country
**Uncertainties:**
- CRITICAL: This is a different platform (GrooveKart) used as reference — which of these behaviours are to be replicated exactly in the Pradize engine vs merely inspiration must be decided.
- CRITICAL: Upsell timing semantics (pre-purchase page vs post-purchase one-click upsell with saved payment) — payment/security impact; not visible.
- MEDIUM: Bump pricing (can a bump have its own discounted price, or full price only?).
- MEDIUM: Whether custom-field uploads are moderated/validated (file types, size).
- LOW: Related products placement on storefront (assume a standard carousel).

### Edit Product Page — Create Bundle Detail (`admin-021-edit-product-page-03-upsell-bumps-and-bundles-02.jpg`)
**Purpose:** Close-up of the Product Bundles section with the bundle creation form open (GrooveKart reference).
**Visible elements:**
- Heading "Product Bundles"
- "Available Bundle Discounts for this product" table — columns: Discount Name, Discount %, Bundle Product, Action; empty state "This product has no bundle discounts."
- "All Bundle Discounts" table — columns: Discount Name, Discount %, Primary Bundle Product, Secondary Bundle Product, Action; empty state "No bundle discounts found."
- "+ New Bundle" button (pink)
- "Create a bundle" panel:
  - Help text "You can select only one product"
  - Search input "Search and add a product to make bundle"
  - "Discount in percentage %" numeric input
  - "Create Bundle" button (pink)
**Implied features:**
- Bundle = current product + exactly one other product, with a percentage discount
- Bundles are directional (primary/secondary) but visible from both products ("Available ... for this product" vs "All Bundle Discounts")
**Uncertainties:**
- MEDIUM: What the discount % applies to (secondary product only, or whole bundle total?).
- MEDIUM: Where "Discount Name" comes from (auto-generated? no name field in the create form).
- LOW: Storefront presentation of bundles (assume "buy together" block on product page).

### Payment Processing (`admin-XXX-payment-processing.jpg`)
**Purpose:** Manage connected payment processors, defaults per payment method, and the store's default currency.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/payment`
- Top bar: logo, section selector "Payment Process..." dropdown, Search, "Pradize", avatar "CB"
- Heading: "2 Processors connected"
- "Default store currency" label + dropdown (value "USD")
- "Connect Other Payment Processors" button (green)
- Processor rows (2):
  - Stripe logo + green badge "DEFAULT FOR CREDIT CARDS" + "Settings" dropdown button
  - PayPal logo + green badge "DEFAULT FOR PAYPAL" + "Settings" dropdown button
**Implied features:**
- Multiple processors connectable; per-payment-method default designation (credit cards vs PayPal wallet)
- Per-processor settings (credentials, capture mode?) behind the Settings dropdown
- Catalogue of additional connectable processors behind "Connect Other Payment Processors"
- Store-wide default currency affecting prices and processor charges
**Uncertainties:**
- CRITICAL: Contents of per-processor Settings (API keys, capture vs authorize, webhooks, test mode) — money/security core, not visible.
- CRITICAL: Effect of changing default store currency on existing product prices and past orders — money data-model impact.
- MEDIUM: Which other processors are offered by "Connect Other Payment Processors".
- MEDIUM: Multi-currency support (single default shown; per-domain currency implied elsewhere by localization annotations).
