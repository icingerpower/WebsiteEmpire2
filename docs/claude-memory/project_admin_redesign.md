---
name: admin-redesign-program
description: Full custom admin UI redesign program (ADR-045) — phases, decisions, approach for the Pradize admin
metadata:
  node_type: memory
  type: project
  originSessionId: f949a749-5e55-46c3-ace7-2b2bad071483
  modified: 2026-08-30T20:23:55.771Z
---

BIG multi-phase program approved by Cédric 2026-08-12. Audit found the Pradize admin is 100% STOCK Django
(both StoreAdminSite/SuperAdminSite vanilla AdminSite, ~91 ModelAdmins, no chrome/CSS). Spec wants a full custom
CommerceHQ/GrooveKart-style SaaS admin (dark top nav + logo + 4-col mega-menu, branded login, KPI dashboard, 12
chart reports, wizards, WYSIWYG, card lists). Spec screenshots: /home/cedric/Dropbox/Applications/WebsiteEmpire2/
spec-ecom/admin-001..021 + admin-005-reports-01..12 + admin-XXX-payment. Audit captures: /home/cedric/aspire_rescrape/admin_audit/.

ADR: docs/adr/ADR-045-admin-ui-redesign.md.

APPROVED APPROACH (skinning foundation): override Django AdminSite base templates ONCE (templates/admin/base_site.html,
base.html, index.html, login.html, app_index.html) + new `adminui` app holding a shared `--pa-*` admin design-system CSS
(same token pattern as storefront, NO Node build) + a data-driven mega-menu registry (like stores/modules.py,
permission-filtered via check_module_access, store-vs-super discriminator). Reskins ALL ~91 pages with zero per-model work
+ zero CRUD/scoping/permission risk (template inheritance is orthogonal to the registration-time scoping mixins).
Rejected: bespoke admin app rebuilding CRUD.

APPROVED DECISIONS: Chart.js (vendored UMD, no npm) for admin charts only [CLAUDE.md dep-approval]; Gross Sales = order
totals INCL shipping, BEFORE refunds, consistent across all reports. Other ADR forks P3-P9 proceed on ADR defaults
(engagement cols deferred to Phase 3b; bundle discount = % on secondary via checkout line adjustment, NOT a coupon/CampaignType;
attach-UI = inlines in Ph4 then full change-form in Ph6; login brand-only).

PRIORITY FEATURES (Cédric uses these): Analytics+Reports, Upsells/order-bumps/BUNDLES (Bundle model is GREENFIELD — none
exists), Abandoned-checkout recovery (entire data layer EXISTS, only UI missing). Gift-card SELLING deprioritized.

PHASES (each gated by screenshot-vs-spec + a 200-render regression test protecting CRUD):
STATUS (as of 2026-08-12):
0 DONE redirects 500 fix (permalinks/admin.py get_queryset->cross_store_unsafe + test).
2 DONE shell/design-system: new `adminui` app (tokens.css/admin.css/menu.py mega-menu registry/context.py AdminShellContextMixin),
  project templates/admin/{base_site,index,login,app_index}.html override both sites. ~87 pages reskinned. Verified vs admin-001/002/003.
2.5 DONE fixed 8 more latent super-site IsolationError 500s (aijobs x6, discounts.CampaignReward, shipping.ShippingRate) +
  engagement.LeadSignup store-site list_filter -> scoped SimpleListFilter. adminui render-guardrail now ZERO-tolerance.
3A DONE dashboard: analytics/reporting.py aggregation service (gross_sales incl shipping before refunds, GROSS_SALES_STATUSES=
  PAID/PARTIALLY_REFUNDED/REFUNDED), analytics/admin_views.py + dashboard.html (KPI tiles/trends/funnel/latest-orders/top-products/
  top-referrers), Chart.js 4.4.3 vendored at adminui/static/adminui/js/vendor/chart.min.js. Verified vs admin-002.
3B DONE all 12 reports: new `reports/` app (registry.py 12 descriptors, data.py, views.py ReportView+CSV, report_base.html,
  periods.py). Reuses analytics/reporting.py. Hub matches admin-005. device/geo read raw Event (no top_device/top_geo
  AggregatedMetric yet — Phase-3b follow-up; render honest empty-state).
