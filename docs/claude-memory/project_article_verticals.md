---
name: article-page-type-verticals
description: "Health/Fashion split of the Article page type via a shared PageTypeArticleBase, per-engine page-type scoping, and how PaneTaxonomies now respects it"
metadata: 
  node_type: memory
  type: project
  originSessionId: f918aa22-775f-4416-ab29-0f92b87429d1
  modified: 2026-09-03T12:27:28.698Z
---

Built 2026-09-03, prompted by ideamoda (a fashion site) needing the Article
editor without the health-specific symptom-links picker, and by
`PaneTaxonomies` showing an irrelevant "Symptoms" card on any non-health site.

## Class structure
- `PageTypeArticleBase` (`WebsiteEmpireLib/website/pages/`) — shared base, NOT
  registered via `DECLARE_PAGE_TYPE` (no TYPE_ID, can't be constructed via
  `createForTypeId()`). Holds 7 generic blocs (Category, Text, Social,
  AutoLink, CategoryLinks, SocialMedia, Meta) in a *protected* `m_blocs`
  list, plus every generic virtual (buildHeadMetaTags, prepareJsonLdImage,
  hasSvg, addInnerTopCode, autoSeoTitle/Description, etc.).
- `PageTypeArticleHealth` — was `PageTypeArticle` before this split, renamed.
  **`TYPE_ID = "article"` deliberately unchanged** — it's what's persisted in
  `pages.typeId`/`strategies.json`, so the rename was a zero-migration pure
  C++ identifier change. Appends `m_symptomLinksBloc` to the inherited
  `m_blocs`, overrides `getRenderBlocs()` to reposition it between category
  and text.
- `PageTypeArticleFashion` — `TYPE_ID = "article_fashion"`. No extra blocs,
  no overrides at all — inherits the base's 7 blocs unchanged. This is the
  template for any future vertical with no extra blocs.
- `PageTypeLegal` still inherits `PageTypeArticleHealth` (unchanged
  behavior, including the symptom-links bloc it never used before either —
  not restructured, out of scope for this pass).

## Per-site scoping via Engine, not global registry
`AbstractEngine::getPageTypes()` (already existed) is the mechanism for
"which page types does this site use" — `EngineArticles` = Health article +
JsApp, new `EngineArticlesFashion` = Fashion article only. Engine choice
happens once via the existing `DialogPickEngine` at site creation (lists
`ALL_ENGINES()` generically, no changes needed there).

`PaneTaxonomies` was rewired from iterating the *global*
`AbstractPageType::allTypeIds()` registry to iterating the *active engine's*
`getPageTypes()` (`PaneTaxonomies::setup()` now takes the engine). A fashion
site (`EngineArticlesFashion`) shows zero taxonomy cards; a health site
(`EngineArticles`) still shows exactly "Symptoms," unchanged. This also
simplified `PaneTaxonomies` — it no longer owns its own `AbstractPageType`/
`CategoryTable` instances, since the engine already owns live ones.

## Known follow-up, not done
`DialogAddGeneration`'s page-type combo (`WebsiteEmpire/gui/dialogs/`) still
lists every globally-registered type unconditionally — same
engine-scoping fix would apply there if wanted. Left out to keep this
change's blast radius to what was asked.

## Multi-taxonomy + translation (built 2026-09-03, right after the above)

`AbstractPageBloc::taxonomy()` (singular) was extended with a plural
`taxonomies()` (default wraps the singular — zero change needed for
Symptoms). `TaxonomyDescriptor` gained `bool translatable = false`.
`syncTaxonomy()` gained a leading `taxonomyId` param so one bloc can sync N
independent vocabularies.

New `PageBlocFashionTaxonomyLinks` (data-driven over 5 dimensions: Color,
Season, Occasion, Material, Style Aesthetic — each own `taxonomyId`/
`hubPrefix`/aspire `sourceColumn`, all `translatable=true`) is now bloc #8
on `PageTypeArticleFashion` — mirrors `PageBlocSymptomLinks` exactly,
including its defensive `engine.isPageAvailable()` no-op: it renders
nothing until hub pages (`/colors/<slug>` etc.) exist. **Hub-page generation
itself was NOT built** — a `SymptomHubSyncer`-equivalent subsystem
(dirty-set tracking, index pages, routing) is a separate, larger follow-up.

`PaneTaxonomies` now shows one card per `taxonomies()` entry (was
one-per-bloc) — a Fashion site shows 5 cards, each independently
synced/browsed.

