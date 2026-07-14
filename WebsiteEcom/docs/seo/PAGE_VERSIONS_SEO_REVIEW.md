# SEO Review — A/B Page Versions (TICKET-042, ADR-028)

**Reviewer:** SEO Agent · **Date:** 2026-07-11 · **Scope:** 042-A storefront/SEO surface
**ADR:** `docs/adr/ADR-028-ab-variant-pages.md` — this review is the mandatory SEO gate the ADR names ("canonical regression is the existential risk").

**Evidence gate:** `DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py test catalog permalinks sitemaps storefront` — **906 tests, OK** (9.4 s). Additionally `feeds.tests.test_adr_named_cases` — 8 tests, OK (includes the armed `ABVariantSlugNeverInLinkTest`).

---

## Rule 1 — Canonical → primary, never self-canonical: VERIFIED

`_resolve_canonical_target_url` (`permalinks/templatetags/permalinks_tags.py:219-247`), all branches traced:

| `request.seo_canonical_path` state | Behaviour | Verified at |
|---|---|---|
| attribute absent (every non-version page) | falls through to `_resolve_canonical_url` — current URL, pre-ticket behaviour byte-identical | `permalinks_tags.py:236-238` |
| `""` (primary unresolvable) | returns `""` → `canonical_url.html` renders **no tag at all** (template is `{% if canonical_url %}`-guarded) | `permalinks_tags.py:239-240`, `permalinks/templates/permalinks/canonical_url.html` |
| real path | `base_url(locale.language) + path` — absolute, language-correct (`base_url` includes the `/fr` prefix for path-prefixed languages), trailing slash included by the view | `permalinks_tags.py:241-247` |
| real path but `request.locale` missing/mocked | returns `""` (no tag) — defensive, never self | `permalinks_tags.py:241-243` |

The setter (`storefront/views.py:472-491`) resolves the primary via the active product-CT `Permalink` for `(store, lang)` — the same lookup pattern the view already uses — and on failure sets `""` **and logs at ERROR** (`views.py:485-491`). No branch can ever produce a self-canonical on a version page. §XV-1 satisfied: loud, not wrong.

Tests: `storefront/tests/test_page_version_seo.py:104` (canonical = primary, absolute, trailing slash), `:187` (unresolvable primary → no `rel="canonical"` anywhere in HTML + `assertLogs` ERROR). Resolver-level fallback for inactive-version/inactive-product URLs is a defensive 302 to primary (`permalinks/registry.py:429-442`) — never a rendered duplicate.

## Rule 2 — No noindex on version pages: VERIFIED

Version pages render `storefront/pages/product.html` → `base.html`. The only `noindex` producers in the storefront are: preview mode (`base.html:32`), thin collections (`{% noindex_if_thin %}`, collection template only), and the utility templates (cart/checkout/search/404/thank-you). None touch the product template; no middleware emits `X-Robots-Tag` (`storefront/middleware.py` — comment only). robots.txt (`sitemaps/views.py:109-146`) disallows only `/cart/`, `/checkout/`, `/orders/`, `/search`, admin, feeds, and beacon endpoints — root-level `/<slug>-<suffix>/` URLs are crawlable (ROB-004: crawlers can fetch the canonical tag). **Test gap:** no explicit assertion that a version page contains no robots meta — see FIX-3.

## Rule 3 — og:url divergence, exactly scoped: VERIFIED

The split is structural, not conditional-per-page-type:

- `{% canonical_url %}` reads the override via `_resolve_canonical_target_url` (`permalinks_tags.py:264`).
- `{% og_url %}` **never** reads `seo_canonical_path` — hardwired to `_resolve_canonical_url` (`permalinks_tags.py:280`), with a docstring citing META-004 explicitly to stop a future "fix" (`:41-51`), as ADR risk 2 demanded.
- `request.seo_canonical_path` is set in exactly one place in the codebase (`storefront/views.py:483/:491`, only when `page_version is not None`) — verified by repo-wide grep. Every other page type (collection, static, home, checkout) hits the "attribute absent" branch → canonical == og:url as before, and `test_primary_page_is_still_self_canonical` (`test_page_version_seo.py:150`) locks both tags on the primary product page. No leak surface exists.

Divergence is asserted by name: `test_version_url_og_url_is_its_own_url_not_primary` (`:130`) + `test_version_url_canonical_is_primary...` (`:104`) on the same URL.

