# 06 — Settings Requirements

> Complete settings inventory for the Pradize ecommerce engine.
> Every configurable setting is documented here. For each group the admin site is identified.
> Settings that must be configured before the storefront can go live are marked **BLOCKS LAUNCH**.

**Status: WRITTEN 2026-07-02 by Spec Agent — first full pass.**

Confirmed decisions applied:
- Zero tax config engine-side (prices always tax-inclusive).
- Day-1 payment processors: Stripe + PayPal only.
- Product video: embed URL only (YouTube/Vimeo).
- Admin: two Django admin sites — `/admin/` (per-store) and `/superadmin/` (cross-org).
- OpenAI API: opt-in super-admin toggle only.
- Excluded from this file: Taxes module, Invoice Orders, Shipping Plus, Zapier, CSV Templates, Files section.

---

## Count summary

| # | Group | Site | Settings count |
|---|-------|------|---------------|
| 1 | Store information | /admin/ | 7 |
| 2 | Password protect store | /admin/ | 2 |
| 3 | Standards and format presets | /admin/ | 3 |
| 4 | Page code injection | /admin/ | 2 |
| 5 | Robots.txt | /admin/ | 1 |
| 6 | Root-directory file upload | /admin/ | 1 |
| 7 | Product page feature toggles | /admin/ | 8 |
| 8 | Currency converter | /admin/ | 4 per currency |
| 9 | Pixels | /admin/ | 5 platforms × ~3 |
| 10 | Catalog feeds | /admin/ | 3 feeds × ~3 |
| 11 | Review settings | /admin/ | 3 |
| 12 | Recent purchase notification | /admin/ | 4 |
| 13 | Lead capture overlay — global | /admin/ | 1 |
| 14 | Abandoned checkout global | /superadmin/ | 1 |
| 15 | Checkout globals | /superadmin/ | 2 |
| 16 | Security badge | /superadmin/ | 11 per badge |
| 17 | Shipping zones | /superadmin/ | 8 per zone |
| 18 | Shipping carriers | /superadmin/ | 2 per carrier |
| 19 | Automated email templates | /superadmin/ | 7 per template |
| 20 | Theme library | /superadmin/ | 2 |
| 21 | AI runner configuration | /superadmin/ | 8 |
| 22 | Amazon FBA integration | /superadmin/ | 3 — PENDING |
| 23 | Organization settings | /superadmin/ (Control Plane) | 11 per org |
| 24 | Processor account settings | /superadmin/ (Control Plane) | 10 per account |
| 25 | Payment method settings | /superadmin/ (Control Plane) | 6 per method |
| 26 | Organization rules — geography | /superadmin/ (Control Plane) | 5 per rule |
| 27 | Organization rules — allocation | /superadmin/ (Control Plane) | 3 per rule |
| 28 | Control Plane — global | /superadmin/ (Control Plane) | 1 |
| 29 | Launch readiness checklist | cross-cutting | — |

**Total individual setting fields: approximately 155 (excluding per-currency and per-platform repetitions).**

---

## Store Admin (/admin/)

---

### 1 — Store Information — /admin/

Source: `admin-016-general-settings.jpg`

- **Store name**
  - Type: text
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes
  - Notes: displayed in automated emails, storefront page titles, and admin chrome.

- **Store contact email address**
  - Type: email
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes
  - Notes: the screen shows a prefix input + fixed `@<domain>.com` suffix. PENDING — email domain handling: is the suffix always the store domain? SPF/DKIM setup path is unspecified. See uncertainty `Settings-C1` below.

- **Store primary domain**
  - Type: text (subdomain rename flow)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes
  - Notes: a "Change subdomain" button initiates the rename flow. The fully qualified domain (custom domain vs platform subdomain) is managed separately in the Domains module; this field sets the platform subdomain.

- **Store logo**
  - Type: file (image upload)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: displayed in admin chrome and storefront header. Not visible as a separate field in `admin-016` but implied by the top-bar logo rendering; confirmed by theme library screenshots.

- **Timezone**
  - Type: select (IANA timezone list; e.g. CET)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: edit requires clicking an "Edit" link before the dropdown is enabled. Used for report date bucketing, abandoned-checkout timer, and promo scheduling.

- **Unit system**
  - Type: select (Metric / Imperial)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: influences default weight unit choice and product form labels.

- **Default weight unit**
  - Type: select (g / kg / lb / oz)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: determines shipping-weight field unit on the product form and affects shipping rate calculations.

---

### 2 — Password Protect Store — /admin/

Source: `admin-016-general-settings.jpg`

- **Password protect entire store (enable toggle)**
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: when enabled, the storefront is accessible only with a password. Useful for staging/pre-launch.

- **Storefront access password**
  - Type: text (password)
  - Required for launch? conditional — required only when the protect toggle is on
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: field appears when the toggle is on (assumption — field not shown in screenshot because toggle is off). Must not be persisted in plaintext.

---

### 3 — Standards and Format Presets — /admin/

Source: `admin-016-general-settings.jpg`

- **Unit system** (see group 1 — Store Information; duplicated in the Standards section of the same screen)
- **Default weight unit** (see group 1)
- **Timezone** (see group 1)

These three fields are listed under "STANDARDS AND FORMAT PRESETS" in the screenshot and cross-referenced in group 1 above. They are a single settings block, not duplicates — grouped here for completeness.

---

### 4 — Page Code Injection — /admin/

Source: `admin-016-general-settings.jpg`

