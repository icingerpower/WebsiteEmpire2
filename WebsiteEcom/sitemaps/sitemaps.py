"""
Per-language sitemap data building (TICKET-026).

SM-001: /sitemap.xml per domain is an index; one child per published language.
SM-002: Each URL entry carries xhtml:link hreflang alternates from the same
        Permalink map — sitemap and page-level hreflang share one source.
SM-010: Included types — active permalinks only (products, collections, static pages,
        home pages). lastmod = last content-affecting update, not permalink creation
        time (SEO review M3): StaticPage entries use StaticPage.updated_at (or the
        newer of StaticPage/StaticPageTranslation.updated_at for a translated entry)
        via PermalinkSitemap.get_urls._lastmod_for.
SM-030: Excluded — inactive permalinks, A/B page-version URLs (TICKET-042, ADR-028 §4:
        implemented via a `.exclude(content_type=page_version_ct)` filter on the
        primary queryset — corrects this comment's earlier, stale attribution to
        TICKET-029), noindex pages.

All URL composition goes through base_url() (ADR-005 §3 — single resolver principle).

Two-query strategy per language:
1. Active Permalink rows for (store, lang_code) — the current language's URLs.
2. All active Permalink rows for the store — grouped by (content_type_id, object_id)
   to build cross-language hreflang alternates (SM-002, CAN-005).
"""

from __future__ import annotations

from dataclasses import dataclass, field

from django.contrib.contenttypes.models import ContentType
from django.db.models import Count

from catalog.models import Collection, CollectionProduct, Product, ProductPageVersion
from catalog.templatetags.catalog_tags import COLLECTION_INDEXABLE_THRESHOLD
from pages.models import HREFLANG_EXEMPT_KINDS, StaticPage, StaticPageTranslation
from permalinks.models import Permalink
from permalinks.resolver import base_url
from stores.models import StoreLanguage


@dataclass
class SitemapAlternate:
    """One xhtml:link rel="alternate" entry in a sitemap URL block (SM-002, CAN-005)."""

    lang_code: str
    href: str


@dataclass
class SitemapUrl:
    """Data for one <url> entry in a per-language sitemap XML."""

    loc: str
    lastmod: object  # datetime or None
    changefreq: str
    priority: float
    alternates: list[SitemapAlternate] = field(default_factory=list)


def _full_url(store_language: StoreLanguage, slug: str) -> str:
    """
    Build the full absolute URL for a (store_language, slug) pair.

    Delegates to base_url() — the ONLY place scheme + host + prefix are composed
    (ADR-005 §3).  Trailing slash is appended per URL-002.
    """
    return f"{base_url(store_language)}/{slug}/"


