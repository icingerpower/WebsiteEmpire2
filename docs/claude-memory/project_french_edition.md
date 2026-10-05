---
name: project-french-edition
description: "French /fr storefront edition for Pradize — how it's built, what's translated, localization runbook + remaining gaps"
metadata:
  type: project
---

French edition (`/fr`) for the Pradize store, built 2026-09-07/08. English at
root, French under `/fr/` on the same domain. Demo lives on the :8099 scratch DB
(see [[project_local_8099_scratch_env]]).

**Architecture (already existed):** `StoreLanguage` (lang_code, use_path_prefix,
is_default, domain) — two rows share one StoreDomain: en (root, default) + fr
(use_path_prefix=True). `permalinks/resolver.py::resolve_locale` maps host+prefix
→ language. Storefront enforces NO mixed-language: a `/fr/` page 404s unless a
PUBLISHED translation exists in that lang (a collection page also needs ≥1
translated PRODUCT). Translations = ProductTranslation/CollectionTranslation/
StaticPageTranslation (status PUBLISHED, auto-published by the `translation`
AiJob). Enabling a language does NOT auto-queue — must launch the backfill.

**Runbook (to populate /fr):** 1) add fr StoreLanguage (store admin → Add a
language, use_path_prefix) + a target market (ShippingCountry or
PlatformDefaultShippingCountry(lang_code="fr")) so hreflang+sitemap include /fr.
2) author+publish English legal/contact static pages first (they're seeded as
unpublished placeholders; clean() blocks publishing until real content). 3) AI
Job Launcher → Content Translation (targets=all) to queue. 4) drain with the
worker (run_job.sh) or run_ai_jobs_cli. 5) email_template_translation is a
SEPARATE job. Translating PRODUCTS (not just collections) is required — a /fr
collection page needs its products translated.

**Localization work done (ADR-074 + chantier-1 i18n), commits 91b042a + 5dd273c:**
- chantier 1: wrapped hardcoded strings (breadcrumb Home→Accueil, inventory
  In stock→En stock etc. in storefront/views.py _get_inventory_status,
  size-guide headers Bust/Hips→Poitrine/Hanches in size_guide.py _MEASUREMENT_LABELS,
  aria-labels) + fr .po in storefront/locale.
- ADR-074 locale-aware size rendering (NO schema change — data already
  structured): units per locale (IMPERIAL_LANG_CODES={"en"} → en shows "in / cm"
  inch-first UNCHANGED; metric locales cm-only, in→cm fallback); size-guide
  region columns per locale (DISPLAY_REGIONS_BY_LANG, never duplicate the SIZE
  column's market); NEW catalog/services/size_display.py (MARKET_PRIORITY_BY_LANG
  + format_size_label) — variant SIZE label reordered visitor-market-first
  (/fr → "FR-34 | EU-32 | UK-6 | US-2 | …"; /en unchanged, US-first). Selector
  now sources structured VariantOptionValue.region_values_json + non-size options
  use VariantOptionValueTranslation; falls back to the legacy flat string when a
  product has no parsed markets. add-to-cart stays correct (option <value> = raw
  source string; variant chosen by variant_id PK, only the label localizes).
  NOTE: the fr size label shows ALL 8 markets (FR-first); trimming = edit
  MARKET_PRIORITY_BY_LANG. Cédric may ask to shorten it.

**Cart/checkout locale (ADR-075, done 2026-09-08, commits 9db5555/6094dd3/0b7503d):**
Root cause of "cart in English": TWO language systems — gettext chrome (session
sf_lang) vs `request.locale` (from URL path, drives menu/CollectionTranslation +
ProductTranslation + hreflang). Cart/checkout/search were un-prefixed (ADR-015
§6) so request.locale=en → menu + product names English even when chrome was
French. ALSO /cart/ had no Cache-Control → browser/SW served stale English (fixed
67494ea @never_cache). ADR-075 locale-prefixes the tunnel:
- Wave1 (9db5555): LocaleMiddleware rewrites request.path_info to the
  prefix-stripped path when a `/fr` prefix is consumed → /fr/cart/ resolves with
  request.locale=fr. HARD deny-list (admin/superadmin/api/static/sw.js/sitemap/
  robots) — /fr/admin/ still 404s. New permalinks.paths.locale_url() + {% locale_url %}.
- Wave2 (6094dd3): all cart/checkout/mini-cart/product links → locale_url; product
  form action → /fr/cart/add/; add_to_cart redirect built from request.locale
  (→ /fr/cart/); cart_page/search_view drop the sf_lang decorator (URL locale
  now drives it), keep @never_cache; cart line-item + mini-cart show the fr
  ProductTranslation.title.
- Wave3 (0b7503d): sw.js isNeverCachePath strips one lang segment (never-caches
  /fr/cart/ etc.) + PRADIZE_ASSET_VERSION dev→dev-2 (forces SW refetch); robots.txt
  emits Disallow for /<lang>/cart|checkout|orders/search per enabled prefixed
  StoreLanguage; cart/checkout stay noindex/out-of-sitemap/no-hreflang (verified).