- **Header include code**
  - Type: textarea (arbitrary HTML / JS / CSS)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: injected into the `<head>` of every storefront page. Typical use: verification meta tags, analytics loader, custom fonts.

- **Body include code**
  - Type: textarea (arbitrary HTML / JS / CSS)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: injected just before `</body>` on every storefront page. Example value: `<meta name="p:domain_verify" content="..."/>`. Pixels and conversion scripts installed here or via the dedicated Pixels module.

---

### 5 — Robots.txt Append Section — /admin/

Source: `admin-016-general-settings.jpg`

- **Robots.txt include text**
  - Type: textarea (plain text, robots.txt directives)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: merged after the platform-generated base `robots.txt`. Used for custom `Disallow` or `Sitemap` directives. An invalid entry here could de-index the entire storefront — no validation shown.

---

### 6 — Root-Directory File Upload — /admin/

Source: `admin-016-general-settings.jpg`

- **Root-directory static files**
  - Type: file upload (drag-and-drop zone; supported formats: txt, xml, html, json)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: only for files that must exist at the domain root (e.g. Google site-verification `googleXXX.html`, `ads.txt`). General media files use the Files section (excluded from this spec).

---

### 7 — Product Page Feature Toggles — /admin/

Source: `admin-017-product-pages.jpg`

Each toggle controls whether the corresponding field appears on the product edit form and on the storefront product page. Disabling a toggle hides the field; existing data is preserved, not deleted (design decision — PENDING confirmation on data-model impact).

- **Type toggle** (show product type field)
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: product type is used by rule-based collections. Disabling hides the field and disables type-based collection rules.

- **Description Tabs toggle** (enable multi-tab description)
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: when enabled, a tab chip editor appears. The default tab "Description" always exists. Additional tabs (e.g. "Shipping", "Care") are added via a "+" button and reordered by drag handle. Tab names defined here are global; content is filled per product.

- **Description tab list** (ordered list of named tabs)
  - Type: ordered list of text labels (drag-to-reorder)
  - Required for launch? conditional — required when Description Tabs toggle is on
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: at least the "Description" tab must always be present. PENDING: whether tab names are translatable per locale.

- **Compare at price toggle**
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: shows a "Compare at" field on each product/variant to display an original price crossed out on the storefront.

- **Shipping weight toggle**
  - Type: toggle (on/off)
  - Required for launch? conditional — BLOCKS LAUNCH if weight-based shipping rates are configured
  - Connector? no
  - Visible in launch-readiness checklist? yes (when weight-based shipping zones exist)
  - Notes: must be on for weight-based rate exceptions in shipping zones to function.

- **Vendor toggle**
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: "Vendor" = product sourcing origin. Useful for display and filtering; not tied to fulfillment logic.

- **SKU toggle**
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: SKU is required for CSV fulfillment exports and Amazon FBA matching if those features are in use.

- **Tags toggle**
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: tags drive tag-based collection rules and storefront search. Disabling hides the field; existing tags persist on products.

---

### 8 — Currency Converter — /admin/

Source: part B screen inventory (currencies module — `admin-XXX-currencies`); extra-spec references "Currencies (super-admin)" but per the two-admin-sites decision currencies display at `/admin/`.

The currency converter has ~~one global enable toggle~~ **DECIDED (human, 2026-07-11, ADR-023 §3 addendum)**: there is no single global on/off switch — enablement is **per-currency** (`StoreCurrencySetting.is_enabled`, one row per currency); a store with zero enabled rows behaves as "off" (the picker does not render). This section's placement of the whole screen at `/admin/` is also superseded for the master-data half — see ADR-023 §3 (`CurrencyDefinition`/`CurrencyRate`/`CurrencyConverterSettings` live at `/superadmin/`; only the per-store enable/disable checklist stays at `/admin/`) — and a **per-currency configuration** block for every additional currency.

- **Default store currency**
  - Type: select (ISO 4217 currency code, e.g. USD)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes
  - Notes: also appears on the Payment Processing screen. Changing the default currency on existing product prices has PENDING semantics (does a currency change reprice all products?).

- **Additional enabled currencies** (per-currency block, repeat for each)

  - **Currency code**
    - Type: select (ISO 4217)
    - Required for launch? no
    - Connector? no
    - Visible in launch-readiness checklist? no

  - **Currency symbol**
    - Type: text (e.g. €, £, ¥)
    - Required for launch? no
    - Connector? no
    - Visible in launch-readiness checklist? no

  - **Symbol placement**
    - Type: select (prefix / suffix)
    - Required for launch? no
    - Connector? no
    - Visible in launch-readiness checklist? no
    - Notes: PENDING — rate source and update frequency for conversion (real-time API vs manual rate entry) are not shown in the screen inventory.

  - **Code visibility** — **DECIDED (human, 2026-07-11, ADR-023 §2 addendum)**: restored per `screen_inventory_parts/part_B_admin_006-012.md` admin-011 ("Code visibility" segmented Yes/No + Prefix/Suffix control), dropped between the screenshot spec and the original ADR-023 draft.
    - Type: toggle (Yes/No) + select (prefix / suffix), independent of Symbol placement above
    - Required for launch? no
    - Connector? no
    - Visible in launch-readiness checklist? no
    - Notes: e.g. Canadian Dollar with symbol prefix + code suffix renders `"$45.99 CAD"` (`CurrencyDefinition.show_code` / `code_placement`).

---

### 9 — Pixels — /admin/

Source: `admin-018-pixels.jpg`

Five platforms are shown. Each has an install/uninstall flow via a "..." menu when installed. Day-1 scope: all five platforms are supported as they appear in the screenshot.

