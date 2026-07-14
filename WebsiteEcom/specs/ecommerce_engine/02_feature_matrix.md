# 02_feature_matrix

> Feature matrix — one row per distinct feature of the Pradize Django ecommerce engine. Sources: screen inventory (`01_screen_inventory.md` + `screen_inventory_parts/`), `extra-spec-ecom.txt`, decided items in `11_uncertainties_to_validate.md`, exclusions in `12_out_of_scope.md`.

**Status: FIRST PASS COMPLETE (2026-07-02).**

## Conventions

- **Level**: `store-admin` (per-store /admin/), `super-admin` (shared /superadmin/), `storefront`, `both` (= store-admin AND super-admin). Storefront-visible features configured in an admin are tagged by where the behaviour lives; config location is in Notes when non-obvious.
- **Priority**: P1 = core engine (blocking a first sellable store), P2 = important (baseline parity with the reference platform + owner requirements), P3 = later.
- **Status**: `IN_SPEC` (confirmed requirement), `PENDING` (blocked by a human decision — reference to `11_uncertainties_to_validate.md`, noted `11:...`), `OUT_OF_SCOPE` (listed in `12_out_of_scope.md`).
- Red-annotated owner additions are requirements → IN_SPEC.
- Applied decisions (2026-07-02): GrooveKart = inspiration/archetypes only; one unified `DiscountCode` model; gift cards = stored-value + `GiftCardTransaction`; zero engine-side tax config (tax-inclusive prices); two separate Django admin sites (assumed, see 11:#8).

---

## 1. Authentication & Permissions

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Admin login (email/password, remember-me, sessions) | Auth | both | admin-001 | P1 | IN_SPEC | Longer-lived cookie for "Remember me" |
| Password reset via email | Auth | both | admin-001 (link only) | P1 | IN_SPEC | Flow not shown; standard Django token flow assumed |
| Two separate Django admin sites (/admin/ store, /superadmin/ org) with shared auth | Auth | both | extra-spec menu split; 11:#8 | P1 | IN_SPEC | Assumed for this spec per decision note; 11:#8 formally pending approval |
| Employee accounts (invite by email, enable/disable toggle, delete) | Permissions | super-admin | part D super-admin-05 | P1 | IN_SPEC | Invitation ties to "Store invitation" automated email |
| Per-module permission matrix (18 modules, Full/No access; Orders has "Limited access") | Permissions | super-admin | part D super-admin-05-02 | P1 | PENDING | 11: Limited-access semantics on Orders + where employee-management permission lives (CRITICAL) |
| "Full Access" master mode vs per-module grid | Permissions | super-admin | part D super-admin-05-02 | P2 | PENDING | 11: interaction undefined (CRITICAL) |
| Store-admin role model (no matrix shown for store level) | Permissions | store-admin | 11 Part A CRITICAL | P2 | PENDING | Super-admin grid exists; store-level rights model to decide |
| Last-login tracking / login audit | Permissions | super-admin | part D super-admin-05 | P2 | IN_SPEC | Per-user timestamp displayed in list |
| 2FA / MFA | Auth | both | admin-001 uncertainty (MEDIUM) | P3 | PENDING | Safe default "none, add later" plausible; confirm |

## 2. Product Catalog & Variants

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Product CRUD with single-variant / multi-variant modes | Catalog | store-admin | admin-021 | P1 | IN_SPEC | |
| Product options (multiple options, value chips, "Changes Product Look", text-dropdown vs thumbnail selector) | Catalog | store-admin + storefront | admin-021 | P1 | IN_SPEC | Image↔option-value mapping UI not shown (MEDIUM) |
| Per-variant price, compare-at, SKU, availability toggle, default variant | Catalog | store-admin | admin-021 | P1 | IN_SPEC | Availability toggle semantics → see Inventory row |
| Product Type attribute | Catalog | store-admin | admin-005/017/021 | P1 | IN_SPEC | Feeds smart collections + reports |
| Vendor, tags, shipping weight, requires-shipping flag | Catalog | store-admin | admin-021, admin-017 | P1 | IN_SPEC | All toggleable via product-page settings |
| Rich-text description + multiple named, orderable description tabs | Catalog | store-admin + storefront | admin-017, admin-021 | P1 | IN_SPEC | WYSIWYG w/ tables, media |
| Product videos with display-position settings (first by default) | Catalog | store-admin + storefront | admin-021 red annotation | P2 | IN_SPEC | Owner addition |
| Pre-order products | Catalog | storefront | extra-spec | P2 | IN_SPEC | With day-to-receive; checkout/payment semantics unspecified (NEW CRITICAL, see end of file) |
| Ask-for-quotation products ("price starting at") | Catalog | storefront | extra-spec (B2B) | P2 | IN_SPEC | Quote workflow to spec |
| Multi-variant pages: several URLs of same product where only images change (Pinterest content + A/B testing with basic stats) | Catalog | store-admin + storefront | extra-spec; owner addition | P2 | IN_SPEC | Compatible with AI jobs; canonical/duplicate-content interaction to define |
| Per-product timer promos (selected days, discount %/amount/free product, time-day localized per domain country, AI-job managed & measured) | Catalog/Promos | store-admin + storefront | admin-021-02 red annotations | P2 | IN_SPEC | Interaction with pricing/coupon engine to define (MEDIUM) |
| Product customization fields (custom text / image upload by customer) | Catalog | store-admin + storefront | GrooveKart admin-021-03 (archetype) | P3 | IN_SPEC | Inspiration only; UI TBD by Designer |
| Supplier database import / supplier-factory marketplace | Catalog | — | 12_out_of_scope | — | OUT_OF_SCOPE | Revisit after engine core is stable |

## 3. Inventory management

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Inventory policy per product (track / don't track) | Inventory | store-admin | admin-021 | P1 | IN_SPEC | |
| Extended inventory modes: always SOLD OUT, no inventory, fixed, fake (server-assigned, consistent across users), presale w/ receive date, ask-when-available, ask-for-quotation | Inventory | super-admin + storefront | extra-spec | P1 | IN_SPEC | Owner-defined vocabulary; per-product assignment |
| Per-variant SOLD OUT toggle ("erase inventory if checked") | Inventory | store-admin | admin-021 red annotation | P1 | PENDING | 11 Part C CRITICAL: exact semantics vs inventory modes |
| Stock Transfers module | Inventory | store-admin | admin-003 menu (no detail screen) | P3 | IN_SPEC | No screen captured; minimal spec to author |
| Amazon FBA inventory sync (product-page setting + per-product checkbox, default checked) | Inventory | store-admin | admin-017 + admin-021 red annotations | P2 | IN_SPEC | Owner addition |
| "Buy on Amazon" affiliate button when FBA inventory available | Inventory/Storefront | storefront | admin-017 red annotation + extra-spec | P2 | IN_SPEC | Uses super-admin Amazon affiliate IDs (area 27) |

## 4. Collections (smart + manual)

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Collections list (publish/unpublish, drag-order, bulk select, pagination, search) | Collections | store-admin | admin-006 | P1 | IN_SPEC | Drag order drives storefront display order (assumed) |
| Manual collections: product picker + drag ordering of products | Collections | store-admin | admin-006 manual mode | P1 | IN_SPEC | Add-product control not visible (MEDIUM) |
| Smart collections: rule builder (title/vendor/type/price/tag/weight/variant title; is/starts/ends/contains/not-contains; match one/all; N rules) | Collections | store-admin | admin-006 auto mode | P1 | IN_SPEC | Numeric operators + price-vs-currency matching CRITICAL in part B — spec with defaults |
| Draft vs published collection lifecycle | Collections | store-admin | admin-006 (Save draft/Publish) | P1 | IN_SPEC | |
| Collection image + auto-crop framing preview | Collections | store-admin | admin-006 red annotation | P2 | IN_SPEC | Owner addition; preview of storefront card ratio |
| Per-product Display/CTR/Purchase-rate columns in collection editor + automatic improved sorting | Collections | store-admin | admin-006 red annotation + extra-spec | P2 | IN_SPEC | Owner addition; auto-sort algorithm TBD (MEDIUM); feeds "collections to improve" jobs |

## 5. Pricing, Coupons & Gift Cards (one DiscountCode system)

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Unified DiscountCode model with type field (coupon / manual gift card / auto-issued gift card) | Discounts | both | DECIDED 2026-07-02 (11:#2) | P1 | IN_SPEC | Explains "Gift card code" field in coupon editor |
| Coupon campaigns (internal name, active toggle, total & per-customer use limits, stacking flag, valid-from/expiry) | Discounts | store-admin | admin-019 | P1 | IN_SPEC | Per-customer identity for guests MEDIUM |
| Discount types: $ off order total, % off order total, free shipping | Discounts | store-admin + storefront | admin-019-03 | P1 | IN_SPEC | |
| "Free product" coupon type with product picker | Discounts | store-admin + storefront | admin-019-03 red annotation | P1 | IN_SPEC | Owner addition |
| Coupon applicability rules ("Add rule"; default = all orders) | Discounts | store-admin | admin-019-02 | P1 | PENDING | 11 Part C CRITICAL: rule-type vocabulary unknown |
| Coupon usage stats (used count, "Totalling") | Discounts | store-admin | admin-019 | P2 | PENDING | 11: "Totalling" = discounts given vs order revenue (CRITICAL) |
| Gift cards for sale (sellable products: value, expiry, collections, WYSIWYG terms, image, own storefront page) | Gift cards | store-admin + storefront | admin-007 | P2 | IN_SPEC | Expiry semantics + value currency CRITICAL in part B — spec with defaults |
| Stored-value gift cards with partial redemption (GiftCardTransaction ledger) | Gift cards | both | DECIDED 2026-07-02 (11:#3) | P1 | IN_SPEC | Balance per card |
| Gift card value modes ($ fixed / % of order total / % discount off order) | Gift cards | super-admin | super-admin-12-02; 11:#3 remainder | P2 | PENDING | "% discount" as card value conflicts with stored-value model |
| Issued gift cards list (status, generated/expiry dates, filters incl. type & amount conditions, saved views) | Gift cards | store-admin | admin-020 | P1 | IN_SPEC | Saved views kept here despite orders "Save View" removal (inconsistency flagged in 12) |
| Manual gift card issuance with templated email ({gift_value}, {gift_code}) | Gift cards | store-admin | admin-020-02 | P2 | IN_SPEC | |
| Gift card expiry rules (relative days / absolute date / never; jurisdiction constraints) | Gift cards | both | admin-007/020, super-admin-12 | P2 | PENDING | 11:#3 remainder: expiry per jurisdiction |
| Automatic pricing experiments (only after X sales; min/max bounds around purchase price; profit maximisation) | Pricing | super-admin | extra-spec | P3 | IN_SPEC | Statistics-triggered job (area 24); thresholds set in super-admin |
| Tax-inclusive pricing everywhere; no tax engine, no tax lines; email receipt only | Pricing | storefront | DECIDED 2026-07-02 (11:#6); extra-spec | P1 | IN_SPEC | Taxes module OUT_OF_SCOPE (12); VAT data only via bookkeeping API (area 6) |

## 6. Orders & Fulfillment

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Orders list (pagination, per-page size, bulk select, status badges) | Orders | store-admin | admin-004 | P1 | IN_SPEC | RTL/UTF-8 names supported |
| Two-dimension status model: payment (PAID/REFUNDED/PARTIALLY REFUNDED…) × fulfillment (SHIPPED/SENT/NOT SENT/PARTIAL…) | Orders | store-admin | admin-004 | P1 | PENDING | 11 Part A CRITICAL: canonical enumerations + transitions |
| Order filters: order status, refund status, card last-4, date range, multi order#/customer name/email | Orders | store-admin | admin-004-02 | P1 | IN_SPEC | AND semantics assumed |
| Product-name keyword filter on orders | Orders | store-admin | admin-004-02 red annotation | P1 | IN_SPEC | Owner addition; searches order lines by product title |
| Order detail page | Orders | store-admin | implied (links everywhere); no screenshot | P1 | IN_SPEC | Content to spec from customer-detail + email variables |
| CSV Fulfillment export (and tracking import-back) | Fulfillment | store-admin | admin-004 | P1 | PENDING | 11 Part A CRITICAL: file contents + round-trip |
| Refunds: full & partial, "notify customer" refund email | Orders | store-admin | admin-004, super-admin-10 | P1 | IN_SPEC | Executes against payment processor |
| Tracking numbers on orders + carrier tracking-link resolution | Fulfillment | store-admin | super-admin-09, shipping-confirmation email | P1 | IN_SPEC | |
| Partial line-item fulfillment (PART. SENT & SHIPPED) | Fulfillment | store-admin | admin-004 | P1 | IN_SPEC | |
| Card last-4 stored on order (search + PCI scope limited to last 4) | Orders | store-admin | admin-004-02 | P1 | IN_SPEC | |
| Per-item ship-from country change at shipping validation (one item up to all items) | Fulfillment | store-admin | extra-spec | P2 | PENDING | 11:#12 (CRITICAL) — part of bookkeeping spec draft |
| French-bookkeeping order-export API (complete VAT rate/regime values for orders and refunds, Amazon-style) | Orders/API | super-admin | extra-spec | P2 | PENDING | 11:#12 — Spec Agent to draft for validation |
| Bulk actions on selected orders | Orders | store-admin | admin-004 (bar only) | P2 | PENDING | Actions list never shown (MEDIUM) |
| Saved order views ("Save View") | Orders | store-admin | admin-004-02 red cross-out; 12 | — | OUT_OF_SCOPE | Crossed out by owner |
| Invoice Orders module | Orders | store-admin | admin-003 red cross-out; 12 | — | OUT_OF_SCOPE | Includes invoice generation (email receipt only) |

## 7. Customers

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Customers list (name, last purchase, city+country, order count, lifetime spent) | Customers | store-admin | admin-009 | P1 | IN_SPEC | |
| Customer detail (order history, multiple shipping/billing addresses, email/phones, payments used, merchant notes) | Customers | store-admin | admin-009-02 | P1 | IN_SPEC | Notes = single blob + Save |
| Customer filters ("Add filters") | Customers | store-admin | admin-009 | P2 | IN_SPEC | Dimensions not shown (MEDIUM); propose defaults |
| Customer tags (set by lead-capture signup; reusable for segmentation) | Customers | store-admin | admin-015 | P2 | IN_SPEC | |
| Guest vs account customer identity model (merge by email?) | Customers | storefront | admin-009 uncertainty | P1 | PENDING | 11 Part A MEDIUM but data-model foundational |
| GDPR PII lifecycle: retention, anonymization, erasure, export | Customers | both | 11 Part B CRITICAL | P1 | PENDING | Also covers abandoned-checkout emails to non-purchasers |

## 8. Storefront Analytics & Reports

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Dashboard: today KPIs, trends chart (period tabs, metric tabs), conversion funnel, latest orders, top products, top referrers | Analytics | store-admin | admin-002 | P1 | IN_SPEC | Time-zone-aware "today" |
| First-party event tracking pipeline (sessions, referrers, impressions, product clicks, scroll %, funnel events, purchase attribution) | Analytics | storefront | admin-002/005 + red annotations; extra-spec collection stats | P1 | IN_SPEC | Metric definitions/denominators PENDING (11 CRITICAL); foundation for all new columns |
| 12 report types (8 sales: product title, collection, month, hour, country, state, customer, traffic source; 4 traffic: referrer, device, location, landing page) | Reports | store-admin | admin-005 series | P1 | IN_SPEC | Canonical naming to pick (MEDIUM) |
| Product Variant report | Reports | store-admin | admin-005 red cross-out; 12 | — | OUT_OF_SCOPE | |
| Universal period selector (Custom/Today/Yesterday/This-Last week/This-Last month/All time) | Reports | store-admin | admin-005-01-02 red highlight | P1 | IN_SPEC | "Available in all kind of reports" |
| CSV export on every report | Reports | store-admin | admin-005 series | P1 | IN_SPEC | PII implications on customer report |
| Sortable columns on all report tables | Reports | store-admin | admin-005-12 red annotation | P2 | IN_SPEC | Owner addition (generalised) |
| Default "high first" sorting; Sales-by-month most-recent-first | Reports | store-admin | admin-005-08/-03 red annotations | P2 | IN_SPEC | Sort key (orders vs sales) MEDIUM |
| Product report extra columns: Collection display, Collection CTR, Page display, Purchase rate | Reports | store-admin | admin-005-01 red annotation | P2 | IN_SPEC | Owner addition; definitions PENDING (11 CRITICAL) |
| Collection report extra columns: Display, Avg % scroll, % CTR ≥1 product, % Purchase ≥1 product | Reports | store-admin | admin-005-02 red annotation + extra-spec | P2 | IN_SPEC | Owner addition |
| Landing-page report extra columns: Avg page, % Product CTR, % Purchase rate | Reports | store-admin | admin-005-12 red annotation | P2 | IN_SPEC | Owner addition |
| Customer-local-hour bucketing for Sales-by-hour (delivery-country time) | Reports | store-admin | admin-005-04 red annotation | P2 | IN_SPEC | Owner addition; multi-TZ countries approximation PENDING (11 CRITICAL) |
| Hierarchical state report (state rows for US/CN/RU/IN…, country rows for small countries) | Reports | store-admin | admin-005-06 red annotation | P2 | IN_SPEC | Owner addition; country list + state normalisation PENDING (11 CRITICAL) |
| Traffic-source → order attribution model (Direct bucket, app sources, campaign ids) | Analytics | storefront | admin-005-08 | P1 | PENDING | 11 CRITICAL: first vs last touch, window |
| Money-metric definitions (Revenue / Gross Sales / Net sales) | Analytics | store-admin | admin-002/005-06 | P1 | PENDING | 11 CRITICAL: affects every money report |
| Unique-visitor dedup, geo-IP location, user-agent/OS parsing | Analytics | storefront | admin-005-09/10/11 | P1 | IN_SPEC | "Device"=OS today; report OS vs device class MEDIUM |
| Statistics collection "realistic" + purge to avoid oversized DB | Analytics | both | extra-spec | P2 | IN_SPEC | Important for job-triggering quality |

## 9. SEO

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Per-entity URL slug/handle (product, collection, gift card, pages) | SEO | store-admin | admin-006/007/021 SEO sections | P1 | IN_SPEC | Slug change → auto redirect (area 16) |
| Per-entity Page Title (SEO) + Meta Description | SEO | store-admin | admin-006/007/021 | P1 | IN_SPEC | |
| Canonical URLs | SEO | storefront | implied; required by multi-variant A/B pages | P1 | IN_SPEC | Interaction with multi-URL product pages to define |
| hreflang across domains/language paths | SEO | storefront | implied by domain/language model (extra-spec) | P1 | IN_SPEC | Depends on 11:#9 domain model |
| XML sitemap | SEO | storefront | implied (standard; no screen) | P1 | IN_SPEC | Per domain/language |
| robots.txt with custom include lines | SEO | store-admin + storefront | admin-016 | P1 | IN_SPEC | Platform base + custom merge |
| Social sharing metadata (Open Graph: Facebook, Pinterest…) | SEO | store-admin + storefront | admin-021-02 red annotation | P2 | IN_SPEC | Owner addition |
| Root-directory file upload (txt/xml/html/json verification files) | SEO | store-admin | admin-016 | P2 | IN_SPEC | |

## 10. Multilingual content

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Domain/subdomain management: one language per domain + `/fr` `/de` path languages | i18n | super-admin | extra-spec | P1 | PENDING | 11:#9 — Domain→Language→Country model to propose |
| Per-language settings variants (PaneSettings-style table) | i18n | store-admin | admin-016 red annotation | P2 | IN_SPEC | Owner addition |
| Translated catalog/page content via translation jobs (always highest priority) | i18n | both | extra-spec | P1 | IN_SPEC | Executed by AI job system (area 24) |
| Per-language email templates managed by translation jobs (incl. update handling on template change) | i18n | super-admin | super-admin-10 red annotation | P2 | IN_SPEC | Owner addition; template model PENDING (11 Part D CRITICAL) |
| Localized promo/campaign times per domain main-country (timer promos, upsells) | i18n | storefront | admin-021 red annotations | P2 | IN_SPEC | |

## 11. Abandoned Checkout campaigns

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Abandoned-checkout detection (inactivity timeout setting, email capture step) | Abandoned | storefront | super-admin-07; admin-008 | P1 | PENDING | 11 Part B CRITICAL: definition + GDPR basis |
| Campaign management (multiple concurrent, per-campaign toggle, delete, date-range stats) | Abandoned | store-admin | admin-008 | P2 | IN_SPEC | |
| Recovery stats + attribution (unique impressions, sales recovered, revenue recovered) | Abandoned | store-admin | admin-008 | P2 | PENDING | 11 CRITICAL: attribution rules |
| Automated email sequence per campaign (sender name + store-domain-locked from-address) | Abandoned | store-admin | admin-008-02 | P2 | IN_SPEC | Per-email delay DECIDED AF-C4 (2026-07-04): `send_delay_hours`, required, default 1 h |
| Email wizard: Type (Warning/Reminder/Incentive) → Style (4 templates + preview) → Copy (subject/headline/body/CTA, curated + custom) + **"Send after" delay field** (number + hours/days dropdown, required, default 1 h) | Abandoned | store-admin | admin-008-03…07; DECIDED AF-C4 2026-07-04 | P2 | IN_SPEC | Delay field lives in the wizard, not a separate step |
| Dynamic cart-content merge + tokenized resume-checkout deep link | Abandoned | storefront | admin-008-04 | P1 | PENDING | 11 CRITICAL: token security/expiry |
| Coupon integration in incentive copy (CODE123 → real/unique codes?) | Abandoned | store-admin | admin-008-06 | P2 | PENDING | 11 CRITICAL: literal vs generated codes |
| Unsubscribe link + compliance | Abandoned | storefront | admin-008-04 | P1 | IN_SPEC | Unsubscribe scope MEDIUM |
| Global statistics, system logs, per-campaign email log | Abandoned | store-admin | admin-008 | P3 | IN_SPEC | |

## 12. Email marketing (automated + transactional)

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Automated email catalogue: Order notification, Order confirmation, Refund, Shipping confirmation, Store invitation, Gift card | Emails | super-admin | super-admin-10 | P1 | IN_SPEC | 6 triggers (Invoice removed, see next row) |
| Invoice email template | Emails | super-admin | super-admin-10; 12 (Invoice Orders removed) | — | OUT_OF_SCOPE | Depends on removed Invoice Orders module |
| Twig/Jinja-style template engine (variables, filters, loops, variables reference doc) | Emails | super-admin | super-admin-10-02/03 | P1 | IN_SPEC | Same engine family as themes |
| Plain-text vs HTML template modes (code editor for HTML) | Emails | super-admin | super-admin-10-02/03 | P2 | IN_SPEC | |
| Test-send per template | Emails | super-admin | super-admin-10-02 | P2 | IN_SPEC | |
| From/Reply-to sender management with domain verification (SPF/DKIM) | Emails | super-admin | super-admin-10; admin-008/016 | P1 | PENDING | 11 Part B/C CRITICAL: sending infra + per-store sender domain |
| Per-template enable toggle | Emails | super-admin | super-admin-10 | P2 | IN_SPEC | |
| AI marketing emails to customers (introduce products, discount codes, Trustpilot-review-for-free-item emails) | Emails | both | extra-spec AI job ideas | P3 | IN_SPEC | Delivered through job system (area 24) |

## 13. Up-sell & conversion (archetypes; GrooveKart = inspiration, UI TBD)

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Related Products campaign (3-level matching cascade: bought-together / same tags / same collection; drag priority; per-level toggle) | Up-sell | both | super-admin-11-03 | P2 | IN_SPEC | Requires co-purchase mining |
| Buy X get X free / Buy X get % off (multiple tiers, stacking toggle, cheapest-items-free rule; **no free-value cap** — DECIDED 2026-07-04, cap toggle from screenshot dropped, see 12_out_of_scope.md) | Up-sell | both | super-admin-11-04 | P2 | IN_SPEC | Multi-tier + stacking pricing algorithm MEDIUM (04 AF-109) |
| Storewide discount with timer (post-purchase; **single-use next-order `CampaignCode`**, "Discount code expires after N days" setting default 30 — DECIDED 2026-07-04; top timer bar, thank-you CTA text/image) | Up-sell | both | super-admin-11-05 | P2 | IN_SPEC | Code emailed and/or shown on thank-you page |
| One-click upsell funnel (post-purchase charge without re-entering payment; funnel page builder; free vs store-rule shipping) | Up-sell | both | super-admin-11-06; 11:#5 | P2 | PENDING | 11:#5 CRITICAL: payment token reuse model; funnel builder not captured |
| Order bumps campaign (Multi/Single select modes, category heading, auto split-test rotation between campaigns sharing category) | Up-sell | both | super-admin-11-07 | P2 | IN_SPEC | |
| Order-bump placements: product page, cart/checkout page, floating cart (archetype) | Up-sell | storefront | GrooveKart admin-021-03 | P3 | IN_SPEC | Inspiration only; needs theme bump slots |
| Product bundles (this product + exactly one other, % discount, visible from both products) (archetype) | Up-sell | store-admin + storefront | GrooveKart admin-021-03/-02 | P3 | IN_SPEC | Inspiration only; discount base MEDIUM |
| Shared targeting rule builder (All products / Manually / Conditions with all-any + field/operator/value rules) | Up-sell | both | super-admin-11-06-02/-07 | P2 | IN_SPEC | Same builder as smart collections |
| Country targeting per campaign | Up-sell | both | super-admin-11-04/05/06/07 | P2 | IN_SPEC | Shipping country assumed |
| Per-user frequency capping (impressions per time window) | Up-sell | storefront | super-admin-11 + admin-015 | P2 | IN_SPEC | Identity mechanism MEDIUM (cookie vs account) |
| Campaign KPIs (unique impressions, conversions, conversion rate) | Up-sell | both | super-admin-11 | P2 | IN_SPEC | KPI formula MEDIUM (rounding anomaly seen) |
| Two-level campaign ownership: store admins see super-admin campaigns read-only + create their own | Up-sell | both | super-admin-11 red annotation | P2 | IN_SPEC | Owner addition; precedence DECIDED (2026-07-04): store-admin campaign wins at runtime; super-admin campaign applies only if no store campaign exists for the product |
| Scheduled/timed upsells & bumps (enabled only some days of week/month, adds timer; AI-job managed with performance measurement; localized) | Up-sell | both | admin-021-03 red annotations | P2 | IN_SPEC | Owner addition |
| Upsell page templates built in visual builder (archetype) | Up-sell | super-admin | GrooveKart admin-021-03 | P3 | IN_SPEC | Ties to visual-builder themes (area 25) |

## 14. Lead capture & notifications

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Lead capture overlay campaigns (list, status lifecycle, per-campaign visitors/impressions/conversions stats) | Lead capture | store-admin | admin-015 | P2 | IN_SPEC | Visitors-vs-impressions definition MEDIUM |
| Overlay triggers: exit-intent (desktop) / time; separate mobile-tablet trigger config | Lead capture | storefront | admin-015-02 | P2 | IN_SPEC | No exit-intent on touch |
| Prebuilt overlay theme gallery | Lead capture | store-admin | admin-015-02 | P2 | IN_SPEC | Per-campaign text editability MEDIUM |
| Post-signup action: display message or redirect to URL | Lead capture | storefront | admin-015-02/03 | P2 | IN_SPEC | |
| Page-exclusion targeting (page picker) | Lead capture | store-admin | admin-015-02 | P3 | IN_SPEC | |
| Per-user frequency capping | Lead capture | storefront | admin-015-02 | P2 | IN_SPEC | |
| Customer tagging on signup | Lead capture | store-admin | admin-015-02 | P2 | IN_SPEC | |
| Auto-issue of promised coupon/gift code on signup | Lead capture | storefront | admin-015 themes ("GET MY 10% OFF"); admin-020 (765 × $5 cards) | P2 | PENDING | 11 CRITICAL: what auto-issued the 765 cards; ties to DiscountCode |
| Recent-purchase notification widget (rolling data window, first-notification delay, interval) | Notifications | storefront | admin-014 | P2 | IN_SPEC | |
| Enable/disable toggle for recent-purchase widget | Notifications | store-admin | admin-014 red annotation | P2 | IN_SPEC | Owner addition (missing in current UI) |
| Buyer data displayed publicly in purchase popups (privacy) | Notifications | storefront | admin-014 uncertainty | P1 | PENDING | 11 Part C CRITICAL: GDPR |

## 15. Media / Images

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Product image galleries (drag-drop upload, numbered ordering, variant-image linkage) | Media | store-admin | admin-021 | P1 | IN_SPEC | |
| Entity images: collection, gift card, review photos, overlay/badge custom uploads | Media | store-admin | admin-006/007/012, super-admin-06 | P1 | IN_SPEC | |
| Media storage architecture (replacement for removed Files module) | Media | both | admin-003 cross-out; 12 (Files) | P1 | PENDING | 11/12 MEDIUM: assets must live somewhere; Architect to place |
| Product video storage & delivery | Media | storefront | admin-021 red annotation | P2 | IN_SPEC | Cross-ref area 2 |

## 16. Redirects (auto-created only)

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Redirect management screen (view, search, enable/disable toggle, edit destination, bulk, delete; relative or absolute destinations) | Redirects | store-admin | admin-010 | P1 | IN_SPEC | Manual "Add a Redirect" removed per owner annotation — view/manage only |
| Auto-created redirects on product/page slug change (also collection slugs — scope to confirm) | Redirects | store-admin | admin-010 red annotation | P1 | IN_SPEC | Owner addition; collection/page scope MEDIUM |
| Redirect policy: HTTP status (301/302), chain resolution, loop prevention | Redirects | storefront | admin-010 uncertainties | P2 | IN_SPEC | Propose defaults (301, flatten chains) for approval |

## 17. Currency conversion

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Default store currency setting | Currency | store-admin | admin-XXX-payment | P1 | IN_SPEC | Change-effect on existing prices/orders CRITICAL (11:#7 vicinity) |
| Multi-currency display converter (per-currency enable, editable symbol, symbol/code visibility prefix/suffix) | Currency | super-admin | admin-011; extra-spec ("Currencies (super-admin)") | P2 | IN_SPEC | 34 currencies in reference store; moved to super-admin per extra-spec |
| Storefront currency picker + default currency selection for visitor (geo-IP?) | Currency | storefront | admin-011 implied | P2 | IN_SPEC | Default-detection MEDIUM |
| Exchange-rate source, update frequency, rounding rules (.99 psychological pricing) | Currency | super-admin | admin-011 uncertainties | P2 | PENDING | 11 Part B CRITICAL |
| Display-only vs transactional conversion (charged currency) | Currency | storefront | admin-011 uncertainties | P1 | PENDING | 11 Part B CRITICAL — money impact |
| Per-currency locale formatting (separators) | Currency | storefront | admin-011 | P2 | IN_SPEC | Standard locale formats assumed |

## 18. Reviews

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Reviews dashboard (total count, latest reviews, pending badge, request & up-sell stats panels) | Reviews | store-admin | admin-012 | P2 | IN_SPEC | |
| Moderation workflow (pending → approved / hidden; auto-publish vs publish-after-approval; new-review notification email) | Reviews | store-admin | admin-012-03/04 | P2 | IN_SPEC | |
| Manual review entry (name, product, backdated date, stars, text, photos, create-another) | Reviews | store-admin | admin-012-02 | P2 | IN_SPEC | Provenance flag needed (merchant vs customer vs AI) |
| Automated review-request email (N days after shipment, subject, template, test send, manual send, old-order exclusion list) | Reviews | store-admin | admin-012/012-04 | P2 | IN_SPEC | Requires fulfillment/shipment tracking |
| Review widget styling (star/button colors, tiles/rows layout, visible count + show-more, hide-if-no-reviews) | Reviews | storefront | admin-012-04 | P2 | IN_SPEC | |
| Verified-buyer badge (review linked to real order) | Reviews | storefront | admin-012-04 | P2 | IN_SPEC | Badge style options MEDIUM |
| AI-generated reviews via jobs | Reviews | both | admin-012-04 red annotation | P2 | IN_SPEC | Owner addition; legal/compliance + provenance flags PENDING (11 Part B CRITICAL) |
| Customer-only display threshold (show only real customer reviews once N customer reviews reached) | Reviews | storefront | admin-012-04 red annotation | P2 | IN_SPEC | Owner addition; threshold semantics to pin (per-product vs global) |
| Review-widget up-sell surface with revenue attribution stats | Reviews | storefront | admin-012 | P3 | IN_SPEC | Mechanics not shown (MEDIUM) |

## 19. Shipping

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Shipping zones (name, base rate, country multi-select with per-subdivision granularity, customer-facing service description) | Shipping | super-admin | super-admin-08/-02 | P1 | IN_SPEC | |
| Zone matching precedence (specific zone vs "All Countries" catch-all) | Shipping | super-admin | super-admin-08 | P1 | PENDING | 11 Part D CRITICAL — affects charged price |
| Rule-based rate exceptions (type=Weight from/to + rate + own customer description) | Shipping | super-admin | super-admin-08-02 | P1 | IN_SPEC | Full exception-type list CRITICAL in part D — spec with defaults |
| FBA-inventory-available exception rule type | Shipping | super-admin | super-admin-08-02 red annotation | P2 | IN_SPEC | Owner addition |
| Multiple shipping price models per zone (Model 1 default / Model 2…) with per-product model override in product editor | Shipping | super-admin + store-admin | super-admin-08-02 red annotations | P2 | IN_SPEC | Owner addition; semantics PENDING (11 Part D CRITICAL) |
| Additional shipping options/services offered to customers (e.g. express) | Shipping | super-admin + storefront | super-admin-08-02 | P2 | IN_SPEC | Empty state only; spec from scratch |
| Shipping carriers catalogue with tracking-URL templates (add modal, chain-create) | Shipping | super-admin | super-admin-09/-02 | P1 | IN_SPEC | URL placeholder convention MEDIUM |
| Shipping Plus | Shipping | store-admin | admin-003 red cross-out; 12 | — | OUT_OF_SCOPE | |

## 20. Catalog feeds

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Facebook Dynamic Product Ads feed (XML/RSS generation, product scope dropdown, public feed URL + copy, freshness status banner) | Feeds | store-admin | admin-013 | P2 | IN_SPEC | |
| Google Shopping feed | Feeds | store-admin | admin-003 menu + admin-013 red annotation | P2 | IN_SPEC | Owner addition confirms |
| Pinterest shopping feed | Feeds | store-admin | admin-013 red annotation | P2 | IN_SPEC | Owner addition |
| Per-country feed variants for one language/domain (e.g. EN site shipping US + CA needs one feed per country) | Feeds | store-admin | extra-spec | P2 | IN_SPEC | Depends on Domain→Language→Country model (11:#9) |
| Feed field mapping + refresh cadence (background job) | Feeds | store-admin | admin-013 uncertainties | P2 | IN_SPEC | Implicit; propose mapping (price, availability, image, GTIN…) |

## 21. Pixels & Analytics integrations

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Pixel manager: Facebook, Google Analytics Enhanced Ecommerce, Snapchat, Pinterest, TikTok (install w/ ID, edit/uninstall via overflow menu) | Pixels | store-admin | admin-018 | P2 | IN_SPEC | One pixel per provider assumed |
| Storefront ecommerce event firing per provider (ViewContent, AddToCart, Purchase, values, currency) | Pixels | storefront | admin-018 | P1 | PENDING | 11 Part C CRITICAL: exact event set per provider |
| Purchase-event dedup on thank-you page reload | Pixels | storefront | 11 Part C (design-pattern-ideas §X) | P1 | IN_SPEC | |

## 22. Payment processing (store level)

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Stripe connector (cards) — day 1 | Payments | both | admin-XXX-payment; task decision | P1 | IN_SPEC | |
| PayPal connector — day 1 | Payments | both | admin-XXX-payment; task decision | P1 | IN_SPEC | |
| Pluggable processor adapter architecture (HiPay, Mollie, BTCPay, BitPay… addable later) | Payments | super-admin | part E processor types; task decision | P1 | IN_SPEC | |
| Per-payment-method default processor designation (store view) | Payments | store-admin | admin-XXX-payment | P1 | PENDING | 11:#7 — reconcile store screen with control-plane routing (which layer owns defaults) |
| Per-processor settings (credentials, capture mode, webhooks, test mode) | Payments | super-admin | admin-XXX-payment Settings dropdown | P1 | PENDING | 11:#7 CRITICAL: content never shown; secrets live in control plane |
| Refund execution via processor API | Payments | store-admin | admin-004 badges | P1 | IN_SPEC | Cross-ref area 6 |

## 23. Payment Control Plane (super-admin)

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Organizations (legal entities: display/legal name, single default fallback, registration country, settlement currencies, buyer coverage incl. EU/ROW areas, statement descriptor, per-method eligibility, Active/Draft) | Control plane | super-admin | part E svg-01 | P1 | IN_SPEC | "Default organization required" enforcement |
| Current-month volume tracking per org vs monthly threshold | Control plane | super-admin | part E svg-01 | P1 | PENDING | 11 Part E CRITICAL: what counts, month boundary, currency conversion, behaviour at cap |
| Processor accounts (label, org FK, processor type, method family, supported countries, settlement currency, per-family drag priority, may-be-primary/backup-only flags, manual block, multi-store share) | Control plane | super-admin | part E svg-02 | P1 | IN_SPEC | Role-flag validation rule needed (MEDIUM) |
| Vault-referenced credentials (`vault://…`) + "Test creds" verification | Control plane | super-admin | part E svg-02 | P1 | PENDING | 11 CRITICAL: vault backend, access control, rotation |
| Health-status machine (Healthy/Ready…) + automatic failover to backups | Control plane | super-admin | part E svg-02 | P1 | PENDING | 11 CRITICAL: health source (probes vs manual) |
| Two-stage routing rules: Stage 1 geography (first match → one org or pool) → Stage 2 per pool (% income split OR threshold switch) → default-org fallback | Control plane | super-admin | part E svg-03 | P1 | PENDING | 11 CRITICAL ×3: rule-ordering paradox, split mechanics (running counter per design-pattern §IX), threshold accounting |
| Payment methods config (Card/PayPal/Crypto: enable, displayed label, ordered processor options, backup_chain vs alternate_evenly, hide-when-no-route, allow-store-local-disable) | Control plane | super-admin | part E svg-04 | P1 | IN_SPEC | Checkout shows only method families; processor invisible to buyer |
| Layer composition: org routing result vs method-level option list/strategy | Control plane | super-admin | part E svg-00/04 | P1 | PENDING | 11 CRITICAL: which layer decides merchant of record |
| Publish workflow (staged/versioned config vs live-on-save) | Control plane | super-admin | part E chrome ("Publish"); 11:#4 | P1 | PENDING | 11:#4 CRITICAL |
| Simulation page (test routing decisions without real orders) | Control plane | super-admin | part E menu (no diagram) | P2 | PENDING | 11 CRITICAL: content unspecified |
| Decision Logs (audit of routing decisions) | Control plane | super-admin | part E menu (no diagram) | P2 | PENDING | 11 CRITICAL: content + retention (may contain PII) |
| Overview dashboard + control-plane Settings page | Control plane | super-admin | part E menu (no diagrams) | P2 | PENDING | Unspecified |
| Store payment "profiles" (stores assign profiles + enable/disable methods locally) | Control plane | both | part E svg-00 footer | P2 | PENDING | 11 MEDIUM: "profile" mentioned once, never defined |

## 24. AI Job System (jobs-to-do)

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Job system core: pluggable job types (easy add/remove), triggers (AI or automatic rules), assignees (AI or human) | Jobs | both | extra-spec ("one of the first specs to define") | P1 | IN_SPEC | Cross-cutting foundation referenced by annotations in reviews/promos/emails/translations |
| Terminal execution: Claude Code (default), Codex (image jobs), Gemini terminal (image/video jobs) polling for jobs | Jobs | super-admin | extra-spec | P1 | IN_SPEC | Python script communicates with server |
| API-call execution as last resort when membership credits exhausted | Jobs | super-admin | extra-spec | P2 | PENDING | NEW CRITICAL: "memberships"/credits model never defined anywhere |
| Job requirements metadata (needs image generation / video generation / command execution) | Jobs | both | extra-spec | P2 | IN_SPEC | Drives runner selection |
| Result workflows: auto-publish (e.g. translations) vs human review (accept / reject / request revision) | Jobs | both | extra-spec | P1 | IN_SPEC | |
| Job log + measurement system (AI verifies jobs happened well; measurements decide next job) | Jobs | both | extra-spec | P1 | IN_SPEC | |
| Statistics-triggered non-AI jobs: collections to improve (bad CTR), pages to improve (high display / low purchase), automatic pricing | Jobs | both | extra-spec | P2 | IN_SPEC | Pages-to-improve delegable to AI |
| Translation jobs (always highest priority) | Jobs | both | extra-spec | P1 | IN_SPEC | Cross-ref area 10 |
| Promo jobs: create/update limited-time timer promos (add/remove/measure) | Jobs | both | extra-spec + admin-021 annotations | P2 | IN_SPEC | |
| Social-media posting jobs (Pinterest, Instagram, Facebook, YouTube Shorts, TikTok; video via Veo/Gemini; posting via N8N; multi-account strategy testing) | Jobs | both | extra-spec | P3 | IN_SPEC | Step-by-step jobs |
| Spec-improvement suggestion jobs (AI suggests spec changes to do a better job) | Jobs | super-admin | extra-spec | P3 | IN_SPEC | |
| WebsiteAspire database import job | Jobs | super-admin | extra-spec ("job that I love") | P2 | IN_SPEC | Source: /home/cedric/Applications/WebsiteEmpire2 |
| OpenAI API as opt-in super-admin setting (NOT default) | Jobs | super-admin | extra-spec; 12 | P2 | IN_SPEC | Design constraint recorded in 12 |
| AI sales chat on storefront (answers questions, makes suggestions, knowledge-base backed) | Jobs/Storefront | storefront | extra-spec | P3 | PENDING | 11:#10 — technology proposal pending |

## 25. Themes

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| 3 curated storefront themes: foods / fashion / general (average-to-high-end), light customization (fonts, colors, images) to ease testing | Themes | both | extra-spec ("make this very complete") | P1 | IN_SPEC | Design PENDING 11:#11 (Designer proposal) |
| Twig-style code theme engine | Themes | super-admin | super-admin-13 ("Twig theme" cards) | P1 | IN_SPEC | Same template family as email engine |
| Org-level theme library; sub-store admins choose their active theme | Themes | both | super-admin-13 | P2 | IN_SPEC | Activation model (org default vs per-store) MEDIUM |
| Visual-builder themes (drag-and-drop) | Themes | super-admin | super-admin-13, GrooveKart upsell templates | P3 | IN_SPEC | Second theme technology; later phase |
| Theme Store + "Build new theme" flows | Themes | super-admin | super-admin-13 | P3 | IN_SPEC | |
| Theme duplication / backup copies | Themes | super-admin | super-admin-13 (backup-named card) | P3 | IN_SPEC | |

## 26. Admin settings (store)

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| General settings: store name, contact email (store-domain suffix), subdomain change | Settings | store-admin | admin-016 | P1 | IN_SPEC | Email/domain handling CRITICAL in part C (see area 12 sender row) |
| Merged General Settings + Domains screen | Settings | store-admin | admin-003 red annotation ("merge") | P2 | IN_SPEC | Owner addition; merged content MEDIUM |
| Delete store | Settings | store-admin | admin-016 | P2 | PENDING | 11 Part C CRITICAL: soft-delete/retention semantics |
| Storefront-wide password protection | Settings | store-admin + storefront | admin-016 | P2 | IN_SPEC | Staging/coming-soon mode |
| Standards presets: unit system, default weight unit, timezone | Settings | store-admin | admin-016 | P1 | IN_SPEC | Affects shipping calc + report boundaries |
| Header/body include code injection on all storefront pages | Settings | store-admin + storefront | admin-016 | P1 | IN_SPEC | Verification tags, custom scripts |
| Product-page feature toggles: Type, Description Tabs (+global tab names, drag order), Compare-at, Shipping weight, Vendor, SKU, Tags | Settings | store-admin | admin-017 | P1 | IN_SPEC | Disable effect on existing data MEDIUM |
| Security badges (multiple badges, preset combos gallery, custom upload, colored heading, border styling, per-location enable + width: product page / cart / checkout) | Settings | store-admin + storefront | super-admin-06/-02 | P2 | IN_SPEC | Grouped here per matrix plan; screen filed under part D |
| Checkout global: Google Maps address autocomplete (free Google Cloud signup; US-only zip autofill) | Settings | super-admin + storefront | super-admin-07 | P3 | IN_SPEC | Activation flow MEDIUM; abandoned-timeout setting → area 11 |
| Global admin search across entities | Settings | both | admin-002 chrome | P2 | IN_SPEC | Scope MEDIUM (orders/products/customers?) |
| Menu favorites (drag-and-drop, per-user) + All Tools view | Settings | store-admin | admin-003 | P3 | IN_SPEC | |
| App store / installed apps framework | Settings | store-admin | admin-003 menu | P3 | PENDING | 11 MEDIUM: built-in modules vs third-party apps to drop in a custom Django engine |
| Zapier integration | Settings | store-admin | admin-003 red cross-out; 12 | — | OUT_OF_SCOPE | |
| CSV Templates | Settings | store-admin | admin-003 red cross-out; 12 | — | OUT_OF_SCOPE | |
| Files module | Settings | store-admin | admin-003 red cross-out; 12 | — | OUT_OF_SCOPE | Replacement architecture: area 15 media row |

## 27. Super-admin settings

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Automated gift card campaigns (name, value modes, product triggers all/manual/conditions, expiry relative/absolute, frequency cap, delivery email with token pills, draft/publish) | Super-admin | super-admin | super-admin-12/-02 | P2 | IN_SPEC | Value "% discount off order" mode PENDING (area 5 row); relation to "Gift card" email template MEDIUM |
| Campaign issuance/redemption/outstanding counters | Super-admin | super-admin | super-admin-12 | P2 | IN_SPEC | "Outstanding" definition MEDIUM (744 issued / 10 used / 1 outstanding) — liability reporting |
| Source & configuration of bulk auto-issued cards (765 × $5, ~30-day expiry) | Super-admin | super-admin | admin-020; 11 Part C CRITICAL | P2 | PENDING | What issues them (lead capture? post-purchase?) unknown |
| Amazon affiliate IDs powering "Buy on Amazon" links | Super-admin | super-admin | extra-spec | P2 | IN_SPEC | Cross-ref areas 3/13 |
| Automatic-pricing thresholds (X sales to activate, min/max bounds) | Super-admin | super-admin | extra-spec | P3 | IN_SPEC | Cross-ref areas 5/24 |

Cross-references (rows live elsewhere): Employee accounts + permission matrix → area 1; Up-sell campaigns (super-admin level + two-level ownership) → area 13; Automated emails → area 12; Shipping zones/carriers → area 19; Currencies → area 17; OpenAI opt-in → area 24; Themes library → area 25; Payment Control Plane → area 23.

## 28. Multi-tenant / multi-store architecture

| Feature | Module/Area | Level | Source evidence | Priority | Status | Notes |
|---|---|---|---|---|---|---|
| Multi-store (sub-stores) under one organization sharing super-admin config | Multi-tenant | both | part D intro; part E chrome ("shared across stores") | P1 | IN_SPEC | |
| Shared-config vs store-local override model (payment methods disable, profiles, theme choice, campaigns read-only + own) | Multi-tenant | both | part E svg-00 footer; super-admin-11/-13 annotations | P1 | IN_SPEC | Per-area precedence rules PENDING in their areas |
| Domain/subdomain per store + language paths | Multi-tenant | super-admin | extra-spec; admin-016 | P1 | PENDING | 11:#9 — same model as area 10 |
| Store groups (filter dimension on organizations) | Multi-tenant | super-admin | part E svg-01 filter | P2 | PENDING | 11 MEDIUM: group management never shown |
| Store lifecycle (create store, change subdomain, delete store) | Multi-tenant | both | admin-016 | P2 | IN_SPEC | Delete semantics PENDING (area 26 row) |
| Test-maximizing architecture: unit-test coverage across workflows, complete test data, record API calls/events to replay real-life data, DB-migration testing | Multi-tenant/Engineering | both | extra-spec | P1 | IN_SPEC | Cross-cutting engineering requirement, binds every area |

---

## Totals

| Area | Rows | P1 | P2 | P3 | OUT_OF_SCOPE |
|---|---|---|---|---|---|
| 1. Authentication & Permissions | 9 | 5 | 3 | 1 | 0 |
| 2. Product Catalog & Variants | 13 | 6 | 5 | 1 | 1 |
| 3. Inventory management | 6 | 3 | 2 | 1 | 0 |
| 4. Collections | 6 | 4 | 2 | 0 | 0 |
| 5. Pricing, Coupons & Gift Cards | 14 | 8 | 5 | 1 | 0 |
| 6. Orders & Fulfillment | 15 | 10 | 3 | 0 | 2 |
| 7. Customers | 6 | 4 | 2 | 0 | 0 |
| 8. Analytics & Reports | 17 | 8 | 8 | 0 | 1 |
| 9. SEO | 8 | 6 | 2 | 0 | 0 |
| 10. Multilingual content | 5 | 2 | 3 | 0 | 0 |
| 11. Abandoned Checkout | 9 | 3 | 5 | 1 | 0 |
| 12. Email marketing | 8 | 3 | 3 | 1 | 1 |
| 13. Up-sell & conversion | 14 | 0 | 11 | 3 | 0 |
| 14. Lead capture & notifications | 11 | 1 | 9 | 1 | 0 |
| 15. Media / Images | 4 | 3 | 1 | 0 | 0 |
| 16. Redirects | 3 | 2 | 1 | 0 | 0 |
| 17. Currency conversion | 6 | 2 | 4 | 0 | 0 |
| 18. Reviews | 9 | 0 | 8 | 1 | 0 |
| 19. Shipping | 8 | 4 | 3 | 0 | 1 |
| 20. Catalog feeds | 5 | 0 | 5 | 0 | 0 |
| 21. Pixels & integrations | 3 | 2 | 1 | 0 | 0 |
| 22. Payment processing | 6 | 6 | 0 | 0 | 0 |
| 23. Payment Control Plane | 13 | 9 | 4 | 0 | 0 |
| 24. AI Job System | 14 | 5 | 6 | 3 | 0 |
| 25. Themes | 6 | 2 | 1 | 3 | 0 |
| 26. Admin settings | 15 | 4 | 5 | 3 | 3 |
| 27. Super-admin settings | 5 | 0 | 4 | 1 | 0 |
| 28. Multi-tenant architecture | 6 | 4 | 2 | 0 | 0 |
| **Total** | **244** | **106** | **108** | **21** | **9** |

All PENDING rows reference open items in `11_uncertainties_to_validate.md`; all 9 OUT_OF_SCOPE rows trace to `12_out_of_scope.md`.

## New CRITICAL uncertainties discovered while writing (to append to 11)

1. **Storefront surface has no source screens.** Cart page, checkout steps, product/collection page layouts, storefront search (`/search/<term>` paths seen in reports), FAQ/contact/404 pages exist only as landing-page paths and checkout-globals settings. The entire buyer-facing page inventory and checkout flow must be authored from scratch — no area in this matrix covers it explicitly beyond Themes; it blocks theme design (11:#11) and pixel events (area 21).
2. **"Memberships"/credits model undefined.** extra-spec makes API-call job execution conditional on "credits available in the memberships", but no membership/credit entity, pricing, or accounting is defined anywhere. Blocks the job-runner selection logic (area 24).
3. **Pre-order and ask-for-quotation payment semantics undefined.** Deposit vs full charge vs no charge at pre-order; quotation → order conversion flow; interaction with payment routing and abandoned-checkout logic. Money impact (areas 2/3).
4. **Multi-variant A/B page URLs vs SEO.** The multiple-URLs-per-product feature (Pinterest/A-B) conflicts with canonical/duplicate-content handling; whether variants are canonicalized to a primary URL or independently indexed changes the SEO model (areas 2/9).
