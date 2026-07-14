"""
Template tags for the permalinks app (TICKET-024 + TICKET-025, ADR-005 §4).

{% hreflang_tags content_object %}
    Renders <link rel="alternate" hreflang="xx" href="..."> for all published
    translations of content_object.  Requires request.store to be set by
    HostResolutionMiddleware.

    Intersection rule (AC-091): only emits hreflang for languages that are both
    (a) active in the Permalink table (is_active=True) and (b) enabled in
    StoreLanguage (is_enabled=True).  Never emits hreflang for pending translations.

    x-default: added for the store's default language (is_default=True).

    Caching (ADR-005 §4): passes request to get_hreflang_entries() so the
    Permalink query is performed at most once per (content_type, object_id) pair
    per HTTP request.

{% canonical_url %}
    Renders <link rel="canonical" href="..."> for the current page.

    The canonical URL is composed from request.locale.language (for the base URL)
    and request.locale.path (the clean URL path with the language prefix stripped
    and no query string — so UTM parameters are automatically excluded, AC-090).

    A/B page-version EXCEPTION (TICKET-042, ADR-028 §4, DECIDED — this is a
    deliberate, spec-mandated divergence from {% og_url %}, not a bug):
    storefront.views.product_page sets request.seo_canonical_path when rendering
    a ProductPageVersion:
      - a real path (e.g. "/blue-dress/")  → canonical points to the PRIMARY
        product URL in the same language, never to the version's own URL
        (FM-C4 — canonicalizing to self would be the duplicate-content bug this
        entire feature exists to prevent).
      - ""  (empty string, set when the primary permalink could not be resolved)
        → NO canonical tag is rendered at all. Never fall back to the current
        URL in this case (§XV-1 — loud, not silently wrong).
    When request.seo_canonical_path is not set at all (every page except an
    active page-version render), behaviour is unchanged: the current URL is
    the canonical (see _resolve_canonical_url below).

{% og_url %}
    Renders <meta property="og:url" content="..."> for the current page.

    META-004 / SEO review L3 normally require og:url to equal canonical — and
    they still agree on every ordinary page. The ONE deliberate exception is a
    page-version render (TICKET-042, ADR-028 §4): og:url stays the version's
    OWN URL (Pinterest needs a distinct pin per image set) while {% canonical_url %}
    points at the primary. This tag intentionally does NOT read
    request.seo_canonical_path — it always uses _resolve_canonical_url() (the
    current URL) so a future refactor does not accidentally "fix" this back to
    self-canonical divergence-free agreement and break META-004's own exception.

{% og_locale %}
    Renders <meta property="og:locale" content="..."> using the og:locale format
    (language_TERRITORY, e.g. fr_FR, en_US, pt_BR).

    Mapping follows the spec:
      en → en_US, fr → fr_FR, de → de_DE, pt-br → pt_BR.
    Unmapped single-code langs fall back to <lang>_<LANG.upper()> (e.g. es → es_ES).
    Unmapped region-suffix langs use the region: zh-tw → zh_TW.

{% seo_head content_object %}
    Combined tag: renders canonical, hreflang, and og:locale in a single call.
    Delegates to the three sub-tags above via seo_head.html.
"""

from django import template

from permalinks.hreflang import get_hreflang_entries
from permalinks.resolver import base_url
from stores.models import StoreLanguage

register = template.Library()


# ---------------------------------------------------------------------------
# og:locale mapping (TST-T025)
# ---------------------------------------------------------------------------

# Explicit overrides where the default <lang>_<LANG.upper()> fallback is wrong.
# en_EN is not a real locale — the standard is en_US for the Open Graph protocol.
_OG_LOCALE_MAP = {
    "en": "en_US",
    "fr": "fr_FR",
    "de": "de_DE",
    "pt-br": "pt_BR",
}


def _lang_to_og_locale(lang_code: str) -> str:
    """
    Map an ISO 639-1 lang_code (e.g. 'fr', 'pt-br') to og:locale format
    (language_TERRITORY, e.g. 'fr_FR', 'pt_BR').

    Resolution order:
      1. Explicit entry in _OG_LOCALE_MAP (handles the en → en_US special case).
      2. Region-suffix codes (e.g. zh-tw → zh_TW): split on '-', upper the region.
      3. Fallback: <lang>_<LANG.upper()> (e.g. es → es_ES).
    """
    mapped = _OG_LOCALE_MAP.get(lang_code)
    if mapped:
        return mapped
    if "-" in lang_code:
        lang, region = lang_code.split("-", 1)
        return f"{lang}_{region.upper()}"
    return f"{lang_code}_{lang_code.upper()}"


# ---------------------------------------------------------------------------
# {% hreflang_tags content_object %}  (TICKET-024 — updated for T025 caching)
# ---------------------------------------------------------------------------