**Per platform (repeat for each of the 5):**

- **Facebook Standard Pixel**
  - *Pixel ID* — Type: text; Required for launch? no; Connector? yes — Facebook; Visible in checklist? no
  - *Enabled events* — Type: multi-select or auto (ViewContent, AddToCart, InitiateCheckout, Purchase, and values/currency) — PENDING: exact event list and value schema per provider.
  - Notes: the Install/uninstall flow is behind the "..." menu when installed. No multi-pixel-per-provider support (one ID per platform). GDPR consent integration: out of scope for this version.

- **Google Analytics Enhanced Ecommerce**
  - *Tracking ID (GA4 measurement ID)* — Type: text (e.g. `G-XXXXXXXX` or legacy `UA-XXXXXXXX`); Required for launch? no; Connector? yes — Google Analytics; Visible in checklist? no
  - Notes: "Enhanced Ecommerce" events include purchase, add-to-cart, begin-checkout. PENDING: confirmation of GA4 vs Universal Analytics expected.

- **Snapchat Pixel**
  - *Pixel ID* — Type: text; Required for launch? no; Connector? yes — Snapchat; Visible in checklist? no

- **Pinterest Universal Tag**
  - *Tag ID* — Type: text; Required for launch? no; Connector? yes — Pinterest; Visible in checklist? no

- **TikTok Pixel**
  - *Pixel ID* — Type: text; Required for launch? no; Connector? yes — TikTok; Visible in checklist? no

CRITICAL uncertainty on all pixels: which ecommerce events are fired per provider (view content, add-to-cart, purchase, value, currency, dedup on thank-you reload) is not specified. See `11_uncertainties_to_validate.md` Part C section.

---

### 10 — Catalog Feeds — /admin/

Source: `admin-013-shipping-feeds.jpg` + owner annotation requesting Google Shopping and Pinterest feeds.

**Facebook Dynamic Product Ads (DPA) feed**

- **Products to sync scope**
  - Type: select (All products from all collections / Specific collection — PENDING: exact options list not confirmed from screenshot)
  - Required for launch? no
  - Connector? yes — Facebook DPA
  - Visible in launch-readiness checklist? no
  - Notes: PENDING — feed field mapping (price, availability, image, GTIN…) is implicit and must be confirmed.

- **Feed URL** (read-only, auto-generated)
  - Type: URL (read-only + copy button)
  - Required for launch? no
  - Connector? yes — Facebook DPA
  - Visible in launch-readiness checklist? no
  - Notes: public unauthenticated endpoint. Feed refresh trigger (cron / on product change / manual) is PENDING.

**Google Shopping feed** — IN_SPEC (requested via owner annotation)

- **Google Shopping feed URL** (read-only, auto-generated)
  - Type: URL (read-only + copy button)
  - Required for launch? no
  - Connector? yes — Google Shopping
  - Visible in launch-readiness checklist? no
  - Notes: field mapping (title, description, price, availability, GTIN/MPN) must be confirmed. PENDING.

**Pinterest Shopping feed** — IN_SPEC (requested via owner annotation)

- **Pinterest feed URL** (read-only, auto-generated)
  - Type: URL (read-only + copy button)
  - Required for launch? no
  - Connector? yes — Pinterest Shopping
  - Visible in launch-readiness checklist? no
  - Notes: PENDING — Pinterest Catalog format (RSS/XML schema) must be confirmed.

---

### 11 — Review Settings — /admin/

Source: part B screen inventory (reviews module).

- **Review request delay after shipment**
  - Type: integer (days)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: number of days after a shipment confirmation is sent before the automated review-request email fires.

- **Verified-buyer badge type**
  - Type: select (badge style options — e.g. "Verified Buyer" text badge, icon+text, etc.)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: appears on storefront review display when the reviewer's order email matches.

- **Customer-only display threshold** — IN_SPEC (from owner annotation in extra-spec)
  - Type: integer (minimum number of reviews required before reviews section is publicly visible)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: PENDING — confirms that reviews remain hidden until this count is reached. Intended to prevent empty states on new products.

---

### 12 — Recent Purchase Notification — /admin/

Source: `admin-014-recent-purchase-notification.jpg`

- **Enable recent purchase notifications (global toggle)** — IN_SPEC (requested via owner annotation)
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: the screenshot lacks this toggle; owner annotation requests it. When off, no notification widget appears on the storefront.

- **Rolling window for purchase data (hours)**
  - Type: integer (hours; screenshot value: 1)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: only purchases within this window are shown.

- **Delay before first notification (seconds)**
  - Type: integer (seconds; screenshot value: 2)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Interval between notifications (seconds)**
  - Type: integer (seconds; screenshot value: 2)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: CRITICAL uncertainty: what buyer data is shown publicly (name, city?) — GDPR impact. See `11_uncertainties_to_validate.md` Part C section.

---

### 13 — Lead Capture Overlay — Global — /admin/

Source: `admin-015-lead-capture-overlay.jpg`

Per-campaign settings (trigger type, themes, post-signup action, frequency cap, customer tags) are configured at campaign level (covered in admin flows spec). Only the global setting lives here.

- **Lead capture overlay — global enable toggle**
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: PENDING — the screenshot does not show a global toggle; all campaigns can be individually inactive. If a global toggle exists, it disables all overlay campaigns at once regardless of per-campaign status. To be confirmed.

---

## Super-Admin (/superadmin/)

---

### 14 — Abandoned Checkout — Global — /superadmin/