## Rule 4 — hreflang: zero tags, tag/sitemap parity (CAN-005): VERIFIED

- `ProductPageVersion.hreflang_exempt` is a constant-`True` property (`catalog/models.py:984-987`).
- `base.html:24` passes `page_version|default:content_object` into `{% hreflang_tags %}`; the tag's existing first-line exemption check (`permalinks_tags.py:132-133`) short-circuits to zero entries. No template-tag change, per ADR §4.
- Test is a **real** exemption check, not vacuous: the positive control asserts the primary page (2 shipping-configured languages) *does* emit hreflang before asserting the version page emits none (`test_page_version_seo.py:111-128`).
- Sitemap-alternates parity: version permalinks never appear as `<loc>` (content-type exclude) **and** cannot appear as `xhtml:link` alternates of the product entry — the alternates map is keyed by `(content_type_id, object_id)` (`sitemaps/sitemaps.py:260-268`), and version rows key under the version CT, which is never looked up. `test_version_url_absent_from_sitemap` asserts the version slug is absent from the entire XML, which covers alternates too. The two sources cannot disagree.

## Rule 5 — Sitemap exclusion by content type: VERIFIED

`.exclude(content_type=page_version_ct)` on the primary queryset (`sitemaps/sitemaps.py:145-152`), applied per language (the builder runs once per `(store, StoreLanguage)`). The stale TICKET-029 attribution is corrected in both the module docstring (`:12-15`) and the class docstring (`:73-78`). AC-093 "exactly one `<loc>` per product" is asserted by count (`test_page_version_seo.py:158-169`); multiple versions covered (`:173-183`).

## Rule 6 — Redirect semantics: VERIFIED (two accepted cosmetic edges)

- **Deactivate → 302** to primary, per language (`permalinks/registry.py:171-212`, `RedirectType.TEMPORARY`, trigger `PAGE_VERSION_CHANGE`); **delete → 301** (`:215-260`, `PERMANENT`). Both route through the single `create_slug_redirect` (`permalinks/redirects.py:64-139`), inheriting loop-rejection and chain-collapse — no parallel redirect implementation was created.
- **Product-slug rename cascades**: default-language path (`permalinks/signals.py:101-109`) and translated-slug path (`catalog/signals.py:279-287`) both call `rename_page_version_permalinks_for_language` (`registry.py:263-301`) inside the same transaction, writing per-version 301s. Late-published translations create the missing version permalinks (`catalog/signals.py:316-329`), idempotently.
- Resolver precedence: an active Permalink always wins over a redirect row (`permalinks/resolver.py` step 1 before step 2), so reactivation restores 200s even though the deactivation 302 row lingers (see FIX-4).
- **Flagged edge — deactivated then deleted keeps the 302:** `delete_page_version_permalinks` only processes `is_active=True` permalinks (`registry.py:239-243`); after deactivation there are none, so the existing 302 row is never upgraded to 301. **Assessment: acceptable.** The target is identical (the primary), the URL never 404s (§XII holds), and Google treats long-standing 302s as permanent for canonicalization purposes — equity consolidates, just marginally slower. Upgrading the row on delete would be a two-line nicety, not a gate item.

Tests: `permalinks/tests/test_page_version_permalinks.py` covers creation-per-language, atomic collision rollback, 302-on-deactivate, 301-on-delete, reactivation, rename cascade + 301s, late-translation creation, and resolver dispatch (200 with version images / 302 inactive / 301 deleted).

## Rule 7 — Reserved-pattern edge (`go` + `en` → `go-en`): VERIFIED, layered

- Layer 1 (admin-visible): `ProductPageVersion.clean()` composes the slug for **every** language with an active product permalink and runs `_validate_slug_not_reserved` — the same single validator every permalink write uses (`catalog/models.py:1014-1033`). Surfaces as a `slug_suffix` form error, not a 500. Test: `catalog/tests/test_page_versions.py:83-114`.
- Layer 2 (backstop): `Permalink.save()` enforces the validator on every write (`permalinks/models.py:214-231`), so signal-path writes can never register a reserved composed slug even if clean() was bypassed.
- Reachability note: a composed `xx-yy` reserved slug requires a 2-letter product slug, which the same validator already forbids at product-permalink creation — the edge is only reachable via legacy/import rows (the test simulates this with `bulk_create`, documented at `test_page_versions.py:88-94`). The rename/late-translation signal paths therefore cannot realistically hit an unhandled ValidationError from this rule. Defense is correctly layered.

