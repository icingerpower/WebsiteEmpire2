"""
Permalink resolution and locale resolution (ADR-005 §3, ADR-008 §2).

## PermalinkResolver

The only authorised way to resolve an internal URL.

Resolution order for a given (store, lang, slug):
    1. Active Permalink row (store, lang, slug, is_active=True) → serve the page.
    2. Active SlugRedirect row (store, from_lang=lang, from_slug=slug, is_active=True):
       - redirect_type permanent or temporary → return ResolvedRedirect (301 or 302).
       - redirect_type 'none' → return None (intentionally dead URL, treat as 404).
    3. Neither → None (404).

All lookups are scoped to the store via .for_store() — never cross-store
(ADR-001 §4, StoreScopedManager).

Returns:
    ResolvedPage      — a page to render.
    ResolvedRedirect  — a redirect response.
    None              — 404 (no permalink, no active redirect, or redirect_type='none').

## Locale resolution (ADR-008 §2a, §2b)

resolve_locale(host, path) — the ONLY place that maps Host + URL path prefix →
(store, domain, language).  Nothing else in the codebase may perform this mapping.

base_url(language) — the ONLY place that composes scheme + host + language prefix.
Consumers append '/' + slug + '/' to form full page URLs.
"""

from __future__ import annotations

import logging
from dataclasses import dataclass

from django.http import Http404

from permalinks.models import Permalink, SlugRedirect, http_status_for

logger = logging.getLogger("permalinks.resolver")


@dataclass
class ResolvedPage:
    """Carries everything the view needs to render the page."""

    content_type: object  # ContentType instance (or None for HOME)
    object_id: int | None
    permalink: Permalink


@dataclass
class ResolvedRedirect:
    """Carries the redirect destination and HTTP status code."""

    new_slug: str
    http_status: int


def resolve(store, lang: str, slug: str) -> "ResolvedPage | ResolvedRedirect | None":
    """
    Resolve a URL slug for the given store and language.

    Args:
        store: a Store instance (must be set on request by HostResolutionMiddleware).
        lang:  ISO 639-1 language code (e.g. 'en', 'fr', 'de').
        slug:  URL path without a leading slash (e.g. 'my-shirt' for a product
               or 'collections/my-collection' for a collection).

    Returns a ResolvedPage, ResolvedRedirect, or None (404).
    """
    # Step 1 — active permalink
    try:
        permalink = Permalink.objects.for_store(store).get(
            lang=lang, slug=slug, is_active=True
        )
        return ResolvedPage(
            content_type=permalink.content_type,
            object_id=permalink.object_id,
            permalink=permalink,
        )
    except Permalink.DoesNotExist:
        pass

    # Step 2 — redirect
    try:
        redirect = SlugRedirect.objects.for_store(store).get(
            from_lang=lang, from_slug=slug, is_active=True
        )
        status = http_status_for(redirect.redirect_type)
        if status is None:
            # redirect_type='none' marks an intentionally dead URL — treat as 404.
            return None
        return ResolvedRedirect(
            new_slug=redirect.to_slug,
            http_status=status,
        )
    except SlugRedirect.DoesNotExist:
        pass

    # Step 3 — 404
    return None


# ---------------------------------------------------------------------------
# ADR-008 §2a — Locale resolution
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class RequestLocale:
    """
    Result of resolve_locale: the resolved (store, domain, language) for a request.

    path is the original request path with the language prefix stripped (if any);
    leading slash is preserved.  The storefront view receives this as the slug to look up.
    """

    store: object    # Store instance
    domain: object   # StoreDomain instance
    language: object  # StoreLanguage instance
    path: str        # path with language prefix stripped, leading slash kept


class _GoneError(Exception):
    """
    Raised by resolve_locale when a disabled language is accessed (ML-012 → 410 Gone).

    Public alias: GoneError.  Middleware catches this and returns HttpResponse(status=410).
    """


GoneError = _GoneError  # public name for middleware imports


def resolve_locale(host: str, path: str) -> RequestLocale:
    """
    Single host + path-prefix → (store, domain, language) resolution (ADR-008 §2a, ML-004).

    This is the ONLY function that maps a Host header + URL path to a StoreLanguage.
    Nothing else in the codebase may perform host→store or prefix→language mapping.

    Algorithm:
        1. Normalise host: lowercase, strip port, strip trailing dot.
        2. Look up StoreDomain by host (is_active=True).  Unknown host → Http404.
        3. Try prefix match: first path segment vs StoreLanguage.lang_code
           (use_path_prefix=True rows on this domain).  Match → strip prefix from path.
        4. No prefix match → root language (use_path_prefix=False).
           No root language → Http404 + error log (launch misconfiguration).
        5. Language disabled (is_enabled=False) → GoneError (middleware → 410 Gone).
        6. Return RequestLocale.

    Raises:
        Http404   — unknown host, or domain has no root language (misconfiguration).
        GoneError — the resolved language is disabled (ML-012).
    """
    from stores.models import StoreDomain

    # Step 1: normalise host
    normalised = host.lower().split(":")[0].rstrip(".")

    # Step 2: look up StoreDomain
    # Also requires store__deleted_at__isnull=True: a soft-deleted store's kill switch
    # must take effect immediately — the domain must not route even if is_active was
    # not yet cascaded (ADR-008 §2a step 2, defense-in-depth).
    try:
        domain = (
            StoreDomain.objects
            .select_related("store")
            .get(host=normalised, is_active=True, store__deleted_at__isnull=True)
        )
    except StoreDomain.DoesNotExist:
        raise Http404(f"No active domain: {normalised!r}")

    store = domain.store
    languages = list(domain.languages.all())

    # Step 3: try prefix match against use_path_prefix=True languages
    segments = path.lstrip("/").split("/", 1)
    first_segment = segments[0] if segments else ""
    remainder = "/" + (segments[1] if len(segments) > 1 else "")

    matched_language = None
    stripped_path = path

    for lang in languages:
        if lang.use_path_prefix and lang.lang_code == first_segment:
            matched_language = lang
            stripped_path = remainder
            break

    # Step 4: no prefix match → root language
    if matched_language is None:
        matched_language = domain.default_language()
        if matched_language is None:
            logger.error(
                "Active domain %r has no root language (use_path_prefix=False). "
                "This is a launch misconfiguration (ADR-008 §2a step 4).",
                normalised,
            )
            raise Http404(f"Domain {normalised!r} has no root language.")
        stripped_path = path

    # Step 5: enabled gate (ML-012)
    if not matched_language.is_enabled:
        raise GoneError(
            f"Language {matched_language.lang_code!r} is disabled on {normalised!r} (ML-012)."
        )

    return RequestLocale(
        store=store,
        domain=domain,
        language=matched_language,
        path=stripped_path,
    )


def base_url(language) -> str:
    """
    Compose the absolute URL base for a StoreLanguage (ADR-008 §2b).

    Returns 'https://host' or 'https://host/lang_code' — no trailing slash.
    This is the ONLY place scheme + host + language prefix are composed.
    Consumers append '/' + slug + '/' to form full page URLs.

    Examples:
        dedicated domain (use_path_prefix=False): 'https://mystore.fr'
        path-prefixed language  (use_path_prefix=True):  'https://mystore.com/fr'
    """
    host = language.domain.host
    if language.use_path_prefix:
        return f"https://{host}/{language.lang_code}"
    return f"https://{host}"
