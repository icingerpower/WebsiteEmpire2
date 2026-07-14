# Storefront Theme System — 3 Curated Themes (TICKET-029)

**Status: FIRST DRAFT — 2026-07-05. Spec Agent. Visual designs PENDING (11:#11 — Designer Agent proposals).**

Sources: `10_implementation_tickets.md` (TICKET-029), `extra-spec-ecom.txt` (3 customizable themes, "make this very complete"), `design-pattern-ideas.txt` (§III, §VI, §X, §XI, §XV — binding), `super-admin-13-customizable-themes-that-admin-can-choose.jpg`, `02_feature_matrix.md` area 25, `03_user_flows.md` UF-001–UF-021, `04_admin_flows.md` AF-111, `05_database_schema.md` (Theme `[H]`), `06_settings_requirements.md` §7/§16/§20, `07_multilingual_seo.md` §2/§5, ADR-001 §3b, ADR-005, ADR-008.

---

## 1. Overview

Pradize ships with a storefront theme system and **three fully built curated themes**:

| Theme | Vertical | Positioning |
|---|---|---|
| **B2B** | Professional / B2B | Clean, trust-first, catalog-dense |
| **Fashion** | Fashion & apparel | Editorial, minimal, image-led |
| **General** | General products | Average to high-end — never cheap-looking |

(Theme names decided 2026-07-05: B2B / Fashion / General — see ADR-012.)

### 1.1 What a "theme" is (and is not)

Per `extra-spec-ecom.txt`: *"Do light customizations to reduce chance of bugs and make testing more easy (different fonts / different colors / different images to pick)."* This sentence is the central architectural constraint:

- **All three themes share ONE template set and ONE DOM contract.** A theme is a *skin*: a design-token bundle (colors, font pairing, spacing/radius scale, card aspect ratio) + curated imagery defaults + a small whitelist of overridable partials. Themes are **not** three divergent codebases.
- Store-admin customization is **enumerated, not free-form**: pick a palette preset, pick a font pair from the theme's curated list, upload logo/favicon/hero images, edit announcement-bar text. No custom CSS, no layout builder in P1.
- Consequence for testing: functional tests run once against the shared DOM contract (stable `data-testid` attributes); per-theme testing reduces to token/asset rendering and visual regression.

The reference platform's library (super-admin-13) shows two theme technologies: **"Twig theme"** (code) and **"Visual builder theme"** (drag-and-drop). P1 implements the Twig-style code engine only; the visual builder is Phase 3 (feature matrix area 25).

### 1.2 Ownership and activation (DECIDED)

- The theme library is **owned by the platform super-admin** (ADR-001 §3b — no `organization` FK on Theme; adding one is a design error).
- Each store activates exactly one theme: `Store.theme` FK (05_database_schema).
- Super-admin marks a **platform default** theme (blue checkmark in super-admin-13); store admins see it pre-selected and may pick any other library theme (AF-111 steps 6–12).

### 1.3 Page scope (DECIDED — FM-C1b + 07 §2)

Themes must cover every buyer-facing page: **home**, **collection**, **product** (standard + all inventory-mode variants + A/B image-variant URLs), **cart** + **floating cart**, **checkout**, **thank-you** (with up-sell funnel slot), **search results**, **404**, **static pages** (FAQ, contact, quotation form, policies). All 87 source screenshots are admin-side; the storefront is designed from scratch using the feature toggles, checkout globals, analytics funnel and user flows as constraints (DECIDED FM-C1).

### 1.4 Mobile-first

**ASSUMPTION (LOW, confirm):** all three themes are designed mobile-first (majority of social/Pinterest traffic is mobile; the A/B image-variant feature explicitly targets Pinterest). Desktop is the progressive enhancement, not the other way around.

---

## 2. User Stories

- **US-T1 (Super-admin):** As the super-admin, I manage the platform theme library (grid of cards: thumbnail, name, engine label, last-edited timestamp), set the platform default theme, and see which theme each store uses — so every new store starts from a sane default.
- **US-T2 (Store admin):** As a store admin, I open Themes in `/admin/`, see the library with the default pre-selected, pick the theme matching my vertical, customize logo, palette preset, font pair and hero imagery, preview the storefront before going live, then activate — without writing any code.
- **US-T3 (Store admin, multilingual):** As a store admin running `mydomain.com` + `/fr` + `/de`, my theme renders every published language correctly — translated content, translated slugs in every internal link, language-appropriate imagery where configured — with zero per-language theme configuration.
- **US-T4 (Shopper):** As a shopper on a phone, I get a fast storefront (hero loads quickly, images lazy-load, add-to-cart is one thumb-reach away) where prices always state that taxes are included, in my language and display currency.
- **US-T5 (Developer/Test agent):** As a developer, I test storefront behavior once against a stable DOM contract; switching the active theme cannot break a functional test, only visuals.
- **US-T6 (Integrations):** As the developer of T023/T028/T030/T034/T035/T036/T038, I mount my widget into a documented named slot that every theme is guaranteed to render.

---

## 3. Functional Requirements

### 3.1 Template engine & theme library

- **TH-001 — Template engine.** The storefront uses a Twig-style engine. **PENDING APPROVAL (MEDIUM)** — options:
  - **(a) Jinja2 via Django's built-in `django.template.backends.jinja2` — RECOMMENDED.** Jinja2 is the Python sibling of Twig (near-identical syntax `{{ }}`/`{% %}`/filters), matching both the "Twig theme" cards in super-admin-13 and the email template engine ("same engine family as themes", feature matrix area 12). First-party Django support, no extra dependency beyond Jinja2 itself.
  - (b) Django template language everywhere — simplest, but not Twig-style and diverges from the email engine decision.
  - (c) Real Twig-compat layer — rejected: no maintained Python implementation worth the risk.
- **TH-002 — Theme model** per 05_database_schema: `Theme(name, engine=twig_code|visual_builder, source_ref, customization_json, is_backup_copy, status, store FK nullable)`. P1 creates `engine=twig_code` rows only. `source_ref` points to the on-disk theme package (versioned in the repo, not uploaded blobs, for P1). `customization_json` on Theme holds the theme's **defaults** (see TH-021 for per-store values).
- **TH-003 — Platform ownership.** Library rows have `store=NULL`. No `organization` FK exists (ADR-001 §3b). Store admins have read-only access to library rows; their only write is the activation pointer and their own customization record.
- **TH-004 — Library screen (`/superadmin/` → Themes)**, per super-admin-13 + AF-111: header "N Themes in the Library"; grid of cards (live-render or stored thumbnail, name, engine label "Twig theme"/"Visual builder theme", last-edited timestamp); blue checkmark badge on the platform default; sort dropdown "Sort by last edited (Newest first)"; pagination ("1 – 13 of 13" + prev/next). The screenshot's **"Build new theme"** and **"Theme Store"** buttons are P3 (recorded in §7 Out of scope); P1 renders the library seeded with the 3 curated themes.
- **TH-005 — Store activation screen (`/admin/` → Themes).** Same card grid, read-only; platform default pre-selected visual state; clicking a card (with confirm dialog) sets `Store.theme`. Gated by the employee permission matrix row "Themes: Full access / No access" (04_admin_flows).
- **TH-006 — Activation is atomic and immediate.** Switching themes changes rendering on the next request; it never requires a deploy or downtime. Any per-store compiled-CSS cache (TH-024) is invalidated in the same transaction. Switching themes never touches catalog data, URLs, or customization records of other themes.
- **TH-007 — Preview before activation. RECOMMENDED, PENDING APPROVAL (MEDIUM):** a store admin can open a signed preview URL (`?theme_preview=<token>`, token bound to admin session, TTL ≤ 24 h) that renders the storefront with a non-active theme + draft customization. Preview responses send `X-Robots-Tag: noindex` and fire **no** analytics events and **no** pixels. Alternative (not recommended): activate-and-look, which briefly exposes shoppers to a half-configured theme.
- **TH-008 — Fallback chain.** Store with `theme=NULL` → platform default theme → the "reference theme" (the first curated theme built, which doubles as the engine's reference implementation per TICKET-029's note that "engine + one reference theme can proceed" before designer sign-off). Rendering must never 500 because a theme row is missing; log + fall back (visible-failure principle §XV-1 satisfied by an admin launch-checklist warning L11 "Active theme selected", 06 §checklist).

### 3.2 Per-store customization (light, enumerated)

- **TH-020 — Customizable per store (P1 exhaustive list):**
  1. **Logo** (image; shown in header + emails per 06 §general) and **favicon**.
  2. **Palette preset** — single-select among the theme's 3–4 curated presets (TH-124/128/132), PLUS per-slot hex override for the limited slot set: `primary`, `accent`, `background`, `surface`, `text`, `muted-text`, `badge/sale`, `announcement-bar`. Hex-validated. Nothing else is colorable.
  3. **Font pair** — single-select among the theme's curated pairs (heading + body). No free font upload in P1.
  4. **Imagery**: homepage hero image(s), collection fallback hero, announcement-bar visibility + text, footer tagline. Product/collection images come from the catalog, not the theme.
  5. **Structural toggles** (boolean only): sticky header on/off, announcement bar on/off, breadcrumbs on/off, "hover shows second product image" on/off (Fashion default on).
- **TH-021 — Storage.** Per-store values live in a dedicated **`StoreThemeCustomization(store FK, theme FK, customization_json, updated_at)`** record, unique per (store, theme) — NOT on the shared `Theme` row (a platform row cannot hold N stores' colors) and NOT overwriting `Theme.customization_json` (which holds the theme's defaults). Switching back to a previously used theme restores that theme's saved customization. **ASSUMPTION (LOW)** — schema detail for the Architect; the split platform-defaults vs per-store-values is the requirement, the table name is not.
- **TH-022 — Resolution order** for any token: per-store override → selected preset → theme default. One resolver function, used by both the storefront renderer and the admin customization preview (single resolution function, §XV-4).
- **TH-023 — Validation at persistence** (§XV-5): hex format; contrast check — if resolved `text`-on-`background` or `primary`-CTA contrast falls below WCAG AA 4.5:1, the admin form shows a blocking warning requiring explicit "save anyway" confirmation (ASSUMPTION LOW: warn-and-confirm, not hard-block).
- **TH-024 — Serving customization.** Resolved tokens render as a CSS custom-property block (`:root { --p-primary: …; }`) inlined in `<head>` (one DB/cache read, no extra request, no FOUC). Cached per (store, theme, customization.updated_at); invalidated on save and on theme switch. Theme structural CSS never contains store-specific values — only `var(--p-*)` references.
- **TH-025 — Image customization slots** accept uploads through the standard media pipeline (TICKET-003-MEDIA) with the same validation (formats, size limits). For language-specific imagery (e.g. a FR-text hero banner), the slot supports optional per-language variants resolved by the `TranslatedMedia`-style model helper with fallback exact-lang → default (design-pattern §VI; fallback logic lives in the model helper, never in templates).

### 3.3 Template architecture (shared skeleton)

- **TH-040 — Three-level inheritance:** `base` (engine-owned skeleton: `<head>`, SEO block, analytics beacon, header/footer includes, named slots) → `page templates` (home, collection, product, cart, checkout, thankyou, search, 404, static) → **theme layer** (design tokens + assets + optional overrides of a whitelisted partial list only — e.g. `partials/hero`, `partials/product-card-badge`, `partials/footer-band`). A theme MUST NOT override page templates or the base skeleton; the whitelist is enforced by the theme loader.
- **TH-041 — SEO head block.** Exactly one `{% block seo_head %}` in the base template; canonical, hreflang, og:*, structured data are produced by TICKET-025's layer into this block (design-pattern §XI). Themes never emit their own canonical/hreflang/meta-robots. Page templates may only *extend* the block via the T025 API (e.g. product OG image).
- **TH-042 — URL emission.** Every internal href — menus, breadcrumbs, product cards, related products, pagination, language switcher, logo link — goes through the PermalinkResolver template function (URL-003, TICKET-016). Hardcoded internal URLs in any template are a review-blocking defect; a template lint check (grep for `href="/` outside the resolver tag) runs in CI. Menu items reference objects by PK, resolved per `request` language at render time (§III).
- **TH-043 — Analytics beacon** JS is **inlined** in the base template's single first-party `<script>` block (design-pattern §X — no separate file, no defer-related missed events). It emits the event set of TICKET-012: `EVT_PAGE_VIEW`, `EVT_PRODUCT_VIEW`, `EVT_COLLECTION_IMPRESSION` (IntersectionObserver — impressions only for cards actually entering the viewport, UF-001), `EVT_SCROLL_DEPTH` (25/50/75/100 %), `EVT_VARIANT_SELECT`, `EVT_ADD_TO_CART`. Pixels (T030) mount separately in the pixel slot and must respect FiredPixel dedup semantics.
- **TH-044 — i18n.** All theme-owned UI strings ("Add to Cart", "Sold Out", "Your cart is empty"…) come from the translation catalog for `resolve_locale(request)` (ADR-008); catalog translation for store languages ships with the engine (translated via the AiJob CLI pipeline like all customer-visible content — memory: never direct API). Content strings (titles, descriptions, review text) come from the translation tables with `status='published'` only (ML-041). RTL layout is out of scope P1 (§7).
- **TH-045 — Named integration slots.** The base skeleton declares documented slots which every theme renders (empty when the feature is off): `slot.announcement`, `slot.header_icons` (search, currency, cart), `slot.overlay` (lead capture T034), `slot.social_proof` (recent-purchase toast T035), `slot.chat_launcher` (T038, P3), `slot.pixels` (T030), `slot.consent` (see TH-141), `slot.security_badge` (checkout/product), `slot.bump_product`, `slot.bump_cart`, `slot.bump_floating_cart` (T027 order-bump placements), `slot.thankyou_funnel` (T028), `slot.amazon_buy` (T036). A theme that drops a slot fails the theme conformance test suite.
- **TH-046 — DOM contract.** Interactive/testable elements carry stable `data-testid` attributes identical across themes (`data-testid="add-to-cart"`, `variant-selector`, `qty-stepper`, `coupon-input`, `checkout-submit`, `funnel-accept`, `funnel-decline`, …). The contract is a versioned document in the repo; changing it requires updating the shared test suite in the same change.

### 3.4 Static assets & delivery

- **TH-060 — Asset pipeline. PENDING APPROVAL (MEDIUM)** — options:
  - **(a) Plain hand-written CSS (custom properties) + vanilla ES-module JS, no Node build step — RECOMMENDED.** Matches "light customization to reduce bugs", removes an entire toolchain from CI, keeps themes reviewable diffs. Budgets (§4) are enforceable by file-size checks.
  - (b) Tailwind + build step — faster iteration for the Designer agent, but adds Node to the pipeline and produces low-readability diffs.
  - (c) Vite bundle — overkill for a no-framework storefront.
- **TH-061 — Serving.** Django `staticfiles` with `ManifestStaticFilesStorage` (content-hashed filenames), served by WhiteNoise/nginx with `Cache-Control: public, max-age=31536000, immutable`. CDN is out of scope for now (VPS hosting per memory) but hashed filenames make later CDN fronting a config change.
- **TH-062 — Fonts self-hosted** as WOFF2 with `font-display: swap`, subset to the store's published languages' charsets. **No Google Fonts CDN** (GDPR — German case law treats the CDN call as unlawful PII transfer; also removes a third-party render dependency).
- **TH-063 — JavaScript is progressive enhancement.** Core flows (view product, add to cart, cart page, checkout submit, quotation/notify-me forms) must work as standard form POSTs without JS (resolves UF-003's PENDING "quotation without JavaScript" → **ASSUMPTION (LOW): yes, all storefront forms work no-JS**; the floating-cart popup, gallery swipe, and impression/scroll events are JS-only enhancements by nature).
- **TH-064 — Images.** Responsive `srcset`/`sizes` on all catalog imagery; `loading="lazy"` everywhere **except** the LCP candidate (hero / first product image), which is `fetchpriority="high"`; explicit `aspect-ratio` boxes to eliminate CLS. **Card aspect ratio is a theme token** (TH-121/125/129) — this also answers 04_admin_flows' PENDING "image auto-crop preview aspect ratio": the admin crop preview reads the active theme's card ratio.
- **TH-065 — Third-party JS** (pixels, chat) loads only through its slot, only when configured, and never render-blocking.

### 3.5 Page-by-page requirements

All behavior below references the binding user flows (03_user_flows); this section fixes what themes must *render*.

- **TH-080 — Home** (`/[lang/]`, indexable, `WebSite`+`SearchAction` JSON-LD — 07 SD-008). Ordered sections, each individually toggleable per store: announcement bar → header → **hero** (image/headline/CTA → links to a chosen collection) → **featured collections** grid (admin-picked, 2–4) → **featured products** carousel (admin-picked collection as source) → theme-specific band (B2B: "trust/freshness" icons; Fashion: lookbook strip; General: benefits row) → reviews/social-proof strip (aggregate stars, T023 data) → newsletter signup (wired to lead-capture list, T034) → footer. Exact composition and section options **PENDING APPROVAL with 11:#11 designer proposals**; the section *mechanism* (ordered, toggleable, admin-picked sources by PK) is the P1 requirement.
- **TH-081 — Collection** (`/[lang/]collections/<slug>/`) per UF-001: optional hero image, title, description, product-card grid (2-col mobile / 3–4-col desktop per theme), **numbered pagination** (RECOMMENDED over infinite scroll: crawlable, and scroll-depth % — a KPI here — is meaningless on an infinite page; ASSUMPTION MEDIUM), sort control, minimal filtering (sort + tag/type filter only if the store has the corresponding product toggles on; full faceted filtering is P3 — §7). Impression + scroll-depth events per TH-043. Empty state: "No products available" (published-but-empty collections; SEO gates for thin/empty collections are T025's).
- **TH-082 — Product** (`/[lang/]<slug>/`) per UF-002/003/004/005, honoring the **Product Page Feature Toggles** (06 §7 — type, description tabs + tab list, compare-at, vendor, SKU, tags; a disabled toggle removes the element from the DOM, not `display:none`):
  - Gallery: numbered, swipeable, thumbnails; variant-image switching when the option "Changes Product Look".
  - Title, star summary (average + count; hidden when zero reviews and "hide widget" configured — T023), price + compare-at strikethrough (never shown when compare-at ≤ price), **"All taxes included" note adjacent to every price** (extra-spec; storefront-wide rule, also cart/checkout/thank-you).
  - Description tab set as configured; variant selectors (dropdown or thumbnail per option config); quantity stepper (default 1).
  - **CTA states by inventory mode** (UF-003/004): Add to Cart / "Pre-Order Now" + "Expected to ship by [DATE]" notice / disabled "Sold Out" badge / "Notify Me When Available" + inline email form / "Request a Quotation" + form (Name, Email, Company?, ~~Quantity,~~ Message) (**DECIDED, human, 2026-07-10, 18:P2: quantity removed from all customer-facing quotation forms — quotation-mode products are not cartable, so a requested quantity is not meaningful at request time; `QuotationRequest.quantity` model field retained, default 1, for potential admin-side use only**) + "Starting from [price]" when configured. Fake-inventory "X left" label rendering PENDING (UF uncertainty #B).
  - Security badge slot, reviews section (star breakdown, photo reviews, pagination, "Write a Review" → UF-018), related-products carousel (UF-011), product-page order-bump slot, Amazon buy-button slot (T036), timer-promo countdown (PENDING — UF critical uncertainty #A).
  - A/B image-variant URLs (UF-005) render with the **same template**, swapping only the image set; canonical → primary (URL-004).
- **TH-083 — Cart + floating cart** per UF-006: floating mini-cart popup opening on add-to-cart (line items with thumbnail/variant/qty stepper/line total, subtotal, Checkout CTA, floating-cart bump slot, continue-shopping and go-to-cart actions); full `/cart/` page (qty edit, remove, subtotal, cart bump slot, checkout CTA, taxes-included note, empty state hiding the checkout CTA).
- **TH-084 — Checkout** (`/checkout/`, noindex) per UF-007–UF-010: contact + shipping address (Google Maps autocomplete slot when the super-admin global is on — 06 §15), shipping method (T032 rates), payment element (processor from T020 routing; card fields are processor-hosted iframes/elements — the theme styles the container only), **coupon input and gift-card input as two distinct fields** (UF-008/009), order summary (line items incl. pre-order dates and bump items, discounts, shipping, total, taxes-included), security-badge slot, trust footer. Step structure **PENDING APPROVAL (MEDIUM)**: recommend **single-page checkout with grouped sections** (fewer templates to test, fewer abandonment cliffs; the abandoned-checkout timer of UF-007 keys off address entry, which works in either shape); alternative: classic 2-step (information → payment) matching `/checkout/ (+ steps)` in 07 §2.
- **TH-085 — Thank-you** (`/orders/<order-id>/thank-you/`, noindex, token-guarded per T028): order recap (items — provisional funnel items excluded, totals, shipping address, email confirmation notice), **`slot.thankyou_funnel`** rendering the T028 one-click upsell steps while the capture window is open (accept/decline POST-only with the single-use `upsell-act` token; widget disappears at `capture_window_expires_at` — countdown visible), storewide-discount code display when that campaign type fires (T027), no purchase-pixel re-fire on reload (FiredPixel dedup, T030).
- **TH-086 — Search** (`/search/?q=`, noindex): search input in header (`slot.header_icons`), results as the standard product-card grid + pagination, empty state with suggested collections. Search backend scope = product title/tags match (ASSUMPTION LOW; anything smarter is a later ticket).
- **TH-087 — 404**: real HTTP 404 (never soft-200 — 07 §2 notes 7,813 landings on `/404` at the reference store), localized text, search bar + featured-collections links to recover the visit.
- **TH-088 — Static pages** (FAQ, contact, quotation, policies): title + rich-text body from the static-page model, contact/quotation forms with server-side validation and no-JS fallback (TH-063). Policy pages get the T025 legal no-op hreflang override (design-pattern §XI).

### 3.6 The 3 curated themes

Binding structural contracts below; **all visual specifics (exact hexes, exact typefaces, imagery) are PENDING APPROVAL — Designer Agent proposals (11:#11).** Every theme ships 3–4 palette presets and 2–3 font pairs (the store admin's enumerated choices, TH-020).

- **TH-120 — Theme A "B2B" (Professional / B2B).**
  - Direction: clean, trust-first, catalog-dense; professional imagery does the selling.
  - Layout: full-bleed hero with overlaid headline/CTA; icon trust-band section (shipping / certifications / secure payment); **sticky bottom add-to-cart bar on mobile product pages**.
  - **TH-121** Card aspect ratio: **1:1**; product grid 2-col mobile / 4-col desktop.
  - Type: clean sans-serif display + humanist sans body.
  - **TH-122** Palette approach: cool neutral base (white/light grey) + strong professional accent per preset (e.g. deep blue / forest / slate — presets PENDING APPROVAL).
- **TH-124 — Theme B "Fashion" (Fashion & apparel).**
  - Direction: editorial, minimal chrome, imagery-led lookbook feel (matches the reference store's own fashion themes in super-admin-13: thin type, wide heroes, "Limited edition products" banners).
  - Layout: **centered-logo header** with hairline nav; tall lookbook hero; minimal borders — whitespace instead of boxes; **product-card hover swaps to second image** (toggleable, TH-020.5); large uncluttered gallery on product pages.
  - **TH-125** Card aspect ratio: **3:4 portrait**; grid 2-col mobile / 3-col desktop.
  - Type: high-contrast fashion serif or wide grotesque display + neutral sans body.
  - **TH-126** Palette approach: monochrome base (white/near-black) + single restrained accent per preset; sale badge is the only strong color.
- **TH-128 — Theme C "General" (General, average-to-high-end).**
  - Direction: premium, reassuring, conversion-oriented — "not cheap" (extra-spec). The default platform theme and the **reference theme** built first (TH-008).
  - Layout: classic left-logo header with icon cluster; hero + benefits row (guarantee / shipping / support); **most prominent review/social-proof treatment of the three** (stars on cards, review strip on home); clear boxed sections.
  - **TH-129** Card aspect ratio: **4:5**; grid 2-col mobile / 4-col desktop.
  - Type: refined neutral sans pairing (display weight + text weight).
  - **TH-130** Palette approach: cool neutral base (ink/charcoal on white) + premium accent presets (e.g. deep navy + brass; forest + copper — PENDING APPROVAL).
- **TH-132 — Conformance.** Each theme passes the same conformance suite: renders all pages of §3.5, all slots of TH-045, the full DOM contract of TH-046, and the §4 budgets. A theme is "done" only when the suite passes with the theme active.

### 3.7 Multilingual & SEO integration

- **TH-140 —** Language resolution per ADR-008 `resolve_locale` (domain/path); **language switcher** in header or footer linking each published language's URL for the *current page* via the resolver map (same source as hreflang — never independently assembled, §III). Cart/checkout/search render un-prefixed (07 §2) but localize their strings.
- **TH-141 — Consent banner slot** (`slot.consent`): pixels and any non-essential storage load only after consent where required. Banner copy/behavior policy **PENDING** — GDPR items are already an open cross-backlog blocker (10 §cross-backlog "GDPR"); the theme requirement is only that the slot exists and gates `slot.pixels`.
- **TH-142 —** Structured data (Product, Offer, AggregateRating, BreadcrumbList, WebSite+SearchAction) is emitted by the T025 layer through `seo_head`; themes supply no microdata of their own (single source, §XI).

---

## 4. Non-Functional Requirements

- **NFR-T1 — Performance budgets** (per page, storefront, mid-range phone / Fast-3G-class): LCP < 2.5 s, CLS < 0.1, theme CSS ≤ 60 KB gzipped, first-party JS ≤ 50 KB gzipped, zero render-blocking third-party requests, ≤ 2 font files above the fold. Budgets enforced by CI size checks (+ Lighthouse run in the release gate).
- **NFR-T2 — Accessibility**: WCAG 2.1 AA — semantic landmarks, keyboard-operable cart/gallery/variant selectors, focus states in every theme, alt text from catalog data, AA contrast enforced at customization time (TH-023), touch targets ≥ 44 px.
- **NFR-T3 — Security**: auto-escaping on (Jinja2 configured with autoescape); `|safe` only on server-side-sanitized rich-text fields (product description, static pages — sanitizer allowlist is the Safety Agent's to approve); no inline event handlers; the inline beacon script (TH-043) is CSP-nonce'd; storefront forms CSRF-protected; thank-you/funnel tokens per T028 (never logged, never in analytics).
- **NFR-T4 — Testability**: shared DOM contract (TH-046); theme conformance suite (TH-132); template lint for hardcoded URLs (TH-042); rendering any page with any theme + any customization must not raise (missing customization falls back per TH-022, missing theme per TH-008 — with a logged warning, never silent, §XV-1).
- **NFR-T5 — Multi-tenancy isolation**: theme rendering context receives exactly one store; a template can never read another store's data; customization records are store-scoped (StoreOwnedModel pattern).
- **NFR-T6 — No new runtime dependencies without approval** (global rule): Jinja2 is the only anticipated addition (TH-001); WhiteNoise if not already present (TH-061).

---

## 5. Component Inventory

Every component exists in all 3 themes (skinned by tokens), carries its `data-testid`, and hides (absent from DOM) when its feature is disabled.

| # | Component | Appears on | Wired to / events | Notes |
|---|---|---|---|---|
| C-01 | Announcement bar | all pages | customization text | toggleable (TH-020.5) |
| C-02 | Header (logo, nav, search, currency, cart count) | all | resolver-based menu (PK refs) | Fashion: centered logo variant |
| C-03 | Mobile hamburger nav / drawer | all (mobile) | same menu source | |
| C-04 | Breadcrumbs | product, collection, static | resolver; BreadcrumbList via T025 | toggleable |
| C-05 | Footer (menus, policies, tagline, language switcher, newsletter) | all | resolver; T034 list | |
| C-06 | Language switcher | all | resolver map (TH-140) | only published languages |
| C-07 | Currency switcher | header | T031 display conversion | display-only, AC-161 |
| C-08 | **Product card** (image, title, price+compare, stars, sale badge) | collection, search, home, related | impression + click CTR events (UF-001) | ratio per theme; Fashion hover-swap |
| C-09 | Collection grid + numbered pagination | collection, search | scroll-depth events | TH-081 |
| C-10 | Sort / minimal filter controls | collection | | full facets P3 |
| C-11 | Image gallery (swipe, thumbnails, variant switching) | product | EVT_VARIANT_SELECT image swap | |
| C-12 | Price display (+ taxes-included note, compare-at) | product, card, cart, checkout, thank-you | T031 currency | single shared partial |
| C-13 | Variant selector (dropdown / thumbnail modes) | product | EVT_VARIANT_SELECT | error state when unselected (UF-002) |
| C-14 | Quantity stepper | product, cart, floating cart | | |
| C-15 | CTA block — all inventory-mode states | product | EVT_ADD_TO_CART | add / pre-order / sold out / notify / quote (TH-082) |
| C-16 | Notify-me email form | product (Branch C) | waitlist table | dedup silently (UF-003) |
| C-17 | Quotation request form | product (Branch D), static quotation page | quotation table + admin email | no-JS POST (TH-063) |
| C-18 | Pre-order notice (date) | product, cart, checkout, thank-you | line-item flag | UF-004 |
| C-19 | Description tabs | product | Product Page toggles (06 §7) | |
| C-20 | **Review stars summary** + review list (photos, pagination) + write-review form | product; stars on cards/home | T023 | hidden at zero reviews when configured |
| C-21 | Related-products carousel | product | T027 related-products archetype | UF-011 |
| C-22 | **Order bump card** | product page, cart page, floating cart | T027 bump slots; EVT_ADD_TO_CART | 3 placements (TH-045) |
| C-23 | Floating cart / mini-cart popup | after add-to-cart | UF-006 | |
| C-24 | Cart page (line items, edit, remove, subtotal) | /cart/ | | empty state C-33 |
| C-25 | Checkout form (contact, address + Maps autocomplete slot, shipping method, payment container) | /checkout/ | T032, T020/T018/T019, 06 §15 | processor fields are hosted elements |
| C-26 | Coupon input + gift-card input (distinct) | checkout | T008 engine | UF-008/009 error/success states |
| C-27 | Order summary panel | checkout, thank-you | | provisional funnel items excluded on thank-you |
| C-28 | **Thank-you upsell funnel widget** (offer, countdown, accept/decline) | thank-you | T028 slot, tokens, POST-only | disappears at window expiry |
| C-29 | Storewide next-order discount code display | thank-you | T027 | |
| C-30 | Security badge widget | product, checkout | 06 §16 config | preset or custom image + colored text |
| C-31 | **Lead capture overlay** | any storefront page | T034: exit-intent / timer, exclusions, frequency cap | `slot.overlay` |
| C-32 | **Recent-purchase notification toast** | product (+ configurable) | T035 rolling window | GDPR-gated buyer display (PENDING) |
| C-33 | Empty states (cart, collection, search) | respective pages | | |
| C-34 | Search bar + results grid | header, /search/ | | noindex |
| C-35 | 404 recovery page | unmatched URLs | real 404 status | TH-087 |
| C-36 | Static/rich-text page shell + contact form | FAQ, contact, policies | | legal hreflang no-op |
| C-37 | Consent banner | all (where required) | gates `slot.pixels` | policy PENDING (TH-141) |
| C-38 | Amazon "Buy on Amazon" button | product | T036 FBA + affiliate ID | slot only in T029 |
| C-39 | AI chat launcher | all | T038 (P3) | slot reserved, empty in P1 |
| C-40 | Timer-promo countdown | product | promo feature | blocked on UF uncertainty #A |
| C-41 | Analytics beacon (inline JS) | all | T012 event set | TH-043; not a visual component |

---

## 6. Open Questions — PENDING APPROVAL

Items 1–4 need decisions before or at TICKET-029 implementation start; the engine + reference theme (General) can proceed on the recommendations meanwhile (per the ticket's own note).

1. **PENDING APPROVAL (CRITICAL for visuals, existing 11:#11)** — The three theme **visual designs** (final names, exact palettes/presets, typefaces, imagery, homepage section compositions per TH-080). Designer Agent to propose; this spec's structural contracts (§3.6) are the constraints. Blocks the 2 non-reference themes only.
2. **DECIDED (2026-07-05, ADR-012)** — Template engine: **Django Template Language (DTL)** — zero new dependencies, works natively with all Django admin integrations. Jinja2 rejected (extra dependency, no benefit at this scale).
3. **PENDING APPROVAL (MEDIUM, TH-060)** — Asset pipeline: **recommend plain CSS custom properties + vanilla JS, no Node build step**. Alternatives: Tailwind, Vite.
4. **PENDING APPROVAL (MEDIUM, TH-084)** — Checkout shape: **recommend single-page checkout with grouped sections**; alternative 2-step (information → payment).
5. **PENDING APPROVAL (MEDIUM, TH-007)** — Theme preview mode via signed preview token before activation: **recommend yes** (spec'd above); alternative: none in P1.
6. **PENDING APPROVAL (MEDIUM, TH-081)** — Collection pagination: **recommend numbered pagination** (SEO + meaningful scroll-% KPI) over infinite scroll / load-more.
7. **Inherited PENDING items surfacing in themes** (owned elsewhere, listed for traceability): timer-promo UX (UF critical #A → C-40); fake-inventory "X left" label (UF #B); recent-purchase buyer-data GDPR display (T035); consent-banner policy (GDPR blocker, TH-141); description-tab-name translatability (06 §7); rich-text sanitizer allowlist (NFR-T3, Safety Agent).
8. **ASSUMPTIONS (LOW) made in this spec, continue unless vetoed:** mobile-first (§1.4); `StoreThemeCustomization` split table (TH-021); contrast warn-and-confirm rather than hard block (TH-023); all storefront forms work without JS (TH-063); search = title/tags match (TH-086); card aspect ratios 1:1 / 3:4 / 4:5 (TH-121/125/129) which also fix the admin image-crop-preview ratio question (04_admin_flows).

---

## 7. Out of Scope (TICKET-029 / P1)

- **Visual-builder theme engine** (drag-and-drop) — P3, feature matrix area 25.
- **"Build new theme" flow, "Theme Store" (third-party import), theme duplication / backup copies** — P3 (buttons visible in super-admin-13; recorded, not dropped).
- **Free-form custom CSS / code editing by store admins** — contradicts the light-customization constraint; revisit only with the visual builder.
- **Custom font uploads** — curated pairs only in P1.
- **Faceted filtering (price sliders, multi-facet)** on collections — P3; P1 ships sort + minimal tag/type filter.
- **RTL languages** — layout mirroring not in P1 (launch languages EN/FR/DE per SLUG-003).
- **CDN asset serving** — architecture is CDN-ready (hashed filenames) but not configured now.
- **Blog page type** — no source screens, no ticket.
- **Per-store divergent template forks** — explicitly prohibited, not merely deferred (§1.1).
- Supplier database import/migration — global exclusion (12_out_of_scope.md).
