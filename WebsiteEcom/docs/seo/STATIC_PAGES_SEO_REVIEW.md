# SEO Review — Static Pages (ADR-018, T029-SP)

**Reviewer:** SEO Agent · **Date:** 2026-07-10 · **Gate:** mandatory pre-release SEO review named in ADR-018.
**Scope:** `pages/` app, `permalinks/` (registry dispatch, hreflang, templatetags), `sitemaps/sitemaps.py`, `storefront/views_pages.py`, `storefront/templates/storefront/pages/static_page.html`, compared against `specs/ecommerce_engine/07_multilingual_seo.md` (ML-011, ML-020, CAN-002/004/005/006/007, SLUG-030, SM-010/030, META-001/002/004, SD-*) and `docs/adr/ADR-018-static-pages.md`.
**Validation:** `python3 manage.py test pages sitemaps` — **131 tests, all pass** (includes `pages/tests/test_seo_parity.py`, `test_permalink_lifecycle.py`, `test_dispatch.py`).

---

## Verdict summary

**PASS-WITH-FIXES.** The core SEO architecture is correct and follows the binding lessons: canonical/hreflang live in the shared base blocks, drafts are unreachable (real 404, no permalink), the hreflang exemption for policy pages holds on both the page and the sitemap side with a parity test, the nav tag never links a URL that would 404, and default-language slug changes create 301s through the shared redirect machinery. Three HIGH items must be resolved before release: a translated-URL orphaning bug on unpublish/republish, the missing 301 on translated-slug changes (engine-wide, inherited), and a binding-spec contradiction on policy-page indexing that needs a spec amendment.

---

## Findings

### HIGH

#### H1 — Unpublish → republish permanently orphans translated URLs (404 with no recovery path)

**Evidence:** `pages/signals.py:81-97` (`sync_static_page_permalink`):
- `is_published=False` → deactivates permalinks for **all** languages (correct).
- `is_published=True` again → reactivates **only** the `(store, store.primary_language)` permalink. Translated permalinks (e.g. `fr`) stay `is_active=False` forever, even though their `StaticPageTranslation` rows are still `status=PUBLISHED`.

Nothing reactivates them: `_post_save_static_page_translation` (`pages/signals.py:141-165` → `catalog/signals.py _sync_translation_permalink`) only fires on a translation **status change** or `slug_hint`, and the AiJob trigger skips because the fingerprint matches (`_should_queue_static_page_translation` returns False). Result: previously indexed `/fr/…/` URLs 404 permanently — a silent SEO regression of exactly the invisible-failure class §XV-1 forbids. It also breaks ADR-018's own claim of mirroring the catalog pattern: the Product equivalent (`permalinks/signals.py:216-222 sync_permalink_on_status_change`) reactivates permalinks for **all** languages on republish.

**Test gap:** `pages/tests/test_permalink_lifecycle.py:54` (`test_republish_reactivates_existing_permalink_not_duplicate`) only covers the default language.

**Fix:** on republish, reactivate the default-language permalink **plus** every language that has a `PUBLISHED` `StaticPageTranslation` for the page. Do not copy the Product blanket-reactivate verbatim — it has the inverse flaw (it would re-activate a language whose translation was reverted to draft while the page was unpublished); the translation-status-filtered form is correct for both models. Add a regression test: publish → publish FR translation → unpublish page → republish page → FR permalink active, FR URL 200.

#### H2 — Translated slug change creates no 301 (old translated URL 404s)

The review question was: does the staticpage branch get the redirect mechanism? **Default language: yes.** `catalog.signals.detect_slug_change` (unfiltered `pre_save`, `catalog/signals.py:58-86`) fires for StaticPage; `permalinks/signals.py:102-105` has the StaticPage branch; redirect + chain collapse + loop rejection are proven by `pages/tests/test_permalink_lifecycle.py:123-169`. **Translated languages: no.**

**Evidence:** the only mutation path for a translated slug is `catalog/signals.py _sync_translation_permalink` — when a `slug_hint` differs from the existing auto-created permalink's slug it does `existing.slug = new_slug; existing.save()` and **creates no `SlugRedirect`**. The `slug_changed` signal that this Permalink save emits (Permalink has `slug`+`pk`, so `detect_slug_change` fires) is discarded because `handle_slug_change` only accepts `sender in {Product, Collection, StaticPage}` (`permalinks/signals.py:96-108`) and only ever operates on `lang = store.primary_language` (`:111`). So a re-run AI translation job that proposes a new slug orphans the previously published translated URL → 404 instead of 301. This is an SEO regression against SLUG-030 ("this applies per language") and §XII.