Verified end-to-end: /fr product → add → 302 /fr/cart/ → French chrome + menu
(Robes) + line-item title. Header collections all have fr translations now (menu
FR on /fr pages). Deferred (T-LP-8, needs approval): full sf_lang retirement +
prefixed payment-provider return URLs. Residual nit: product image alt-text in
cart may still show the EN title. Every code change here needs a `:8099` restart;
the SW version bump auto-updates installed browsers.

**Single-page checkout (ADR-077) — MERGED to master 2026-09-09 (merge b22d7eb):**
Built in an isolated worktree, then merged cleanly (auto-merge, no conflicts —
theme.css regions were disjoint from the concurrent ADR-076 PWA work) after the
PWA session committed 7daebd4. storefront+pwa 1046 + engagement 160 OK on the
merged tree; verified in a real browser fully French + styled
(pradize_fr_checkout_MERGED.png: "FINALISER LA COMMANDE", French shipping
placeholder). Worktree/branch cleaned up. Details of the build below.
Converted the 4-step checkout funnel to ONE page (all sections visible, shipping
rates data-gated on address, one "Complete order" button) matching a clothing-store
reference. Design: docs/adr/ADR-077 (committed to master 8b106cb). Implementation
lives on isolated git worktree branch **worktree-agent-ad7294e8ffdcc895a, HEAD
38298e6** (worktree at .claude/worktrees/agent-ad7294e8ffdcc895a). Fully done:
dual-mode AJAX section/discount endpoints (JSON fragments, non-XHR = old flow as
no-JS baseline), single-page checkout.html (stepper+accordion+inline-onclick
removed), checkout.js deferred-intent Stripe Elements, 3-theme CSS, FR i18n
("Finaliser la commande", shipping placeholder), Safety verdict GO + escape_html
hardening. begin_checkout/money logic byte-identical (git diff master -- cart/
empty). storefront 930 tests OK; verified in a real browser
(pradize_fr_checkout_onepage.png). NO migration; PRADIZE_ASSET_VERSION bumped.
**NOT MERGED** — blocked by a concurrent session's uncommitted ADR-076 PWA release
work colliding on the 3 theme.css + a pwa test file (see [[feedback_shared_cwd_hazard]]).
Cédric chose "wait for the PWA session to finish, then merge" (clean 3-way). When
the main tree is clean: `git merge worktree-agent-ad7294e8ffdcc895a` into master,
resolve theme.css, re-run tests, migrate scratch (none), screenshot on :8099.

**Checkout country dropdown + phone-required (2026-09-09, commits 916396c/6e49c4d):**
- GOTCHA: the checkout ADDRESS country dropdown is populated by
  `storefront/views_checkout.py::_build_country_choices(store)` from the store's
  ACTIVE ShippingRate zones (enumerated country_codes) — NOT from ShippingCountry
  target markets. A store with only a catch-all zone ([] country_codes) or no
  active rate → EMPTY dropdown ("can't select a country"). Fix = create an
  ENUMERATED ShippingZone (country_codes list) + an active ShippingRate for the
  store. Demo: created zone "International (Pradize)" (104 codes from
  PlatformDefaultShippingCountry) + FLAT rate "Livraison standard" 9.99. The
  dropdown labels are now localized "CODE - Name" (commit 70c004e): new
  storefront/country_names.py (249 ISO names as gettext_lazy) + country_label();
  _build_country_choices returns (code, country_label(code)) sorted by localized
  label; VALUE stays the raw code (validation intact). Full fr .po translations.
  Verified: /fr "DE - Allemagne", /en "DE - Germany". No dependency (Cédric chose
  gettext over babel).
- Phone-required: settings hierarchy `stores/checkout_settings.py`
  (PlatformCheckoutSettings singleton `phone_required_default`=True +
  `Store.checkout_phone_mode` inherit/required/optional/hidden) →
  `resolve_checkout_phone_required(store)` (store mode → platform default → True).
  ContactForm.phone required reflects it; checkout.html shows "*" + drops
  "Facultatif" when required. Migration stores/0024 (fixed so existing stores
  INHERIT, not OPTIONAL — required-by-default per Cédric). Locale-agnostic.

**Remaining gaps (ranked):** (1) chantier 4 — product_type ("Clothing") + tags
("blush pink"/"women"/"adult") are single-language DATA with NO translation
mechanism; needs a new model + AiJob sub-type + ADR (NOT built). (2) no
auto-queue-translations-on-language-enable signal (footgun: enabling fr →
/fr all 404 until backfill launched). (3) no *Mentions légales* page seeded.
(4) storefront .po still ~10% untranslated (checkout errors, "No products
found", PWA prompt). (5) rewrite-review admin UI missing (approvals CLI-only:
bulk_approve_enrichments / review_enrichments). Relates to
[[project_remote_job_worker]], [[project_ai_job_launcher]], [[feedback_translation_cli]].
