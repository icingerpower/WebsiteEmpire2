# Part D — Super-admin: accounts, checkout, shipping, emails, upsell campaigns, gift cards, themes

Source: screenshots of `pradize.commercehq.com/admin/...` (CommerceHQ reference UI). Super-admin scope = settings common to every sub-store of the multi-tenant organization.

Common chrome visible on almost every screenshot (listed once, applies to all screens below):
- Top dark navigation bar with: platform logo (left), hamburger-menu dropdown showing the current section name (e.g. "Employee Accou...", "Security Badge", "Shipping", ...) with a caret indicating a section switcher, centered global Search input, organization name "Pradize", user avatar "CB" with dropdown caret.

---

### Employee accounts list (`super-admin-05-emplye-accounts-01.jpg`)
**Purpose:** List all employee (staff) accounts of the organization with their status and last login.
**Visible elements:**
- Page title/counter: "7 Active Employee Accounts".
- Green "Invite New User" button (top right).
- Employee list rows (7 rows), each with:
  - Green status dot (active indicator).
  - Full name (partially redacted in screenshot).
  - Email address (gmail.com, tiksuper.com, yahoo.com addresses visible).
  - "Last logged in <date> at <time>" label with bold date/time (e.g. "11/12/2025 at 5:10pm", "04/03/2026 at 8:47am", "06/25/2020 at 5:14pm").
  - Right chevron (>) to open the account detail.
- No pagination control, no filter, no column headers.
**Implied features:** invite-by-email flow; per-account detail/edit screen (chevron); active vs inactive account states (green dot implies other colors exist); login tracking/audit (last-login timestamp stored per user); search possibly applies to employees via global search bar.
**Uncertainties:**
- MEDIUM: whether inactive/pending-invitation accounts appear in this list with a different dot color, or are filtered out ("7 Active" suggests a filter on status).
- LOW: sorting order of the list (appears unordered / by creation).

### Employee account detail / invite new user (`super-admin-05-emplye-accounts-02-invite-user.jpg`)
**Purpose:** Create (invite) or edit an employee account and configure its per-module access rights.
**Visible elements:**
- Left sidebar: "New Employee" entry (highlighted) + list of the 7 existing employees with green dots (master-detail layout).
- Right detail panel:
  - "Account details" header with an on/off toggle (enabled state shown) and a close (X) button.
  - "Full name" text field (placeholder "Employee name").
  - "Email" text field (placeholder "Email address").
  - "Phone" text field.
  - Access mode segmented selector: "Full Access" OR "Limited Access" (Limited Access selected, checkmark shown).
  - "ACCESS OPTIONS" section header, one row per module, each with radio choices "Full access" / "No access" (all set to "No access" in screenshot). Modules listed:
    - Apps
    - CMS
    - Customers
    - Dashboard
    - Domains
    - Gift cards
    - Inventory
    - Orders — this row uniquely has a third radio option "Limited access"
    - Products & Collections
    - Reports
    - Settings
    - Themes
    - CSV templates
    - Up-sell campaigns
    - Abandoned campaigns
    - Invoice orders
    - Pixels
    - Files
  - "Save" button (blue), "Cancel" button, "Delete Account" button (red outline, bottom right).
**Implied features:** role/permission matrix persisted per employee; account enable/disable toggle independent of deletion; email invitation sent on save (ties to "Store invitation" automated email seen on email setup screen); an "Orders: Limited access" mode with narrower rights than full (e.g. view-only or no-refund).
**Uncertainties:**
- CRITICAL: exact semantics of "Limited access" on the Orders module (view-only? no refunds? no export?) — impacts security model and money operations.
- CRITICAL: what "Full Access" master mode does versus per-module grid (does selecting Full Access hide/override the grid?), and who may grant Settings/Employee-management rights (no "Employees" module row exists — employee management permission source unclear).
- MEDIUM: whether the module list is fixed or dynamic per installed apps ("Apps" row suggests an app system).
- MEDIUM: whether phone is used for 2FA/notifications or informational only.
- LOW: toggle at top = account active/inactive (safe assumption).

### Security badge list (`super-admin-06-security-badge.jpg`)
**Purpose:** List reassurance/security badge configurations displayed on storefront cart/checkout pages.
**Visible elements:**
- Page title/counter: "1 Active Badge".
- Green "Create a new Badge" button (top right).
- One list row: green status dot, badge name "Safe checkout", placement summary text "Set on Cart and Checkout", "Edit" button, "Delete" button.
- URL shows this lives under `admin/apps/app/security-badge` (an "app" module).
**Implied features:** multiple badges possible; per-badge placement (cart, checkout, and — per edit screen — product page); active/inactive badge state.
**Uncertainties:**
- LOW: behaviour when several badges are active on the same location (assume stacked or last-wins; list shows only one).

