# Part B — Admin: collections, gift cards, abandoned checkout, customers, redirects, currencies, reviews

Source screenshots: existing ecommerce admin (`pradize.commercehq.com/admin/...`). Red text/strikethroughs in some screenshots are the store owner's own annotations (change requests for the new engine) and are inventoried as such, not as existing UI.

---

### Collections list (`admin-006-collection.jpg`)
**Purpose:** List all product collections with bulk selection, reordering, and per-row publish/edit/delete actions.
**Visible elements:**
- Browser URL: `/admin/collections`
- Top bar: logo, module switcher dropdown labelled "Collections List" (hamburger icon + caret), global search field, store name "Pradize", user avatar "CB" with dropdown caret
- Page-level view selector: "All Collections" with dropdown caret (implies saved/filtered views)
- Green primary button "Add a collection" (top right)
- Bulk selection bar: master checkbox + "0 collections selected" counter
- Pagination summary: "Collections: 1 - 20 of 257" with previous `<` and next `>` arrow buttons (20 per page)
- Table rows, each with: row checkbox, drag handle (6-dot grip icon — manual ordering), thumbnail image, collection title as blue link (e.g. "Ethereal Dresses And Fancy Flowy Gowns", "Peplum Midi Dresses", "Open Summer Mesh Sneakers For Women", "Glitter Heels", "Thigh High Heeled Boots", "Sexy Stripper Heels (Platform & stiletto)", "Glitter & Sequins Evening Dresses", "Black Elegant Dresses", "Black Elegant Long Dresses", "Black Elegant Midi Dresses", "Shimmery dresses for women", "Business casual and professionnal outfits", "Animal print dresses", "Boot Cut Flare Pants Bell Bottoms", "Tweed dresses", "Elegant Pajamas", "Sweater dresses"), "Edit" button, "Unpublish" button, trash/delete icon
**Implied features:** publish/unpublish state per collection (all shown published since button says "Unpublish"); bulk actions on selected collections; drag-and-drop manual sort order of collections; search across collections; saved views/filters under "All Collections"; clicking title opens edit page.
**Uncertainties:**
- MEDIUM: What bulk actions become available once collections are selected (delete? publish/unpublish?) — not shown.
- MEDIUM: What alternate views the "All Collections" dropdown offers (published/unpublished/drafts?).
- LOW: Whether the drag-handle order drives storefront display order (safe default: yes).

---

### Collection edit — auto mode, condition field dropdown (`admin-006-collection-02-edtiing-auto-01.jpg`)
**Purpose:** Create a collection ("Add a collection" form) with automatic product membership based on rules; shows the condition field dropdown open.
**Visible elements:**
- URL: `/admin/collections/view`
- Page title "Add a collection"
- Field "Collection Title" (placeholder "Eg. Summer Collection")
- Field "Collection Description" (multi-line textarea, placeholder "Short collection description")
- Image drop zone (right column): "Drop Image Here to upload New image" with upload icon
- Section "Collection Presets" with two toggle tabs: "Manually Add Products" | "Auto add products based on conditions" (auto selected, blue)
- Field "Product has to" — dropdown showing "Match one condition" (match-mode selector)
- "Condition #1" row: field dropdown (open), operator dropdown ("Select type"), value text input, trash icon to delete the condition
- Open dropdown options: "Product title" (highlighted), "Product vendor", "Product type", "Product price", "Product tag", "Weight", "Variant title"
- SEO section (partially covered by dropdown): "Meta Description" input, "URL & Handle" input (a "Page Title (SEO)" field exists — see next screenshot)
- Footer buttons: "Save draft" (grey), "Publish" (blue)
**Implied features:** rule-based smart collections evaluated against product attributes incl. variant-level (variant title, weight); draft vs published lifecycle for collections; SEO metadata and URL handle per collection.
**Uncertainties:**
- CRITICAL: "Product price" condition vs multi-currency and sale prices — which price is matched (base price? sale price? which currency?) affects data model.
- MEDIUM: "Match one condition" dropdown alternatives — presumably "Match all conditions" (AND/OR), but the full option list is not shown.
- MEDIUM: Whether auto-collections re-evaluate in real time when products change, or on save/cron.
- LOW: Weight unit used in the Weight condition (assume store weight unit).

---