**Shared scope:** this gap is inherited — it affects ProductTranslation and CollectionTranslation identically. It is not newly introduced by T029-SP, but ADR-018 explicitly promised "Redirect + chain collapse + loop rejection come for free (§XII)" for static pages, and for translated slugs that promise does not hold.

**Fix (once, §XV-4):** inside `_sync_translation_permalink`, when updating an existing auto-created permalink's slug, create a `SlugRedirect(from_slug=old, to_slug=new, from_lang=to_lang=instance.lang_code)` and run the same loop-rejection/chain-collapse routine as `handle_slug_change` (extract it into a shared helper). One fix covers products, collections and static pages. Add a regression test per model.

#### H3 — Binding spec contradicts shipped behavior for policy pages (CAN-002 / §5 / SM-030 / TST-CAN-002 vs ADR-018)

**Spec** (`07_multilingual_seo.md`): CAN-002 — legal pages render **no hreflang and `noindex` in alternate languages**; §5 table — legal pages "indexable in default language only"; SM-030 — "alternate-language legal pages" are **sitemap exclusions**; TST-CAN-002 — FR privacy policy must carry `noindex`.

**Implementation** (per ADR-018 D1 SEO): translated policy pages are fully indexable, appear in every language's sitemap `loc` list, and only the hreflang alternates are suppressed — `permalinks_tags.py:114` (`hreflang_exempt` no-op), `sitemaps/sitemaps.py:186-197` (alternates cleared, loc kept), asserted by `pages/tests/test_seo_parity.py:105` (`test_policy_page_stays_in_sitemap_loc_list`).

**Assessment:** the implemented behavior is SEO-safe, and arguably *better* than the spec: CAN-002's rationale ("legal text is kept in the store's default language", marked ASSUMPTION LOW) does not hold in this engine — policy bodies are genuinely translated by AiJobs, so translated policy pages are legitimate per-language content, not duplicates. Indexable translated pages without hreflang grouping is valid (hreflang is an optimization, not a prerequisite for indexing). No code change recommended.

**But** the spec is stated as binding, and its acceptance test TST-CAN-002 now fails by design. This must not be left as a silent divergence. **Fix:** Spec Agent amends `07_multilingual_seo.md` — CAN-002, the §5 legal-pages row, SM-030's exclusion list, and TST-CAN-002 — to the ADR-018 model (policy pages: indexable per published language, hreflang no-op via `hreflang_exempt`, kept in sitemap loc, alternates suppressed). Human approval of the amendment closes this finding; alternatively (not recommended) implement noindex + sitemap exclusion for alternate-language policy pages.

### MEDIUM

#### M1 — Cross-language field fallback on translated pages violates ML-020 / META-002

**Evidence:** `storefront/views_pages.py:120-123` — for a non-default language with a published translation: `title = translation.title or page.title`, `body = translation.body or page.body`, `seo_title = translation.seo_title or page.seo_title`, `seo_description = translation.seo_description or page.seo_description`. A published FR translation with an empty body (or empty SEO fields) serves **default-language text on the FR URL** — exactly the mixed-language/duplicate-content case ML-020 forbids ("storefront rendering NEVER silently falls back to another language for indexable text content") and META-002 forbids for SEO overrides ("never to the default-language override text").

**Shared scope:** `product.html:143-147` has the same fallback for the description (`{% elif product.description %}`); static pages extend the precedent to all four fields in the view.

**Fix:** drop the default-language fallback for indexable text on translated renders — render the field empty (or 404 if title is empty, since a title-less page is broken anyway), and enforce non-empty required fields at the AI-persist boundary (§XV-5). At minimum fix `body`/`seo_description`; the empty-title case is already mitigated by the slug factory requiring a title in practice.

#### M2 — Placeholder publish guard missing (thin/duplicate content, review area 5)

**Evidence:** seeds are one-line bodies containing the literal marker "Replace this text before publishing." (`pages/seeding.py:17,72-73`). Drafts are correctly uncrawlable (verified: no permalink until publish; `test_seeding` asserts no permalinks). The risk arises when store admins publish unedited.

**Assessment of the cross-domain duplicate-content risk:** for *real* legal boilerplate across hundreds of store domains — **low**. Google treats legal boilerplate leniently; policy pages don't need to rank and near-identical terms pages across independent domains do not trigger sitewide penalties. **Recommendation-level only** on that axis. The concrete risk here is different: the seeded body is not boilerplate legal text but a one-sentence placeholder, so an unedited publish produces an indexable, sitemapped page whose entire content is "…placeholder content. Replace this text before publishing." — thin content, a sitewide quality signal, and embarrassing in SERPs/Stripe review.