@register.inclusion_tag("permalinks/hreflang_tags.html", takes_context=True)
def hreflang_tags(context, content_object):
    """
    Render hreflang <link> elements for all published, enabled translations of content_object.

    Usage in templates::

        {% load permalinks_tags %}
        {% hreflang_tags product %}

    Requires the request object to be available in the template context
    (use RequestContext / render() in views, or TEMPLATE_CONTEXT_PROCESSORS).

    §XI legal no-op (ADR-018 D1 SEO): when content_object.hreflang_exempt is True
    (policy-kind StaticPages), no alternates are rendered at all — this is the ONE
    place that check happens for page-level hreflang; the sitemap builder
    (sitemaps/sitemaps.py) reads the same `kind`-derived property independently so
    the two sources can never disagree (CAN-005).
    """
    if getattr(content_object, "hreflang_exempt", False):
        return {"entries": []}

    request = context.get("request")
    if not request or not hasattr(request, "store"):
        return {"entries": []}

    store = request.store
    # Pass request for request-scoped caching (ADR-005 §4 — never one query per tag).
    entries_raw = get_hreflang_entries(
        store, type(content_object), content_object.pk, request=request
    )

    if not entries_raw:
        return {"entries": []}

    # Fetch all enabled StoreLanguages for the store in one query, keyed by lang_code.
    # select_related('domain') avoids a per-language extra query in base_url().
    lang_map = {
        sl.lang_code: sl
        for sl in StoreLanguage.objects.select_related("domain").filter(
            store=store, is_enabled=True
        )
    }

    # Determine the default language for x-default (AC-091).
    default_lang = next(
        (lc for lc, sl in lang_map.items() if sl.is_default),
        None,
    )

    entries = []
    for e in entries_raw:
        sl = lang_map.get(e["lang"])
        if sl is None:
            # Language is disabled or not configured — omit from hreflang (AC-091, AC-103).
            continue
        url = base_url(sl) + "/" + e["slug"] + "/"
        entries.append({
            "lang": e["lang"],
            "url": url,
            "is_default": e["lang"] == default_lang,
        })

    # CAN-007: a single-entry hreflang group is malformed per Google guidelines.
    # Google treats it as an error and ignores the tags entirely.  Emit nothing
    # rather than a broken single-entry group.
    if len(entries) < 2:
        return {"entries": []}

    return {"entries": entries}


# ---------------------------------------------------------------------------
# {% canonical_url %}  (TICKET-025, AC-090, AC-093)
# ---------------------------------------------------------------------------

def _resolve_canonical_url(request) -> str:
    """
    Compute the current page's own canonical URL, or "" if it cannot be resolved.

    Shared by {% canonical_url %} and {% og_url %} (SEO review L3, META-004:
    og:url must equal canonical) so the two tags can never disagree — one
    resolution path, not two independently-maintained copies.

    Requires request.locale to be set by LocaleMiddleware (stores.middleware).
    The locale carries the stripped path (no language prefix, no query string),
    so UTM parameters and other query strings are automatically excluded (AC-090).

    Returns "" if request.locale is not set — this covers admin pages, error
    pages, and any path skipped by LocaleMiddleware.
    """
    if not request or not hasattr(request, "locale"):
        return ""

    locale = request.locale
    # base_url returns 'https://host' or 'https://host/lang_code' (no trailing slash).
    # locale.path has a leading slash (e.g. '/blue-dress/' or '/collections/summer/').
    # Concatenation gives: 'https://example.com/blue-dress/' — correct (AC-090).
    # Guard against mock/lightweight locale objects (e.g. from RequestFactory tests)
    # that carry lang_code but not a full StoreLanguage with domain FK loaded.
    try:
        return base_url(locale.language) + locale.path
    except (AttributeError, TypeError):
        return ""


def _resolve_canonical_target_url(request) -> str:
    """
    Compute the canonical target URL, honouring the page-version override
    (TICKET-042, ADR-028 §4).

    request.seo_canonical_path distinguishes three states via getattr's default:
      - attribute absent (getattr returns None) → not a page-version render;
        canonicalize to the current URL, exactly as before this ticket
        (_resolve_canonical_url).
      - "" (empty string) → a page-version render whose primary permalink could
        NOT be resolved (data corruption). Return "" so NO canonical tag is
        rendered — never fall back to self-canonical (FM-C4, §XV-1).
      - a real path (e.g. "/blue-dress/") → a page-version render whose primary
        WAS resolved. Compose base_url(request.locale.language) + path so the
        canonical is the PRIMARY product URL, same language, absolute, with the
        trailing slash storefront.views.product_page already included.
    """
    override_path = getattr(request, "seo_canonical_path", None) if request else None
    if override_path is None:
        return _resolve_canonical_url(request)
    if override_path == "":
        return ""
    locale = getattr(request, "locale", None)
    if locale is None:
        return ""
    try:
        return base_url(locale.language) + override_path
    except (AttributeError, TypeError):
        return ""


