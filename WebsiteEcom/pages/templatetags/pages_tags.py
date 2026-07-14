"""
{% static_page_links zone %} — flag-driven header/footer nav links (ADR-018 D6).

No menu builder exists (or is planned for v1) — StaticPage.show_in_header /
show_in_footer + nav_position flags are the whole mechanism (ADR-018 D6). All
routability/label/href rules are enforced HERE, not in the template (design-pattern-
ideas.txt §VI/§III — object refs resolved through the one permalink mechanism, never
a stored URL).

Rules:
  - Emits only pages that are is_published=True, flagged for the requested zone, AND
    have an active Permalink for the request language — Permalink presence is the
    routability test (ML-011); a page must never be linked if it would 404.
  - Label: published StaticPageTranslation.title for the request lang; source `title`
    when the request lang IS the store's default language. No cross-language fallback.
  - Hrefs are composed via the same permalink_url tag internals used everywhere else
    (never a stored URL, design-pattern-ideas.txt §III).
  - At most 3 queries (2 in the common default-language case: pages + permalinks;
    the store's default language is read from request.store.primary_language, which
    is already loaded — no extra query needed to detect it). Results are cached on
    the request object per (store, lang) so calling the tag twice per request
    (header AND footer) never re-queries (mirrors _seo_hreflang_cache).
  - Ordered by nav_position, then pk.
"""

from django import template

register = template.Library()

_CACHE_ATTR = "_static_page_links_cache"


def _lang_code(request) -> str:
    locale = getattr(request, "locale", None)
    lang_obj = getattr(locale, "language", None) if locale else None
    return getattr(lang_obj, "lang_code", "en") if lang_obj else "en"


def _build_href(context, slug: str) -> str:
    """Compose the storefront URL for a slug, reusing {% permalink_url %}'s logic."""
    from permalinks.templatetags.permalinks_tags import permalink_url

    return permalink_url(context, slug)


def _load_links(context, request, store, lang: str) -> list:
    """
    Load all published, header-or-footer-flagged pages with an active Permalink for
    `lang`, along with the correct-language label and href.

    Returns a list of dicts: {"href": str, "label": str, "show_in_header": bool,
    "show_in_footer": bool}, ordered by nav_position, pk.
    """
    from django.contrib.contenttypes.models import ContentType
    from django.db.models import Q

    from pages.models import StaticPage
    from permalinks.models import Permalink

    pages = list(
        StaticPage.objects.for_store(store)
        .filter(is_published=True)
        .filter(Q(show_in_header=True) | Q(show_in_footer=True))
        .order_by("nav_position", "pk")
    )
    if not pages:
        return []

    page_ids = [p.pk for p in pages]
    ct = ContentType.objects.get_for_model(StaticPage)
    slug_map = {
        p.object_id: p.slug
        for p in Permalink.objects.for_store(store).filter(
            content_type=ct, object_id__in=page_ids, lang=lang, is_active=True,
        )
    }

    # Translation titles are only needed for non-default languages (the default
    # language is served directly from StaticPage.title — no translation row exists
    # for it). store.primary_language is already loaded on the request's store, so
    # this check costs no extra query.
    title_map: dict = {}
    is_default_lang = lang == getattr(store, "primary_language", None)
    if not is_default_lang:
        from catalog.models import TranslationStatus
        from pages.models import StaticPageTranslation

        title_map = {
            t.page_id: t.title
            for t in StaticPageTranslation.objects.for_store(store).filter(
                page_id__in=page_ids, lang_code=lang, status=TranslationStatus.PUBLISHED,
            )
        }

    links = []
    for page in pages:
        slug = slug_map.get(page.pk)
        if slug is None:
            continue  # No active permalink for this language — never link a 404 (ML-011).
        if is_default_lang:
            label = page.title
        else:
            label = title_map.get(page.pk)
            if not label:
                continue  # No published translation for this lang — no fallback label.
        links.append({
            "href": _build_href(context, slug),
            "label": label,
            "show_in_header": page.show_in_header,
            "show_in_footer": page.show_in_footer,
        })
    return links


@register.inclusion_tag("storefront/partials/static_page_links.html", takes_context=True)
def static_page_links(context, zone):
    """
    Render nav links for the given zone ("header" or "footer").

    Usage in templates::

        {% load pages_tags %}
        {% static_page_links "header" %}
    """
    request = context.get("request")
    store = getattr(request, "store", None) if request else None
    if store is None:
        return {"links": []}

    cache = getattr(request, _CACHE_ATTR, None)
    if cache is None:
        cache = {}
        setattr(request, _CACHE_ATTR, cache)

    lang = _lang_code(request)
    cache_key = (store.pk, lang)
    if cache_key not in cache:
        cache[cache_key] = _load_links(context, request, store, lang)

    zone_field = "show_in_header" if zone == "header" else "show_in_footer"
    return {"links": [link for link in cache[cache_key] if link[zone_field]]}