**Fix (small, testable):** validation at the persistence boundary (§XV-5) — reject (or hard-warn in admin) setting `is_published=True` while `_PLACEHOLDER_MARKER` is present in `body`. Keeps L12 ("legal pages published") as the launch-checklist complement. Test: publishing a seeded page unmodified raises ValidationError; publishing after editing the body succeeds.

#### M3 — Sitemap `lastmod` sourced from `Permalink.created_at` (SM-010 violation)

**Evidence:** `sitemaps/sitemaps.py:271` — `lastmod=p.created_at`. SM-010 requires "lastmod = last content-affecting update". `Permalink.created_at` never changes when the page body is edited, so lastmod is permanently frozen at first publish — a wrong-but-real value that tells crawlers edited policy/about pages never changed. `StaticPage.updated_at` and `StaticPageTranslation.updated_at` exist and are maintained (`pages/models.py:74,154`).

**Shared scope:** preexisting for products/collections; static pages inherit and slightly worsen it (policy pages are edited over time). **Fix:** bulk-map `(content_type, object_id) → max(updated_at)` per content type in `PermalinkSitemap.get_urls` (one extra query per type), falling back to `p.created_at`. Engine-wide follow-up ticket acceptable.

### LOW

- **L1 — Title separator diverges from META-001.** `static_page.html:6` and `product.html:6` render `<title>{{ … }} | {{ store }}</title>`; META-001 specifies `<page title> — <store name>`. Consistent engine-wide, so no duplicate-content concern; align the spec formula or the templates (one-line change either way). **RESOLVED (2026-07-10, human): spec aligned to `|`, see Required Actions table.**
- **L2 — META-001 description fallback not implemented.** Empty `seo_description` → no `<meta name="description">` at all (`static_page.html:7-9`); spec says fall back to first ~155 chars of page content. Shared with product pages. Harmless (Google generates snippets) but spec'd.
- **L3 — `og:url` absent** (META-004: og:url = canonical). Shared with `product.html`. Add `og:url` from the same resolved canonical in both templates.
- **L4 — Exemption logic exists in two forms.** Page level reads the property (`StaticPage.hreflang_exempt`, `pages/models.py:76-83`; consumed at `permalinks_tags.py:114`); the sitemap re-expresses it as a queryset filter `kind=StaticPageKind.POLICY` (`sitemaps/sitemaps.py:190-193`) because a property can't be queried. Parity currently holds and is locked by `test_seo_parity.py` (CAN-005 satisfied in practice). Hardening suggestion: a single `HREFLANG_EXEMPT_KINDS = frozenset({StaticPageKind.POLICY})` constant on the model, used by both the property and the sitemap filter, so adding a future exempt kind cannot desynchronize them.
- **L5 — Slug change on a never-published draft creates an active 301 to a 404.** `handle_slug_change` (`permalinks/signals.py:113-157`) has no "old slug was ever published" condition (SLUG-030); renaming a draft's slug writes a live `SlugRedirect` whose target permalink is inactive. Low impact (301→404 is not indexed), shared with products; gate on `published_at`/permalink existence in a follow-up.
- **L6 — Breadcrumb label "Home" is untranslated English** on translated pages (`views_pages.py:137`; same at `views.py:445`). Visible indexable text in a foreign language — ML-020 in spirit. Wrap with `gettext`.
- **L7 — Structured data: spec is silent for static pages — optional enhancement only.** Product pages emit `Product`(+`Offer`) and `BreadcrumbList` JSON-LD (`views.py:531-548`); static pages emit none. No `SD-*` requirement names static pages, so `ContactPage`/`AboutPage` JSON-LD is **not** required — record as optional enhancement, do not implement without a spec update. Separately noted: SD-006 (`Organization` "emitted on every page") is not implemented anywhere in the storefront — a preexisting engine-wide gap that predates this feature; track it as its own ticket, not against T029-SP.

---

## Review areas — verified-correct behavior (no finding)