Source: `super-admin-07-checkout-page.jpg`

- **Abandoned checkout session timeout**
  - Type: integer (minutes; recommended value: 10)
  - Required for launch? yes — BLOCKS LAUNCH (feeds abandoned-checkout campaign trigger; without a value the system cannot detect abandonment)
  - Connector? no
  - Visible in launch-readiness checklist? yes
  - Notes: also listed under Checkout Globals (group 15) — same field, one location. After this many minutes of inactivity on an active checkout session, the checkout is marked abandoned and eligible for campaign follow-up. PENDING: cart session persistence duration (see `11_uncertainties_to_validate.md` UF-E).

---

### 15 — Checkout Globals — /superadmin/

Source: `super-admin-07-checkout-page.jpg`

- **Google Maps address autocomplete — enabled toggle**
  - Type: toggle or activate-flow button (on/off via "Activate")
  - Required for launch? no
  - Connector? yes — Google Maps Platform
  - Visible in launch-readiness checklist? no
  - Notes: when activated, as customers type their shipping address, Google suggests and autofills the complete address. Zipcode autofill works in the US only. Activation flow collects a Google Cloud API key (PENDING: OAuth vs API key entry).

- **Google Maps API key**
  - Type: text (API key)
  - Required for launch? conditional — required only when the Google Maps autocomplete toggle is on
  - Connector? yes — Google Maps Platform
  - Visible in launch-readiness checklist? no
  - Notes: stored securely; not shown in plaintext after entry.

(The abandoned-checkout timeout from group 14 appears on this same screen but is listed separately for checklist clarity.)

---

### 16 — Security Badge — /superadmin/

Source: `super-admin-06-security-badge.jpg` and `super-admin-06-security-badge-02-edit.jpg`

Multiple badges can exist. Per-badge configuration:

- **Badge name**
  - Type: text (internal label, not shown to customers)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Status (active toggle)**
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Badge combination — preset selection**
  - Type: single-select from scrollable gallery of presets (4+ presets visible; images of payment logo combinations)
  - Required for launch? conditional — required if custom upload is not provided
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Badge combination — custom upload**
  - Type: file (image upload)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: replaces the preset. One of preset or custom must be selected.

- **Show "Guaranteed Safe Checkout" text (toggle)**
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