Translation machinery was found ALREADY mostly built and unused:
`TaxonomyDb` already had a full translation API (`setTranslation`/
`translationFor`/`loadTranslated`), and `TaxonomyTranslator` was already a
complete generic batched-CLI translator wired into `--translateCommon` —
it just translated `taxonomyDb->allTypes()` unconditionally (why nothing
was ever translated: never run, not a code gap). Added
`TaxonomyTranslationFilter::filterTranslatable()` (pure function, tested
directly against the real `EngineArticles`/`EngineArticlesFashion`
fixtures) to gate `--translateCommon` to only `translatable==true`
taxonomies — Symptoms opts out by construction (never set the flag), no
special-casing needed. Verified as a side-finding: symptom hub permalinks
are already built from the English name via `SymptomNav::slugify()`, never
the translated display text — translation was never actually a permalink
risk, though leaving Symptoms untranslated was still the right call to
avoid touching working prod behavior.

## Fashion tag hub pages (built 2026-09-03, completing the interlinking loop)

`SymptomHubSyncer`/`PageTypeSymptomHub` was investigated and rejected as a
template — it's a reference-data page (conditions matching a symptom, from
a separate aspire cross-reference table), not an article-listing hub. The
correct template, confirmed and mirrored, is `CategoryHubSyncer`/
`PageTypeCategory`/`PageBlocHubGrid` — a crash-safe, dirty-set-driven hub
that lists member articles by CTR → views → recency.

New: `PageTypeFashionTagHub` (ONE type, `TYPE_ID="fashion_tag_hub"` — the
specific dimension/tag value are page DATA, not baked into 5 classes, same
pattern `category_hub` already uses), `PageBlocFashionHubGrid` (mirrors
`PageBlocHubGrid`, membership = "article's `PageBlocFashionTaxonomyLinks`
selection for this hub's dimension includes this hub's tag value", matched
by key **suffix** not exact/hardcoded bloc index — tolerant to bloc reorder,
same convention `CategoryHubSyncer`/`PageBlocHubGrid` already use for
`"*_categories"`), `FashionHubDirtySet` (new sibling to
`CategoryHubDirtySet`, not a refactor of it — deliberately, to avoid risking
working category-hub crash-safety code for an unrelated feature),
`FashionTaxonomyHubSyncer` (one syncer, all 5 dimensions via
`PageBlocFashionTaxonomyLinks::dimensions()`; stub vocabulary sourced from
`TaxonomyDb::load(dimension)` — the already-synced local cache — not a
second direct aspire-DB read).

**Real bug caught mid-implementation, not anticipated in planning:**
`AbstractPageType::save()`/`load()` prefix every bloc's storage keys with
its bloc index ("N_") before hitting `page_data` — code operating on RAW
page_data outside the bloc object model (syncers, `PageGenerator.cpp`) must
know this and either hardcode the index (bloc's own known-fixed index, e.g.
`PageBlocFashionHubGrid` is always index 0 in its own page type → hardcode
`"0_"`, exactly matching `CategoryHubSyncer.cpp`'s existing `"0_categories"`
precedent) or suffix-match (when reading a *different* page type's bloc
whose index isn't controlled here, e.g. reading an arbitrary Fashion
article's tag selections → match by `endsWith("_" + dimension)` instead of
assuming `PageBlocFashionTaxonomyLinks`'s bloc index 7). Getting this wrong
produces permanently-empty hub pages with no error — caught only because a
test asserted a real write→read round trip through the repository instead
of mocking it.

`PageBlocFashionTaxonomyLinks::slugify()` was promoted from private to a
public static (also used by `FashionTaxonomyHubSyncer` and
`PageGenerator.cpp`'s translated-permalink branch) so a tag's outbound link
and its hub's own permalink can never disagree on the slug — deliberately
a separate implementation from `categoryHubSlug()`/`SymptomNav::slugify()`,
not shared, since each vertical's slug rule is allowed to evolve
independently.

Hub URLs are **translated per language** (user's explicit choice, matching
Category hubs, not Symptoms) — safe because the generic `redirects` table +
`outPathOverride` mechanism in `PageGenerator.cpp` already handles a
translation being edited later, with zero Fashion-specific redirect code
needed.

**Not wired:** `onPageSaved()`-triggered dirty-marking exists and is tested
but isn't connected to any live "article saved" GUI event — same gap
`CategoryHubSyncer::onPageSaved()` already has; no such hook exists for hub
types in general today. Stub creation and stats-staleness checks run via
the existing `syncStubs()`-adjacent call sites in `LauncherPublish`,
`PaneDomains::deployLocally()`, `PaneGeneratedPages`.

See [[project_raster_image_generation]] for the sibling feature (strategy-driven
image generation) built the same week, usable by any Article vertical since it
lives in `GenStrategyTable`/`GenPageQueue`, not in the page type itself.
