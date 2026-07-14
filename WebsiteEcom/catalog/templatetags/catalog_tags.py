"""
Template tags for the catalog app (TICKET-025, AC-092).

{% noindex_if_thin collection %}
    Renders <meta name="robots" content="noindex"> when the collection
    has fewer than 3 published (status='active') products.

    Purpose: thin collections (0, 1, or 2 active products) are SEO dead weight
    and could waste crawl budget.  Collections with fewer than 3 published
    products should not be indexed.  Collections with 3 or more active products
    are considered indexable.

    Note: this tag is consumed on storefront collection pages, not in the admin.
    The view-layer is expected to return 404 for empty collections once the
    storefront theme is implemented (T017); this is a tracked open item.
    This tag is a belt-and-suspenders SEO signal for thin collections that the
    view does serve.
"""

from django import template

from catalog.models import CollectionProduct, Product

register = template.Library()

# Minimum number of active products for a collection to be considered indexable.
# Public name (no underscore) so the sitemap builder can import it.
# TODO T007: make per-store configurable when the general settings model lands.
COLLECTION_INDEXABLE_THRESHOLD = 3


def is_collection_indexable(collection) -> bool:
    """
    Return True when a collection has at least 3 active products.

    A collection with fewer than 3 active (STATUS_ACTIVE) products is considered
    'thin' and should not be indexed.  This predicate is shared by the
    noindex_if_thin template tag and the sitemap builder so that page-level
    noindex and sitemap membership can never disagree.

    Args:
        collection: A Collection model instance.

    Returns:
        True when the collection has >= 3 active products, False otherwise.
    """
    # ADR-031 addendum audit (TICKET-051): .count() used to silently bypass
    # StoreScopedManager's isolation raise (core/managers.py) — a real latent
    # unscoped read, only harmless in practice because the `collection=`
    # filter already pins every row to one store. Scoped explicitly now that
    # .count() raises on the unscoped manager.
    count = CollectionProduct.objects.for_store(collection.store).filter(
        collection=collection,
        product__status=Product.STATUS_ACTIVE,
    ).count()
    return count >= COLLECTION_INDEXABLE_THRESHOLD


@register.inclusion_tag("catalog/noindex_if_thin.html")
def noindex_if_thin(collection):
    """
    Render <meta name="robots" content="noindex"> if the collection
    has fewer than 3 published products.

    Usage in templates::

        {% load catalog_tags %}
        {% noindex_if_thin collection %}

    A "published product" is a Product with status='active'.  Draft and archived
    products do not count toward the collection's visible size.

    The threshold is 3 active products: 0, 1, and 2 are all considered thin
    (AC-092).  Delegates to is_collection_indexable() so the same predicate
    drives both this tag and sitemap exclusion.

    Args:
        collection: A Collection model instance.
    """
    return {"should_noindex": not is_collection_indexable(collection)}