## Rule 8 — Duplicate-content assessment (the honest one)

**Setup:** up to 11 URLs per product per language (primary + ≤10 versions, P1 cap), identical title/description/price/variants, different images, all indexable-but-canonicalized to primary.

**Signal inventory — what Google actually uses for canonical selection, and where each points:**

| Signal | Points to | Status |
|---|---|---|
| `rel=canonical` | primary | verified (Rule 1) |
| Sitemap membership | primary only | verified (Rule 5) |
| Internal links (menus, breadcrumbs, collections, `{% permalink_url %}`) | primary only — nothing internal ever links a version URL (`registry.py` dispatch is entry-only) | verified |
| hreflang | primary group only; versions emit none | verified (Rule 4) |
| Feed `g:link` (Merchant Center) | primary, exactly | armed test green (`feeds/tests/test_adr_named_cases.py:244-326`) |
| Redirects on removal | version → primary (302/301) | verified (Rule 6) |
| Inbound external links | **versions** (Pinterest pins, ad clicks) | by design — but Pinterest outbound links are nofollow/ugc and paid clicks pass no ranking signal |
| `og:url` | version (self) | **not a canonicalization input for Google** — Open Graph is a sharing protocol; Google's duplicate-selection factors (redirects, canonical, sitemap, internal linking, hreflang, HTTPS) do not include og:url |
| JSON-LD `offers.url` | **version (self)** — see FIX-1 | the one on-page signal that disagrees |