- **"Guaranteed" word color**
  - Type: color (hex + swatch picker; default #000000)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

- **"Safe" word color**
  - Type: color (hex + swatch picker; default #088650)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

- **"Checkout" word color**
  - Type: color (hex + swatch picker; default #000000)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Show border (toggle) + border color + border width**
  - Type: toggle + color (hex) + integer (px)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Location: Product page / Under buy button — enabled + width**
  - Type: toggle (on/off) + select (width in px: 200/250/300/350/400/450/500)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Location: Cart page — enabled + width**
  - Type: toggle (on/off) + select (width in px)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: confirmed from badge list summary "Set on Cart and Checkout"; dedicated row implied but below the fold.

- **Location: Checkout page — enabled + width**
  - Type: toggle (on/off) + select (width in px)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

---

### 17 — Shipping Zones — /superadmin/

Source: `super-admin-08-shipping.jpg` and `super-admin-08-shipping-02-add-zone.jpg`

At least one active shipping zone is required before launch. Per-zone configuration:

- **Zone name** (internal, not shown to customers)
  - Type: text
  - Required for launch? yes — BLOCKS LAUNCH (at least one zone must exist)
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Base shipping rate**
  - Type: money (decimal; currency = store default; $0 = free)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Countries covered**
  - Type: multi-select (country codes + area shortcuts e.g. EU, ROW)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **States/provinces covered** (per country)
  - Type: multi-select of subdivisions per country (partial or all)
  - Required for launch? no (defaults to all subdivisions when country is added)
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Shipping service description** (visible to customers at checkout)
  - Type: text (e.g. "Free 3-5 Weeks Shipping")
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes
  - Notes: PENDING — per-language variants of this description are not addressed in the screen; see multilingual spec.

- **Rule-based exceptions** (per exception within a zone)
  - *Exception type* — Type: select (Weight shown; PENDING: Price / Quantity / FBA availability per annotation)
  - *From value* — Type: decimal (e.g. 3,500 g)
  - *To value* — Type: decimal (e.g. 99,999,999 g)
  - *Exception rate* — Type: money
  - *Exception service description* — Type: text (visible to customers)
  - Notes: exception rule type list is PENDING — critical for the rating engine data model.

- **Additional shipping options for customers** (per extra service within a zone)
  - *Service name and rate* — PENDING: fields not shown in the empty state; only an "Add a service" button is visible.

- **Shipping model** — IN_SPEC via annotation (PENDING)
  - Type: select (Model 1 / Model 2 / …)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: owner annotation sketches multiple named rate models per zone, with product-level override. Semantics are undefined — Architect to specify.

---

### 18 — Shipping Carriers — /superadmin/

Source: `super-admin-09-shipping-carrier.jpg` and `super-admin-09-shipping-carrier-02-add.jpg`

20 pre-populated carriers are shown. Admins can add, edit, and delete carriers. Per-carrier:

- **Carrier name**
  - Type: text
  - Required for launch? no (pre-populated list covers common carriers)
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Tracking URL template**
  - Type: URL (tracking number appended to the end, e.g. `https://tools.usps.com/go/TrackConfirmAction_input?qtc_tLabels1=`)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: PENDING — URL templating convention (append-at-end vs placeholder syntax); Mondial Relay example contains a hardcoded number which suggests the template may require a placeholder token system.

---

### 19 — Automated Email Templates — /superadmin/

Source: `super-admin-10-automated-email.jpg`, `super-admin-10-automated-email-02.jpg`, `super-admin-10-automated-email-03.jpg`

Seven fixed system email templates. Each template is configured identically. Per-template:

- **Enabled toggle**
  - Type: toggle (on/off)
  - Required for launch? conditional (Order confirmation = BLOCKS LAUNCH; others no)
  - Connector? no
  - Visible in launch-readiness checklist? yes for Order confirmation only

- **From name**
  - Type: text (e.g. "Pradize")
  - Required for launch? yes (for enabled templates)
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **From email**
  - Type: email (protected by "Edit" link — likely triggers sender verification)
  - Required for launch? yes (for enabled templates) — BLOCKS LAUNCH for Order confirmation
  - Connector? no
  - Visible in launch-readiness checklist? yes
  - Notes: PENDING — whether "Edit" gates a sender domain verification flow (SPF/DKIM) or only email address entry.

- **Reply-to address**
  - Type: email (protected by "Edit" link)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Subject line**
  - Type: text (supports template variables — e.g. `{{ store_name }}`)
  - Required for launch? yes (for enabled templates)
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Email format**
  - Type: select (Plain Text / HTML)
  - Required for launch? yes (for enabled templates)
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Email body**
  - Type: textarea (plain text) or code editor (HTML mode) — Jinja/Twig-style templating with variables and loops
  - Required for launch? yes (for enabled templates)
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: a "View all available email variables" link opens a reference. Variables confirmed so far: `{{ store_name }}`, `{{ order.full_name }}`, `{{ order.order_date|date }}`, `{% for item in order.items %}`, `{{ item.text }}`, `{{ order.payment_method }}`, `{{ address.shipping_street }}`, `{{ address.shipping_suite }}`, `{{ address.shipping_city }}`, `{{ address.shipping_state }}`, `{{ address.shipping_zip }}`, `{{ address.shipping_country_name }}`, `{{ address.phone }}`, `{{ address.billing_street }}`, `{{ address.billing_suite }}`, `{{ address.billing_city }}`, `{{ address.billing_state }}`, `{{ address.billing_zip }}`, `{{ address.billing_country_name }}`.

**Per-language variant** — IN_SPEC (from owner annotation)
  - Type: per-language body + subject (PENDING — translation mechanism)
  - Required for launch? PENDING
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: owner annotation requests multilingual template variants with translation assignment via the jobs-to-do system, including retranslation when the source template is updated. Data model (one template per language vs fallback chain) is PENDING. See `11_uncertainties_to_validate.md` Part D section.

**Seven templates and their launch-criticality:**

| Template | Trigger | Blocks launch? |
|---|---|---|
| Order notification | Every order (to store owner, optional) | no |
| Order confirmation | Customer places order | yes |
| Refund | Refund with "notify customer" | no |
| Shipping confirmation | Tracking number added | no |
| Store invitation | New staff member invited | no |
| Gift card | Gift card issued | no |

Note: the "Invoice" template (shown in screenshot) is excluded — Invoice Orders are out of scope.

---

### 20 — Theme Library — /superadmin/

Source: `super-admin-13-customizable-themes-that-admin-can-choose.jpg`

- **Organization default / active theme**
  - Type: single-select from the theme library (blue checkmark badge on selected card)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes
  - Notes: the library contains themes built with two technologies: "Twig theme" (template-code based) and "Visual builder theme" (drag-and-drop builder). The active theme for the org is marked; per the filename sub-stores choose among the shared library. PENDING — whether the checkmark marks org-level default or per-sub-store active theme; architecture TBD.

- **Per-sub-store active theme**
  - Type: single-select from the shared organization theme library
  - Required for launch? yes — BLOCKS LAUNCH per store
  - Connector? no
  - Visible in launch-readiness checklist? yes
  - Notes: PENDING — whether this setting lives in per-store admin or in super-admin; recommendation: per-store admin selects from org library.

---

### 21 — AI Runner Configuration — /superadmin/

Source: `extra-spec-ecom.txt` (owner spec notes); confirmed decisions from `11_uncertainties_to_validate.md`.

This group configures the four AI runner connectors available for processing the job queue.

- **Claude Code runner — enabled toggle**
  - Type: toggle (on/off)
  - Required for launch? no (preferred runner; can operate without it)
  - Connector? yes — Claude Code
  - Visible in launch-readiness checklist? no
  - Notes: a Python script communicates with the Claude Code server and dispatches jobs. Claude Code is the preferred runner for text/code jobs.

- **Claude Code runner — server endpoint / path**
  - Type: text (URL or filesystem path to the Claude Code server)
  - Required for launch? conditional — required when Claude Code runner is enabled
  - Connector? yes — Claude Code
  - Visible in launch-readiness checklist? no

- **Codex runner — enabled toggle**
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? yes — OpenAI Codex
  - Visible in launch-readiness checklist? no
  - Notes: preferred runner for image-related jobs.

- **Codex runner — API key**
  - Type: text (API key, masked after entry)
  - Required for launch? conditional — required when Codex runner is enabled
  - Connector? yes — OpenAI Codex
  - Visible in launch-readiness checklist? no

- **Gemini Terminal runner — enabled toggle**
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? yes — Google Gemini Terminal
  - Visible in launch-readiness checklist? no
  - Notes: preferred runner for image/video generation jobs.

- **Gemini Terminal runner — configuration endpoint**
  - Type: text (PENDING — Gemini Terminal connectivity parameters not fully specified)
  - Required for launch? conditional — required when Gemini runner is enabled
  - Connector? yes — Google Gemini Terminal
  - Visible in launch-readiness checklist? no

- **OpenAI API runner — enabled toggle** (opt-in only — not default)
  - Type: toggle (on/off; default: OFF)
  - Required for launch? no
  - Connector? yes — OpenAI API
  - Visible in launch-readiness checklist? no
  - Notes: OpenAI API is used as a fallback runner only when membership credits remain and no terminal runner is available. Not enabled by default per the confirmed decision.

- **OpenAI API runner — API key**
  - Type: text (API key, masked after entry)
  - Required for launch? conditional — required when OpenAI API runner is enabled
  - Connector? yes — OpenAI API
  - Visible in launch-readiness checklist? no
  - Notes: PENDING — whether the OpenAI API key is the same key used for other OpenAI features (if any), or a runner-specific key.

---

### 22 — Amazon FBA Integration — /superadmin/ — PENDING

Source: `admin-017-product-pages.jpg` (owner annotations) + `extra-spec-ecom.txt`

Status: **PENDING** — owner annotation requests this feature; no UI has been designed yet. The following settings are derived from the annotation text.

- **Amazon FBA integration — enabled toggle**
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? yes — Amazon FBA
  - Visible in launch-readiness checklist? no

- **Amazon Affiliate ID per locale/marketplace**
  - Type: key-value list (marketplace code → affiliate ID; e.g. US → `pradize-20`, FR → `pradize-fr-21`)
  - Required for launch? conditional — required if "Add buy on Amazon button" is enabled
  - Connector? yes — Amazon Associates
  - Visible in launch-readiness checklist? no
  - Notes: affiliate links are appended to "Buy on Amazon" buttons when a matched product has FBA inventory.

- **Add "Buy on Amazon" button toggle**
  - Type: toggle (on/off)
  - Required for launch? no
  - Connector? yes — Amazon FBA
  - Visible in launch-readiness checklist? no
  - Notes: when enabled, product pages where FBA inventory is available show a "Buy on Amazon" button using the affiliate ID for the storefront's locale/marketplace.

---

## Payment Control Plane (/superadmin/)

The Control Plane is a dedicated section of the super-admin. It has its own left navigation (Overview, Organizations, Processor Accounts, Organization Rules, Payment Methods, Simulation, Decision Logs, Settings). A global **Publish** button stages config before it goes live — semantics are PENDING (see `11_uncertainties_to_validate.md` item 4).

---

### 23 — Organization Settings — /superadmin/ (Control Plane > Organizations)

Source: `super-admin-01-organizations.svg`

Per-organization record. The system requires exactly one organization flagged as default.

- **Display name**
  - Type: text (e.g. "Cedric SASU / FR")
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Legal name**
  - Type: text (e.g. "Pradize SASU")
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Default organization flag**
  - Type: checkbox (exactly one organization must be default; UI warns if none)
  - Required for launch? yes — BLOCKS LAUNCH (system blocks config if no default exists)
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Registration country**
  - Type: select (ISO 3166-1 alpha-2 country code)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Settlement currencies**
  - Type: multi-select (ISO 4217 currency codes; e.g. EUR, USD)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Buyer country/area coverage**
  - Type: multi-value (country codes + area aliases: EU, ROW, etc.)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Monthly threshold amount**
  - Type: decimal (e.g. 100000)
  - Required for launch? no (some orgs may have no threshold)
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: PENDING — threshold semantics (what triggers cap behavior, what counts toward the threshold, reset period, cascade behavior). See `11_uncertainties_to_validate.md` CRITICAL items.

- **Threshold currency**
  - Type: select (ISO 4217)
  - Required for launch? conditional — required when monthly threshold is set
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Support / statement descriptor**
  - Type: text (max ~22 chars — Stripe constraint; shown on buyer's bank statement)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Eligible for card payments**
  - Type: checkbox (on/off per method family)
  - Required for launch? yes — BLOCKS LAUNCH (at least one payment eligibility flag must be on)
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Eligible for PayPal payments**
  - Type: checkbox
  - Required for launch? conditional
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Status**
  - Type: select (Active / Draft; assume also Inactive)
  - Required for launch? yes (must be Active)
  - Connector? no
  - Visible in launch-readiness checklist? yes

---

### 24 — Processor Account Settings — /superadmin/ (Control Plane > Processor Accounts)

Source: `super-admin-02-processor-accounts.svg`

Per-processor-account record. Day-1 processors: Stripe (Card) and PayPal (PayPal). Additional processors added later via the connector registry.

- **Organization** (FK)
  - Type: select (from existing Organizations)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Account label**
  - Type: text (internal, e.g. "Stripe FR Main")
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Processor type**
  - Type: select (Stripe / PayPal for Day-1; HiPay / Mollie / BTCPay / BitPay added later via connector registry)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? yes — the selected processor
  - Visible in launch-readiness checklist? yes

- **Method family**
  - Type: select (Card / PayPal / Crypto — fixed set for v1)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Supports buyer countries**
  - Type: multi-value (country codes + area aliases)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Settlement currency**
  - Type: select (ISO 4217; single currency per account)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Secret / credential reference**
  - Type: text (`vault://` URI — external secret vault; inline secrets never stored)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? yes — the selected processor
  - Visible in launch-readiness checklist? yes
  - Notes: PENDING — vault backend (HashiCorp Vault or equivalent) is unspecified. Access control and rotation policy are security-critical. A "Test creds" action validates the vault-referenced credentials against the processor API.

- **May be used as primary (flag)**
  - Type: checkbox
  - Required for launch? yes (at least one account must allow primary usage)
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **May be used as backup only (flag)**
  - Type: checkbox
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: "may be primary" and "backup only" should be mutually exclusive — validation rule needed (PENDING).

- **Block new traffic manually (kill-switch)**
  - Type: checkbox (on = block all new payments through this account)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Shared across multiple stores**
  - Type: checkbox
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

---

### 25 — Payment Method Settings — /superadmin/ (Control Plane > Payment Methods)

Source: `super-admin-04-payment-methods.svg`

Three fixed method families (Card / PayPal / Crypto). Per method:

- **Enabled at checkout**
  - Type: checkbox (on/off)
  - Required for launch? yes — BLOCKS LAUNCH (at least one method must be enabled)
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Displayed label**
  - Type: text (e.g. "Card", "PayPal", "Crypto" — customizable)
  - Required for launch? yes (for enabled methods)
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: PENDING — whether labels are translatable per storefront language.

- **Processor option list** (ordered; per processor account assigned to this method)
  - Type: ordered list of ProcessorAccount references (drag-to-reorder)
  - Required for launch? yes — BLOCKS LAUNCH (method must have at least one processor option)
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Option strategy**
  - Type: select (backup_chain / alternate_evenly)
  - Required for launch? yes (for methods with more than one processor option)
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: "backup_chain" = first option used; others only on unavailability. "alternate_evenly" = rotate traffic; skip blocked options gracefully. Single-option methods have no strategy (not shown). PENDING — alternation scope (per store / per method globally / per buyer session) and strict round-robin vs random.

- **Hide method when no processor route is available**
  - Type: checkbox (on/off; default: on)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Allow stores to locally disable this method**
  - Type: checkbox (on/off; default: on)
  - Required for launch? no
  - Connector? no
  - Visible in launch-readiness checklist? no

---

### 26 — Organization Rules — Geography (Stage 1) — /superadmin/ (Control Plane > Organization Rules)

Source: `super-admin-03-organization-rules.svg`

Per geography rule:

- **Rule name**
  - Type: text
  - Required for launch? yes — BLOCKS LAUNCH (at least one geography rule needed for routing)
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Priority / order**
  - Type: integer (drag-to-reorder in the UI; determines evaluation sequence)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes
  - Notes: CRITICAL — overlapping geography rules ordering (e.g. broad "EU" rule at priority 1 shadowing "FR" at priority 3) must be resolved. See `11_uncertainties_to_validate.md` CRITICAL items.

- **Buyer country / area match set**
  - Type: multi-value selector (ISO country codes + area aliases: EU, ROW, etc.)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Result cardinality**
  - Type: radio (Return one organization / Return an organization pool for stage 2)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Returned organization(s)**
  - Type: multi-select (from existing Organizations; single if "one org" mode)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? yes

- **Status**
  - Type: select (Active / Draft)
  - Required for launch? yes (must be Active)
  - Connector? no
  - Visible in launch-readiness checklist? yes

---

### 27 — Organization Rules — Allocation (Stage 2) — /superadmin/ (Control Plane > Organization Rules)

Source: `super-admin-03-organization-rules.svg`

Per allocation rule (one per geography rule that returns a pool):

- **Applies to geography rule** (FK)
  - Type: select (references a Stage-1 rule that returns a pool)
  - Required for launch? yes — BLOCKS LAUNCH (required for any pool-returning geography rule)
  - Connector? no
  - Visible in launch-readiness checklist? no

- **Mode**
  - Type: radio (Percentage income split / Threshold switch)
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? no
  - Notes: CRITICAL — percentage split mechanics (per-order probabilistic rotation or amount accounting) are PENDING. Threshold switch cascade (what happens when the second org also hits its cap) is PENDING.

- **Mode configuration**
  - For percentage split: per-organization percentage allocations (must sum to 100%)
  - For threshold switch: ordered organization sequence with threshold amounts and currencies per step
  - Type: depends on mode
  - Required for launch? yes — BLOCKS LAUNCH
  - Connector? no
  - Visible in launch-readiness checklist? no

---

### 28 — Control Plane Global — /superadmin/ (Control Plane > Settings)

Source: `super-admin-00-page-map.svg` (Settings menu entry exists but has no diagram)

- **Publish / staging configuration**
  - Type: action button ("Publish" in top chrome) that promotes staged config to live
  - Required for launch? yes — BLOCKS LAUNCH (configuration must be published to take effect)
  - Connector? no
  - Visible in launch-readiness checklist? yes
  - Notes: PENDING — whether config is fully versioned (draft vs published state with rollback) or live-on-save. See `11_uncertainties_to_validate.md` item 4.

Other Control Plane Settings page content (Overview, Simulation, Decision Logs) has no diagrams. Content is PENDING.

---

## Launch Readiness Checklist

The following settings must be configured and active before the storefront can accept live orders. An automated pre-launch check should verify each item and block the "Go Live" action if any are missing.

### Tier 1 — Blocks all traffic

| # | Setting | Group |
|---|---------|-------|
| L1 | Store name set | 1 |
| L2 | Store contact email set | 1 |
| L3 | Store domain configured | 1 |
| L4 | At least one active shipping zone with at least one country and a base rate | 17 |
| L5 | At least one active shipping zone has a customer-facing service description | 17 |
| L6 | At least one active Organization (default flag set) in the Control Plane | 23 |
| L7 | At least one active Processor Account with valid credentials (Test creds passed) | 24 |
| L8 | At least one payment method enabled at checkout with at least one processor option | 25 |
| L9 | At least one active Geography rule in the Control Plane | 26 |
| L10 | Control Plane configuration Published | 28 |
| L11 | Active theme selected | 20 |
| L12 | Order confirmation email template enabled, From Name, From Email, and body set | 19 |
| L13 | Default store currency set | 8 |
| L14 | Abandoned checkout session timeout set | 14 |

### Tier 2 — Recommended before launch (warnings, not hard blocks)

| # | Setting | Group |
|---|---------|-------|
| R1 | Statement descriptor set on the default Organization | 23 |
| R2 | Shipping service description set on all active zones | 17 |
| R3 | Store logo uploaded | 1 |
| R4 | Robots.txt reviewed (not blocking crawlers) | 5 |
| R5 | Security badge configured on at least Checkout location | 16 |
| R6 | Pixel(s) installed (if ad campaigns are planned) | 9 |
| R7 | At least one currency enabled | 8 |

### Tier 3 — Optional / post-launch

Pixels, catalog feeds, review settings, recent purchase notifications, lead capture overlays, upsell campaigns, AI runners, Amazon FBA, abandoned-checkout campaign copy.

---

## Deployment / Environment Configuration (non-UI)

Unlike the groups above, the following settings have no admin screen — they are
deployment-time environment variables read by `webecom/settings/base.py`. Listed
here so they are not lost from the settings inventory.

### GIFT_CARD_FR_MIN_VALIDITY_DAYS
- Type: environment variable (`os.environ`), integer, default `1826` (5 × 365 + 1 leap-year buffer)
- Read by: `webecom/settings/base.py`
- Purpose: the statutory minimum validity floor (in days) that ADR-029 D6 enforces on any `GiftCardCampaign` whose issuing store targets the French market (same country-targeting detection ADR-027 D3 uses). `discounts.validators.validate_gift_card_campaign()` rejects publishing/issuing a campaign with a shorter-than-this expiry for an FR-targeting store.
- Is a floor, never a ceiling, and is never applied to stores that do not target France.
- Required for launch? No admin UI, not in the Launch Readiness Checklist — but the default must not be lowered without legal review, since it encodes the French "prescription commerciale" 5-year rule.
- Overridable via env for legal review only (e.g. if legal counsel determines a different floor applies); not exposed to store admins or super-admins in any screen.
- Group: ADR-029 (Gift Card Campaigns) — see `05_database_schema.md` §3 (`GiftCardCampaign`) and `docs/adr/ADR-029-gift-card-campaigns.md` D6.

---

## New CRITICAL Uncertainties Discovered During This Pass

The following uncertainties were identified while writing this document. They supplement the list in `11_uncertainties_to_validate.md`.

### Settings-C1 — CRITICAL: Store email domain handling
The General Settings screen shows a contact email input with a fixed `@<store-domain>.com` suffix plus an adjacent second input field. It is unclear whether:
(a) the store sends all transactional email from this address (requiring SPF/DKIM setup at the store domain level), or
(b) emails are sent from a platform-managed sender domain (e.g. `mail.pradize.com`) and the store address is only the "reply-to".
Implication: if (a), each store needs outbound email infrastructure. If (b), the platform manages a shared sender domain. Money-adjacent (affects deliverability of order confirmations, abandoned-checkout emails, gift card emails).

### Settings-C2 — CRITICAL: Control Plane "Publish" semantics
Already listed in `11_uncertainties_to_validate.md` item 4. Repeated here because this setting directly gates L10 in the launch checklist: without knowing whether "Publish" is required once or on every change, the checklist item cannot be reliably automated.

### Settings-C3 — CRITICAL: Vault backend for processor credentials
The `vault://payments/...` credential reference scheme in Processor Accounts implies an external secret manager. Until the vault backend is chosen (HashiCorp Vault, AWS Secrets Manager, or Django-level encrypted field), the "Test creds" action and the credential entry flow cannot be designed. This is a security-critical blocker for the payment setup flow.

### Settings-C4 — MEDIUM: Processor account primary/backup mutual exclusivity
The "May be used as primary" and "May be used as backup only" checkboxes are independent. If both are unchecked the account is unusable; if both are checked the semantics are contradictory. A validation rule or a single role enum (Primary / Backup / Either) must be decided before the Processor Accounts form is built.

### ~~Settings-C5 — MEDIUM: Per-language shipping service descriptions~~ DECIDED (human, 2026-07-10, ADR-019)
The "Shipping service description" field is customer-facing and shown at checkout. **Placement:** stays on `ShippingZone` per the spec text (`ShippingZone.service_description`) — human decision. Accepted consequences: super-admin-edited only (store admins cannot change it) and shared by every store shipping to that zone; copy must stay operational and store-neutral (e.g. "Ships from EU warehouse — no customs"), never store-branded. The Architect's rate-level alternative was considered and rejected. **Translation:** single-language for v1, matching the existing `announcement_text` precedent (no per-language mechanism); tracked on the "known multilingual debt" list in `docs/adr/ADR-019-shipping-service-description.md` rather than a one-off translation table. Does not block the multilingual checkout spec.

### Settings-C6 — MEDIUM: Description tab names — global vs per-locale
The description tabs defined in Product Page Feature Toggles (group 7) are global. For multilingual stores (e.g. "Shipping" → "Livraison") the tab names must be translated. Whether tab names are translatable is not addressed anywhere. Blocks multilingual product-page spec.