4 DONE upsells/bumps/BUNDLES: catalog Bundle/ProductUpsell/ProductBump models + bundle_pricing.py (proven calc) + product-form
  inlines + BundleAdmin. Migrations 0022 (models) + 0023 (self-ref CheckConstraints + NaN/float pricing guards, safety pass).
  SAFETY-REVIEWED: isolation solid, pricing bounded, models INERT (storefront/checkout wiring DEFERRED — must call bundle_pricing
  when both products in cart; also ProductRelated/CustomizationField + P6 bump-scheduling + P5=b full change-form deferred).
5 DONE abandoned-checkout recovery: campaigns/recovery_* (dashboard stats recovery%/impressions/sales/revenue, editor, sequence
  builder, 3-step Type/Style/Copy wizard). migration 0008 (Campaign.abandoned_from_name/from_email_local, EmailStep.email_headline/
  cta_label). Email SENDING pipeline NOT wired to new copy fields (emails-app follow-up). Verified vs admin-008 (4.76% seeded).
ALL 3 PRIORITY FEATURES DONE (Reports, Upsells/Bundles, Abandoned-checkout) + shell.
6 DONE (waves): W1 customer directory (customers/ app, aggregates orders) + currency manager (currency/manager_views, super-site).
  W2 StoreProductPageSettings (catalog 0024) + StoreReviewSettings (reviews 0002) + reviews dashboard + Store general-settings
  fields (stores 0018: unit_system/weight/header_include_code/body_include_code/robots_txt_include — STORED ONLY, not rendered).
  W3 product change-form sectioned editor (catalog change_form.html + adminui product_editor.js; de-teal fix).
  W4 spec-review + safety. SAFETY: clean, no CRIT/HIGH; 1 MEDIUM CSV-formula-injection FIXED (reports/csv.py _neutralize).
  SPEC-REVIEW: 13/16 MATCH; 3 PARTIAL (admin-004 orders list = stock Django changelist not bespoke card rows; admin-016 general
  settings = raw Django form not grouped page + no root-dir upload; admin-021 product editor = sectioned but no WYSIWYG/variant-
  builder/image-dropzone). ADR-045 planned scope COMPLETE + safe.
REMAINING (post-ADR-045, NOT done): (A) 3 PARTIAL screens' bespoke fidelity; (B) STOREFRONT-ENFORCEMENT WIRING backlog —
  admin settings that don't yet take effect on storefront: product-page toggles, review-widget styling, header/body include-code
  injection (needs safety review), robots.txt emission, bundle/upsell/bump checkout wiring (call bundle_pricing when both in cart);
  (C) new features: device/geo analytics tracking (top_device/top_geo AggregatedMetric), AI-reviews, Amazon FBA, reports extra cols.
COMMITTED (2026-08-13): everything on WebsiteEcom branch master, NOT pushed. 10 admin-redesign+enrichment commits (9239b5f..baacbc4)
  + 3 storefront-enforcement commits (5da5372 toggles/review/bundle-pricing+VAT-books-fix, 6884aa2 bumps/upsells, 26a5124
  include-code/robots). Working tree clean. To push: `git push origin master` (or master:main).

STOREFRONT-ENFORCEMENT WIRING DONE (2026-08-13, the "B backlog"): admin settings now take effect on storefront —
  (A) product-page toggles hide sections + review widget styled (catalog/services/product_page_settings.py, reviews/storefront.py);
  (B) bundle checkout pricing via cart/bundle_service.py threaded into the ONE canonical total (cart/checkout.py compute_checkout_totals
  + cart/service.py), persisted Order.bundle_discount_amount (migration orders/0021), SAFETY-APPROVED; VAT-books fix in bookkeeping/
  export.py (_goods_discount, discount_amount capped at subtotal); (B2) order-bump slots + post-purchase upsells (storefront/offers.py,
  bump_slots.py); (C) super-admin include-code + robots.txt injection (storefront/base.html, sitemaps/views.py — |safe, super-admin-only,
  store-scoped; NO CSP exists — must whitelist if one is added).

