# Part A — Admin: login, dashboard, menu, orders, reports

Source: screenshots of the existing admin (`pradize.commercehq.com/admin/...`, CommerceHQ platform) used as reference for the Pradize Django ecommerce engine. Red hand-written annotations on the screenshots are change requests from the owner and are inventoried explicitly. Red cross-outs mark features to EXCLUDE from the new engine.

---

### Admin Login (`admin-001-login.jpg`)
**Purpose:** Authenticate an admin user into the store back office.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/user/login`
- Centered white login card with platform logo ("HQ", blue)
- `Email` text input (placeholder "Email")
- `Password` input (placeholder "Password", masked value shown as dots)
- `Remember me` checkbox (checked, blue)
- `Forgot password?` link (right of the checkbox)
- Full-width blue `Login` button
- Plain light-grey page background, no header/footer, no registration link
**Implied features:**
- Session-based admin authentication with persistent session ("Remember me" → longer-lived cookie)
- Password-reset flow via email (Forgot password link)
- Admin user accounts distinct from storefront customers (URL namespace `/admin/user/...`)
**Uncertainties:**
- MEDIUM — Password reset flow details (email token, expiry, page sequence) are not shown; only the entry link exists.
- MEDIUM — No 2FA/MFA visible; unclear whether it exists or is required for the new engine (security impact, but a safe default of "no 2FA, add later" is plausible — kept MEDIUM).
- LOW — Lockout/rate-limiting behaviour after failed logins not visible; safe default assumable.

---

### Dashboard (`admin-002-dashboard.jpg`)
**Purpose:** At-a-glance store health: today's KPIs, trends, conversion funnel, latest orders, top products and referrers.
**Visible elements:**
- URL: `pradize.commercehq.com/admin`
- Top nav bar (dark): platform logo, `Dashboard` hamburger menu button with dropdown caret, global `Search` field, store name `Pradize`, user avatar `CB` with dropdown caret
- Greeting: "Good Morning, Cedric"
- `View Live Store >` button (top right, blue)
- Three KPI cards: `REVENUE TODAY` ($0.00), `ORDERS TODAY` (0), `VISITORS TODAY` (15)
- `TRENDS` panel with period tabs: `TDY / YTA / 1W / 1M / 3M / CUSTOM` (1M active)
  - Three sub-tabs acting as metric selectors: `Revenue $59.99` (active), `Orders 1`, `Visitors 2,359`
  - Line chart of the selected metric over the period, x-axis with dates (14 Mar … 11 Apr), y-axis $0–$60, data-point markers
- `CONVERSION` panel with the same period tabs (`TDY YTA 1W 1M 3M CUSTOM`)
  - Funnel chart with three stages and percentages: `Added to Cart 1.74%`, `Reached Checkout 1.23%`, `Purchased 0.04%`
- `LATEST ORDERS` table, columns: `ORDER #` (link, e.g. 3111), `DATE` (e.g. "04/07/2026 at 10:26pm"), `PLACED BY` (customer name, link), `TOTAL` ($ amount); 5 rows shown
- `TOP PRODUCTS` panel with period tabs (`TDY YTA 1W 1M 3M CUSTOM`, 1M active): horizontal bar list — "Chloe's Colorful Charms Multicolor Heels" with bar and count `1`
- `TOP REFERRERS` panel with period tabs: donut/pie chart with labelled segments and percentages — `com.pinterest 40.51%`, `www.google.com 21.91%`, `www.pinterest.com 16.37%`, `com.google.android... 5.53%`, `Others 15.72%`
**Implied features:**
- Visitor/session analytics tracking pipeline (visitors count, referrer attribution) built into the platform
- Funnel event tracking (add-to-cart, checkout-reached, purchase events)
- Time-zone-aware "today" KPI computation
- CUSTOM period implies a date-range picker on the dashboard widgets
- Order and customer detail pages (order # and customer names are links)
- Global admin search across entities (search bar in header)
**Uncertainties:**
- CRITICAL — Definition of "Revenue" (gross vs net of refunds/taxes/shipping) is not specified anywhere; affects all money reporting.
- MEDIUM — Whether the trends metric tabs (Revenue/Orders/Visitors) show numbers for the selected period or all-time; the displayed values suggest period totals but this is not confirmed.
- MEDIUM — Scope of the global Search (orders only? products, customers, pages?) not shown.
- LOW — Greeting logic ("Good Morning, Cedric") — simple local-time greeting assumable.

---

### Main Menu / Tools Mega-Menu (`admin-003-menu.jpg`)
**Purpose:** Global navigation dropdown giving access to every admin module, organised in four columns plus a favorites area.
**Visible elements:**
- Opened from the hamburger `Dashboard` button in the top nav
- Column 1 — `Favorites` (star icon): `Reports`, `Apps Store`, `Dashboard`, `Orders`
- Column 2 — `Manage & Measure`: `Dashboard`, then a separator; `Product List`, `Add a Product`, `Inventory`, `Transfers`, `Collections List`, `Add a Collection`, `Gift Cards for Sale`; separator; `Orders`, `Invoice Orders` (RED CROSS-OUT — to remove), `Abandoned Checkouts`; separator; `Customers`, `Reports`
- Column 3 — `Apps`: `Apps Store`, `Installed Apps`; separator; `Google Shopping Feed`, `Redirects`, `Currency Converter`, `Shipping Plus` (RED CROSS-OUT — to remove), `Reviews`, `FB Dynamic Product ...` (truncated, i.e. FB Dynamic Product Ads), `Security Badge`, `Zapier` (RED CROSS-OUT — to remove), `Lead Capture Overlay`, `Recent Purchase Noti...` (truncated, i.e. Recent Purchase Notification)
- Column 4 — `Store Setup`: `General Settings` and `Domains` (both framed in red with annotation "merge" — to be merged into one screen), `Themes`, `Payment Processing`, `Checkout Page`, `Product Page`, `Shipping`, `Shipping Carriers`, `Taxes` (RED CROSS-OUT — to remove), `Automated Email`, `Employee Accounts`, `CSV Templates` (RED CROSS-OUT — to remove), `Up-sell Campaigns`, `Pixels`, `Coupon Codes`, `Issued Gift Cards`, `Automated Gift Cards`, `Files` (RED CROSS-OUT — to remove)
- Footer of menu: `All Tools <` link (bottom left) and help text "Tip: To set menu items as favorites just drag and drop them into the favorites area"
- Dashboard content visible behind the overlay (same as admin-002)
**Implied features:**
- Full module list of the target engine: products, inventory, stock transfers, collections, gift cards (sale + issued + automated), orders, abandoned checkout recovery, customers, reports, app store/plugins, Google Shopping feed, URL redirects, multi-currency conversion, product reviews, FB dynamic ads feed, security badge widget, lead capture overlay, recent-purchase notification widget, themes, payment processing config, checkout page config, product page config, shipping config + carriers, automated (transactional/marketing) emails, employee accounts with (implied) permissions, up-sell campaigns, tracking pixels, coupon codes
- Per-user customisable favorites with drag-and-drop
- "All Tools" expanded view of the complete tool list
- Owner-requested changes: remove Invoice Orders, Shipping Plus, Zapier, Taxes, CSV Templates, Files; merge General Settings + Domains into a single settings screen
**Uncertainties:**
- CRITICAL — `Taxes` is crossed out: does the new engine really need NO tax configuration at all (prices tax-inclusive?), or is tax handled elsewhere (e.g. payment processor)? Money/compliance impact.
- CRITICAL — `Employee Accounts` implies a roles/permissions model, but no permission matrix is shown anywhere (security impact).
- MEDIUM — Crossed-out `Files` module: unclear whether file/media management is dropped entirely or absorbed into product/theme editing (images must still be uploaded somewhere).
- MEDIUM — Scope of the "merge" of General Settings + Domains — merged page content unknown beyond the two source screens.
- MEDIUM — Which "Apps" are real built-in modules to reimplement vs third-party integrations to drop (e.g. Apps Store / Installed Apps in a custom Django engine).
- LOW — Favorites drag-and-drop persistence (per-user setting) — safe default assumable.

---

### Orders List (`admin-004-orders.jpg`)
**Purpose:** Paginated list of all orders with payment/fulfillment status, bulk selection and CSV fulfillment.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/orders`
- Top nav: logo, `Orders` menu button, search bar, `Pradize`, `CB` avatar
- View selector: `All Orders ⌄` (dropdown — implies saved/filtered views)
- `CSV Fulfillment` button (top right, blue)
- Bulk-select bar: header checkbox + "0 orders selected"
- Pagination/paging controls (top right of list): `Display 20 ⌄ orders per page`, "1 - 20 of 784", previous `<` / next `>` arrows
- Order rows, each with: row checkbox, order number (e.g. 3111), date+time ("04/07/2026 at 10:26pm"), customer name (blue link; one row shows a Hebrew RTL name "ראובן זאיאנץ"), total ($59.99 … $478.94), payment-status badge, fulfillment-status badge, chevron `>` to open the order
- Payment status badges seen: `PAID` (green), `REFUNDED` (yellow), `PARTIALLY REFUNDED` (yellow)
- Fulfillment status badges seen: `SHIPPED` (dark grey), `SENT TO FULFILLMENT` (grey), `NOT SENT TO FULFILLMENT` (outlined/white), `PART. SENT TO FUL. & SHIPPED` (grey, combined partial state)
- 20 rows visible (3111 down to 3085; note gaps in numbering: 3102-3104, 3094-3096, 3098 missing from this page — numbering is not strictly contiguous on screen)
**Implied features:**
- Fulfillment pipeline with an external fulfillment provider: orders are "sent to fulfillment" then "shipped"; CSV Fulfillment button implies bulk export/import of orders to/from a fulfillment partner via CSV
- Partial fulfillment at line-item level (PART. SENT TO FUL. & SHIPPED)
- Full and partial refunds (REFUNDED / PARTIALLY REFUNDED) → refund workflow against the payment processor
- Bulk actions on selected orders (selection bar exists; actions themselves not shown)
- Order detail page (chevron / order-number link)
- Saved order views ("All Orders" dropdown)
- UTF-8/RTL customer names supported
**Uncertainties:**
- CRITICAL — What the `CSV Fulfillment` export contains and whether an import-back (tracking numbers) step exists — core order/fulfillment data-flow.
- CRITICAL — Exact order status model: payment status and fulfillment status appear to be two independent dimensions; full enumeration of states and transitions is not visible (data model impact).
- MEDIUM — Which bulk actions are available once orders are selected (only CSV fulfillment? mark shipped? refund?).
- MEDIUM — Whether "All Orders ⌄" holds predefined views (Open/Unfulfilled/…) or user-saved views (note: "Save View" is crossed out in the filter panel, see next screen).
- LOW — Default sort (appears to be order date/number descending); safe default assumable.

---

### Orders Filter Panel (`admin-004-orders-02.jpg`)
**Purpose:** Refine the orders list with multi-criteria filters.
**Visible elements:**
- Same page (`/admin/orders`), `All Orders ⌄` opens a left filter panel containing:
  - `Order status` — dropdown, placeholder "Select order status"
  - `Refund status` — dropdown, placeholder "Refund status"
  - `Last 4 card digits` — text input
  - `Date range` — dropdown, default "All time"
  - `Order number` — text input, placeholder "Enter order numbers" (plural → multiple values)
  - `Customer name` — text input, placeholder "Enter customer names" (plural)
  - `Customer email` — text input, placeholder "Enter customer emails" (plural)
  - Buttons: `Refine View` (blue), `Save View` (RED CROSS-OUT — to remove), `Reset`, `Cancel`
- RED ANNOTATION (owner requirement): Add also a "Product Name" filter — keyword(s) input; any order containing a product whose title contains the keywords is displayed
**Implied features:**
- Order search by payment card last-4 → the order record stores card last-4 (PCI-scope: only last 4)
- Multi-value filtering (several order numbers / names / emails at once)
- Distinct "order status" and "refund status" filter dimensions (matches the two badge families on the list)
- NEW requirement: filter orders by product title keywords (requires order-line → product title search)
- Saved views feature exists in the source platform but must NOT be reimplemented (crossed out)
**Uncertainties:**
- CRITICAL — Values of the `Order status` and `Refund status` dropdowns are not expanded — the canonical status enumerations are unknown (data model impact).
- MEDIUM — "Product Name" keyword filter matching semantics: all keywords AND vs any keyword OR; match against title at order time (snapshot) or current product title.
- MEDIUM — `Date range` options list not expanded here (probably same preset list as reports: Custom/Today/Yesterday/This week/Last week/This month/Last month/All time).
- LOW — Whether filters combine with AND semantics (safe default: AND).

---

### Reports Home — "Select a Report" (`admin-005-reports.jpg`)
**Purpose:** Hub page listing all available reports, grouped by SALES and TRAFFIC.
**Visible elements:**
- URL: `pradize.commercehq.com/admin/reports`
- Top nav: logo, `Reports` menu button, search, `Pradize`, `CB`
- Page title: `Select a Report`
- Section `SALES` — card grid, each card = icon + label:
  - `By Product Title`
  - `By Product Collection`
  - `By Product Variant` (RED CROSS-OUT — to remove)
  - `Orders by Month`
  - `Orders by Hour`
  - `Orders by Country`
  - `Sales by State`
  - `Orders by Customers`
  - `Orders by Traffic Source`
- Section `TRAFFIC` — card grid:
  - `By Referrer`
  - `By Device`
  - `By Location`
  - `By Landing Page`
**Implied features:**
- 12 reports to implement (13 minus crossed-out Product Variant)
- Two data domains: sales (order-based) and traffic (visitor/session-based analytics)
**Uncertainties:**
- MEDIUM — Naming inconsistency between hub cards and report pages ("Orders by Month" card opens "Sales by month"; "Orders by Hour" → "Sales by hour"; "Orders by Country" → "Sales by country"; "Orders by Customers" → "Sales by customer"; "Orders by Traffic Source" → "Sales by traffic source") — canonical names to pick.
- LOW — Card icons are decorative; safe default assumable.

---

### Report: Sales by Product Title (`admin-005-reports-01-product-title.jpg`)
**Purpose:** Sales performance per product (by title) over a selected period.
**Visible elements:**
- URL: `/admin/reports/report?type=title`; title `Sales by product title`
- Period dropdown (default `All time`) + `Export to CSV` button (top right)
- Table columns: `Product title` (blue link per product), `Type` (e.g. Shoes, Coat, Dresses, Tops, "Dresses midi", Skirts), `Orders` (count), `Gross Sales` ($)
- ~23 product rows visible (e.g. "Chloe's Colorful Charms Multicolor Heels" $59.99, "White Work Office Coat" $124.99, "Red Floral Sequined Evening Long Gown" $134.99 …)
- RED ANNOTATION (owner requirement): Add columns: `Collection display | Collection CTR | Page display | Purchase rate`
**Implied features:**
- Product "Type" attribute in the catalog data model
- Product-title links → product detail/edit page
- CSV export per report
- NEW requirement: per-product traffic metrics (collection impressions, collection click-through rate, product-page views, purchase rate) → requires impression/click tracking on collection pages joined with sales data
**Uncertainties:**
- CRITICAL — Exact definitions of the new columns (Collection display = impressions of the product inside collection listings? Page display = product page views? Purchase rate = orders/page views?) — analytics data model impact.
- MEDIUM — "Gross Sales" definition (before/after discounts, refunds, shipping) — recurring across all reports.
- MEDIUM — Aggregation key: by title string vs by product ID (renamed products merge or split?).
- LOW — Row sort order (appears unsorted/insertion order); safe default: sortable columns.

---

### Report: Period Selector Detail (`admin-005-reports-01-product-title-02.jpg`)
**Purpose:** Shows the date-period dropdown component shared by all reports.
**Visible elements:**
- Same "Sales by product title" report; the period dropdown is expanded (highlighted in red by the owner)
- Options: `Custom period`, `Today`, `Yesterday`, `This week`, `Last week`, `This month`, `Last month`, `All time`
- `Apply` (blue) and `Cancel` buttons inside the dropdown
- `Export to CSV` button next to it
- Table identical to previous screenshot (Product title / [Type hidden behind overlay] / Orders / Gross Sales)
**Implied features:**
- Shared reusable period-picker component for all reports (the owner's note on the traffic-source screen confirms: "available in all kind of reports")
- `Custom period` implies a from/to date-range picker (not shown)
**Uncertainties:**
- MEDIUM — Week start convention (Monday vs Sunday) and time zone used for period boundaries.
- LOW — Custom period picker UI (calendar) not shown; standard date-range picker assumable.

---

### Report: Sales by Collection (`admin-005-reports-02-product-collection.jpg`)
**Purpose:** Sales performance aggregated per product collection.
**Visible elements:**
- URL: `/admin/reports/report?type=collection`; title `Sales by collection`
- Period dropdown (`All time`) + `Export to CSV`
- Table columns: `Collection` (blue link, e.g. "Work Office Dresses", "Babydolls", "Bodycon Dresses", "Price $15 & Under", "Occasion: Ball" …), `Type` (Coat, Dresses, Nightwear, "Dresses midi", Shoes, Lingerie, "Two Piece Skirts", Skirts, Tops), `Orders`, `Gross Sales`
- ~25 rows visible; note `All Skirts` appears twice with different Type values (Dresses / Skirts) and `Workwear Dresses` appears twice (Coat / Two Piece Skirts)
- RED ANNOTATION (owner requirement): Add columns: `Display | Avg % scroll | % CTR click at least 1 product | % Purchase at least 1 product`
**Implied features:**
- Collections have a Type attribute; a product's sales can count into multiple collections
- NEW requirement: collection-page engagement analytics — page displays, average scroll depth %, CTR (sessions clicking ≥1 product), conversion (sessions purchasing ≥1 product) → requires front-end scroll/click event tracking per collection page
**Uncertainties:**
- CRITICAL — Attribution when a product belongs to several collections (orders/gross sales double-counted per collection?) — reporting data model impact.
- CRITICAL — Precise definitions/denominators of the new metrics (Avg % scroll per session? % CTR = sessions with ≥1 product click / collection page views?) — analytics model impact.
- MEDIUM — Duplicate collection names with different types ("All Skirts" ×2): aggregation by collection ID, not name, apparently — to confirm.
- LOW — Sorting; safe default sortable columns.

---

### Report: Sales by Month (`admin-005-reports-03-sales-by-month.jpg`)
**Purpose:** Monthly sales totals over the store's lifetime.
**Visible elements:**
- URL: `/admin/reports/report?type=month`; title `Sales by month`
- Period dropdown (`All time`) + `Export to CSV`
- Table columns: `Month` ("April 2021" … "October 2022" visible), `Orders`, `Gross Sales`
- Rows currently sorted oldest-first
- RED ANNOTATION (owner requirement): "Most recent month should be displayed first" (descending sort)
**Implied features:**
- Month bucketing of orders; combined with period filter (months within the selected range)
**Uncertainties:**
- MEDIUM — Time zone used for month boundaries (store TZ vs UTC vs customer TZ — see the by-hour report annotation which asks for customer-country time).
- LOW — Whether months with zero orders appear as $0 rows; safe default assumable (show only months with data, or all months in range).

---

### Report: Sales by Hour (`admin-005-reports-04-orders-by-hours.jpg`)
**Purpose:** Distribution of orders and gross sales across the 24 hours of the day.
**Visible elements:**
- URL: `/admin/reports/report?type=hour`; title `Sales by hour`
- Period dropdown (`All time`) + `Export to CSV`
- Table columns: `Hour` (12 AM, 01 AM … 11 PM — 24 rows), `Orders`, `Gross Sales`
- `TOTALS` footer row: 626 orders, $45,435.95
- RED ANNOTATION (owner requirement): "Make sure to record the hour of the customer depending on its delivery country" — bucket orders by the customer's local hour (derived from delivery country), not server time
**Implied features:**
- Totals row pattern for reports
- NEW requirement: store or derive customer-local timestamp per order (delivery-country → time zone mapping)
**Uncertainties:**
- CRITICAL — Countries span multiple time zones (US, Canada, Australia…): delivery country alone cannot give one local hour — need state/zip-based TZ or an accepted approximation (data model impact on order timestamps).
- MEDIUM — Whether existing orders must be retro-converted or only new orders recorded with local hour.
- LOW — 12-hour AM/PM labels; safe default assumable.

---

### Report: Sales by Country (`admin-005-reports-05-orders-by-country.jpg`)
**Purpose:** Orders and gross sales per delivery country.
**Visible elements:**
- URL: `/admin/reports/report?type=country`; title `Sales by country`
- Period dropdown (`All time`) + `Export to CSV`
- Table columns: `Country` (United Arab Emirates, Austria, Australia, Belgium, Brazil, Canada, Switzerland, Colombia, Czech Republic, Germany, Estonia, Spain, Finland, France, United Kingdom, Georgia, Greece, Guam, Croatia, Ireland, Israel — visible portion), `Orders`, `Gross sales`
- Rows sorted by country ISO code (alphabetical by code: AE, AT, AU, BE, BR, CA, CH, CO, CZ, DE, EE, ES, FI, FR, GB, GE, GR, GU, HR, IE, IL), not by sales
**Implied features:**
- Country taken from delivery address; ISO-country reference data
**Uncertainties:**
- MEDIUM — Preferred sort: by the "sorted by high first" rule the owner wrote on the traffic-source report (may generalise to all reports) vs current code order — 2 options.
- LOW — Country display names/localisation; safe default assumable.

---

### Report: Sales by State (`admin-005-reports-06-orders-by-state.jpg`)
**Purpose:** Sales per state/province of the delivery address.
**Visible elements:**
- URL: `/admin/reports/report?type=state`; title `Sales by state`
- Period dropdown (`All time`) + `Export to CSV`
- Table columns: `State`, `Net sales` (NOTE: this report says "Net sales" while all others say "Gross Sales")
- Rows are a raw mix of free-text state values from all countries: Saitama, Wilayah Persekutuan Kuala Lumpur, Lombardia, Daegu-gwangyeoksi, Gyeonggi-do, Okinawa, Alberta, Acquitaine [sic], Adjara, Alabama, `ALL` ($77.77), Antwerp, Antwerpen, Arkansas …
- Data-quality issues visible: `$0.00` rows (Saitama, Antwerp, Arkansas), duplicate spellings (Antwerp/Antwerpen), garbage value `ALL`, misspelled "Acquitaine"
- RED ANNOTATION (owner requirement): "For country that have state / province (US, China, russia, india...). Otherwise you can just display for instance France for small countries."
**Implied features:**
- NEW requirement: hierarchical/grouped report — state-level breakdown only for large multi-state countries, country-level rows for the rest; implies a normalised state/region reference list instead of free-text states
**Uncertainties:**
- CRITICAL — `Net sales` vs `Gross Sales` inconsistency: is this report intentionally net (of refunds/discounts?) or a labelling accident? Money definition impact.
- CRITICAL — The list of countries that get state-level breakdown is open-ended ("US, China, russia, india...") — exact list must be decided (data model/reference data).
- MEDIUM — State normalisation strategy for existing free-text values (Antwerp vs Antwerpen, $0.00 rows, "ALL") — cleanup vs display-as-is.
- MEDIUM — Whether the state report should also show an `Orders` column (all other reports have one; this one only shows Net sales — possibly cropped).
**Note:** the screenshot is cropped on the right; an `Orders` column may exist off-screen.

---

### Report: Sales by Customer (`admin-005-reports-07-orders-by-customer.jpg`)
**Purpose:** Sales aggregated per customer.
**Visible elements:**
- URL: `/admin/reports/report?type=customer`; title `Sales by customer`
- Period dropdown (`All time`) + `Export to CSV`
- Table columns: `Customer name` (blue link; names blurred in screenshot for privacy), `Customer email` (partially blurred; domains visible: gmail.com, hotmail.com, verizon.net, yahoo.co.uk, aol.com, tpg.com.au, yahoo.com, sig.biz, iastate.edu…), `Orders` (mostly 1, one row 2), `Gross Sales` ($19.90 … $194.68)
**Implied features:**
- Customer entity aggregation (link → customer detail page)
- Contains PII (names + emails) → export and access control implications
**Uncertainties:**
- MEDIUM — Customer identity key: account vs email (guest checkouts merged by email?).
- MEDIUM — Sort order (values look unsorted); the "high first" rule may apply.
- LOW — Blurring is manual screenshot redaction, not a product feature; safe to assume plain display in-app.

---

### Report: Sales by Traffic Source (`admin-005-reports-08-orders-by-traffic.jpg`)
**Purpose:** Orders and gross sales attributed to the referring URL/source of the purchase session.
**Visible elements:**
- Title `Sales by traffic source`; period dropdown expanded (highlighted red): `Custom period, Today, Yesterday, This week, Last week, This month, Last month, All time` + `Apply`/`Cancel` + `Export to CSV`
- RED ANNOTATION: "Notice this, available in all kind of reports" (the period selector is universal)
- Table columns: `Referring URL`, `Orders`, `Gross Sales`
- Rows: `Direct` (265 orders, $19,535.25), `429047995` (numeric source id, 2, $119.98), `ch.pinterest.com`, `com.google.android.googlequicksearchbox`, `com.pinterest` (36, $3,137.18), `cz.pinterest.com`, `duckduckgo.com`, `in.pinterest.com`, `instagram.com`, `l.facebook.com`, `l.instagram.com`, `lens.google.com`, `pinterest.com`, `webmail.tim.it`, `www.bing.com`, `www.ecosia.org`, `www.google.co.kr`, `www.google.co.uk`, `www.google.com` (155, $9,934.53)
- Rows currently sorted alphabetically; RED ANNOTATION (owner requirement): "Should be sorted by high first" (descending by orders/sales)
**Implied features:**
- Session→order attribution by referrer (with `Direct` bucket and app-style sources like Android package names)
- Numeric row `429047995` suggests campaign/ad IDs also land as sources
**Uncertainties:**
- CRITICAL — Attribution model (first-touch vs last-touch, attribution window/cookie lifetime) is undefined — determines the whole traffic→order data model.
- MEDIUM — Sort key for "high first": Orders or Gross Sales.
- MEDIUM — Referrer normalisation (com.pinterest vs pinterest.com vs regional subdomains counted separately today — group or keep raw?).

---

### Report: Traffic by Referrer (`admin-005-reports-09-by-referrer.jpg`)
**Purpose:** Unique visitors per referring URL (traffic domain, not sales).
**Visible elements:**
- URL: `/admin/reports/report?type=referrer`; title `Traffic by referrer`
- Period dropdown (`All time`) + `Export to CSV`
- Table columns: `Referring URL` (blue links), `Unique Visitors`
- Rows sorted descending: com.pinterest 156,400; www.pinterest.com 123,454; www.google.com 42,516; pinterest.com 15,133; com.google.android.googlequicksearchbox 3,588; www.pinterest.co.uk 2,903; www.pinterest.de 2,823; in.pinterest.com 2,761; www.bing.com 2,756; www.pinterest.fr 2,673; www.pinterest.jp 2,626; 429047995 2,626; www.pinterest.ca 2,450; duckduckgo.com 2,266; www.pinterest.com.au 1,912; lens.google.com 1,909; yandex.ru 1,761; www.pinterest.es 1,594
**Implied features:**
- Unique-visitor deduplication (cookie/fingerprint) per referrer
- This report is already "high first" sorted — consistent with owner's requested default
**Uncertainties:**
- MEDIUM — "Unique visitor" definition window (unique per period vs per day summed) — 2-3 options.
- LOW — Whether referring URLs link to anything when clicked (they are styled as links); safe default: external link or drill-down.

---

### Report: Traffic by Device (`admin-005-reports-10-by-device.jpg`)
**Purpose:** Unique visitors per device/operating system.
**Visible elements:**
- URL: `/admin/reports/report?type=device`; title `Traffic by device`
- Period dropdown (`All time`) + `Export to CSV`
- Table columns: `Device`, `Unique Visitors`
- Rows (unsorted): KaiOS 16, Android 257,309, Tizen 12, Windows 76,480, Other 1,358, iOS 182,550, Mac OS X 39,645, Ubuntu 460, Linux 11,877, Fedora 35, FreeBSD 2, Windows Phone 3, Chromecast 1, Chrome OS 2,576
- `TOTALS` footer row: 572,324
**Implied features:**
- User-agent parsing to OS families, with `Other` fallback; totals row
**Uncertainties:**
- MEDIUM — "Device" actually means OS here; decide whether new engine reports OS, device class (mobile/desktop/tablet), or both.
- LOW — Sorting (owner's "high first" rule presumably applies); safe default.

---

### Report: Traffic by Location (`admin-005-reports-11-by-location.jpg`)
**Purpose:** Unique visitors per country (geo-IP).
**Visible elements:**
- URL: `/admin/reports/report?type=trafficlocation`; title `Traffic by location`
- Period dropdown (`All time`) + `Export to CSV`
- Table columns: `Location`, `Unique Visitors`
- Rows (unsorted): Papua New Guinea 17, Mayotte 3, Bhutan 28, Kyrgyzstan 349, Poland 3,587, Northern Mariana Islands 11, Liechtenstein 10, Sint Maarten 20, Guinea-Bissau 7, Lebanon 272, Norway 2,278, Cameroon 174, Bahrain 160, South Sudan 3, British Virgin Islands 18, Madagascar 157, Tajikistan 37, France 16,118, Mali 12, Czech Republic 3,500
**Implied features:**
- Geo-IP resolution of visitors to country (distinct from delivery-country used in sales reports)
**Uncertainties:**
- MEDIUM — Geo granularity: country only, or drill-down to region/city (only country visible).
- LOW — Sorting ("high first" rule presumably applies); safe default.

---

### Report: Traffic by Landing Page (`admin-005-reports-12-by-landing-page.jpg`)
**Purpose:** Unique visitors per session landing page.
**Visible elements:**
- URL: `/admin/reports/report?type=landing`; title `Traffic by landing page`
- Period dropdown (`All time`) + `Export to CSV`
- Table columns: `Landing Page` (blue links, path form), `Unique Visitors`
- Rows: `/cart` 624, `/` 19,541, `/faq` 13, `/dresses` 1, `/contact` 10, `/404` 7,813, `/search` 6, `/sets-collection` 1, `/all-dresses` 12, `/collection/sale` 86, `/shipping-policy` 5, `/search/purple` 1, `/search/micokini` 1, `/search/club` 2, `/search/princess` 1, `/collections/` 15, `/search/heels` 3, `/search/sparkle` 11, `/tops-collection` 3
- Notable data point: `/404` as landing page for 7,813 visitors (storefront has a 404 page tracked as landing)
- RED ANNOTATION (owner requirement): "Add columns sorting + add columns: `Avg page | % Product CTR | % Purchase rate`"
**Implied features:**
- Session landing-page tracking; storefront route inventory visible (cart, faq, contact, search with keyword paths `/search/<term>`, collections, shipping policy, 404)
- NEW requirements: sortable columns on this report (and likely all reports), plus per-landing-page engagement metrics: average pages per session ("Avg page"), % of sessions clicking a product, % of sessions purchasing
**Uncertainties:**
- CRITICAL — Definitions of the new columns ("Avg page" = average pages viewed per session starting on this landing page? "% Product CTR" and "% Purchase rate" denominators?) — analytics data model impact.
- MEDIUM — "Add columns sorting": this report only, or a global requirement for every report table (the annotation wording suggests generalisation).
- LOW — Path normalisation (trailing slash, query strings); safe default assumable.