### Collection edit — auto mode, operator dropdown + Add Rule (`admin-006-collection-02-edtiing-auto-02.jpg`)
**Purpose:** Same "Add a collection" form showing the condition operator choices and the ability to add more rules.
**Visible elements:**
- Everything from the previous screen (title, description, image drop zone, presets tabs with "Auto add products based on conditions" active, "Product has to" = "Match one condition", Condition #1 with field "Product title", value input, trash icon)
- Operator dropdown open ("Select type") with options: "Is exactly" (highlighted), "Starts with", "Ends with", "Contains", "Does not contain"
- "Add Rule" button below Condition #1 (adds Condition #2, #3, ...)
- SEO section fully visible: heading "SEO", fields "Page Title (SEO)", "Meta Description", "URL & Handle"
- Footer: "Save draft", "Publish"
**Implied features:** multiple stacked conditions per collection; string-matching operators for text fields.
**Uncertainties:**
- MEDIUM: Operator list for numeric fields (Product price, Weight) — likely differs (greater/less than) but not shown.
- LOW: Case sensitivity of the string operators (safe default: case-insensitive).

---

### Collection edit — manual mode, existing collection (`admin-006-collection-02-edtiing-manually.jpg`)
**Purpose:** Edit an existing collection ("Ethereal Dresses And Fancy Flowy Gowns") with manually assigned products, current image, and delete option.
**Visible elements:**
- URL: `/admin/collections/view?id=338` (numeric collection id in query string)
- Page header: collection name "Ethereal Dresses And Fancy Flowy Gowns" + red-bordered "Delete collection" button (top right)
- Field "Collection Title" (filled: "Ethereal Dresses And Fancy Flowy Gowns")
- Field "Collection Description" (filled: "Stunning and unique ethereal dresses and fancy flowy gowns for evening with fantasy.")
- Right column: image drop zone "Drop Image Here to Upload Replacement" + "Current image" panel showing the current collection image
- "Collection Presets" tabs: "Manually Add Products" active (blue), "Auto add products based on conditions" inactive
- Manual product list, each row: drag handle (manual sort), product thumbnail, product title + variant count (e.g. "Evangeline's Ethereal Fairycore White Dress Gown — 6 variants", "Anastasia's Blue Fairycore Ethereal Dress Gown — 7 variants", 8 more similar rows), price (e.g. "$99.99", "$129.99"), trash icon (remove product from collection)
- OWNER ANNOTATION (red, feature request): "Display rate | CTR rate | Purchase rate (Then automatic improved sorting)" next to the first product row — wants per-product display/CTR/purchase-rate metrics in this list and automatic sorting improvement
- OWNER ANNOTATION (red, feature request): "Suggest auto cropping framing so if image doesn't have the them image, we have a preview of what will be displayed" next to the image panel — wants auto-crop preview for collection images
**Implied features:** product picker to add products manually (not shown but required); drag ordering of products within a collection; SEO section presumably below the fold; per-product performance stats + auto-sorting (requested, not existing).
**Uncertainties:**
- MEDIUM: How products are added in manual mode (search modal? inline autocomplete?) — the add-product control is not visible in the screenshot.
- MEDIUM: Requested "automatic improved sorting" — algorithm unspecified (sort by purchase rate? CTR? blended score?). Needs product-owner definition.
- MEDIUM: Whether a collection can mix modes or switching tabs discards the other mode's data.
- LOW: Image auto-crop aspect ratio for the requested preview (assume the storefront card ratio).

---

### Gift cards list (`admin-007-gift-card.jpg`)
**Purpose:** List gift card products offered for sale, with the same list mechanics as collections.
**Visible elements:**
- URL: `/admin/products/gift-cards` (gift cards live under products)
- Top bar module switcher: "Gift Cards for Sale", global search, "Pradize", avatar "CB"
- View selector "All Gift cards" with caret
- Green button "Add a gift card"
- Bulk bar: master checkbox, "0 gift cards selected"
- Pagination: "Gift cards: 1 - 3 of 3", prev/next arrows (disabled state)
- 3 rows, each: checkbox, gift-card thumbnail image, title as blue link ("Gift Card - 140 USD for a CryoGex Cooling Clothe", "Gift Card - 68", "Gift Card - 48 USD for the Abs Belt 450G"), "Edit" button, "Unpublish" button, trash icon
**Implied features:** gift cards are sellable products with publish state; titles suggest fixed-value cards, sometimes tied to a target product's price.
**Uncertainties:**
- CRITICAL: Gift card redemption model is entirely absent from this batch (code generation, balance tracking, partial redemption, expiry enforcement, refund of gift-card-paid orders). Money-handling design required.
- LOW: No drag handles here (unlike collections) — assume no manual ordering for gift cards.

---

### Gift card create/edit (`admin-007-gift-card-02-editing.jpg`)
**Purpose:** "Sell a Gift Card" form defining value, expiration, collection placement, content, and SEO.
**Visible elements:**
- URL: `/admin/products/gift-cards/view`
- Page title "Sell a Gift Card"
- Field "Gift card value" (text input with `$` prefix, focused)
- Field "Expiration date" (input, presumably date picker)
- Field "Collections" (input, placeholder "Choose a collection" — assign gift card to collections)
- Field "Gift card title"
- "Description" section with tab "Details and terms" (blue/active) above a full WYSIWYG rich-text editor. Toolbar buttons: Source, undo, redo, Bold, Italic, Underline, Strikethrough, subscript, superscript, remove format, numbered list, bulleted list, decrease indent, increase indent, blockquote, div/container, align left, center, right, justify, paragraph direction (LTR/RTL ×2), link, unlink, anchor/flag, image, table, horizontal rule, Styles dropdown, Format dropdown, Font dropdown, Size dropdown, text color, background color; resizable editor area
- Right column: image drop zone "Drop Image Here to upload New image"
- "SEO" section: "Page Title (SEO)", "Meta Description", "URL & Handle"
- Footer: checkbox "Create another", "Publish" (blue), "Save draft"
**Implied features:** gift cards get their own storefront page (SEO + handle + image + description); "Create another" speeds batch creation; expiry per card product.
**Uncertainties:**
- CRITICAL: Expiration date semantics — fixed calendar date on the product vs validity period from purchase; legal constraints on gift card expiry vary by jurisdiction. Money impact.
- CRITICAL: Currency of "Gift card value" — `$` prefix only; interaction with the multi-currency converter (is value fixed in USD and converted at display?) undefined.
- MEDIUM: Whether "Details and terms" is a single tab or one of several (only one tab visible).
- LOW: "Create another" behavior (reopen blank form after publish — safe assumption).

---

### Abandoned checkout — campaign dashboard (`admin-008-abandonned-checkout.jpg`)
**Purpose:** Dashboard listing abandoned-checkout email campaigns with per-campaign recovery stats.
**Visible elements:**
- URL: `/admin/abandoned?id=1`
- Top bar module switcher "Abandoned Chec..." (truncated), search, "Pradize", "CB"
- Header "You have 1 Active Campaign"
- Header-right buttons: "Global statistics", "Logs", green "New Campaign"
- Left sidebar: campaign list with one entry "First abandoned checkout" preceded by a green status dot (active)
- Main panel: campaign name "First abandoned checkout" + on/off toggle (on, blue); close "X" (top right of panel)
- "Date range" dropdown, value "All time"
- Stats block: "Percentage of abandoned checkouts that got recovered — 4.76%"; three sub-metrics: "Unique Impressions 630", "Sales recovered 30", "Revenue recovered $2,080.45"
- Footer actions: "Email log" button, "Edit Settings" (blue), "Delete campaign" (text link)
**Implied features:** multiple concurrent campaigns; per-campaign enable toggle; date-range filtering of stats; email send log; global (cross-campaign) statistics view; system logs view; attribution of recovered sales/revenue to campaign emails.
**Uncertainties:**
- CRITICAL: Recovery attribution rules (click-through window, coupon-based attribution, last-touch?) — directly drives the "Revenue recovered" money metric.
- CRITICAL: Definition of an "abandoned checkout" (email captured at which checkout step? how long after inactivity?) — data model + GDPR relevance (emailing people who never completed purchase).
- MEDIUM: "Unique Impressions" meaning — email opens vs sends vs landing views.
- MEDIUM: Date range options in the dropdown (only "All time" visible).

---

### Abandoned checkout — new campaign form (`admin-008-abandonned-checkout-02-edit.jpg`)
**Purpose:** Create a new abandoned-checkout campaign: name, status, sender identity, and automated email sequence.
**Visible elements:**
- URL: `/admin/abandoned/view`
- Page title "New Abandoned Checkout Campaign"
- Section card "CAMPAIGN" containing: field "Campaign name" (empty); "Campaign status" toggle labelled "Active" (on); field "From name" (value "Pradize"); field "From email" — split input: local part ("support") + fixed domain suffix "@pradize.com"
- Full-width block/button "Add automated email" (grey — starts the email wizard)
**Implied features:** multiple automated emails per campaign (a sequence with delays); sender domain locked to the store domain (deliverability/DKIM implied).
**Uncertainties:**
- CRITICAL: Email sending infrastructure and domain authentication (SPF/DKIM) — the fixed `@pradize.com` suffix implies verified sending domain per store.
- MEDIUM: Per-email send delay/scheduling after abandonment — must exist for a sequence but is not visible in any screenshot.
- LOW: Whether campaign name is required before adding emails.

---

### Abandoned checkout — email wizard step 1: Type (`admin-008-abandonned-checkout-03-edit-choose-type.jpg`)
**Purpose:** Modal wizard for an automated email; step 1 chooses the email intent type.
**Visible elements:**
- Modal over the campaign form; close "X"
- 3-step progress indicator: "Type" (active) — "Style" — "Copy"
- Three selectable cards with radio marks:
  - "Warning" + icon (exclamation) + text "Let them know a product is going out of stock soon ot the discount is ending" (sic — typo "ot" in original UI)
  - "Reminder" + bell icon + "Let them know that their cart is still there waiting for them" (selected, blue check)
  - "Incentive" + gift icon + "Send them special limited time offer such as a coupon code"
- Full-width blue "Continue" button
**Implied features:** email type drives the template copy suggestions in later steps; scarcity/urgency messaging (stock/discount ending) for Warning type; coupon integration for Incentive type.
**Uncertainties:**
- MEDIUM: Whether "Warning" type reads real stock/discount data or is copy-only.
- LOW: Ability to go back between steps (progress bar suggests yes).

---

### Abandoned checkout — email wizard step 2: Style (`admin-008-abandonned-checkout-04-edit-choose-style.jpg`)
**Purpose:** Step 2 chooses the email visual template with a live preview.
**Visible elements:**
- Progress: Type (done, green check) — Style (active) — Copy
- Four template options, each with thumbnail + radio: "Plain Text" (selected, blue check), "Stylish", "Clean", "Minimal"
- "Preview" panel rendering the chosen template with sample content: heading "YOUR SHOPPING CART MISSES YOU.", body copy ("We saw that you were about to get some things in our shop and that you didn't get a chance to finish your order. We wanted to let you know that your cart is still there waiting for you."), link "Click here to go back to your checkout", "CONTENTS" section listing cart line items (sample: "CommerceHQ Blue Jacket Men Medium Sale 20% / Quantity: 1 / Total: $149.99" ×3), "TOTAL: $449.97", second "Click here to go back to your checkout" link, "Unsubscribe" link
- Blue "Continue" button
**Implied features:** dynamic merge of actual cart contents (items, quantities, line totals, cart total) into emails; tokenized resume-checkout deep link; mandatory unsubscribe link (compliance).
**Uncertainties:**
- CRITICAL: Resume-checkout link security — tokened URL restoring another session's cart/PII; token expiry and scope need definition (security impact).
- MEDIUM: Unsubscribe scope — per campaign vs all marketing email vs all store email.
- LOW: Whether template thumbnails are customizable (assume fixed set of 4).

---

### Abandoned checkout — email wizard step 3: Copy, subject (`admin-008-abandonned-checkout-05-edit.jpg`)
**Purpose:** Step 3 (Copy) — pick or write the email subject; accordion of 4 copy parts.
**Visible elements:**
- Progress: Type (done) — Style (done) — Copy (active)
- Accordion item "1 — Email subject" (open) with radio choices: "You want to make a deal?" (selected, check), "Can we tempt you?", "OK - Let's make a deal", "10% off to finish your order", "Order now and we will throw in a 10% discount", free-text option "Write your own..."
- Blue "Continue" button
- Collapsed accordion items: "2 — Email headline", "3 — Body text", "4 — Call to action button"
**Implied features:** curated copy library per email type + custom copy; four-part email composition model (subject/headline/body/CTA).
**Uncertainties:**
- LOW: Whether suggested copy varies by the Type chosen in step 1 (likely yes; safe to assume a per-type suggestion list).

---

### Abandoned checkout — email wizard step 3: Copy, body text (`admin-008-abandonned-checkout-06-edit.jpg`)
**Purpose:** Same Copy step with subject/headline completed and body-text choices shown.
**Visible elements:**
- Progress bar as before (Copy active)
- Item "1 — Email subject: You want to make a deal?" (done, green) + "Edit" link
- Item "2 — Email headline: You want to make a deal?" (done, green) + "Edit" link
- Item "3 — Body text" (open) with radios: "Get 10% off when you finish your order within the next 24 hours by using code CODE123" (selected), "We were so sorry to see you leave our site. If you come back now and complete your purchase we will be taking 10% off your order. Just use this coupon CODE123", "Write your own..."
- Blue "Continue" button
- Collapsed item "4 — Call to action button"
**Implied features:** coupon code placeholder (`CODE123`) inside body copy — expects a real coupon to substitute; inline re-editing of completed steps.
**Uncertainties:**
- CRITICAL: Coupon code source — is `CODE123` a literal the merchant must replace manually, or does the system generate/link a real discount code (and per-customer unique codes)? Money impact.
- MEDIUM: Whether the "24 hours" urgency is enforced (coupon expiry) or copy-only.

---

### Abandoned checkout — email wizard step 3: Copy, CTA button (`admin-008-abandonned-checkout-07-edit.jpg`)
**Purpose:** Final part of the Copy step — choose the call-to-action button label.
**Visible elements:**
- Progress bar (Copy active); items 1-3 completed with green labels + "Edit" links ("Email subject: You want to make a deal?", "Email headline: You want to make a deal?", "Body text: Get 10% off when you finish your order within the next 24 hours by using code CODE123")
- Item "4 — Call to action button" (open) with radios: "RESUME YOUR CHECKOUT" (selected), "TELEPORT TO YOUR CART", "RESTORE MY CART", "COMPLETE CHECKOUT", "GRAB IT NOW", "Write your own..."
- Blue "Continue" button (presumably finalizes the email)
**Implied features:** CTA deep-links to restored checkout; wizard completion returns to campaign form with the email added to the sequence.
**Uncertainties:**
- MEDIUM: What follows "Continue" — a review/schedule step (send delay) or immediate save; the delay setting is never shown.

---

### Customers list (`admin-009-customers.jpg`)
**Purpose:** Paginated list of all customers with purchase summary data and filtering.
**Visible elements:**
- URL: `/admin/customers`
- Top bar module switcher "Customers", search, "Pradize", "CB"
- Header "All Customers" + filter control "Add filters to narrow the data you are viewing" with caret
- Bulk bar: master checkbox, "0 customers selected"
- Pagination: "Customers: 1 - 20 of 1388", prev/next arrows
- Table columns: (checkbox), "Name" (blurred in screenshot), "Last purchase" (date + time, e.g. "04/07/2026 at 10:26pm"), "Location" ("City, Country" — e.g. "Temecula, United States", "Singapore, Singapore", "East Brighton , Australia", "Blackburn, United Kingdom", "Harish, Israel", "Shimla, India", "Port Of Spain, Trinidad and Tobago"), "Orders" (count, values 1-2), "Lifetime spent" (currency, e.g. "$59.99", "$718.91")
- Per-row chevron ">" (opens customer detail)
**Implied features:** customer aggregation from orders (last purchase, order count, lifetime value); configurable filters; bulk operations on customers; global search including customers.
**Uncertainties:**
- CRITICAL (GDPR/data model): Customer PII storage and lifecycle (the screenshot itself blurs names) — retention, anonymization, and export/delete rights must be specified for the new engine.
- MEDIUM: Available filter dimensions in "Add filters" (location? spend? order count?) — not shown.
- MEDIUM: Whether guest checkouts create customer records or only account holders (list likely includes guests given 1388 count).
- LOW: Sort order of the list (appears to be last-purchase desc; safe default).

---

### Customer detail (`admin-009-customers-02.jpg`)
**Purpose:** Single-customer view: order history, lifetime total, merchant notes, and contact/payment sidebar.
**Visible elements:**
- URL: `/admin/customers/view/1391` (numeric customer id)
- Page title "Customer: Christina Blietz"
- "ORDER HISTORY" table with columns: "Order #" (blue link, e.g. "3111"), "Date" ("04/07/2026 at 10:26pm"), "Payment" (status "Refunded"), "Shipping Status" ("Not Sent To Fulfillment"), "Total" ("$59.99")
- Line "TOTAL FROM ALL ORDERS: $59.99"
- "Notes" free-text textarea + blue "Save" button (merchant-internal notes)
- Right sidebar cards: "Shipping Addresses" (PO Box + country, partially blurred), "Billing Addresses" (same, blurred), "Email" (blue mailto link "tempchristina@yahoo.com"), "Phones" (section present, empty here), "Payments Used" ("PayPal")
**Implied features:** order → customer linkage; payment status incl. refunds; fulfillment status per order; multiple shipping/billing addresses per customer (plural headings); record of payment methods used; clickable order links to order detail.
**Uncertainties:**
- CRITICAL (GDPR): addresses/emails/phones are personal data — same retention/erasure requirements as the list view.
- MEDIUM: Full vocabulary of "Payment" and "Shipping Status" values (only "Refunded" / "Not Sent To Fulfillment" visible).
- LOW: Notes are single-blob text with Save (no note history) — safe to replicate as-is.

---

### URL redirects list (`admin-010-redirects.jpg`)
**Purpose:** Manage active URL redirects (old path → new path/URL) with enable toggles.
**Visible elements:**
- URL: `/admin/apps/app/redirects` (redirects implemented as an "app")
- Top bar module switcher "Redirects", search, "Pradize", "CB"
- Header "5 Active Redirects" + dedicated search input "Type to search redirects" with magnifier icon
- Green button "Add a Redirect" — OWNER ANNOTATION: crossed out in red, with red note "Redirects are added from product page edit. We can view them there." → in the new engine, manual creation is not wanted; redirects are auto-created from product/page slug changes and this screen is view/manage only
- Bulk bar: master checkbox, "0 redirects selected"
- Pagination: "Redirects: 1 - 5 of 5", prev/next arrows
- 5 rows, each: checkbox, source path, arrow icon, destination, on/off toggle (all on/blue), "Edit" button, trash icon. Examples: "inserts → https://pradize.com/product/full-length-shoe-inserts-pumps" (absolute URL destination), "sexy-nightwear-sleepwear-pyjamas-chemises → collection/sexy-sleepwear-nightgowns-pajamas-nighties", "sequined-dresses-mini → collection/sequin-short-classy-dresses", "tight-short-dresses-mini → collection/formal-short-dresses-tight-classy", "collection/sexy-night-club-dresses → collection/party-night-club-dresses-sexy"
**Implied features:** redirects to relative paths or absolute URLs; per-redirect enable/disable without deletion; auto-generation of redirects when a product/collection handle changes (per owner annotation); bulk delete/toggle.
**Uncertainties:**
- MEDIUM: Redirect HTTP status (301 vs 302) — not indicated anywhere; SEO-relevant choice.
- MEDIUM: Owner annotation scope — auto-create redirects only from product edits, or also collection/page slug changes (examples include collection→collection redirects)?
- MEDIUM: Chain handling (redirect pointing to a path that is itself redirected) and loop prevention — unspecified.
- LOW: Whether "Edit" should remain available if creation is removed (assume yes, editing destination stays useful).

---

### Currency converter settings (`admin-011-currency-conversion.jpg`)
**Purpose:** Configure the storefront currency converter: which currencies are active and how symbol/code are displayed.
**Visible elements:**
- URL: `/admin/apps/app/currency-converter` (also an "app")
- Top bar module switcher "Currency Convert...", search, "Pradize", "CB"
- Header "34 Active Currencies"
- Panel "CURRENCY CONVERTER" with column headers: "Currency name", "Symbol", "Code", "Symbol visibility", "Code visibility"
- First row in edit mode (Australian Dollar): enable toggle (on), editable symbol input ("$"), code "AUD", "Symbol visibility" segmented buttons Yes/No + Prefix/Suffix (Yes + Prefix selected), "Code visibility" segmented buttons Yes/No + Prefix/Suffix (Yes + Suffix selected), confirm (✓) and cancel (×) buttons
- Read-only rows with green active dot, symbol, code, settings summary text, "Edit" button per row: Brazilian Real ($, BRL, "Yes as Prefix", "Yes as Suffix"), Canadian Dollar ($, CAD), Swiss Franc (—, CHF), Chilean Peso ($, CLP), Chinese Yuan (¥, CNY, code "No"), Colombian Peso ($, COP), Czech Koruna (—, CZK), Danish Krone (—, DKK), Euro (€, EUR, code "No"), British Pound (£, GBP, code "No"), Hong Kong Dollar ($, HKD), Croatian Kuna (—, HRK), Hungarian Forint (—, HUF), Indonesian Rupiah (—, IDR), Israeli Shekel (₪, ILS, code "No") — list continues below the fold (34 total)
**Implied features:** per-currency display formatting (e.g. "$59.99 AUD"); enable/disable currencies individually; storefront currency picker fed by this list; exchange-rate source somewhere (not shown).
**Uncertainties:**
- CRITICAL: Exchange-rate source, update frequency, and whether conversion is display-only or transactional (is the customer charged in the converted currency?). Direct money impact.
- CRITICAL: Rounding rules for converted prices (psychological pricing like .99, rounding direction) — not shown, affects revenue.
- MEDIUM: What determines the default currency shown to a visitor (geo-IP? manual picker only?).
- LOW: Decimal/thousand separator localization per currency (assume standard locale formats).

---

### Reviews dashboard (`admin-012-reviews.jpg`)
**Purpose:** Product reviews overview: latest reviews, review-request email stats, and review-widget up-sell stats.
**Visible elements:**
- URL: `/admin/apps/app/reviews` (an "app")
- Top bar module switcher "Reviews", search, "Pradize", "CB"
- Header "266 Reviews"
- Header controls: status dropdown with green dot "Automated Review Request Emails Enabled" (caret — toggleable), "Add review" button, "Send review request" button, gear/settings icon button
- Panel "LATEST REVIEWS" with yellow badge "0 PENDING" and button "Manage reviews"
- 5 review rows, each: reviewer avatar/product thumbnail, text "<Name> left a review for Sidifu Luxe Gathered Purse" (Connie, Dana, Lisa, Jutta, Jon), timestamp ("07/05/2024 at 2:14pm" etc.), star rating (5★ ×4, one 4★), green badge "APPROVED"
- Panel "REVIEW REQUEST STATISTICS" with date-range dropdown "Last 30 days": metrics "Sent 1", "Accepted 0", "Acceptance rate 0%"
- Panel "UP-SELL STATISTICS" with date-range dropdown "Last 30 days": metrics "Impressions 0", "Clicks 0", "CTR 0%", "Orders 0", "Revenue $0.00", "Order rate 0%"
- Bottom-right button "Old orders that won't trigger a request"
**Implied features:** automated post-purchase review-request emails with global enable/disable; manual review entry by merchant; manual on-demand review requests; review moderation states (pending/approved); reviews widget doubles as an up-sell surface with revenue attribution; exclusion list of old orders from request automation.
**Uncertainties:**
- MEDIUM: What the "up-sell" in the reviews widget actually is (related-product block inside review emails or widget?) — mechanics not shown, only its stats.
- MEDIUM: "Accepted" definition in request stats (email led to a submitted review?).
- LOW: "Old orders that won't trigger a request" — assume a viewer/manager of the excluded-orders list.

---

### Reviews — add review modal (`admin-012-reviews-02-add.jpg`)
**Purpose:** Merchant manually adds a review (name, product, date, rating, text, photos).
**Visible elements:**
- Modal "ADD A REVIEW" with close "X"
- Field "Reviewer's name"
- Field/button "Select a product" (product picker)
- Field "Review date" (backdating allowed)
- Star rating selector: five segmented options 5★/4★/3★/2★/1★ (5★ preselected, blue)
- Textarea "Review text"
- File upload drop zone "Drop Files Here to Upload" (review photos)
- Checkbox "Create another"
- Blue full-width "Submit" button
**Implied features:** merchant-authored/imported reviews indistinguishable in storage from customer reviews; multi-photo reviews; batch entry via "Create another".
**Uncertainties:**
- MEDIUM: Whether manually added reviews are flagged internally as merchant-created vs customer-created (owner's settings annotation about "AI-created reviews" suggests provenance tracking is wanted).
- LOW: Accepted file types/count for uploads (assume images, multiple).

---

### Reviews — manage/moderation modal (`admin-012-reviews-03-manage.jpg`)
**Purpose:** Moderate all reviews: filter by status, search, read full text/photos, hide reviews.
**Visible elements:**
- Modal "MANAGE REVIEWS" with close "X"
- Status tabs: "Pending", "Approved", "Hidden", "All reviews" (active)
- Search input "Search by customer name or email" with magnifier
- Scrollable review list; each entry: "<Name> reviewed <Product link>" (blue product link "Sidifu Luxe Gathered Purse"), star rating, timestamp, full review text (e.g. Connie: "This is the best makeup bag I've ever used..."; Dana: "This storage bag is very convenient, specially designed for lazy people..."; Lisa: "There are a few loose threads, but they don't affect the functionality..." ; Jutta 4★: "Product quality: The quality looks good but we will see if it lasts..."), photo thumbnails on some reviews (Dana ×2, Lisa ×1, Jutta ×2), "Hide" button per review
- Scrollbar indicating longer list
**Implied features:** three-state moderation workflow (pending → approved / hidden); search by reviewer identity (email is stored even though not displayed publicly); per-review hide action from any tab.
**Uncertainties:**
- MEDIUM: Actions available on the "Pending" and "Hidden" tabs (Approve/Unhide buttons presumed but not visible in this screenshot).
- LOW: Whether hiding is reversible (Hidden tab implies yes).

---

### Reviews — settings (`admin-012-reviews-04-settings.jpg`)
**Purpose:** Configure review handling, storefront widget styling, and the review-request email.
**Visible elements:**
- URL: `/admin/apps/app/reviews/settings`; page title "Review Settings"
- OWNER ANNOTATION (red, feature requests): "Add those settings: | Reviews create by AI (jobs) | Display only customer reviews only it reach | | customers reviews" — wants (a) a setting for AI-generated reviews via jobs, and (b) a setting to display only real customer reviews once enough customer reviews exist (threshold-based switchover); exact wording garbled
- Section "GENERAL — How to handle and notify you about new reviews": segmented choice "Published after approval" | "Automatically published" (auto selected, blue); field "Email to send new reviews notifications" (value "cbettingervente@gmail.com")
- Section "STYLE OF REVIEWS STORE WIDGET — How will the reviews look in your store":
  - "STARS COLOR": color swatch + hex input "#FFB400"
  - "WRITE A REVIEW BUTTON": "Button color" swatch + "#007DCC", "Text color" swatch + "#FFFFFF"
  - "VERIFIED BUYER BADGE": dropdown, value "Checkmark with text"
  - "LAYOUT FOR DISPLAYING REVIEWS": radio thumbnails "Tiles" (selected) | "Rows"
  - "PRODUCTS WITHOUT REVIEWS YET": segmented "Show review widget" | "Hide review widget" (hide selected)
  - "NUMBER OF VISIBLE REVIEWS": dropdown "10" + help text 'The rest of the reviews will be visible with a "Show more" button'
- Section "REVIEW REQUEST EMAIL — This is the email that will be sent to your customers to request reviews": "Email subject" dropdown ("So what did you think of our products?"), "Email timeout after order shipped" numeric input "45" + unit dropdown "Days", buttons "Change template" and "Send test"; email preview area cut off at bottom of screenshot
**Implied features:** auto-publish vs moderated pipeline (ties to Pending state); verified-buyer badge logic (review linked to a real order); review request triggered N days after shipment (not order) — requires fulfillment tracking; templated request email with test-send; requested: AI review generation jobs + threshold-based display switch to genuine customer reviews.
**Uncertainties:**
- CRITICAL: Requested "Reviews created by AI (jobs)" — generating fake customer reviews has legal/compliance implications (consumer-protection law) and needs explicit provenance flags in the data model; the display-threshold rule ("display only customer reviews once they reach [N]") is incompletely specified (threshold value, per-product vs global).
- MEDIUM: "Verified buyer badge" dropdown alternatives (checkmark only? no badge? text only?) — full option list not visible.
- MEDIUM: Email subject dropdown — fixed suggestion list vs free text; "Change template" editor capabilities not shown (preview cut off).
- LOW: Timeout unit options besides "Days" (assume days/weeks; default 45 days).