STILL OPEN: (A-backlog) 3 PARTIAL admin screens (orders card-list admin-004, bespoke general-settings page admin-016, product-editor
  WYSIWYG/variant-builder/image-dropzone admin-021). (C-backlog) new features: device/geo analytics tracking (top_device/top_geo
  AggregatedMetric), AI-reviews, Amazon FBA, reports extra cols. Designer polish on the plain bump/upsell partials. Finding-C spec Q
  (multi-primary shared-secondary bundle under-discounts, merchant-favorable). PLUS the big one: PRODUCTION CUTOVER to drop CommerceHQ — see [[product-url-scheme]].
  CUTOVER PREP DONE + COMMITTED (2026-08-13): app runs ONLY locally (no prod server). Cédric chose: FIRST-TIME deploy + BULK-APPROVE
  rewrites at launch. Runbook docs/runbooks/CUTOVER_RUNBOOK.md (gated G0-G10, owner per step, rollback=revert DNS while CommerceHQ
  stays live, biggest gate G7=publication completeness vs old sitemap). Tooling built+tested: `bulk_approve_enrichments`
  (--yes-skip-review opt-in, bypasses E6) + `publish_catalog` (cursor-bug-free) + existing generate_legacy_url_redirects/import_
  aspired_catalog/queue_enrichment/run_ai_jobs_cli. EXECUTION IS CÉDRIC'S (server provision, real secrets in server .env NEVER shared,
  push master to a remote, ~19h agy enrichment, DNS repoint, cancel CommerceHQ). Policy: CHAT_KILL_SWITCH=true until Anthropic DPA review.
  15 commits this session on master, UNPUSHED (git push origin master when ready).