@register.inclusion_tag("permalinks/canonical_url.html", takes_context=True)
def canonical_url(context):
    """
    Render <link rel="canonical" href="..."> for the current page.

    Usage in templates::

        {% load permalinks_tags %}
        {% canonical_url %}

    See _resolve_canonical_target_url() for the page-version override rules
    (TICKET-042, ADR-028 §4) and _resolve_canonical_url() for the default
    (current-URL) composition rules.
    """
    return {"canonical_url": _resolve_canonical_target_url(context.get("request"))}


@register.inclusion_tag("permalinks/og_url.html", takes_context=True)
def og_url(context):
    """
    Render <meta property="og:url" content="..."> for the current page (META-004).

    Usage in templates::

        {% load permalinks_tags %}
        {% og_url %}

    Uses _resolve_canonical_url() (the current URL), NOT _resolve_canonical_target_url()
    — on every ordinary page that is the same URL {% canonical_url %} renders, so og:url
    and canonical agree (SEO review L3). The ONE deliberate exception is a page-version
    render (TICKET-042, ADR-028 §4): {% canonical_url %} overrides to the PRIMARY product
    URL via request.seo_canonical_path (FM-C4 — self-canonical would be duplicate
    content), while {% og_url %} intentionally does NOT read that override and stays on
    the version's OWN URL (Pinterest needs a distinct pin per image set) — see the
    module docstring's "{% og_url %}" section above for the full rationale.
    """
    return {"og_url": _resolve_canonical_url(context.get("request"))}


# ---------------------------------------------------------------------------
# {% og_locale %}  (TICKET-025)
# ---------------------------------------------------------------------------

@register.inclusion_tag("permalinks/og_locale.html", takes_context=True)
def og_locale(context):
    """
    Render <meta property="og:locale" content="..."> for the current language.

    Usage in templates::

        {% load permalinks_tags %}
        {% og_locale %}

    Requires request.locale to be set by LocaleMiddleware.
    Returns an empty context (no output) if request.locale is not set.
    """
    request = context.get("request")
    if not request or not hasattr(request, "locale"):
        return {"og_locale": ""}

    lang_code = request.locale.language.lang_code
    return {"og_locale": _lang_to_og_locale(lang_code)}


# ---------------------------------------------------------------------------
# {% seo_head content_object %}  (TICKET-025, ADR-005 §4)
# ---------------------------------------------------------------------------

@register.inclusion_tag("permalinks/seo_head.html", takes_context=True)
def seo_head(context, content_object):
    """
    Render the full SEO <head> block: canonical URL, hreflang tags, og:locale.

    Usage in templates::

        {% load permalinks_tags %}
        {% seo_head product %}

    This tag delegates to the three sub-tags ({% canonical_url %},
    {% hreflang_tags content_object %}, {% og_locale %}) via seo_head.html.
    It passes request and content_object into the sub-template's context so
    the sub-tags can access them.

    Requires request.locale and request.store to be set by LocaleMiddleware.
    Legal pages may override the {% block seo_head %} with a no-op to suppress
    all alternate-language indexing (ADR-005 §4).
    """
    return {
        "request": context.get("request"),
        "content_object": content_object,
    }


# ---------------------------------------------------------------------------
# {% permalink_url slug %}  (Fix 4 — internal links on path-prefixed languages)
# ---------------------------------------------------------------------------


@register.simple_tag(takes_context=True)
def permalink_url(context, slug):
    """
    Build a storefront URL path for the given slug with the correct language prefix.

    For path-prefixed languages (e.g. /fr/), returns '/fr/slug/'.
    For root languages, returns '/slug/'.
    Returns '/' (or '/lang_code/') when slug is empty (language root).

    Usage in templates::

        {% load permalinks_tags %}
        <a href="{% permalink_url item.slug %}">...</a>

    Requires the request object to be present in the template context
    (set by RequestContext / the ``django.template.context_processors.request``
    context processor, which is enabled in all Pradize storefront templates).

    This tag does NOT perform a Permalink DB lookup — it expects the slug to be
    pre-computed by the view (e.g. via ``_build_product_slug_map``).  Keeping
    the lookup in the view avoids N+1 queries on collection / search pages that
    render many product cards.

    Args:
        context: template context supplied automatically by takes_context=True.
        slug:    pre-computed URL slug string (no leading or trailing slashes),
                 as stored in the Permalink table.
    """
    request = context.get("request")
    locale = getattr(request, "locale", None) if request else None
    lang_obj = getattr(locale, "language", None) if locale else None

    if lang_obj is not None and getattr(lang_obj, "use_path_prefix", False):
        lang_code = getattr(lang_obj, "lang_code", "")
        if lang_code:
            return f"/{lang_code}/{slug}/" if slug else f"/{lang_code}/"

    return f"/{slug}/" if slug else "/"
