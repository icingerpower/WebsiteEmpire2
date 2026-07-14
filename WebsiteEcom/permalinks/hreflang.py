"""
Hreflang utility for the permalinks app (TICKET-024, ADR-005 §4).

get_hreflang_entries() returns all active Permalinks for a content object across
all languages — restricted to StoreLanguages that have at least one ShippingCountry
configured (CAN-004(b) lenient form).

Used by the {% hreflang_tags %} template tag. The sitemap builder applies the
same ShippingCountry filter independently in sitemaps/sitemaps.py.

Design: one function, one query per concern — callers intersect with
StoreLanguage.is_enabled for the final hreflang set rather than doing a JOIN
here, so the function stays testable in isolation.

Caching (ADR-005 §4 — "cached per page, never one query per tag"):
The optional `request` parameter enables a request-scoped cache stored on the
request object as `_seo_hreflang_cache`.  Callers that pass `request` pay at
most one full lookup per (store_id, content_type_id, object_id) triple per HTTP
request, regardless of how many times the tag is rendered (e.g. via
{% seo_head %}).  The store_id is part of the key to prevent a cross-store cache
collision when two stores happen to share the same content_type_id + object_id.
"""

from django.contrib.contenttypes.models import ContentType

from permalinks.models import Permalink
from stores.models import ShippingCountry, StoreLanguage  # noqa: F401 — ShippingCountry imported for reverse-relation clarity


def get_hreflang_entries(store, content_model_class, object_id, *, request=None):
    """
    Return a list of {'lang': str, 'slug': str} dicts for all published (is_active=True)
    Permalinks for the given content object across all languages.

    Only includes languages for which the store has a StoreLanguage with at least
    one ShippingCountry row (CAN-004(b) lenient form).  A StoreLanguage with zero
    ShippingCountry rows is treated as "not yet configured for shipping" and excluded.

    Deferred items:
    - CAN-004(b) full form: per-product geo restriction (where specific products are
      excluded from specific markets).
    - CAN-004(c): geo-targeted hreflang with country suffixes (e.g. hreflang="en-US").
    Both are out of v1 scope.

    Args:
        store:               Store instance — used to scope the query.
        content_model_class: The Django model class for the content object
                             (e.g. Product, Collection).
        object_id:           PK of the content object.
        request:             Optional HttpRequest.  When provided, results are cached
                             in request._seo_hreflang_cache keyed by
                             (store_id, content_type_id, object_id) for the request
                             lifetime.  Pass this from template tags to avoid
                             duplicate queries when {% seo_head %} calls
                             {% hreflang_tags %} on the same object more than once
                             (ADR-005 §4).  The store_id is part of the key to
                             prevent cross-store cache collisions.

    Returns:
        List of dicts with keys 'lang' and 'slug'.  Empty list if the object
        has no active Permalinks in any shipping-configured language.
    """
    ct = ContentType.objects.get_for_model(content_model_class)
    # Cache key includes store.pk to prevent a cross-store collision: two different
    # stores could legitimately share the same content_type_id + object_id (e.g. both
    # have a Product with pk=1), and returning the wrong store's entries would be wrong.
    cache_key = (store.pk, ct.pk, object_id)

    if request is not None:
        cache = getattr(request, "_seo_hreflang_cache", None)
        if cache is None:
            request._seo_hreflang_cache = {}
            cache = request._seo_hreflang_cache
        if cache_key in cache:
            return cache[cache_key]

    # CAN-004(b) lenient form: only emit hreflang for StoreLanguages that have at
    # least one ShippingCountry configured.  Uses the reverse FK `shipping_countries`
    # on StoreLanguage (related_name of ShippingCountry.store_language).
    # .distinct() is required because a StoreLanguage with multiple ShippingCountry
    # rows would appear multiple times in the join without it.
    configured_lang_codes = set(
        StoreLanguage.objects.filter(
            store=store,
            shipping_countries__isnull=False,
        ).values_list("lang_code", flat=True).distinct()
    )

    result = list(
        Permalink.objects
        .for_store(store)
        .filter(
            content_type=ct,
            object_id=object_id,
            is_active=True,
            lang__in=configured_lang_codes,
        )
        .values("lang", "slug")
    )

    if request is not None:
        request._seo_hreflang_cache[cache_key] = result

    return result