DESIGN-FINISHING PASS DONE + COMMITTED (2026-08-14): Cédric ordered "open each admin/super-admin url, snapshot, finish anything
not visually finished — uniform + modern before I try the ecommerce tech." Ran 5 sequential design waves (general-purpose agents;
sequential because they share adminui CSS) applying the dataviz skill's validated palette: W1 foundation (tokens.css rebuilt on
validated palette, admin.css buttons/forms/tables/messages/de-teal, "P" monogram + Pradize wordmark login); W2 dashboard + 12
report charts to dataviz standard (ordinal-ramp funnel not rainbow bar, single-hue bars + direct labels, donut capped 3+Other,
hover, one-axis); W3 6 bespoke mgmt pages (were already on-system from Ph6; only added reusable .pa-swatch colour picker to review
settings); W4 the 3 PARTIAL screens (orders bespoke card-list w/ status badges, Store settings grouped into 7 section-card
fieldsets, product-editor thumbnail polish); W5 stragglers (site_header WebsiteEcom->"Pradize · Store"/"Pradize · Platform",
dashboard Top-Referrers legend fold to 3+Other, app-list/related-widget action glyphs -> monochrome currentColor tokens) + 30-page
sweep both sites (no 500s, all uniform). ALL presentational — no view/model/query/permission/behavior changes. Verified: full suite
`test adminui analytics reports orders catalog stores webecom` = Ran 1529, OK (skipped=1), pipefail exit 0. Screenshots
/home/cedric/aspire_rescrape/design_wave{1..5}/. 3 logical commits on master f57db76 (foundation+branding), e20e2e5 (charts),
02de034 (bespoke screens) — UNPUSHED (git push origin master when ready; push is Cédric-owned).
INDEX-LAYOUT FIX (2026-08-14, commit 8b7e2bd): Cédric caught the /admin/ + /superadmin/ landing pages had the app-list
crammed into a narrow off-centre column w/ a big empty gutter (glaring at wide viewport >=1440px). ROOT CAUSE = Django admin
base.css `.dashboard #content { width: 600px }` + colMS float model (`.colMS { margin-right: 300px }`, floated #content-main/
#content-related) inheriting through, unopposed by adminui CSS. FIX in admin.css: on body.dashboard override `.dashboard #content
{ width:auto }`, `#content.colMS { margin-right:0 }`, neutralize floats on #content-main/#content-related so title+greeting+
app-list+recent-actions stack full-width. GOTCHA for future: the sweep screenshots were at 1440px which MASKED it — always
verify index/dashboard pages at WIDE viewport (>=1600-1800px). Verified full-width both sites; adminui guardrail green. DEFERRED as separate ADR tickets
(features not design): product WYSIWYG (dep+XSS, safety review), async image dropzone (upload backend), variant-matrix builder
(combinatorics), Store root-dir file upload. Minor left-as-is: changelist boolean check/X = Django standard, palette-aligned.
NOTE: agents seeded throwaway demo rows in dev db.sqlite3 (localhost store, ~44 orders, 1 product) for screenshots — harmless.
KNOWN pre-existing bug: aggregate_metrics throws under DEBUG=True (SQLite last_executed_query vs ? placeholders) — prod DEBUG=False ok.
TWO SEPARATE DEMO DBS (clarified 2026-08-14 when Cédric saw only 1 product in /admin/catalog/product/):
  (1) DESIGN demo = default db.sqlite3 (webecom.settings.development, what the :8099 server ACTUALLY runs on — NO
      DJANGO_SETTINGS_MODULE/--settings flag). Only 1 product: "Aurora Wireless Headphones"/demo-wave4-sample, seeded by a
      design agent for screenshots. This is where ALL the ADR-045 admin-design work was visually verified.
  (2) IMPORT demo = scratch_settings + SCRATCH_TMP=<dir>/importtest2 → scratch_default.sqlite3. Holds the FULL pradize catalog:
      5705 products + 5705 translations + 38 collections. Enrichment PARTIAL (catalog_productenrichmentstate 15201 rows):
      AUTO_APPLIED 5812 (deterministic size/measurement), AWAITING_REVIEW 5629 (agy rewrites, E6 gate), JOB_QUEUED 1946
      (not yet run), ERROR 1774, APPROVED 40. catalog_productpageversion=0 (NOTHING published yet).
  ORIGINAL import DBs were in the JOB tmp (…/jobs/f949a749/tmp/importtest2 [5705] + importtest [3971]) = DELETED when job is
  deleted. PROTECTIVE DURABLE COPY made 2026-08-14: /home/cedric/aspire_rescrape/pradize_import_catalog/ (scratch_default.sqlite3
  130M=5705 products, scratch_analytics.sqlite3, media/ 2.6G). To BROWSE the full catalog on :8099, run runserver against
  scratch_settings with SCRATCH_TMP pointing at a dir containing scratch_default.sqlite3 (+media) — NOT the default db.sqlite3.
ADR-046 measurement-parser fix (2026-08-15): the 1,774 measurement ERRORs were the deterministic size-chart parser
(catalog/services/measurement_chart_parser.py, ADR-041) fail-closing on scraped tables. ADR docs/adr/ADR-046-measurement-parser-
robustness.md APPROVED by Cédric (endorsed by orchestrator). Architect prototyped read-only vs the full import DB: post-fix landing
AUTO_APPLIED 1,492 (all align ≥1 stocked US size) / SKIPPED_NOT_APPLICABLE 199 / ERROR 83. Load-bearing fix = consume-and-ignore
letter-size region rows IN-TABLE so the US join column survives (naive skip-first-table loses US alignment). Found 2 hidden bugs:
(1) parser only knew '(in)' not '(inches)' → silently TRUNCATED 11 currently-AUTO_APPLIED products to in-only data (pids 75,76,99,
100,2158,2160,2336,2339,2586,2587,2588); (2) naive skip would emit empty AUTO_APPLIED. D3 cross-unit: trust cm, accept inch within
max(0.5in,5%) else fail. Developer IMPLEMENTED (staged, UNCOMMITTED): models.py SKIPPED_NOT_APPLICABLE enum + 3 transitions,
migration 0025 (no-op AlterField), parser rewrite, enrichment_queue.py mapping + alignment gate. catalog suite 939/939, golden corpus
14/14 (catalog/tests/fixtures/adr046_measurement_golden_corpus.json).
CÉDRIC DECISIONS 2026-08-15 (override/refine ADR-046): (a) the 11 truncation victims → REQUEUE from corrected text in Phase 4 (correct
deterministic values, not a guess). (b) the 83 genuine-corrupt → FALL BACK TO GENERIC SIZE GUIDE, i.e. MALFORMED maps to
SKIPPED_NOT_APPLICABLE (NOT ERROR) with a DISTINCT diagnostic reason ("source measurement table corrupt…") so still queryable —
measurements stage now emits NO ERROR at all; parser still returns MALFORMED outcome to keep corrupt-vs-no-table distinct. (c) IMAGE
approach for hard cases REJECTED after orchestrator feasibility check: the scraped galleries are 100% product PHOTOS, size charts are
TEXT in the description — no size-chart images exist; estimating measurements from model photos = unreliable, not shipped.
ADR-046 COMPLETE + COMMITTED 2026-08-15 (b086b13, UNPUSHED). All gates passed: developer impl + Amendment-A tweak; test-agent 1000
catalog tests green (parser 97%/queue 90% cov); after-bug-test (inches-truncation PROVEN fail-without-fix; garbage-header NOT_PROVEN-
by-design + forward guards; BUG_TESTS.csv +2 rows); Phase-4 empirical re-run on import DB MATCHED ADR exactly (1774 ERROR → 1492
AUTO_APPLIED / 282 SKIPPED_NOT_APPLICABLE / 0 ERROR; 2019 untouched; 11 truncation victims requeued, pid75 recovered sleeves+cm);
release-manager verdict READY (caught 2 stale docstrings → orchestrator fixed measurement_chart_parser.py + added uncertainties-log
resolution). measurements_json lives on catalog_variantoptionvalue (join via option_id→catalog_variantoption.product_id). NOTE: import
DB now MUTATED with corrected states — the :8099 demo serves fixed size guides. Store id in import DB = 2. To re-run enrichment on it:
queue_enrichment --store-id 2 --stage measurements (env PYTHONPATH incl app dir + aspire_rescrape, SCRATCH_TMP=pradize_import_catalog,
DJANGO_SETTINGS_MODULE=scratch_settings; migration 0025 applied). NEXT AVAILABLE TRACK (not started): AI-rewrite backlog (5629
AWAITING_REVIEW + 1946 JOB_QUEUED [36 rewrite + 1910 size_chart] + publishing 0 page versions).
POST-ADR-046 WORK (2026-08-15..17, all COMMITTED on master, UNPUSHED):
- 0df9e9b seed_demo_data (core/management/commands) — guarded dev seeder (--store-id --yes-demo, DEBUG/--force gate, --reset); makes orders/customers/analytics on REAL ProductVariants. Ran on import DB store 2: 60 customers/300 orders/744 items(100% real)/6012 events.
- 1e2b2ca admin home: store /admin/ redirects to KPI dashboard (gated); Menu->"All Data (Advanced)" (admin:all-data) keeps the 5 unmenued apps reachable (cart/chat/consent/core.User/pages); super /superadmin/ prepends platform-ops KPI band via analytics.reporting.platform_summary() (cross_store_unsafe). templates/admin/super_index.html.
- 116f9bb ADR-047 ORDERS CONSOLE (admin-004 full): CommerceHQ-style single-page master-detail at /admin/orders/order/ (bespoke admin views in orders/admin_views.py + admin_urls.py mounted in webecom/urls.py before catch-all; card-list kept behind one include for rollback; change-form=Advanced editor). Fulfillment workflow: Mark-as-Sent (item/qty modal) + Mark-as-Shipped (tracking+carrier) JSON POST endpoints (CSRF, orders FULL-gated, store-scoped 404-no-oracle, select_for_update qty invariants, forward-only). Derived status in orders/fulfillment.py. risk_level (nullable, charge.succeeded webhook else "Not available"). line_items_json rekeyed variant_id->order_item_id (migration 0023 no-op on current data). send_order_shipped_once() fixes latent create-only email bug. Refund->orders/refund.py. Spec specs/ecommerce_engine/18_orders_console.md. 194 tests (96% new-code cov). SAFETY: APPROVED, 1 HIGH CSV-injection(tab/CR) PATCHED, 3 LOW ticketed (refund idempotency, resend race, malformed-payload 500s). DESIGNER polished (inline-edit modal replaced prompt(), filter card, in-flight button disable). SPEC-REVIEW cut short by session limit (only dev screenshots reviewed) — orchestrator self-verified all key screens MATCH Sélection_989/990; formal spec-review still TODO. 5 veto-able ADR-§12 defaults stand (biggest: fulfillment BLOCKED on unpaid orders, Advanced-override exists). Deferred: CJ dropshipping export; upsell-promotion->recompute_fulfillment_status hook.
OPEN/TODO: (a) formal spec-review of orders console; (b) 3 LOW safety hardening tickets; (c) pre-existing FAIL test_raw_id_widget_sweep (BundleSuperAdmin raw-id widgets, catalog/admin.py) — offered to fix; (d) push master when ready. To SEE it: user runs `bash /home/cedric/aspire_rescrape/run_demo.sh` in THEIR terminal (my sandbox kills persistent servers w/ signal 16; ONE-SHOT start+capture+kill works on a FRESH port, NOT 8099 which is poisoned).
MORE FEATURES COMMITTED (2026-08-18..29, master, UNPUSHED): a815424 carrier tracking-URL templates (super-admin ShippingCarrier {tracking_number}, auto-fill in Mark-as-Shipped) · 6474e44 inline "+New carrier" (super-admin-only, is_super_admin-gated, writes global registry) · a1c3e97 report rows link to live storefront + admin editor (By-Product-Title/By-Collection; best-effort title->object, ADR-002-pure) · 6ee0aa3 base_url settings-driven scheme+PORT (PRADIZE_URL_SCHEME/PRADIZE_URL_AUTHORITY_SUFFIX; demo=http+:8099 in scratch_settings; prod default https unchanged — fixes demo "view live" links) · a60e77c ADR-048 COLLECTION EDITOR + engagement (admin-006: drag-reorder fractional a-z sort keys catalog/ordering.py, thumbnails, per-product Displays/Click-rate 30d; product-impression/click emitters via engagement.js gated at the existing beacon consent point; aggregate_metrics 2 new dimension metric types cid:pid cap 5000; reporting.collection_product_engagement; product field now HiddenInput+for_store queryset; NO migration). Safety SAFE(local): HIGH-1 fixed (public purchase beacon huge properties.total overflowed rollup NUMERIC after idempotent delete -> wiped a day's metrics all-stores 90d; bounded regex + _safe_dec clamp), MEDIUM-1 beacon value OverflowError, LOW-1 vanished-added-rows on validation error. R1 = product-granular tracking legal/DPO sign-off is a PRE-REAL-VISITOR-LAUNCH gate (local demo fine); escape hatch = per-store beacon_requires_consent=True.
a2f415d customer-detail orders link to console (?order=pk). 5916bc7 ADR-049 LARGE-COLLECTION EDITOR: collection "Dresses" id4=1684 products blew DATA_UPLOAD_MAX_NUMBER_FIELDS (inline formset ~10k fields) -> removed the inline, 6 store-scoped/products-gated AJAX endpoints under CollectionAdmin.get_urls (panel/reorder/add/remove/trim-preview/trim-apply), paginate 50/page, reorder sends intent (server resolves neighbor+key_between; JS key-math retired), catalog/services/memberships.py. CURATE/TRIM tool (Cédric): preview->confirm->apply, criteria low_click_rate(no-data kept)/added_before(CollectionProduct.added_at)/keep_top_n; apply RECOMPUTES in-txn (never trusts client pks), stale_preview + would_empty guards, smart-collection 409, NO UNDO by design. Stats freshness: aggregate_metrics --include-today + reader window incl today; ALSO fixed pre-existing aggregate_metrics DEBUG=True crash (qmark->%s placeholders) so rollup runs on the demo. Safety SAFE: MEDIUM-1 fixed (trim audit log now emits via minimal LOGGING config in settings/base.py, scoped catalog.memberships.trim logger). Verified live on 1684-collection: loads/reorder/Save OK, trim preview "remove 1484 keep 200", click->rollup->Displays 2/100%. 1334 tests OK, no migration. Revises ADR-048 UNC-CE-3/CE-4. NOTE: engagement events DO emit+ingest (consent works); to see stats on demo run `aggregate_metrics --include-today` (now works under DEBUG). NOTE2: trim NO-UNDO flagged to Cédric (removes memberships not products; offered soft-delete later).
645ac4b normalize_collection_order cmd — fixed collection 4 (Dresses) SCRAMBLED sort_keys (valid a-z but meaningless order from earlier editor-verification churn on the shared import DB; canonicalize can't self-heal valid keys). Re-keys memberships in added_at order via initial_keys; --store-id, --collection-id XOR --all, --yes, skips smart; ran on demo (38 collections). Order now red->green->black in admin+storefront (per-request sort, no restart needed for THIS data fix). 2830f1c collection editor stats now LIVE: collection_product_engagement hybrid = AggregatedMetric[today-29..yesterday] + raw analytics.Event for TODAY (no double-count: rollup query period_start__lte=yesterday; day boundary = datetime(y,m,d,utc) matching aggregate_metrics, USE_TZ+TIME_ZONE=UTC). So browse->reload editor shows displays/clicks live w/o running aggregate_metrics. Verified: collection 4 = 20 products w/ displays, product58 Victoria's Black = 3 imp/1 click (the user's click), +1-not-+2 proven. 1353 tests OK. IMPORTANT: live-stats is CODE -> user must RESTART :8099 to activate (unlike the order fix which was DB data). DIAGNOSIS that mattered: emission+consent+ingest all WORK (user's browsing recorded 46 imp/3 clicks); the friction was purely the rollup-vs-live read, now fixed.
11c25b9 ADR-050 collection create/edit UX (admin-006 auto+manual): replaced raw smart_rules JSON textarea with rule-builder UI (SmartRulesBuilderWidget over the JSON; "match all/any" + [field][op][value] rows; raw JSON in collapsed Advanced; RULE_FIELDS single vocab in catalog/smart_rules.py drives dropdowns+clean_smart_rules gate+evaluator; extended eval to product_type+vendor). CommerceHQ-style manual BROWSE picker (catalog/services/product_browse.py + product_browse_view, collection-INDEPENDENT so works on Add; facets All/Popular[paid units 30d]/Collections/Product types/Tags/Vendors + search; multi-add <=100 client-chunked). CREATE FLOW staged-then-atomic: staged_products hidden field cap 1000 -> save_related() creates memberships in admin txn, store-scoped re-resolve. Rules now re-materialize immediately on save (apply_rules_for_collection, Collection pre/post_save hook) with BATCHED price map (O(1) queries, was N+1 — matters at 5705). Type toggle: manual->picker, smart->builder+readonly preview; manual->smart replace confirmed(client)+audit-logged(catalog.rules.rematerialize logger in settings/base.py LOGGING). Safety SAFE (authz/isolation/CSRF/rule-validation/no-injection/XSS verified). Migration 0026 help_text-only. 1453 tests OK. Verified via real UI: manual pick-3-then-save + smart vendor-rule, no JSON typed. CODE change -> user must RESTART :8099. §16 accepted defaults: Popular=paid-units-30d, draft pickable/archived not, synchronous re-materialization (now batched).
OPEN/TODO: (a) formal spec-review of orders console (ADR-047) + collection editor (ADR-048/049/050) — both self-verified vs screenshots, formal pass deferred by session limits; (b) PRE-EXISTING STOREFRONT BUG (flagged, NOT fixed): product cards on collection.html/home.html/search.html don't render price because default_variant isn't passed into product_card.html — offered to fix, awaiting Cédric; (c) 5 veto-able ADR-047 console defaults; LOW safety tickets (orders + ADR-048 float overflow MEDIUM-1 done; remaining LOWs); upsell->recompute hook; pre-existing test_raw_id_widget_sweep FAIL (BundleSuperAdmin); (d) push master.
Demo server pid file: /home/cedric/aspire_rescrape/server.pid (agents restart it --noreload on each change). Phase screenshots:
/home/cedric/aspire_rescrape/admin_phase2|phase3|phase3b|phase4/.

CONSTRAINT: specialized team agents (architect/designer/developer/test-agent/spec-reviewer/etc.) are DISCONNECTED this
session — driving with general-purpose agents. See [[product-url-scheme]] for demo env (server :8099, login
admin@demo.local/pradizedemo, scratch_settings has analytics-DB migrated + Celery eager + Store.custom_domain=localhost +
LOGIN_URL=/admin/login/). All this work is on the throwaway scratch demo (importtest2); production is a separate future run.
