"""
Sitemap and robots.txt views (TICKET-026).

All three views rely on request.locale set by stores.middleware.LocaleMiddleware
to scope queries to the correct store and domain.

sitemap_index_view  — /sitemap.xml  (SM-001: index per domain, one entry per language)
sitemap_language_view — /sitemap-<lang>.xml  (SM-001/SM-002/SM-010/SM-030)
robots_txt_view     — /robots.txt   (ROB-001/ROB-002/ROB-003)
"""

from __future__ import annotations

from django.http import HttpResponse
from django.shortcuts import render
from django.views.decorators.cache import cache_page
from django.views.decorators.http import require_GET

from sitemaps.sitemaps import PermalinkSitemap
from stores.models import StoreLanguage


@require_GET
def sitemap_index_view(request):
    """
    Serve /sitemap.xml — sitemap index for the current domain (SM-001).

    Lists one child sitemap per enabled language on this domain.
    The domain is resolved from request.locale.domain (set by LocaleMiddleware).

    SM-001: Single-language domains still use the index + one child (uniform pipeline).
    """
    locale = getattr(request, "locale", None)
    if locale is None:
        return HttpResponse(status=503)

    domain = locale.domain
    domain_host = domain.host

    # Enabled languages on THIS domain only (SM-001: per-domain index).
    languages = list(
        StoreLanguage.objects
        .filter(domain=domain, is_enabled=True)
        .order_by("lang_code")
    )

    sitemap_entries = [
        {
            "lang_code": lang.lang_code,
            "loc": f"https://{domain_host}/sitemap-{lang.lang_code}.xml",
        }
        for lang in languages
    ]

    context = {"sitemaps": sitemap_entries}
    return render(
        request,
        "sitemaps/sitemap_index.xml",
        context,
        content_type="application/xml; charset=utf-8",
    )


@cache_page(60 * 60)  # 1-hour cache (SM-020); production should use Redis.
@require_GET
def sitemap_language_view(request, lang_code: str):
    """
    Serve /sitemap-<lang_code>.xml — per-language sitemap for the current domain (SM-001).

    Returns 404 when lang_code is not an enabled StoreLanguage on this domain.

    hreflang alternates (SM-002): fetched across all enabled languages of the store
    (not just this domain) so that cross-domain hreflang groups are complete.

    A/B-variant exclusion (SM-030): pending TICKET-029 — see PermalinkSitemap docstring.
    """
    locale = getattr(request, "locale", None)
    if locale is None:
        return HttpResponse(status=503)

    store = locale.store

    try:
        store_language = StoreLanguage.objects.select_related("domain").get(
            domain=locale.domain, lang_code=lang_code, is_enabled=True
        )
    except StoreLanguage.DoesNotExist:
        return HttpResponse(status=404)

    # All enabled languages for this store (across all domains) — SM-002 alternates.
    enabled_languages = list(
        StoreLanguage.objects.filter(store=store, is_enabled=True)
        .select_related("domain")
    )

    sitemap = PermalinkSitemap(store, store_language)
    urls = sitemap.get_urls(enabled_languages)

    context = {"urls": urls}
    return render(
        request,
        "sitemaps/sitemap.xml",
        context,
        content_type="application/xml; charset=utf-8",
    )


@require_GET
def robots_txt_view(request):
    """
    Serve /robots.txt — generated per domain (ROB-001/ROB-002/ROB-003).

    Default rules per ROB-001 are always emitted.  Custom lines appended from
    store general settings per ROB-002 are a TODO pending the general settings
    model (TICKET-007).

    The Sitemap: line uses the domain from request.locale.domain.host so that
    the URL is always the correct absolute domain URL (ROB-001).

    `/_consent/` and `/_analytics/` (webecom/urls.py) are internal POST-only
    endpoints, never linked from an <a href>. Disallowing them here is pure
    crawl-error hygiene (SEO consent banner review F1, docs/seo/
    CONSENT_BANNER_SEO_REVIEW.md) — a 405 on GET is harmless either way.
    """
    locale = getattr(request, "locale", None)
    if locale is not None:
        domain_host = locale.domain.host
    else:
        # Defensive fallback: use the Host header (strip port).
        domain_host = request.get_host().split(":")[0]

    lines = [
        "User-agent: *",
        "Disallow: /cart/",
        "Disallow: /checkout/",
        "Disallow: /orders/",
        "Disallow: /search",
        "Disallow: /admin/",
        "Disallow: /superadmin/",
        "Disallow: /feeds/",
        "Disallow: /_consent/",
        "Disallow: /_analytics/",
        f"Sitemap: https://{domain_host}/sitemap.xml",
    ]

    # ROB-002: append custom robots.txt lines from general settings.
    # TODO: read store_settings.robots_txt_custom once TICKET-007 implements
    #       the store general settings model.  The custom text is appended
    #       after the engine defaults; defaults cannot be removed via the textarea.

    return HttpResponse("\n".join(lines) + "\n", content_type="text/plain")