class PermalinkSitemap:
    """
    Sitemap data builder for one (store, language) pair.

    A/B page-version exclusion (SM-030, AC-093, TICKET-042, ADR-028 §4): the
    primary queryset excludes Permalink rows whose content_type is
    catalog.ProductPageVersion — those URLs canonicalize to the primary product
    URL (permalinks/templatetags/permalinks_tags.py canonical_url override) and
    must never appear in the sitemap (exactly one sitemap entry per product,
    AC-093 / TST-SM-030).

    hreflang x-default (CAN-006): points to the store's default language URL.
    If the object has no active permalink in the default language, x-default is omitted.

    hreflang alternate suppression (CAN-007): when fewer than two language-specific
    alternates qualify, the full alternates list (including x-default) is cleared.
    A single self-alternate is useless to Google and mirrors the behaviour of the
    {% hreflang_tags %} template tag.

    ShippingCountry filter (CAN-004(b)): hreflang alternates are only emitted for
    languages that have at least one ShippingCountry configured.  Mirrors the filter
    in permalinks/hreflang.py so sitemap alternates and page-level hreflang tags
    always agree.

    Policy-page hreflang exemption (ADR-018 D1 SEO, §XI legal no-op, CAN-005): alternates
    are cleared for any (content_type, object_id) that identifies a policy-kind StaticPage.
    This reads the SAME `StaticPage.hreflang_exempt` (kind-derived) source that
    {% hreflang_tags %} reads, via one extra query on StaticPage.kind — sitemap and
    page-level hreflang can never disagree.  Policy pages stay in the sitemap `loc` list
    (indexable in each language) — only the alternate links are suppressed.

    Changefreq and priority are store-wide defaults.  Per-page overrides are
    a future extension (not in v1 scope).
    """

    changefreq = "weekly"
    priority = 0.5

    def __init__(self, store, store_language: StoreLanguage) -> None:
        self.store = store
        self.store_language = store_language

    def get_urls(self, enabled_languages: list[StoreLanguage]) -> list[SitemapUrl]:
        """
        Return SitemapUrl list for this (store, language) pair.

        Args:
            enabled_languages: all enabled StoreLanguage rows for the store (across
                all domains), used to resolve hreflang alternates.  Must be
                pre-fetched with select_related("domain") by the caller.

        Returns an ordered list of SitemapUrl — one entry per active permalink.

        Two queries:
        1. Active Permalink rows for (store, lang_code), ordered by slug for
           deterministic XML output.
        2. All active Permalink rows with non-null object references for the
           store, for cross-language hreflang grouping (SM-002).
        """
        lang_code = self.store_language.lang_code

        # CAN-004(b): compute the set of languages that have at least one
        # ShippingCountry configured.  Matches the filter in permalinks/hreflang.py
        # exactly so that sitemap alternates and page-level hreflang tags always agree.
        # A language with no ShippingCountry is "not yet configured for a target market"
        # and must not appear in either hreflang source.
        configured_lang_codes = set(
            StoreLanguage.objects.filter(
                store=self.store,
                shipping_countries__isnull=False,
            ).values_list("lang_code", flat=True).distinct()
        )

        # Query 1 — active permalinks for this language, ordered for determinism.
        # SM-030 / TICKET-042: exclude ProductPageVersion permalinks — they
        # canonicalize to the primary product URL and must never be sitemapped.
        page_version_ct = ContentType.objects.get_for_model(ProductPageVersion)
        primary_qs = (
            Permalink.objects.for_store(self.store)
            .filter(lang=lang_code, is_active=True)
            .exclude(content_type=page_version_ct)
            .select_related("content_type")
            .order_by("slug")
        )
        primary_list = list(primary_qs)
        if not primary_list:
            return []

        # SM-030 / AC-092: exclude thin collection URLs from the sitemap.
        # A collection with fewer than COLLECTION_INDEXABLE_THRESHOLD active products
        # is noindex — page-level noindex and sitemap membership use the same threshold
        # so they can never disagree.
        # Collections are identified by their ContentType.  All other content types
        # (Product, special pages with content_type=None) pass through unchanged.
        collection_ct = ContentType.objects.get_for_model(Collection)
        collection_object_ids = [
            p.object_id
            for p in primary_list
            if p.content_type_id == collection_ct.pk and p.object_id is not None
        ]
        if collection_object_ids:
            # Single annotated query: replaces N individual COUNT(*) queries with one
            # SQL GROUP BY, eliminating the N+1 pattern.
            # for_store() is required: CollectionProduct is a StoreOwnedModel (ADR-001 §4).
            active_product_counts = dict(
                CollectionProduct.objects
                .for_store(self.store)
                .filter(
                    collection_id__in=collection_object_ids,
                    product__status=Product.STATUS_ACTIVE,
                )
                .values("collection_id")
                .annotate(n=Count("id"))
                .values_list("collection_id", "n")
            )
            indexable_collection_pks = {
                pk for pk in collection_object_ids
                if active_product_counts.get(pk, 0) >= COLLECTION_INDEXABLE_THRESHOLD
            }
            primary_list = [
                p for p in primary_list
                if not (
                    p.content_type_id == collection_ct.pk
                    and p.object_id not in indexable_collection_pks
                )
            ]

        # ADR-018 D1 SEO / §XI: exempt-kind StaticPage (content_type, object_id) keys
        # get zero hreflang alternates — one query, filtered by the SAME
        # HREFLANG_EXEMPT_KINDS constant that StaticPage.hreflang_exempt /
        # {% hreflang_tags %} read, instead of re-expressing "kind == POLICY"
        # independently (CAN-005 parity, SEO review L4).
        staticpage_ct = ContentType.objects.get_for_model(StaticPage)
        exempt_object_ids = set(
            StaticPage.objects.for_store(self.store)
            .filter(kind__in=HREFLANG_EXEMPT_KINDS)
            .values_list("pk", flat=True)
        )
        hreflang_exempt_keys = {
            (staticpage_ct.pk, pk) for pk in exempt_object_ids
        }

        # SM-010 (M3 fix): lastmod must reflect the last content-affecting update,
        # not Permalink.created_at (which never changes when the page body is
        # edited — it would freeze lastmod at first publish forever). For
        # StaticPage permalinks, use StaticPage.updated_at, or the newer of
        # StaticPage.updated_at / StaticPageTranslation.updated_at for a
        # translated-language entry. Two bulk queries, keyed for O(1) lookup per
        # permalink — no per-row query. Other content types (Product, Collection)
        # still fall back to Permalink.created_at; giving them the same real-lastmod
        # treatment is a pre-existing, acknowledged follow-up (not blocking here).
        static_page_updated_at: dict[int, object] = dict(
            StaticPage.objects.for_store(self.store).values_list("pk", "updated_at")
        )
        static_page_translation_updated_at: dict[tuple[int, str], object] = {
            (page_id, lc): updated
            for page_id, lc, updated in (
                StaticPageTranslation.objects.for_store(self.store)
                .values_list("page_id", "lang_code", "updated_at")
            )
        }

        def _lastmod_for(permalink) -> object:
            if permalink.content_type_id != staticpage_ct.pk:
                return permalink.created_at
            page_updated = static_page_updated_at.get(permalink.object_id)
            if page_updated is None:
                return permalink.created_at
            translation_updated = static_page_translation_updated_at.get(
                (permalink.object_id, permalink.lang)
            )
            if translation_updated is not None:
                return max(page_updated, translation_updated)
            return page_updated

        # Build lang_code → StoreLanguage lookup for hreflang URL composition.
        # CAN-004(b): intersect with configured_lang_codes so only shipping-configured
        # languages appear in alternates — same rule as permalinks/hreflang.py.
        lang_to_sl: dict[str, StoreLanguage] = {
            sl.lang_code: sl
            for sl in enabled_languages
            if sl.is_enabled and sl.lang_code in configured_lang_codes
        }

        # Determine the store's default language for x-default (CAN-006).
        default_sl: StoreLanguage | None = next(
            (sl for sl in enabled_languages if sl.is_default), None
        )

        # Query 2 — all active, object-bound permalinks for the store.
        # Grouped into: (content_type_id, object_id) → {lang_code: slug}
        alt_map: dict[tuple[int, int], dict[str, str]] = {}
        for p in (
            Permalink.objects.for_store(self.store)
            .filter(is_active=True, content_type__isnull=False, object_id__isnull=False)
        ):
            key = (p.content_type_id, p.object_id)
            if key not in alt_map:
                alt_map[key] = {}
            alt_map[key][p.lang] = p.slug

        # Build SitemapUrl list.
        urls: list[SitemapUrl] = []
        for p in primary_list:
            loc = _full_url(self.store_language, p.slug)
            alternates: list[SitemapAlternate] = []

            if (
                p.content_type_id is not None
                and p.object_id is not None
                and (p.content_type_id, p.object_id) not in hreflang_exempt_keys
            ):
                key = (p.content_type_id, p.object_id)
                lang_slug_map = alt_map.get(key, {})

                # Language alternates — emit only for languages that are enabled
                # and shipping-configured (SM-030, CAN-004(b)).
                for lc, slug in sorted(lang_slug_map.items()):
                    sl = lang_to_sl.get(lc)
                    if sl is None:
                        continue
                    alternates.append(SitemapAlternate(lang_code=lc, href=_full_url(sl, slug)))

                # x-default — default language URL (CAN-006).
                # Omitted when the object has no active permalink in the default language,
                # or when the default language has no ShippingCountry rows (CAN-004(b)):
                # page-level hreflang would also omit x-default in that case, so both
                # sources must agree (CAN-005).
                if default_sl and default_sl.lang_code in lang_slug_map and default_sl.lang_code in configured_lang_codes:
                    default_slug = lang_slug_map[default_sl.lang_code]
                    alternates.append(SitemapAlternate(
                        lang_code="x-default",
                        href=_full_url(default_sl, default_slug),
                    ))

                # CAN-007: suppress single-member hreflang groups — same rule as the
                # {% hreflang_tags %} template tag.  x-default is not counted as a
                # language member; a single self-alternate is not useful to Google.
                lang_member_count = sum(
                    1 for a in alternates if a.lang_code != "x-default"
                )
                if lang_member_count < 2:
                    alternates = []

            urls.append(SitemapUrl(
                loc=loc,
                lastmod=_lastmod_for(p),
                changefreq=self.changefreq,
                priority=self.priority,
                alternates=alternates,
            ))

        return urls
