# 07_multilingual_seo

> Multilingual + SEO requirements — languages, domains/paths, slugs, hreflang, canonicals, indexing, sitemaps, structured data, shopping feeds per country.

**Status: DRAFT v1 (2026-07-02) — Spec Agent.** Requirements are binding unless marked PENDING. Lessons from `design-pattern-ideas.txt` §II (slug NFD), §III (permalink resolution), §XI (hreflang/canonical placement), §XII (redirect table) are translated into hard requirements here — implementers do not need to read the source document.

Requirement IDs: `ML-*` (multilingual), `URL-*`, `SLUG-*`, `CAN-*` (canonical/hreflang), `IDX-*` (indexing), `SM-*` (sitemap), `ROB-*` (robots), `SD-*` (structured data), `META-*`, `FEED-*`, `AMZ-*`, `TST-*` (tests).

---

## 1. Language and domain model

> Data model is **PENDING — Architect proposal** (uncertainty #9 in `11_uncertainties_to_validate.md`: Domain → Language → Country model). The requirements below constrain whatever model the Architect proposes.

### 1.1 Supported configurations

- **ML-001** — A store MUST support **one language per domain** (e.g. `pradize.com` = EN, `pradize.fr` = FR) **or** **multiple languages on one domain via path prefixes** (e.g. `mydomain.com/` = default lang, `mydomain.com/fr/`, `mydomain.com/de/`). Both configurations can coexist across the domains of one store.
- **ML-002** — Each domain has exactly **one default language**. The default language of a domain is served at the path root (no prefix). Non-default languages on the same domain are served under a two-letter lowercase language prefix (`/fr/`, `/de/`).
- **ML-003** — Each (domain, language) pair maps to **one or more target countries** (e.g. EN domain → ships to US + CA). Countries drive shopping feeds (§10) and hreflang availability (§4), not URLs. There is NO country in the URL (`/en-us/` style URLs are out of scope).
- **ML-004** — The resolution `request host + path prefix → (store, language, candidate countries)` MUST be a single shared function used by routing, sitemap generation, feed generation and hreflang generation. No component may re-derive language from the URL independently. *(Extends the single-resolver principle of design-pattern-ideas §III/§XV-4.)*
- **ML-005 — PENDING (Architect)** — Exact entities (Domain, SiteLanguage, ShippingCountry, join tables) and whether "country" lives on the domain, the language, or a per-store shipping matrix.

### 1.2 Adding a language

- **ML-010** — Store admin adds a language to a site in Settings (merged General Settings + Domains screen, see `part_C` admin-016 annotation "per-language settings table"). Adding a language requires choosing: (a) the language code, (b) the domain it lives on and whether it is path-prefixed or a dedicated domain, (c) the target country list for feeds.
- **ML-011** — Adding a language MUST NOT immediately expose untranslated pages. A page becomes reachable in language L only when its translation status for L is `published` (translation jobs are the highest-priority AI job per extra-spec). Until then, the page's L URL returns 404 and is excluded from sitemap and hreflang.
- **ML-012** — Removing/disabling a language MUST keep its redirect entries (§3) and MUST serve `410 Gone` (not soft-200) for previously published URLs of that language. **ASSUMPTION (LOW):** 410 chosen over 404 so crawlers deindex faster; revisit if Architect prefers 301 to default language.

### 1.3 Default / fallback language behavior

- **ML-020** — Storefront rendering NEVER silently falls back to another language for **indexable text content**. A missing translation means the page does not exist in that language (404). Rationale: mixed-language pages are duplicate/thin content and the invisible-failure class of bugs (§XV-1).
- **ML-021** — Non-content assets MAY fall back: language-specific media (translated banners/social images) resolve `exact_lang → default_lang` through a single model-level helper, never in templates (design-pattern-ideas §VI).
- **ML-022** — Requests to the bare domain root when the domain hosts multiple languages serve the domain's default language at `/`. No IP-based automatic redirect to a language version (Googlebot crawls mostly from US IPs; auto-redirects break crawling). A dismissible language suggestion banner is allowed. **ASSUMPTION (LOW).**

### 1.4 Per-country shopping feeds

- **ML-030** — One language serving multiple countries MUST produce **one feed per (feed provider, country, language)** — e.g. EN site shipping to US and CA produces `google-shopping US/en` and `google-shopping CA/en` feeds with country-appropriate shipping/availability values. Details in §10.

### 1.5 Translatable fields

| Field | Translatable? | Notes |
|---|---|---|
| Product slug | **Yes** | Per-language slug, per-language uniqueness (§3) |
| Product title | **Yes** | |
| Product description (incl. description tabs) | **Yes** | Tab names translatable too |
| Meta title (SEO page title) | **Yes** | |
| Meta description | **Yes** | |
| Image alt text | **Yes** | Product images are language-agnostic binaries; alt text is per-language |
| Menu labels (top/footer nav) | **Yes** | Menu items store object references, never URLs (§2 / §III); translation keyed by stable menu-item PK, never by position (§VII) |
| Collection name + collection slug + collection description | **Yes** | |
| Category / product Type names | **Yes** | Storefront display only; internal Type id untranslated |
| Tags | **Yes (display), No (identity)** | Tag matching for auto-collections uses the untranslated tag id; only the displayed label is translated |
| Review content (customer reviews) | **No** by default | Shown in original language with optional machine-translation toggle — **PENDING** (legal/UX); review structured data uses original language |
| Static pages (FAQ, contact, policies) | **Yes** | ~~Legal pages may opt out of alternate-language indexing (§4)~~ **DECIDED (human, 2026-07-10):** legal/policy pages are indexed in every published language like any static page; only hreflang is opted out (§4, CAN-002 amended). |
| Variant option names/values (Size, Color…) | **Yes** | Keyed by stable option-value PK (§VII) |
| Campaign step offer content (`CampaignStep`: title, description, CTA label) | **Yes** | Source keys `title`/`description`/`cta_label` in `offer_config_json`; translated via **AiJob CLI** (job type `campaign_step_translation`, priority 100 — ADR-009 Appendix B, approved 2026-07-04), never direct API. Stored in `CampaignStepTranslation`, keyed by stable step PK + `lang_code` (§VII); only `status='published'` translations are shown (ML-041) |
| SKU, vendor, internal ids, order data | **No** | |
| Coupon/gift-card codes | **No** | |
| Prices / currency | **Not a translation concern** | Currency handled by currency module, per-domain default — see feature matrix |

- **ML-040** — Translations live in per-language translation tables (`ProductTranslation(product, lang, slug, title, description, meta_title, meta_description)` shape — final schema PENDING Architect). The base model holds no display text other than the default language or internal identifiers. Translated and source content are never mixed in one row (design-pattern-ideas §I).
- **ML-041** — Every translation record carries an explicit status (`pending / in_progress / published / error`). Absence of a record is not a state (§XV-3). Only `published` translations are routable, sitemapped, hreflang'ed, or fed.

---

## 2. URL rules per page type

`[lang/]` below means: empty for the domain's default language, `fr/` etc. for path-prefixed languages, absent on dedicated single-language domains.

| Page type | URL pattern | Slug source | Multilingual variant | Indexable (see §5) |
|---|---|---|---|---|
| Home | `/[lang/]` | n/a (path root) | per-language home at the language root | Yes |
| Product (primary) | `/[lang/]<product-slug>/` | translated product slug | per-language slug | Yes |
| Product A/B image-variant | `/[lang/]<product-slug>-<variant-slug>/` | primary slug + variant suffix | follows primary's language | Served, canonical → primary, NOT in sitemap |
| Collection | `/[lang/]collections/<collection-slug>/` | translated collection slug | per-language slug; `collections` path segment itself is NOT translated (**ASSUMPTION, LOW** — keeps routing trivial, matches Shopify convention) | Conditional |
| Search | `/search/?q=<term>` | n/a | not translated (works on any lang prefix but noindex) | No |
| Cart | `/cart/` | n/a | not translated | No |
| Checkout | `/checkout/` (+ steps) | n/a | not translated | No |
| Thank-you | `/orders/<order-id>/thank-you/` | n/a | not translated | No (noindex) |
| Static page (FAQ, contact, quotation form, policies) | `/[lang/]<page-slug>/` | translated page slug | per-language slug | ~~Yes (except legal opt-outs)~~ **Yes, including legal/policy pages — DECIDED (human, 2026-07-10; see CAN-002 amended, §4)** |
| 404 page | any unmatched URL, HTTP 404 | n/a | localized text | No (real 404 status, never soft-200 — the reference store had 7,813 landings on `/404`) |
| robots.txt | `/robots.txt` | n/a | one per domain | n/a |
| Sitemap | `/sitemap.xml` (index) + `/sitemap-<lang>.xml` | n/a | per-language child sitemaps | n/a |
| Feeds | `/feeds/<provider>/<country>-<lang>.xml` | n/a | per (provider, country, lang) | No (Disallow'd; fetched by feed consumers directly) |

- **URL-001** — Product and static-page slugs live at the language root (no `/products/` segment) to match the reference store's flat URLs (`/dresses`, `/faq`). Slug namespace collision between products and static pages is prevented by the shared per-language uniqueness rule (**SLUG-020**). Reserved path segments (`cart`, `checkout`, `orders`, `search`, `collections`, `admin`, `superadmin`, `feeds`, `robots.txt`, `sitemap*`) are rejected as slugs.
- **URL-002** — All indexable URLs end with a trailing slash; the non-slash form 301s to the slash form (one canonical spelling per resource). No `.html` suffixes.
- **URL-003 (§III as requirement)** — A single function `resolve_permalink(source_object_or_url, target_lang) → translated_url` is built **once per rendering context** as a map `{source: {lang: url}}`. EVERY component that emits an internal href — menus, breadcrumbs, product cards on collection pages, related products, hreflang tags, canonical tags, sitemap entries, feed `link` fields — calls this resolver. **Nothing stores a pre-resolved URL in a database field.** Menu items, collection membership and internal links reference objects by stable PK; slugs are presentation, IDs are identity.
- **URL-004** — A/B image-variant URLs are first-class routable URLs (social crawlers must get HTTP 200 with full OG tags), but are invisible to search: canonical → primary (**CAN-020**), excluded from sitemap (**SM-030**), and carry `<meta name="robots" content="noindex">`? — **No**: canonical only, no noindex, so that link equity from Pinterest consolidates to the primary. **ASSUMPTION (LOW):** canonical-without-noindex is the standard consolidation pattern; noindex would block equity flow.
- **URL-005** — Search keyword paths like `/search/<term>` (seen in the reference landing-page report) are NOT reintroduced; only `/search/?q=` exists. Legacy keyword paths, if imported, get redirect entries.

---

## 3. Slug generation rules

- **SLUG-001 (binding — §II as requirement)** — All slugs, in every language, for every model (product, collection, static page, blog if added, variant suffix), MUST be produced by **one single shared slugify function** with this exact algorithm:
  1. Lowercase the input.
  2. **Unicode-normalize to NFD** (decompose accented characters into base letter + combining mark).
  3. Remove all combining marks (Unicode category `Mn`).
  4. Replace every run of remaining non-`[a-z0-9]` characters with a single `-`.
  5. Strip leading/trailing `-`.
  
  Rationale: without step 2, "Santé mentale" yields `sant-mentale` (the base letter is stripped with the combining mark) — a bug that appeared only for accented FR/ES/PT names and only as silent 404s. Django's built-in `slugify()` is NOT sufficient on its own for all locales; the NFD step is mandatory.
- **SLUG-002** — The **redirect subsystem and the slug generator MUST use this same function** — single source of truth. A normalization divergence between them creates orphaned redirects pointing at slugs that never exist.
- **SLUG-003** — Languages whose NFD-stripped form is empty or lossy (CJK, Arabic, Cyrillic…) — **PENDING**: either allow transliteration tables per language or fall back to `product-<id>`. Not needed for launch languages (EN/FR/DE assumed). Mark in admin as "slug could not be generated" rather than silently emitting an empty slug.

### Uniqueness

- **SLUG-020** — Slug uniqueness is **per (store, language)** across the flat-namespace models (products + static pages share the root namespace; collections have their own namespace under `/collections/`). Global cross-language uniqueness is NOT required — `/fr/robe-rouge/` and `/de/robe-rouge/` may coexist.
- **SLUG-021** — On collision, append `-2`, `-3`, … (first free suffix). The admin form shows the final slug before save. AI translation jobs that propose an already-taken slug get the suffixed slug applied automatically and the job output is still accepted (no ERROR state for collisions).

### Slug change policy (§XII as requirement)

- **SLUG-030** — On every save of every model that has a slug field, a **pre-save hook** compares old vs new slug. If the slug changes AND the old slug was ever published, a `Redirect(old_path → new_path, type=301, origin=auto_slug_change)` is created **automatically, in the signal/hook layer — not in the view/form** — so it fires for admin edits, API updates, AI translation jobs, and bulk imports alike. This applies per language (changing the FR slug creates a FR redirect).
- **SLUG-031** — If the page has been **published for more than 7 days**, the admin UI additionally requires an explicit confirmation step before accepting the slug change ("this URL is likely indexed; a 301 redirect will be created"). It must be impossible to change a published slug without the redirect being handled. API/AI paths skip the interactive confirmation but never skip the redirect creation.
- **SLUG-032** — Redirect chains are collapsed **at write time**: when creating `B→C` and `A→B` exists, rewrite it to `A→C`. Request-time chain walking is forbidden.
- **SLUG-033** — The redirect table supports types `301` (slug change), `302` (parked domain → main domain), and **`none`** ("intentionally dead, serve 404, never redirect") to prevent a reused slug from accidentally inheriting an old page's redirect. Deleting a page whose slug is later reused MUST NOT resurrect old redirects.
- **SLUG-034** — The redirects admin screen shows origin (`auto_slug_change` vs `manual` vs `domain_parking`) and creation date. Manual redirects warn before deletion; auto-generated ones are bulk-deletable.

---

## 4. Canonical and hreflang rules

- **CAN-001 (§XI as requirement)** — Canonical, hreflang, `og:locale` and `og:locale:alternate` tags are generated in **ONE block of the base template** (`{% block seo_head %}` with a default implementation) that every page type extends. Per-page-type behavior is an **override of that block**, never a reimplementation. Rationale: when hreflang lived in article-type-specific code, every other page type silently lacked it.
- ~~**CAN-002** — Per-type opt-out via override: legal pages (privacy policy, terms) render **no hreflang** and `noindex` in alternate languages — the default-language legal page is the only indexable one. **ASSUMPTION (LOW):** legal text is kept in the store's default language on international domains.~~
  **CAN-002 — DECIDED (human, 2026-07-10; resolves SEO Agent finding H3, `docs/seo/STATIC_PAGES_SEO_REVIEW.md`, matching shipped ADR-018 behavior):** Per-type opt-out via override applies to **hreflang only**, not to indexing. Legal/policy pages (`StaticPage.kind = POLICY`) render **no hreflang** in any language (self and alternates — the exemption is symmetric, not just "alternate languages"), via the `hreflang_exempt` property derived from a single source, `HREFLANG_EXEMPT_KINDS` (`pages/models.py`), consumed identically by the page-level `{% hreflang_tags %}` tag and by the sitemap builder (parity requirement, **CAN-005**). Policy pages carry **no `noindex`** and are indexable in **every language with a `published` `StaticPageTranslation`**, exactly like any other static page (§5) — there is no "default-language-only" restriction. Rationale for reversing the original ASSUMPTION (LOW): Pradize policy-page bodies are genuinely translated per language via the AiJob CLI pipeline, not boilerplate legal text copy-pasted across locales, so the duplicate-content concern that motivated the original noindex rule does not apply.
- **CAN-003** — Every indexable page emits a **self-referencing canonical** with the absolute URL (scheme + domain + path, trailing slash form).
- **CAN-004** — Hreflang is emitted **only** for pages where ALL of: (a) translation status = `published` in the target language, (b) target language ∈ **translated-languages ∩ available-countries** for that object — a product shipped only to FR and DE emits hreflang `fr` and `de`, never `en`, (c) the target-language URL itself returns 200 **and is indexable** — a collection that is empty in the target language (0 matching products → 404 per §5) or below its quality threshold (`noindex`, **CAN-021**) is excluded from the hreflang group even though its translation record is `published`. **Never emit an hreflang pointing to a URL that would return 404 or carry `noindex` for the target language/country.** If exclusions leave only one member in the group, no hreflang is emitted at all (**CAN-007**).
- **CAN-005** — The hreflang set is derived from the same `resolve_permalink` map as every other link (**URL-003**) — hreflang has no independent resolution logic. The published-translations query is cached per page render context (one query, not one per tag).
- **CAN-006** — Each hreflang group includes `x-default` pointing to the **default-language URL** of the page.
- **CAN-007** — hreflang entries must be reciprocal by construction: since all languages' tags are generated from the single shared map, page X's FR version and EN version always list each other. A page with only one published language emits **no hreflang at all** (a self-only group is noise). **ASSUMPTION (LOW).**
- **CAN-020** — A/B image-variant product URLs emit `canonical → primary product URL` (same language). Variant URLs emit **no hreflang** (only the primary participates in the language group) and no sitemap entry.
- **CAN-021** — Pages that must have **neither hreflang nor indexable canonicals**: cart, checkout, thank-you, search, 404, and any page carrying `noindex`. A noindex page never appears as an hreflang target of another page.
- **CAN-022** — Paginated collection pages (`?page=2`): each page self-canonicalizes (page 2 canonical = page 2 URL, not page 1); `rel=prev/next` optional. **ASSUMPTION (LOW)** — Google's current guidance.

---

## 5. Indexing rules per page type

Default posture: **do NOT index duplicate or thin pages.** Every "Conditional" row has an explicit quality threshold; thresholds are store-level settings with the defaults below.

| Page type | Indexable? | Conditions / quality threshold |
|---|---|---|
| Home (per language) | **Yes** | Language is published on the domain (**ML-011**). No quality threshold: the home page is always indexable once its language is published. Participates in hreflang like any other page (§4). |
| Product (primary) | **Conditional** | Translation `published` in that language AND description ≥ 200 characters AND ≥ 1 image AND product visible/active. Sold-out products stay indexed (availability changes; URL equity persists) unless admin hides the product. **ASSUMPTION (LOW)** on the 200-char threshold. |
| Product A/B variant URL | **No (canonicalized)** | Served 200 for social crawlers; canonical → primary; not in sitemap (FM-C4 decision). |
| Collection | **Conditional** | ≥ **3** matching active products. Below threshold: page stays reachable but `noindex`; at **0** products the page returns 404 or redirects to parent (never render an empty collection — "empty hub" lesson, §V). Threshold configurable per store; default 3. **ASSUMPTION (LOW)** on the value 3. |
| Static page (FAQ, contact, quotation) | **Yes** | Published in that language. |
| Legal pages (privacy, terms) | ~~**Conditional**~~ **Yes** | ~~Indexable in default language only; `noindex` + no hreflang in alternate languages (**CAN-002**).~~ **DECIDED (human, 2026-07-10):** indexable in every language with a `published` translation, exactly like any static page — no `noindex`, no "default-language-only" restriction. Only hreflang is suppressed, in every language including the default (**CAN-002**, amended). |
| Cart | **No** | `noindex` meta + robots.txt Disallow. |
| Checkout (all steps) | **No** | `noindex` + Disallow. |
| Thank-you | **No** | `noindex` (contains order PII; also Disallow `/orders/`). |
| Search results | **No** | `noindex` + Disallow (infinite thin URL space). |
| 404 | **No** | Real HTTP 404 status; never a soft 200. |
| Feeds, robots, sitemap | n/a | Machine endpoints. |
| Admin / superadmin | **No** | Disallow + authentication; `X-Robots-Tag: noindex` header. |

- **IDX-001** — `noindex` is emitted as `<meta name="robots" content="noindex">` from the same base-template block (§4) so no page type can forget it.
- **IDX-002** — Reviews appear in a product's structured data only above a threshold (see **SD-003**); review content itself is on the product page (no separate per-review URLs — no thin review pages).
- **IDX-003** — Faceted/filtered/sorted collection URLs (query parameters like `?sort=`, `?color=`) are `noindex` (canonical → unfiltered collection URL). Only clean collection URLs (with optional `?page=N`) are indexable.

---

## 6. Sitemap rules

- **SM-001** — `/sitemap.xml` per domain is a **sitemap index** referencing one child sitemap per published language on that domain: `/sitemap-en.xml`, `/sitemap-fr.xml`, …. Single-language domains still use the index + one child (uniform pipeline).
- **SM-002** — Each URL entry carries its **hreflang alternates inline** (`xhtml:link rel="alternate"`) generated from the same `resolve_permalink` map — sitemap and page-level hreflang can never disagree because they share one source.
- **SM-010** — Included page types: indexable products (primary URLs only), collections meeting their quality threshold, indexable static pages, the language home page. `lastmod` = last content-affecting update.
- **SM-020** — Update trigger: **event-driven, debounced**. Publish/unpublish, slug change, translation publish, and collection-membership changes mark the sitemap dirty; a background job regenerates it at most every N minutes (default 15) plus a daily full rebuild as safety net. Never regenerated inline in a web request. **ASSUMPTION (LOW)** on the 15-min debounce.
- **SM-030** — Exclusions: A/B variant URLs, any noindex page (cart, checkout, thank-you, search, filtered collection URLs, ~~alternate-language legal pages~~), unpublished translations, below-threshold collections, 404'd languages. **DECIDED (human, 2026-07-10):** alternate-language legal/policy pages are removed from this exclusion list — their `loc` is present in the sitemap like any other published static page. Only their **hreflang alternates** (`xhtml:link rel="alternate"` entries) are omitted from the sitemap entry, consistent with the CAN-002 amendment (§4).
- **SM-031** — Sitemap URLs and canonical URLs must be byte-identical (same host, trailing slash, no query strings). A sitemap entry whose canonical points elsewhere is a build error, not a warning.

---

## 7. robots.txt rules

- **ROB-001** — Each domain serves a generated `/robots.txt` with these default rules:
  ```
  User-agent: *
  Disallow: /cart/
  Disallow: /checkout/
  Disallow: /orders/
  Disallow: /search
  Disallow: /admin/
  Disallow: /superadmin/
  Disallow: /feeds/
  Sitemap: https://<domain>/sitemap.xml
  ```
  (Language-prefixed variants `/fr/cart/` are covered because cart/checkout/search are not language-prefixed — see §2; if a theme links prefixed utility URLs anyway, they 301 to the unprefixed form.)
- **ROB-002** — The store admin can **append** custom lines via the "ROBOTS.TXT INCLUDE TEXT" textarea in General Settings (admin-016). Custom text is appended after the engine defaults; the engine defaults cannot be removed through the textarea. Conflicting custom rules are the admin's responsibility (free-text passthrough, validated only for size/charset).
- **ROB-003** — robots.txt is per-domain (a path-prefixed multilingual domain has ONE robots.txt covering all its languages).
- **ROB-004** — A/B variant URLs are NOT disallowed in robots.txt — crawlers must be able to fetch them to see the canonical tag, and social crawlers must reach them.

---

## 8. Structured data (JSON-LD) per page type

- **SD-001** — All JSON-LD is emitted from a per-page-type provider hooked into the same base-template block family as §4 (one injection point). All text values use the page's language; all URLs go through `resolve_permalink`.
- **SD-002 — Product page**: `Product` with `name`, `image` (all gallery images), `description`, `sku` (if enabled), `brand` (vendor, if enabled); `Offer` with `price`, `priceCurrency` (domain currency), `availability` (mapped from the inventory mode: InStock / OutOfStock / PreOrder for pre-order products), `url` (primary canonical URL), and `priceValidUntil` **when a timed promo is active** (promo end date). Ask-for-quotation products emit `Offer` with no `price` — **PENDING**: schema.org options (`priceSpecification` absent vs `ContactSales`) to be settled when the quotation flow is spec'd (FM-C3).
- **SD-003 — AggregateRating / Review**: included on the product only when the product has ≥ **3 published reviews** (below that, ratings are statistically meaningless and look manipulated). Individual `Review` items: up to the 10 most recent published reviews. Reviews flagged as AI-generated are **excluded from structured data** (compliance risk noted in Part B uncertainties). **ASSUMPTION (LOW)** on thresholds 3 and 10.
- **SD-004 — BreadcrumbList**: on product pages (Home → Collection → Product, using the product's primary collection) and on collection pages (Home → Collection). Breadcrumb URLs come from `resolve_permalink` (§III — breadcrumbs are a known wrong-language regression point).
- **SD-005 — Collection page**: `BreadcrumbList` always; `ItemList` of the first page of products **optional, default off** — **ASSUMPTION (LOW):** ItemList adds page weight for little benefit; enable per store if product-collection rich results become a goal.
- **SD-006 — Organization**: emitted on every page, from store settings (store name, logo, main domain URL, social profile links when the requested social-sharing settings exist). One block, base template.
- **SD-007 — Checkout / cart / thank-you / search**: **no structured data** (they are noindex; markup there is wasted bytes and PII risk).
- **SD-008** — `WebSite` + `SearchAction` (sitelinks search box) on the home page only. **ASSUMPTION (LOW).**

---

## 9. Meta title and description rules

- **META-001** — Formulas per page type (rendered value when no manual override). ~~Separator was specified as an em dash (`—`)~~ — **DECIDED (human, 2026-07-10; resolves SEO Agent finding L1, `docs/seo/STATIC_PAGES_SEO_REVIEW.md`):** the separator is **`|` (pipe)**, matching the shipped templates verbatim — `static_page.html`, `product.html`, `collection.html`, `cart.html`, `checkout.html`, `checkout_payment.html`, `checkout_retry.html`, `checkout_retry_expired.html`, `thank_you.html` and `404.html` all render `<title>{{ page_title }} | {{ store_name }}</title>`. Chosen to keep zero code churn rather than change nine templates to match the original spec text:

| Page type | Title formula | Description fallback |
|---|---|---|
| Product | `<product title> \| <store name>` | First ~155 chars of plain-text product description |
| Collection | `<collection name> \| <store name>` | Collection description, else "Shop <collection name> at <store name>." (translated template string) |
| Static page | `<page title> \| <store name>` | First ~155 chars of page content |
| Home | `<store name> \| <store tagline>` (tagline from settings) | Store description from settings |
| Cart/checkout/search/thank-you/404 | `<function label> \| <store name>` (translated) | none needed (noindex) |

- **META-002** — Per-product/collection/page **manual overrides**: `Page Title (SEO)` and `Meta Description` fields (as on admin-021-02). Empty override → formula fallback. Overrides are **translatable fields** (§1.5) — each language has its own pair; a missing translated override falls back to the formula applied to the **translated** title, never to the default-language override text (no mixed-language snippets, **ML-020**).
- **META-003** — Admin form shows live length counters and **warns** (does not block) beyond **60 characters** for meta title and **160 characters** for meta description. AI translation jobs receive the same limits in their instructions; over-length AI output is accepted with a warning flag on the translation record.
- **META-004** — `og:title`, `og:description`, `og:image`, `og:url` (= canonical), `og:locale` derive from the same resolved values in the same base block; the requested per-network social-sharing overrides (admin-021-02 annotation) extend, not replace, this block. A/B variant URLs override only `og:image`/`og:url` (their own URL — Pinterest needs distinct pins) while keeping canonical → primary.

---

## 10. Shopping feeds (catalog feeds)

Providers at launch: **Facebook Dynamic Product Feed** and **Google Shopping** (admin-013 + owner annotation "add also google + pinterest"; Pinterest feed = same item schema as Google, marked **PENDING** for v1 scope).

- **FEED-001** — Feed matrix: one feed file per **(provider, country, language)** where country ∈ the domain-language's target countries (**ML-003/ML-030**). EN site shipping US + CA → 2 Google feeds + 2 FB feeds. URL scheme: `/feeds/<provider>/<country>-<lang>.xml`, unauthenticated but Disallow'd in robots.txt, with a per-store secret token query param to deter scraping — **ASSUMPTION (LOW)**.
- **FEED-002 — Required fields per item (both providers)**: `id` (product PK + variant suffix — stable, never the slug), `title` (translated), `description` (translated, plain text), `link` (primary product URL via `resolve_permalink` — never an A/B variant URL), `image_link` (+ `additional_image_link`), `price` with currency, `availability` (mapped from inventory mode), `condition` (`new` default), `brand` (vendor), `gtin`/`mpn` if present else `identifier_exists=false` (Google), `item_group_id` for multi-variant products, `google_product_category` — **PENDING**: category mapping source (manual per product Type vs AI job).
- **FEED-003 — Per-country differences within one language**: `link` may carry a country-tracking parameter; `price` in the feed country's **display currency** (**FEED-008**); `shipping` blocks per country (from the shipping module); `availability` reflects whether the product ships to that country — a product not shipped to CA is **excluded from the CA feed entirely**.
- **FEED-004 — Exclusions** (per item, evaluated at generation): ask-for-quotation products (no fixed price), products with zero images, unpublished/hidden products, products not shipped to the feed's country, `always SOLD OUT` inventory-mode products. Out-of-stock (trackable inventory at 0): **included with `availability: out of stock`** rather than dropped (keeps ad item history) — **ASSUMPTION (LOW)**. Pre-order products included with `availability: preorder` + availability date.
- **FEED-005 — Trigger & frequency**: same debounced event-driven model as the sitemap (**SM-020**) — product/price/inventory/translation changes mark affected feeds dirty; regeneration job runs at most every 30 min with a nightly full rebuild. The admin feed screen shows explicit status per feed: `up_to_date / regenerating / stale / error` with last-generated timestamp (state is explicit, §XV-3 — the reference UI's green "Your feed is up to date" banner becomes a real status, not decoration).
- **FEED-006** — "Products to sync" scoping (all products / selected collections) as in admin-013, applied per store before per-item exclusions.
- **FEED-007** — Timed promos (timer promo annotation, admin-021-02) export as `sale_price` + `sale_price_effective_date` (Google) / `sale_price` (FB) rather than mutating `price`.
- **FEED-008 — Per-country feed currency (DECIDED — resolves FEED-C1)**: each feed carries prices in its **target country's display currency**, regardless of the domain's transactional currency — an EN/USD domain shipping US + CA produces a US feed in USD and a **CA feed in CAD**. The conversion uses the currency module's per-country display-currency rates (display-only conversion; the checkout/transactional currency is unchanged — this is the "display-only" branch of the Part B multi-currency decision). `price` **and** `sale_price` (**FEED-007**) are both converted, and each item's currency code matches the feed country's display currency (Google Merchant rejects a CA-targeted feed priced in USD without Merchant-side conversion; we do not rely on Merchant Center currency conversion). Rounding uses the currency module's per-currency rounding rules; feed regeneration (**FEED-005**) is additionally marked dirty by display-rate changes.

---

## 11. Amazon "Buy on Amazon" links

- **AMZ-001** — The "Buy on Amazon" button renders on a product page only when ALL of: (a) the product is linked to an Amazon listing with **FBA inventory currently in stock** (per the FBA sync), (b) the super-admin setting "Add Buy on Amazon button" is enabled, (c) an Amazon affiliate ID is configured for the store/domain (super-admin).
- **AMZ-002** — The link is an external affiliate link and MUST carry `rel="nofollow noopener"` and `target="_blank"`. `sponsored` is added alongside `nofollow` (Google's attribute for paid/affiliate links): `rel="nofollow sponsored noopener"`.
- **AMZ-003** — The engine MUST NOT create any crawlable page, redirect endpoint, or sitemap entry for Amazon products. The affiliate URL appears exactly in one place: the button on the product page. No `/go/amazon/<id>` internal redirector unless it is Disallow'd and noindex'd — **ASSUMPTION (LOW):** direct outbound link, no redirector, for v1.
- **AMZ-004** — Affiliate marketplace/tag selection follows the domain's main country (FR domain → amazon.fr tag) — **PENDING**: per-country tag mapping model (ties into ML-005).

---

## 12. SEO test requirements (acceptance criteria)

Each criterion is a black-box test against rendered HTML / HTTP responses / generated XML. IDs map to the rules above.

**Language & routing**
- **TST-ML-011** — Given product P translated to FR with status `pending`, when requesting `/fr/<fr-slug>/`, then the response is 404 and P's FR URL appears in no sitemap and no hreflang group.
- **TST-ML-020** — Given P published in EN but not DE, when rendering P's EN page, then no DE hreflang is emitted and no DE text appears anywhere in the EN page.
- **TST-ML-012** — Given language DE disabled after publication, when requesting a previously published DE URL, then the response is 410 and the URL is absent from `/sitemap.xml` children.

**URL & permalink resolution**
- **TST-URL-003a** — Given a FR page whose menu items reference objects with FR slugs, when rendering, then every internal `<a href>` on the page resolves to the FR URL (zero default-language URLs in nav, breadcrumbs, product cards, footer).
- **TST-URL-003b** — Given the same page rendered in EN and FR, when comparing hreflang tags, sitemap alternates and breadcrumb URLs, then all three agree byte-for-byte for each language (single-resolver property).
- **TST-URL-002** — Given any indexable URL requested without trailing slash, when fetched, then a single 301 to the trailing-slash form is returned (no chain).

**Slugs & redirects**
- **TST-SLUG-001** — Given a product titled `Santé mentale`, when the slug is generated, then it equals `sante-mentale` (not `sant-mentale`); same for `Müller Größe` → `muller-grosse` and `São João` → `sao-joao`.
- **TST-SLUG-021** — Given an existing slug `red-dress` in FR, when a second FR product slugifies to `red-dress`, then it is stored as `red-dress-2` and both URLs return 200.
- **TST-SLUG-030** — Given a published product whose slug changes from `old-a` to `new-b` via the API (not the admin form), when saving, then GET `/old-a/` returns 301 → `/new-b/` .
- **TST-SLUG-032** — Given redirect `a→b` exists, when the slug changes again `b→c`, then GET `/a/` returns 301 directly to `/c/` (single hop), and GET `/b/` returns 301 → `/c/`.
- **TST-SLUG-033** — Given a deleted page with redirect type `none` on `/dead/`, when a new page reuses slug-adjacent URLs, then GET `/dead/` returns 404 (never redirects to the new page).
- **TST-SLUG-031** — Given a product published 8 days ago, when the admin edits its slug, then the save is blocked until the redirect confirmation is acknowledged; given a product published 2 days ago, no confirmation is required but the 301 is still created.

**Canonical & hreflang**
- **TST-CAN-003** — Given any indexable page, when rendered, then exactly one `<link rel="canonical">` exists and equals the page's own absolute URL.
- **TST-CAN-004** — Given product P published in EN/FR/DE but shipped only to FR and DE, when rendering P in FR, then hreflang tags are exactly {fr, de, x-default} and no `en` entry exists.
- **TST-CAN-006** — Given P's hreflang group, then `x-default` equals P's default-language URL.
- **TST-CAN-020** — Given A/B variant URL `/dress-v2/` of primary `/dress/`, when fetched, then HTTP 200, canonical = `/dress/`, own `og:url`/`og:image`, zero hreflang tags, and `/dress-v2/` appears in no sitemap.
- ~~**TST-CAN-002** — Given the privacy policy in FR (alternate language), when rendered, then it carries `noindex` and no hreflang; the EN (default) version has neither restriction.~~
  **TST-CAN-002 — AMENDED (DECIDED, human, 2026-07-10; resolves SEO Agent finding H3):** Given the privacy policy published in EN (default) and with a `published` FR translation, when either language is rendered, then **both** return HTTP 200 with **no `noindex`**, **no hreflang tags at all** on the page (policy pages are hreflang-exempt in every language, not just alternates), and **no `<meta name="robots">` restriction**; when the full sitemap is generated, **both** the EN and FR URLs appear with `loc` present and **zero `xhtml:link rel="alternate"` entries**. Covered in code by `pages/tests/test_seo_parity.py` (`test_policy_page_hreflang_tags_empty`, `test_policy_page_sitemap_alternates_empty`, `test_policy_page_stays_in_sitemap_loc_list`) — no separate TST-CAN-002 test is added to avoid duplicating that coverage.
- **TST-CAN-001** — Given a newly added page type using the base template without overrides, when rendered, then canonical + og:locale are present (base-block default fires).

**Indexing**
- **TST-IDX-cart** — Given `/cart/`, `/checkout/`, `/search/?q=x`, and a thank-you URL, when fetched, then each contains `noindex` and none contains hreflang or JSON-LD.
- **TST-IDX-404** — Given an unknown URL, when fetched, then HTTP status is 404 (not 200 with a "not found" body).
- **TST-IDX-collection** — Given a collection matching 2 products (threshold 3), when fetched, then 200 + `noindex`; given 0 products, then 404 (or configured redirect to parent); given 3 products, then indexable with no `noindex`.
- **TST-IDX-003** — Given `/collections/dresses/?sort=price`, when rendered, then `noindex` + canonical → `/collections/dresses/`.

**Sitemap**
- **TST-SM-001** — Given a domain with EN(default)+FR, when fetching `/sitemap.xml`, then it is an index referencing `/sitemap-en.xml` and `/sitemap-fr.xml`, and each entry in children carries reciprocal xhtml:link alternates.
- **TST-SM-030** — Given the full sitemap contents, then no URL is an A/B variant, a noindex page, a below-threshold collection, or an unpublished translation.
- **TST-SM-020** — Given a product publish event, when the debounce window elapses, then the sitemap contains the new URL without manual action.

**robots.txt**
- **TST-ROB-001** — Given `/robots.txt`, then it contains Disallow lines for `/cart/`, `/checkout/`, `/orders/`, `/search`, `/admin/`, `/superadmin/`, `/feeds/` and a `Sitemap:` line with the domain's absolute sitemap URL.
- **TST-ROB-002** — Given custom text "Disallow: /promo-test/" saved in General Settings, then robots.txt contains both all default lines and the custom line (defaults not removable).

**Structured data**
- **TST-SD-002** — Given an in-stock product with an active timed promo ending on date D, when rendered, then JSON-LD `Offer` has `price`, `priceCurrency`, `availability=InStock`, `priceValidUntil=D`.
- **TST-SD-003** — Given a product with 2 published reviews, then no `AggregateRating` is emitted; with 3, it is; AI-flagged reviews never appear in `Review` items.
- **TST-SD-004** — Given a FR product page, then `BreadcrumbList` item URLs are all FR URLs.
- **TST-SD-007** — Given the thank-you page, then the response body contains no `application/ld+json` block.

**Meta**
- ~~**TST-META-001** — Given product "Blue Gown" with empty SEO title in store "Pradize", then `<title>` = "Blue Gown — Pradize"; given a manual override, the override wins verbatim.~~ **DECIDED (human, 2026-07-10, separator amendment):** Given product "Blue Gown" with empty SEO title in store "Pradize", then `<title>` = "Blue Gown | Pradize"; given a manual override, the override wins verbatim.
- **TST-META-002** — Given a FR translation with translated title but empty FR meta-description override, then the FR description falls back to the FR description text, never to the EN override.
- **TST-META-003** — Given a 75-char title in the admin form, then a warning is displayed and save still succeeds.

**Feeds**
- **TST-FEED-001** — Given an EN domain shipping US+CA, then 2 Google feeds exist; a product excluded from CA shipping appears in the US feed and not in the CA feed.
- **TST-FEED-002** — Given any feed item, then its `link` equals the product's primary canonical URL (never a variant URL) and its `id` is PK-based (unchanged after a slug edit).
- **TST-FEED-004** — Given an ask-for-quotation product and a zero-image product, then neither appears in any feed; given a tracked product at 0 stock, it appears with `availability: out of stock`.
- **TST-FEED-007** — Given an active timer promo, then the feed item carries `sale_price` (+ effective dates for Google) while `price` stays at the base price.
- **TST-FEED-008** — Given an EN domain with transactional currency USD shipping US+CA, then every item in the US feed is priced in `USD` and every item in the CA feed is priced in `CAD` (display-currency conversion applied to both `price` and `sale_price`), while the storefront checkout still charges USD.

**Amazon**
- **TST-AMZ-001** — Given FBA stock present + setting enabled + affiliate ID set, then the button renders with `rel="nofollow sponsored noopener"`; remove any one condition and the button is absent.
- **TST-AMZ-003** — Given the full sitemap and all internal links, then no URL exists whose target is an Amazon page.

---

## Open items in this document

| ID | Level | Item |
|---|---|---|
| FEED-C1 | **DECIDED** (was NEW CRITICAL) | Per-country feed currency: **resolved by FEED-008** — each feed is priced in its target country's display currency (CA feed → CAD) via the currency module's display-only conversion; transactional currency unchanged. No longer blocks FEED-003. |
| ML-005 | PENDING (blocks schema, not requirements) | Domain→Language→Country data model — Architect (uncertainty #9) |
| SLUG-003 | PENDING | Non-Latin-script slug strategy (transliteration vs `product-<id>`) |
| Review translation (§1.5) | PENDING | Machine-translated review display — legal/UX |
| SD-002 quotation | PENDING | JSON-LD Offer shape for ask-for-quotation products (depends on FM-C3) |
| FEED-002 | PENDING | `google_product_category` mapping source (manual vs AI job) |
| Pinterest feed | PENDING | v1 scope for the annotated Pinterest shopping feed |
| AMZ-004 | PENDING | Per-country Amazon marketplace/affiliate-tag mapping |

---

## SEO Review Notes

*SEO Agent pre-implementation sign-off (2026-07-03) against ADR-005, tickets T016/T024/T025/T026, and design-pattern-ideas §II/§III/§XI/§XII. High-severity corrections were applied inline (Home page rows in §2/§5, CAN-004 condition (c), FEED-008 + FEED-C1 resolution + TST-FEED-008). Medium/low observations below — none blocks implementation.*

1. **(Medium) Redirect loop rejection missing from §3.** ADR-005 §6 states "Loop creation is rejected at save"; SLUG-032 specifies write-time chain collapse but is silent on loops (A→B then B→A). T016 implements ADR-005, so behavior is covered, but SLUG-032 should state loop rejection so a spec-only reader cannot miss it. Suggested addition to SLUG-032: "Creating a redirect that would form a loop is rejected at save."
2. **(Medium) Chain collapse is specified in one direction only.** SLUG-032 covers rewriting existing `A→B` when `B→C` is created. ADR-005 additionally covers the other insertion order: creating `A→B` when `B→C` already exists must write `A→C` directly. Implementers of T016 should follow ADR-005's bidirectional wording; TST-SLUG-032 only exercises the first direction — add a test for the second.
3. **(Medium) Gift-card pages.** ADR-005 §1 lists "gift-card pages" among slug producers, but this document's §2 URL table and §5 indexing table have no gift-card page type. If gift-card landing pages exist as routable storefront pages, they need a URL pattern, an indexing rule (likely `noindex` — coupon/gift-card codes are non-translatable identity per §1.5), and sitemap exclusion. Clarify with the Architect before T016 freezes the reserved-slug list (URL-001).
4. **(Low) Naming alignment.** This spec says `resolve_permalink` (URL-003); ADR-005 and T016 say `PermalinkResolver`. Same contract — implementers should treat URL-003 as the requirement and `PermalinkResolver` (ADR-005 §3) as the canonical implementation name.
5. **(Low) Resolver consumer list.** ADR-005 §3 lists **emails and API serializers** as mandatory `PermalinkResolver` consumers; URL-003's consumer list omits them. The URL-003 principle ("EVERY component that emits an internal href") already implies them, but transactional-email templates (T022) are a classic inline-URL regression point — worth an explicit mention when T022 is written.
6. **(Low) Customer account pages.** No account/login/order-history page type appears in §2/§5. `Disallow: /orders/` (ROB-001) plus admin Disallows cover the URLs currently specified; if a customer-account area is added later it must get an explicit `noindex` + Disallow row here first.
7. **(Low) CAN-007 interaction with x-default.** A page published in a single language emits no hreflang at all (CAN-007), hence also no `x-default` — intentional, but worth stating in the T025 implementation notes since some SEO checklists flag a "missing x-default" on such pages as an error.