**Can Google still elect a version as canonical?** Yes, canonical is a hint. But the failure requires Google to override a cluster where every meaningful signal agrees, and — critically — **identical body text makes this cluster safer, not riskier.** Google honors cross-URL canonicals precisely when the pages are near-duplicates; the canonicals that get ignored are the ones stapled across genuinely different content. Same-text/different-images is the textbook honored case (structurally identical to Shopify's `?variant=` canonicalization at ecommerce-wide scale). Residual probability: low. Blast radius if it happens anyway: Google indexes a version URL that sells the same product with the same copy and a working ATC — a swap, not a loss — and it is visible in GSC ("Duplicate, Google chose different canonical") for correction.

**Would noindex on versions be safer?** No — it would be worse on every axis that matters:

1. **noindex + canonical-to-elsewhere is a contradictory signal pair** Google explicitly advises against (noindex says "drop this page", canonical says "this page's signals belong to X" — Google may propagate the noindex to the canonical target in ambiguous clusters, which is the actual store-wide risk).
2. Long-noindexed pages are eventually treated as noindex,nofollow — any equity from pins/links to version URLs is **dropped** instead of **consolidated** onto the primary. Canonical-only consolidates; noindex discards.
3. It buys nothing: sitemap exclusion + zero internal links + canonical already keep versions out of the index in practice.
4. On the ADR's stated rationale — one correction for the record: Google **Ads** landing-page quality is evaluated by AdsBot, which ignores `noindex` robots meta, so "ad quality score" is not the real argument for no-noindex. The real arguments are (1)–(3) above. The ADR's *conclusion* is right; its cited reason is the weakest of the available ones. No code impact.

**Merchant Center specifically:** MC only ever receives the primary URL (`g:link`, test-armed at the item-dict AND rendered-XML level), MC's crawler lands on the primary, and free listings read structured data from the page Google canonicalizes to. There is no path by which a version URL enters MC. Safe.

**Verdict on Rule 8: the no-noindex + canonical design is correct. Approved as designed**, with FIX-1 to remove the single disagreeing on-page signal.

## Rule 9 — Beacon/measurement: NO SEO IMPACT (verified)

`properties.page_version_id` lives in the JS beacon payload (`storefront/templatetags/storefront_tags.py:258-303`) and a hidden ATC form field — invisible to crawlers' indexing signals; the beacon endpoint is `Disallow: /_analytics/` in robots.txt anyway. `view_content` pixel payload is asserted byte-identical primary vs version (`test_page_version_seo.py:208-231`). Sanity check passes.

---

## Findings

### FIX-1 (should fix before release — low effort, real value) — **APPLIED 2026-07-11**
**JSON-LD `offers.url` on a version page points at the version's own URL**, disagreeing with the canonical. `storefront/views.py:549-554` builds `product_canonical_url` from `locale.path` (the *current* path) and puts it in the Product schema's `offers.url` (`:572`) — on a version render that is `/blue-dress-alt/`. This is the only on-page machine-readable signal contradicting the canonical, in exactly the "signals disagree" scenario Rule 8 analyzes; structured data on canonicalized pages is also what free-listings crawlers read. Fix: when `page_version is not None` and the primary resolved, compose the JSON-LD URL from `request.seo_canonical_path` (already available three lines above); when the primary is unresolvable, omit `offers.url`. Add one assertion to `test_page_version_seo.py`.

Applied: `storefront/views.py` now branches on `page_version is not None` when building `product_canonical_url` — version renders compose `base_url(locale.language) + request.seo_canonical_path` (the same helper `_resolve_canonical_target_url` uses for the canonical tag itself), and omit `offers.url` entirely when `request.seo_canonical_path == ""` (primary unresolvable); primary-page renders are untouched (`request.build_absolute_uri(locale.path)`, unchanged). Tests added: `test_version_url_jsonld_offers_url_is_primary_not_version` and `test_primary_page_jsonld_offers_url_is_unchanged_self_url` in `storefront/tests/test_page_version_seo.py`.

### FIX-2 (test gap — add before release) — **APPLIED 2026-07-11**
**No rendered-HTML test for the canonical on a path-prefixed language version page.** The code is correct by construction (`base_url()` includes the `/fr` prefix; `_resolve_canonical_target_url` composes `base_url + "/robe-bleue/"`), and `test_late_published_translation_creates_matching_version_permalink` covers the permalink row — but no test fetches `/fr/robe-bleue-alt/` and asserts `<link rel="canonical" href="https://host/fr/robe-bleue/">`. ADR test 1 says "same language" — the multilingual half of that claim is currently untested at the HTML level. One test, fixture already exists (the `fr` StoreLanguage in `test_page_version_seo.py`).

Applied: added `test_fr_version_url_canonical_is_absolute_primary_url_with_fr_prefix` to `storefront/tests/test_page_version_seo.py`, fetching `/fr/robe-bleue-alt/` and asserting `<link rel="canonical" href="https://host/fr/robe-bleue/">` in the rendered HTML.

### FIX-3 (test gap — one line) — **APPLIED 2026-07-11**
Add `self.assertNotIn('name="robots"', content)` (or equivalent) to the version-page test — Rule 2 ("deliberately no noindex") is a stated ADR invariant with no guarding assertion; a future "safety" patch adding noindex to version pages would pass the current suite.

Applied: added `test_version_url_emits_no_robots_meta_tag` to `storefront/tests/test_page_version_seo.py`, asserting `name="robots"` is absent from a version page's rendered HTML.

### NOTE-1 (accepted, no action required)
Deactivated-then-deleted versions keep their 302 (never upgraded to 301) — assessed acceptable under Rule 6. Optional two-line nicety: have `delete_page_version_permalinks` also upgrade existing `PAGE_VERSION_CHANGE` 302 rows for the version's slugs to 301.

### NOTE-2 (hygiene, no SEO effect)
Reactivating a version leaves the stale `version-slug → primary` 302 `SlugRedirect` row in place (`registry.py:155-168` never deletes it). Resolver precedence (Permalink before redirect) makes it inert, and the next rename's stale-row cleanup (`redirects.py:122-126`) deletes it — self-healing. Cosmetic admin clutter only.

### NOTE-3 (micro-efficiency, no SEO effect)
Sitemap query 2 (`sitemaps/sitemaps.py:261-268`) loads version-CT permalink rows into `alt_map` even though their keys are never looked up. Harmless (parity proven under Rule 4); an `.exclude(content_type=page_version_ct)` there would shave dead rows on large stores.

---

## Verdict

**PASS-WITH-FIXES**

The existential risk the ADR names — a self-canonicalizing version page or a leaking sitemap — is competently closed: the canonical override is single-writer/single-reader with a fail-loud empty state, the og:url divergence is structurally scoped and refactor-proofed, hreflang exemption reuses the one existing hook with proven tag/sitemap parity, redirects reuse the one hardened `create_slug_redirect`, and the feed negative test is armed and green. 906 tests pass.

Required before release: **FIX-1** (JSON-LD `offers.url` → primary on version renders), **FIX-2** and **FIX-3** (test gaps guarding stated ADR invariants). None are architectural; all are small. NOTE-1..3 are recorded as accepted/optional.