1. **Head correctness.** `static_page.html:4-12` mirrors `product.html` exactly: `<title>` with `seo_title|default:title`, conditional meta description, og:title/og:type, `{{ block.super }}` preserved so the preview-mode noindex from `base.html:23-27` survives. Canonical is emitted by the sibling `seo_links` block (`base.html:13-18`) which child overrides cannot wipe; `{% canonical_url %}` builds an **absolute, language-correct** URL (`base_url(locale.language) + locale.path`, `permalinks_tags.py:193-202`) with the query string stripped — so `?sent=1` PRG variants and UTM params self-canonicalize to the clean URL (CAN-003 satisfied, no duplicate-URL surface). **Draft pages:** no permalink is ever created for drafts (`seed_pages_for_store` publishes nothing; `sync_static_page_permalink` only creates on publish), the resolver only matches `is_active=True` (`permalinks/resolver.py:76`), and the view has a defensive 404 (`views_pages.py:101`). Confirmed: drafts return a real 404 on every path — **noindex is not needed** for unreachable pages.
2. **hreflang exemption parity (CAN-005).** Both consumers derive from the single `kind` field: `{% hreflang_tags %}` short-circuits on `hreflang_exempt` (`permalinks_tags.py:114`), the sitemap clears alternates for policy `(content_type, object_id)` keys (`sitemaps.py:186-197,231-234`). Parity is test-locked from both sides (`test_seo_parity.py:75,95`). **Non-policy pages:** hreflang = active Permalink ∩ enabled StoreLanguage ∩ shipping-configured language (`hreflang.py:82-99`, `permalinks_tags.py:130-156`), so ML-011 routability holds — an unpublished FR translation has an inactive FR permalink and never appears as an hreflang target. CAN-007 single-member suppression and CAN-006 x-default are implemented identically in tag and sitemap.
3. **Sitemap.** Inclusion is automatic via active permalinks (SM-010): published pages appear per routable language, drafts excluded (`test_draft_page_absent_from_sitemap`), policy pages present without alternates (`test_policy_page_sitemap_alternates_empty`, `test_policy_page_stays_in_sitemap_loc_list`). lastmod concern → M3; spec conflict on alternate-language policy inclusion → H3.
4. **Translated slugs.** Default-language slug change: 301 + permalink update + resolver follow-through, all tested (`test_permalink_lifecycle.py:123-169`). Translated slug change → H2. Republish lifecycle → H1.
5. **Duplicate content across stores.** Assessed as recommendation-level for genuine legal boilerplate; real risk is the thin placeholder publish → M2.
6. **Nav tag.** `pages/templatetags/pages_tags.py` enforces everything in the tag, not the template: published + zone-flagged + **active Permalink for the request language** (`:97-99` — never links a 404, ML-011), labels from the published translation with no cross-language fallback (`:100-105`), hrefs composed via `permalink_url`'s shared prefix logic with slugs read from the Permalink table (`:39-43,71-76` — §III compliant, no hand-built or stored URLs), request-scoped cache keyed by (store, lang).
7. **Structured data** → L7 (optional; spec silent).

---

## Required actions before release

| # | Action | Owner | Blocking? |
|---|---|---|---|
| H1 | Republish reactivates translated permalinks with PUBLISHED translations + regression test | Developer, then After-Bug Test Agent | **Yes** |
| H2 | `SlugRedirect` on translated-permalink slug update in `_sync_translation_permalink` (fix once for product/collection/staticpage) + tests — or explicit human acceptance as a named engine-wide follow-up ticket | Developer / Human | **Yes (fix or explicitly accept)** |
| H3 | ~~Amend 07_multilingual_seo.md (CAN-002, §5 legal row, SM-030, TST-CAN-002) to the ADR-018 policy-page model; human approves~~ **RESOLVED (2026-07-10, human): spec amended** — CAN-002, §5 legal-pages row, SM-030 exclusion list, §1.5/§2 cross-refs, and TST-CAN-002 now match shipped ADR-018 behavior verbatim; no code change; `pages`+`sitemaps` suites green (171 tests). | Spec Agent / Human | Closed |
| M1 | Remove default-language fallback for translated indexable fields | Developer | Recommended pre-release |
| M2 | Placeholder-marker publish guard | Developer | Recommended pre-release |
| M3 | lastmod from content `updated_at` | Developer | Follow-up ticket OK |
| L1 | ~~Title separator diverges from META-001 (em dash vs shipped `\|`)~~ **RESOLVED (2026-07-10, human): spec amended** — 07_multilingual_seo.md META-001 (+ TST-META-001, 16_checkout.md SEO-003, 08_acceptance_tests.md AC-100) now specify `\|` engine-wide, matching every shipped template; zero code churn. | Spec Agent / Human | Closed |
| L2–L8 | Backlog / next SEO polish pass | — | No |

---

## Verdict

**PASS-WITH-FIXES** — release is gated on H1 (code + proven regression test), H2 (fix or explicit human acceptance with a named ticket), and H3 (spec amendment + human approval). No BLOCKER: the shipped pages cannot leak drafts, cannot emit hreflang to 404s, and canonicals are correct — the HIGH items concern lifecycle edges and spec consistency, not the steady-state indexable surface. M1/M2 are strongly recommended in the same ticket. Test suites `pages` + `sitemaps`: 131/131 passing at review time.