### Security badge edit (`super-admin-06-security-badge-02-edit.jpg`)
**Purpose:** Configure the visual content, style, and placement of a security badge.
**Visible elements:**
- Breadcrumb/title: `Security Badge «Safe checkout»`.
- "GENERAL SETTINGS" section: "Badge name" text field (value "Safe checkout"); "Status" toggle labelled "Active".
- "BADGE COMBINATION" section: "Upload your own" button (top right of section); a scrollable gallery of preset badge combinations (4 visible), each a card of payment/security logos:
  1. AES-256 BIT green shield + American Express, Visa, MasterCard, Discover, "stripe SECURE PAYMENTS".
  2. SSL ENCRYPTION gold seal + "Powered by stripe" padlock + SECURED SITE + Amex, Discover, Visa, MasterCard.
  3. (selected, blue border + checkmark) "TRUSTED & SECURE CHECKOUT" + Discover, PayPal, Visa, MasterCard, American Express.
  4. "TRUSTED & SECURE CHECKOUT" + Discover, SSL gold seal + Visa, MasterCard, American Express.
  - Vertical scrollbar implies more presets below.
- "REFINE STYLE" section:
  - "Preview width" dropdown (value "500 px", top right).
  - Checkbox "Show Guaranteed Safe Checkout text" (checked).
  - Color inputs with swatch + hex field: "Guaranteed" color (#000000), "Safe" color (#088650), "Checkout" color (#000000).
  - Checkbox "Show Border" (checked); "Border color" (#000000); "Border" width numeric field (value 5, unit px).
  - Live preview pane rendering "Guaranteed SAFE Checkout" heading above the selected badge combination inside a border.
- "LOCATION" section (partially visible):
  - Row "Product page / Under buy button" with an on/off toggle (off) and a size selector strip: 200 px (selected), 250 px, 300 px, 350 px, 400 px, 450 px, 500 px.
  - (List row said "Set on Cart and Checkout" — further location rows for Cart and Checkout are implied below the fold.)
**Implied features:** custom badge image upload; per-location enable + per-location width; live preview; three-word colored heading rendering.
**Uncertainties:**
- MEDIUM: full set of placement locations (product page, cart, checkout confirmed indirectly; others such as footer unknown).
- LOW: preset gallery content is static assets (safe default).

### Checkout page settings (`super-admin-07-checkout-page.jpg`)
**Purpose:** Global checkout behaviour settings shared by all sub-stores.
**Visible elements:**
- Page title "Settings".
- Section "GOOGLE MAPS ADDRESS AUTOCOMPLETE":
  - Help text: "Google maps address autocomplete on the checkout. As your customers type their address, google suggests the correct nearby address to their location and autofills everything. Zipcode autofilling is only working in the United States as it is not 100% accurate elsewhere. This service can be activated and connected for free by just signing up to a google cloud account."
  - Blue "Activate" button.
- Section "ABANDONED CHECKOUT TIMEOUT":
  - Label: "After how many minutes of inactivity is a checkout considered abandoned (10 minutes recommended)".
  - Numeric input (value 10) + "Minutes" label.
- Blue "Save" button (bottom right).
**Implied features:** Google Cloud API key connection flow behind "Activate"; abandoned-checkout detection feeding abandoned-cart campaigns (the "Abandoned campaigns" permission module seen in employee access confirms an abandoned-campaign feature); US-only zipcode autofill logic.
**Uncertainties:**
- MEDIUM: what the "Activate" flow collects (API key entry vs OAuth) — integration design choice.
- LOW: timeout bounds/validation (assume positive integer minutes).

### Shipping zones list (`super-admin-08-shipping.jpg`)
**Purpose:** List active shipping zones with a one-line summary of their rates.
**Visible elements:**
- Page title/counter: "2 Active Shipping Zones".
- Green "Add a New Zone" button (top right).
- Zone rows (2), each with: zone name ("West", "All Countries"), summary text "Free shipping rate with 1 exception and no additional options available to customers", right chevron to open detail.
- URL: `admin/settings/shipping`.
**Implied features:** zone priority/fallback ("All Countries" acts as catch-all while "West" targets specific countries); summary auto-generated from rate + exceptions + additional options.
**Uncertainties:**
- CRITICAL: zone matching precedence when a country belongs to several zones (specific zone vs "All Countries") — affects charged shipping price.
- LOW: "Active" implies inactive zones can exist (not shown).

### Shipping zone edit / add zone (`super-admin-08-shipping-02-add-zone.jpg`)
**Purpose:** Define a shipping zone: name, base rate, covered countries/subdivisions, rule-based rate exceptions, and optional extra services.
**Visible elements:**
- Header/counter: "3 Active Shipping Zones" (list + open editor).
- Left sidebar: "New Zone" entry, existing zones "West" (selected) and "All Countries".
- Right editor panel with close (X):
  - "Zone Name (Not visible to customers)" text field (value "West").
  - "Shipping Rate" money field (value "$0").
  - "Countries" multi-select shown as chip/tag list: Australia, New Zealand, China, Hong Kong, Japan, South Korea, Macau, Taiwan, Czech Republic, Poland, Romania, Russia, Canada, United States, Denmark, Estonia, Finland, United Kingdom, Ireland, Iceland, Lithuania, Latvia, Norway, Sweden, India, Spain, Greece, Italy, Malta, Portugal, Vatican City, United Arab Emirates, Israel, Qatar, Saudi Arabia, Austria, Belgium, Switzerland, Germany, France, Luxembourg, Monaco, Netherlands; dropdown caret at the right end of the chip area.
  - "States and Provinces" table: one row per country having subdivisions, showing country name + selected/total counter and a blue link summarising selection: Australia 8/8 "All 8 states and territories selected"; New Zealand 17/17 "All 17 regions selected"; China 31/31 "All 31 provinces selected"; Japan 47/47 "All 47 prefectures selected"; South Korea 16/16 "All 16 provinces selected"; Romania 42/42 "All 42 counties selected"; Russia 83/83 "All 83 regions selected"; Canada 13/13 "All 13 provinces and territories selected"; United States 54/54 "All 54 states and territories selected"; Ireland 26/26 "All 26 counties selected"; India 36/36 "All 36 states selected"; Spain 19/19 "All 19 provinces selected"; Italy 20/20 "All 20 regions selected"; Portugal 20/20 "All 20 regions selected"; United Arab Emirates 7/7 "All 7 emirates selected".
  - "Shipping service description (visible to customers)" text field (value "Free 3-5 Weeks Shipping").
  - "RULE BASED EXCEPTIONS" section with "Add an exception" button; one exception row: "Type" dropdown (value "Weight"), "From" field (3,500.00 g), "To" field (99,999,999.99 g), "Rate" field ($0.00), trash/delete icon; below it its own "Shipping service description (visible to customers)" field (value "Free 1-2 Weeks Shipping (PROMO)").
  - "ADDITIONAL SHIPPING OPTIONS FOR CUSTOMERS" section with "Add a service" button and empty state text "You have no additional shipping options."
  - "Save" and "Cancel" buttons; red "Delete Shipping Zone" button (bottom right).
- Red handwritten annotations (spec requirements from the author):
  - "Model 1 (default)" / "Model 2" / "=> We can do different prices and in edit product page, the model can be changed for some products." (i.e. multiple shipping price models per zone, product-level model override).
  - "Add a rule (FBA inventory available)" next to the exceptions section (i.e. a new exception rule type based on FBA inventory availability).
**Implied features:** per-subdivision zone granularity (partial state selection via the blue links); weight-based (and likely price/quantity-based) rate exceptions with per-rule customer-facing description; optional paid extra services (e.g. express) offered to customers; per-zone deletion; the annotations imply a "shipping model" concept (multiple named rate models per zone, selectable per product) and an FBA-inventory-based rule type — both are requested extensions, not existing UI.
**Uncertainties:**
- CRITICAL: exception "Type" dropdown option list (Weight shown; Price? Quantity? requested: FBA inventory availability) — determines rating engine data model.
- CRITICAL: semantics of the requested "Model 1/Model 2" shipping models (how a product overrides its zone model; default fallback; interaction with exceptions) — money impact, only sketched in red annotations.
- MEDIUM: whether rate exceptions may overlap and which wins (first match? most specific?).
- MEDIUM: currency of rates — "$" shown; per-store currency conversion for sub-stores unknown.
- LOW: zone name uniqueness (assume required).

### Shipping carriers list (`super-admin-09-shipping-carrier.jpg`)
**Purpose:** Manage the catalogue of shipping carriers and their tracking-URL templates used to build tracking links on orders/emails.
**Visible elements:**
- Page title/counter: "20 Active Shipping Carriers".
- Green "Add Carrier" button (top right).
- Carrier rows, each with: carrier name, tracking URL (blue link), "Edit" button, trash/delete icon. Rows visible:
  - Fastway Australia — https://www.fastway.com.au/tools/track/
  - sypost.net — https://www.sypost.net/search?orderNo=
  - Australian Post — https://auspost.com.au/mypost/track/#/details/
  - 17track.net — https://t.17track.net/en#nums=
  - SFC — http://parcelsapp.com/en/tracking/
  - Yun Express — https://t.17track.net/fr#nums=
  - SF Express — https://www.sf-express.com/cn/en/dynamic_function/waybill/#search/bill-number/
  - EPacket — https://track.aftership.com/china-ems/
  - CJpacket — https://track.aftership.com/cjpacket/
  - Dhl Express — http://www.dhl.com.ph/en/express/tracking.html?brand=DHL&AWB=
  - Mondial Relay — https://www.mondialrelay.fr/suivi-de-colis?codePostal=69003&numeroExpedition=26695399
  - Lettre Verte — http://www.csuivi.courrier.laposte.fr/suivi/index?id=
  - Colissimo — https://www.laposte.fr/particulier/outils/suivre-vos-envois?code=
  - Canada Post — http://www.canadapost.ca/cpotools/apps/track/personal/findByTrackNumber?trackingNumber=
  - DHL Global — http://webtrack.dhlglobalmail.com/?trackingnumber=
  - DHL US — http://track.dhl-usa.com/TrackByNbr.asp?ShipmentNumber=
  - LaserShip — http://www.lasership.com/track/
  - FedEx — http://www.fedex.com/Tracking?action=track&tracknumbers=
  - UPS — http://wwwapps.ups.com/WebTracking/track?track=yes&trackNums=
  - USPS — https://tools.usps.com/go/TrackConfirmAction_input?qtc_tLabels1=
- URL: `admin/settings/carriers`.
**Implied features:** tracking number appended to (or substituted into) the URL template; carriers referenced when adding tracking numbers to orders and in shipping-confirmation emails; delete with likely referential impact on orders.
**Uncertainties:**
- MEDIUM: URL templating convention — most URLs end with `=` (append tracking number), but Mondial Relay contains a hard-coded number and postal code, suggesting either free-form URLs or a placeholder syntax not visible.
- MEDIUM: behaviour on deleting a carrier already referenced by shipped orders (block? orphan?).
- LOW: "Active" state — no visible enable/disable toggle; assume presence in list = active.

### Add shipping carrier modal (`super-admin-09-shipping-carrier-02-add.jpg`)
**Purpose:** Create a new carrier entry with its tracking URL.
**Visible elements:**
- Modal "ADD SHIPPING CARRIER" with close (X).
- "Carrier name" text field.
- "Tracking URL" text field.
- "Add another" checkbox (keep modal open to chain-create).
- "Cancel" and blue "Add" buttons.
**Implied features:** batch creation via "Add another".
**Uncertainties:**
- LOW: field validation (URL format) — safe default assumable.

### Automated emails list (`super-admin-10-automated-email.jpg`)
**Purpose:** Hub listing all transactional/automated email templates with their trigger description.
**Visible elements:**
- Page title "Setup"; URL `admin/emails`.
- Email template rows, each with green status dot, name, trigger description, right chevron:
  - Order notification — "Sent to store owner for every order when activated"
  - Order confirmation — "Sent when a customer successfully places their order"
  - Invoice — "Sent when the store admin clicks \"Email invoice\" on invoice orders page"
  - Refund — "Sent when any refund is done on an order and \"notify customer\" is selected"
  - Shipping confirmation — "Sent when a tracking number is added and or any items are set to shipped"
  - Store invitation — "Sent when you invite a new staff member to your store admin panel"
  - Gift card — "Sent when a new gift card issued"
- Red annotation (spec requirement): "Should support variables +- translation assigning them in jobs to do (handling update also)" — i.e. templates must support variables and per-language translations, with translation assignment handled via the jobs-to-do system, including updates when templates change.
**Implied features:** per-template enable toggle (green dots + "when activated" wording); fixed catalogue of 7 system triggers; refund email conditional on a "notify customer" checkbox in the refund flow; invoice email triggered manually from an invoice-orders page; requested (annotation): multilingual template variants managed through translation jobs.
**Uncertainties:**
- CRITICAL: multilingual template model requested in annotation (one template per language? fallback language? how a sub-store's language selects the variant; retranslation workflow when the source template is updated) — data model impact.
- MEDIUM: whether templates are per-organization only (super-admin) or overridable per sub-store.
- LOW: green dot = enabled (matches toggle in detail screen).

### Automated email edit — Order notification / plain text (`super-admin-10-automated-email-02.jpg`)
**Purpose:** Edit a single automated email template: sender identity, subject, format, and body with template variables.
**Visible elements:**
- Left sidebar: the 7 templates (Order notification selected).
- Detail panel with close (X):
  - Title "Order notification" + enable toggle (on); subtitle "Sent to store owner for every order when activated".
  - "From Name" field (value "Pradize").
  - "From Email" field (value contact@pradize.com) with "Edit" link.
  - "Reply-to Address" field (value support@safold.com) with "Edit" link.
  - "Subject" field (value "Order notification").
  - "What type of email to use" segmented selector: "Plain Text" (selected, checkmark) OR "HTML".
  - "Email body" area headed by link "View all available email variables".
  - Body textarea with Jinja-like template content: `Hello {{ store_name }},`, `{{ order.full_name }} has ordered from your store, {{ order.order_date|date }}:`, loop `{% for item in order.items %} {{ item.text }} {% endfor %}`, `Method of payment processing: {{ order.payment_method }}`, `Shipping address: {{ address.shipping_street }} {{ address.shipping_suite }}`, `{{ address.shipping_city }}, {{ address.shipping_state }} {{ address.shipping_zip }}` (scrollbar — more below).
  - "Send a test" button (bottom left), blue "Save" button (bottom right).
**Implied features:** template engine with variables, filters (`|date`) and loops (Jinja/Twig-style); variables reference documentation modal; test-send to verify rendering; From Email / Reply-to protected behind an "Edit" link (likely domain/sender verification).
**Uncertainties:**
- MEDIUM: what the "Edit" links on From Email / Reply-to gate (sender domain verification flow? shared org-wide sender?).
- MEDIUM: exact variable catalogue (only partially visible; "View all available email variables" implies a defined list).
- LOW: plain-text vs HTML switch keeps two bodies or converts (assume per-format body stored).

### Automated email edit — Order confirmation / HTML (`super-admin-10-automated-email-03.jpg`)
**Purpose:** Same editor shown for the customer-facing Order confirmation template in HTML mode.
**Visible elements:**
- Sidebar identical (Order confirmation selected).
- Title "Order confirmation" + toggle (on); subtitle "Sent when a customer successfully places their order".
- From Name ("Pradize"), From Email (contact@pradize.com, Edit link), Reply-to (support@safold.com, Edit link).
- Subject: "Thank you for your order".
- Type selector: "HTML" selected (checkmark), "Plain Text" unselected.
- "Email body" with "View all available email variables" link; code editor with line numbers (1-19 visible) containing: confirmation sentence with `{{ store_name }}`, `Date {{ order.order_date|date }}`, "Shipping address:" block (`address.shipping_street`, `shipping_suite`, `shipping_city`, `shipping_state`, `shipping_zip`, `shipping_country_name`, `address.phone`), "Billing address:" block (`billing_street`, `billing_suite`, `billing_city`, `billing_state`, `billing_zip`, `billing_country_name`, `address.phone`), items loop `{% for item in order.items %} {{ item.text }} {% endfor %}`.
- "Send a test" and "Save" buttons.
**Implied features:** HTML mode gets a code editor (line numbers, syntax area) vs simple textarea for plain text; same variable set across templates plus billing address variables.
**Uncertainties:**
- LOW: whether HTML editor offers preview/WYSIWYG (not visible; assume raw code + test send).

### Up-sell campaigns list (`super-admin-11-up-sell-campaigns.jpg`)
**Purpose:** List all up-sell campaigns with type, status, and performance KPIs.
**Visible elements:**
- Page title/counter: "2 Active Up-Sell Campaigns"; URL `admin/upsells`.
- Green "Create campaign" button (top right).
- Table header: "Campaign Name and Type", "Unique impressions", "Conversion rate", "Conversions".
- Rows (4), each with a type icon, name, type subtitle, status badge, KPIs, chevron:
  - "Buy 2 get 1 free" / type "Buy X items, Get X items Free or Get % off" — badge "Inactive" — 989 impressions — 6.00% — 61 conversions.
  - "5% for 2 items bought" / same type — "Inactive" — 308 — 0.00% — 0.
  - "Product recommendation" / type "Related Products Campaign" — "Active" (green) — 145140 — 0.00% — 59.
  - "Discount after purchase" / type "Storewide discount with a timer" — "Active" — 812 — 4.00% — 35.
- Red annotation (spec requirement): "Both in super admin and also admin (admin can view super admin campaign in read-only + add more campaign)" — campaigns exist at both levels; store admins see org-level campaigns read-only and can create their own store-level ones.
**Implied features:** impression/conversion tracking per campaign; active/inactive lifecycle; campaign types are fixed archetypes; two-level campaign ownership (org vs store) per annotation.
**Uncertainties:**
- CRITICAL: interaction/precedence between super-admin campaigns and store-level campaigns of the same type on the same product (stacking? exclusivity?) — money impact; the annotation defines visibility but not conflict resolution.
- MEDIUM: KPI definitions (does "conversion rate" = conversions/unique impressions? 59/145140 shown as 0.00% suggests rounding; 61/989 = 6.17% shown as 6.00% suggests different formula or rounding down).
- LOW: "2 Active" counter counts only Active rows while list shows all — safe assumption.

### Up-sell campaign type chooser (`super-admin-11-up-sell-campaigns-02-choice.jpg`)
**Purpose:** Choose the archetype for a new up-sell campaign.
**Visible elements:**
- Centered title "Select Up-Sell Campaign Type"; URL `admin/upsells/create`.
- Five selectable cards, each with an illustration icon, a name, and a stage tag:
  - "Related Products Campaign" — tag "Before checkout".
  - "Buy X Items Get X Items Free or Get % Off" — tag "Before checkout".
  - "Storewide discount with timer" — tag "After checkout".
  - "One Click Upsell Funnel" — tag "After checkout".
  - "Order bumps" — tag "Before checkout".
**Implied features:** the before/after-checkout stage classification drives where the offer renders (product/cart pages vs thank-you page).
**Uncertainties:** None.

### Up-sell — Related Products campaign form (`super-admin-11-up-sell-campaigns-03-before-checkout-product-reco.jpg`)
**Purpose:** Configure a product-recommendation ("related products") campaign shown before checkout.
**Visible elements:**
- Title "Product recommendation campaign" + tag "BEFORE CHECKOUT"; URL `admin/upsells/campaign?type=recommendation`.
- "CAMPAIGN" section header with enable toggle (on); "Campaign name" field (placeholder "Campaign title").
- "TARGETING" section: segmented tabs "All Products" (selected) / "Manually" / "Conditions".
- "MATCHING METHOD" section: three ordered levels, each row with a drag handle (⋮⋮), label, and on/off toggle (all on):
  - LEVEL 1 — "Products most commonly bought together".
  - LEVEL 2 — "Products with the same tags".
  - LEVEL 3 — "Products from the same collection".
- Blue "Submit" button.
**Implied features:** co-purchase statistics ("bought together") requiring order-history mining; fallback cascade of matching strategies with drag-to-reorder priority; manual product selection and condition-based targeting modes (tabs).
**Uncertainties:**
- MEDIUM: where recommendations render (product page? cart?) and how many items are shown — not configurable here.
- MEDIUM: "Manually" and "Conditions" tab contents for this type (not shown; Conditions tab shown on other types suggests same rule builder).
- LOW: levels evaluated in order until enough recommendations found (safe assumption).

### Up-sell — Buy X get X free / % off form (`super-admin-11-up-sell-campaigns-04-before-checkout-buy-more-get-more.jpg`)
**Purpose:** Configure a quantity-based promotion (buy X get X free, or buy X get % off) before checkout.
**Visible elements:**
- Title "Buy X items, Get X items Free or Get % off" + tag "BEFORE CHECKOUT"; URL `admin/upsells/campaign?type=x-items`.
- "CAMPAIGN" toggle (on) + "Campaign name" field.
- "TARGETING" tabs: All Products (selected) / Manually / Conditions.
- "COUNTRY TARGETING" section: "Country targeting (leave blank for all countries)" multi-select (placeholder "Select countries").
- "DISCOUNT" section:
  - Segmented selector: "Buy X items get X items for free" (selected) / "Buy X items get % off".
  - Offer row: "Buy [input] items and get [input] items free".
  - "Add Another Offer" button (full-width) — multiple offer tiers.
  - Toggle "Maximum value of free items" + "$" amount input.
  - Toggle "Allow stacking (Buy 1 get 1 will also mean buy 2 get 2)".
  - Help text: "Please note that the lower priced items will be the ones set to free in the cart".
- Blue "Submit" button.
**Implied features:** tiered offers; cap on free-item value; stacking multiplication; cheapest-items-free allocation rule; per-country offer targeting.
**Uncertainties:**
- CRITICAL: interaction between "Maximum value of free items" cap and stacking, and with multiple offer tiers matching simultaneously (which tier wins) — direct pricing impact.
- MEDIUM: "% off" variant fields (not shown — selector unselected side; presumably Buy [n] items get [p]% off, possibly on the extra items or the whole cart).
- LOW: country targeting matches shipping/billing country (assume shipping).

### Up-sell — Storewide discount with timer form (`super-admin-11-up-sell-campaigns-05-after-checkout-whole-site-discount.jpg`)
**Purpose:** Configure a post-purchase storewide discount with countdown, promoted on the thank-you page and via a top timer bar.
**Visible elements:**
- Title "Storewide discount with timer" + tag "AFTER CHECKOUT"; URL `admin/upsells/campaign?type=storewide`.
- "CAMPAIGN" toggle (on) + "Campaign name" field.
- "TARGETING" tabs: All Products (selected) / Manually / Conditions.
- "COUNTRY TARGETING": Select countries multi-select, "(leave blank for all countries)".
- "DISCOUNT" section: "Discount" amount input with unit dropdown ("$ off" — implies "% off" option); "Discount expires after (Minutes)" numeric input (placeholder "Number of minutes").
- "TIMER BAR" section:
  - Checkbox "Top timer bar".
  - Bar message template input showing: "A [$0.00] discount will be applied to your cart. Expires in [00:00:00]" (tokens rendered as pills).
  - "Bar background color" swatch + hex (#FF6666); "Bar text color" swatch + hex (#FFFFFF).
- "UP-SELL MESSAGE" section:
  - Label "Up-Sell message on Thank you page".
  - Segmented selector: "Text with button" (selected) / "Image".
  - "Call to action text" input: "A [$0.00] discount will be applied to your cart. Expires in [00:00:00]".
  - "Button text" input ("Continue Shopping").
  - "Button background color" (#FF6666) and "Button text color" (#FFFFFF) swatch+hex inputs.
  - Help text: "Please note that the button will be a link to the store home page".
- "FREQUENCY CAP" section: checkbox "Set per user frequency cap"; inputs "[n] impressions per [n] [Minutes ▾]".
- Blue "Submit" button.
**Implied features:** automatic cart-level discount application (no code entry) with expiry countdown; sitewide timer bar during the discount window; image variant of the thank-you message; per-user impression capping (requires user identification via cookie/session).
**Uncertainties:**
- CRITICAL: how the discount is applied and constrained on the *next* order (auto-applied at cart? single use? combinable with other discounts/gift cards?) — money impact.
- MEDIUM: "Image" variant fields (upload + link? not shown).
- MEDIUM: frequency-cap identity mechanism (cookie vs logged-in customer).

### Up-sell — One Click Upsell Funnel form, default (`super-admin-11-up-sell-campaigns-06-after-checkout-one-click-funnel.jpg`)
**Purpose:** Configure a post-purchase one-click upsell funnel (charge without re-entering payment) — campaign-level settings before building funnel pages.
**Visible elements:**
- Title "One Click Upsell Funnel" + tag "AFTER CHECKOUT"; URL `admin/upsells/campaign?type=one-click-upsell`.
- "CAMPAIGN" toggle (on) + "Campaign name" field.
- "TARGETING" tabs: All Products (selected) / Manually / Conditions.
- "SHIPPING" section: segmented selector "Free Shipping" (selected) / "Charge shipping with store rules".
- "COUNTRY TARGETING": Select countries multi-select, "(leave blank for all countries)".
- "FREQUENCY CAP": checkbox "Set per user frequency cap"; "[n] impressions per [n] [Minutes ▾]".
- Blue button "Continue to build funnel pages" (instead of Submit).
**Implied features:** multi-step funnel page builder (next screen after this form); one-click charge on the stored payment method of the just-completed order; shipping either free or computed via the zone rules for the upsell item.
**Uncertainties:**
- CRITICAL: one-click payment capture model (reusing the payment token of the original order; PSP support; separate order vs order amendment) — money/security impact, funnel builder not shown.
- MEDIUM: funnel structure (number of pages, accept/decline branches) — builder screen not captured.

### Up-sell — One Click Upsell Funnel form, Conditions targeting (`super-admin-11-up-sell-campaigns-06-after-checkout-one-click-funnel-02.jpg`)
**Purpose:** Same funnel form with the "Conditions" targeting tab open, revealing the rule builder.
**Visible elements:**
- Identical sections as previous screenshot (CAMPAIGN toggle + name, SHIPPING Free/Charge, COUNTRY TARGETING, FREQUENCY CAP, "Continue to build funnel pages").
- "TARGETING" with "Conditions" tab selected, showing:
  - "Product has to" dropdown (value "Product must match all conditions" — implies an "any conditions" alternative).
  - "Condition #1" rule row: field dropdown (value "Product title"), operator dropdown (placeholder "Select type"), value text input, trash icon.
  - "Add Rule" button.
**Implied features:** generic condition rule builder (field/operator/value) shared across campaign types; ALL/ANY combinator.
**Uncertainties:**
- MEDIUM: available condition fields and operators (only "Product title" visible; likely tag, collection, price, etc.).

### Up-sell — Order Bumps form (`super-admin-11-up-sell-campaigns-07-before-checkout-order-bump.jpg`)
**Purpose:** Configure order-bump offers displayed on the checkout page (add-on products in one click).
**Visible elements:**
- Title "Order Bumps" + tag "BEFORE CHECKOUT"; URL `admin/upsells/campaign?type=order-bumps`.
- "CAMPAIGN" toggle (on) + "Campaign name" field.
- "TYPE" section, radio choice with illustrated mini-previews:
  - "Multi Select" (selected): help text "Customers can click on multiple products to instantly add them to their order on checkout. For example if you are selling a camera you can offer extra batteries, a case, a strap, etc." + preview card "People Also Ordered" with two product tiles ($31.95 each) and carousel arrows.
  - "Single Select": help text "Customers can only pick one option and instantly add it to their order on checkout. For example, you can sell a warranty, and they can select 1 year, 2 years, etc." + preview card "Warranty" with two options ("1 year", "2 years") and carousel arrows.
- "CATEGORY" section: label "Heading text above the order bump section on checkout"; "Select a category" dropdown; note "Note: if multiple campaigns are using the same category then they will rotate in a split test".
- "TARGETING" tabs: All Products / Manually / "Conditions" (selected) with "Product has to" dropdown ("Product must match all conditions"), "Condition #1" row (Product title / Select type / value / trash), "Add Rule" button.
- "COUNTRY TARGETING": Select countries, "(leave blank for all countries)".
- "ORDER BUMP PRODUCTS" section: full-width "Add items" button (empty state).
- "FREQUENCY CAP": checkbox "Set per user frequency cap"; "[n] impressions per [n] [Minutes ▾]".
- Blue "Submit" button.
**Implied features:** checkout-embedded add-on widget with two interaction modes; heading text from a "category" vocabulary; automatic A/B rotation (split test) between campaigns sharing the same category; product picker for bump items.
**Uncertainties:**
- MEDIUM: what the "category" list contains (predefined headings? free text? store categories?) and how split-test results are reported.
- MEDIUM: Single Select option pricing (options like "1 year/2 years" suggest variant-level pricing of one product).
- LOW: targeting = which cart contents trigger the bump (assume cart must contain a matching product).

### Automated gift card campaigns list (`super-admin-12-automated-gift-card.jpg`)
**Purpose:** List automated gift-card campaigns (gift card issued automatically after purchase) with usage counters.
**Visible elements:**
- Page title "All Automated Gift Card Campaigns"; URL `admin/settings/gift-cards`.
- "Add filters to narrow the data you are viewing" dropdown (filter builder).
- Green "Add a campaign" button (top right).
- Bulk selection bar: master checkbox + "0 campaigns selected".
- Pagination indicator: "Gift cards campaigns: 1 - 1 of 1" with previous/next chevron buttons.
- One row: checkbox, green status dot, name "5 USD after purchase", "744 Issued gift cards", "10 Used gift cards", "1 Outstanding gift cards", right chevron.
**Implied features:** bulk actions on selected campaigns; per-campaign issuance/redemption tracking; filterable list; outstanding-liability counter.
**Uncertainties:**
- MEDIUM: meaning of "Outstanding" given 744 issued vs 10 used vs 1 outstanding (outstanding ≠ issued−used; possibly not-yet-expired unredeemed with balance, or currently-valid) — affects liability reporting.
- LOW: available filters (not expanded).

### Automated gift card campaign edit (`super-admin-12-automated-gift-card-02-edit.jpg`)
**Purpose:** Create/edit an automated gift-card campaign: value, trigger products, expiry, cap, and delivery email.
**Visible elements:**
- Page title "Add automated gift card campaign"; URL `admin/settings/gift-cards/view`.
- "Campaign name" text field.
- "Gift card value" radio group: "Set value" (selected) with "$" amount input / "Percentage of order total" / "Set % discount off order".
- "Product triggers" segmented tabs: "All Products" (selected) / "Manually Add Products" / "Products Based on Conditions".
- "Gift card expiry date" radio group: "Set number of days" (selected, numeric input value 1) / "Specific date" (date input).
- "Frequency cap" section: checkbox "Set per user frequency cap"; inputs "[n] gift cards per [n] [Minutes ▾]".
- "Delivery" section: "Email subject line" input (value "Here is your gift card code"); "Message" label with "(Paste default message)" link; message body editor containing: "Hello," / "Here is your [DISCOUNT] gift card code: [GENERATED GIFT CARD CODE]." with variable tokens rendered as pills.
- Footer buttons: "Back" (left), "Save as draft", blue "Publish" (right).
**Implied features:** draft vs published lifecycle; unique code generation; variable tokens in delivery email (amount + code); trigger scoping identical to up-sell targeting (all/manual/conditions); expiry by relative days or absolute date.
**Uncertainties:**
- CRITICAL: semantics of "Set % discount off order" as a *gift card value* option (is the issued card a %-off coupon rather than a stored-value card? redemption rules, partial redemption, currency across sub-stores) — money/data model impact.
- MEDIUM: relation between this delivery message and the "Gift card" automated email template on the emails screen (which one is actually sent; precedence/duplication).
- MEDIUM: whether "Percentage of order total" is computed pre- or post-shipping/tax and any min/max caps.
- LOW: default expiry value 1 day looks like a placeholder, not a recommended default.

### Themes library (`super-admin-13-customizable-themes-that-admin-can-choose.jpg`)
**Purpose:** Library of storefront themes the organization owns; the active theme is marked, and store admins can choose among them.
**Visible elements:**
- Page title/counter: "13 Themes in the Library"; URL `admin/themes`.
- "Sort by last edited (Newest first)" dropdown.
- "Build new theme" button; "Theme Store" button.
- Grid of theme cards (13), each with a storefront preview thumbnail, name, and last-edited caption:
  - "Achieve" — last edited 10/25/2024 at 7:27am — marked with a blue checkmark badge (currently active/selected theme; thumbnail blank/white).
  - "3rd Version" — last edited 03/16/2024 at 3:50pm.
  - "3rd Version - 2022-09-19 backup" — last edited 09/19/2022 at 9:30pm.
  - "Aero" — last edited 12/09/2021 at 2:17pm.
  - "2nd Version" — last edited 10/11/2021 at 5:01pm.
  - "First Version" — last edited 08/31/2021 at 7:26pm.
  - "New Theme 3" — last edited 05/31/2021 at 4:17pm (placeholder/skeleton thumbnail).
  - "VB Vintage" — "Visual builder theme last edited 02/18/2021 at 10:44pm".
  - "Vintage" — "Twig theme last edited 12/30/2018 at 12:44pm".
  - "Montserrat" — "Twig theme last edited 11/26/2018 at 8:29pm".
  - "Raleway" — "Twig theme last edited 11/26/2018 at 8:20pm".
  - "Blizzard" — "Twig theme last edited 11/26/2018 at 8:17pm".
  - "VB Montserrat" — "Visual builder theme last edited 11/17/2018 at 1:05pm".
- Pagination footer: "1 - 13 of 13" with previous/next chevrons.
**Implied features:** two theme technologies coexist — "Twig theme" (template-code based) and "Visual builder theme" (drag-and-drop); theme duplication/backup practice (backup-named copy); a Theme Store marketplace; "Build new theme" creation flow; one active theme (checkmark); per-theme edit (last-edited timestamps imply editors); filename says admins of sub-stores choose among these super-admin-provided themes.
**Uncertainties:**
- MEDIUM: theme activation model in multi-tenant context (checkmark marks org default? each sub-store picks its own active theme from the shared library, per the filename) — 2-3 options possible.
- MEDIUM: what "Build new theme" opens (visual builder vs Twig scaffold choice).
- LOW: card hover actions (preview/duplicate/delete) not visible — assume standard actions exist.
