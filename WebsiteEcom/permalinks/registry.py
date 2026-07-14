"""
Permalink creation helpers and storefront dispatcher (TICKET-016, TICKET-029 Phase 2).

register_product / register_collection:
    Called when a Product or Collection is first created in published/active status to
    register its canonical URL in the Permalink table.  Called once per object at
    creation time via the post_save signal in permalinks/signals.py.

resolve_storefront_path:
    Maps a request URL to the correct storefront view (HttpResponse) by looking up the
    Permalink table.  This is the ONLY place that dispatches content-type → view.
    Uses late imports throughout to avoid circular imports with storefront.views.

URL-path conventions (ADR-005, ADR-014 §10-G1):
    Product    slug 'my-shirt'    → 'my-shirt'          (root-level, no prefix)
    Collection slug 'bestsellers' → 'collections/bestsellers'

Language is taken from store.primary_language.  Translated-language permalinks are
registered by the AI translation job system (ADR-003) when a translation lands.

The target object is identified by ContentType + object_id (generic FK) so these helpers
have no hard dependency on catalog models.
"""

from django.contrib.contenttypes.models import ContentType

from permalinks.models import Permalink, PermalinkTrigger, RedirectTrigger, RedirectType


def register_product(product) -> Permalink:
    """
    Create a Permalink for a newly created Product.

    Args:
        product: a saved Product instance (must have .pk and .store set).
                 Only call this when the product is active/published.

    Returns the created Permalink instance.
    """
    slug = product.slug
    lang = product.store.primary_language
    content_type = ContentType.objects.get_for_model(product)
    p = Permalink(
        store=product.store,
        content_type=content_type,
        object_id=product.pk,
        lang=lang,
        slug=slug,
        auto_created=True,
    )
    p.save()
    return p


def register_collection(collection) -> Permalink:
    """
    Create a Permalink for a newly created Collection.

    Args:
        collection: a saved Collection instance (must have .pk and .store set).
                    Only call this when the collection is published.

    Returns the created Permalink instance.
    """
    slug = f"collections/{collection.slug}"
    lang = collection.store.primary_language
    content_type = ContentType.objects.get_for_model(collection)
    p = Permalink(
        store=collection.store,
        content_type=content_type,
        object_id=collection.pk,
        lang=lang,
        slug=slug,
        auto_created=True,
    )
    p.save()
    return p


# ---------------------------------------------------------------------------
# ProductPageVersion permalink lifecycle (TICKET-042, ADR-028 §2)
# ---------------------------------------------------------------------------
#
# All ProductPageVersion permalinks are ordinary Permalink rows under a
# dedicated content type, one per language that has an active PRODUCT permalink
# (the suffix is language-neutral; only the product-slug part it is appended to
# is translated).  Every write below happens inside one atomic transaction so a
# slug collision in one language aborts the whole operation rather than leaving
# some languages registered and others not (ADR-028 §2 "never partial-language
# registration", §XV-1).


def _page_version_content_type():
    from catalog.models import ProductPageVersion
    return ContentType.objects.get_for_model(ProductPageVersion)


def sync_page_version_permalinks(page_version) -> None:
    """
    Create, reactivate, or rename the Permalink rows for `page_version`, one per
    language that currently has an active product-CT permalink for its product
    (ADR-028 §2).

    For each such language:
    - No existing version permalink → create one, slug = f"{product_slug}-{suffix}".
    - Existing but inactive → reactivate (is_active=True); rename if the slug_suffix
      or the product's slug in that language changed since it was created.
    - Existing and active with a different slug → rename in place.  If the OLD
      slug was already active/routable, a SlugRedirect is written via the shared
      permalinks.redirects.create_slug_redirect() helper (same chain-collapse /
      loop-rejection semantics as every other slug rename in this codebase) so a
      Pinterest pin pointed at the old version URL still resolves.

    Called from catalog/signals.py on ProductPageVersion creation / reactivation,
    and from _sync_translation_permalink (catalog/signals.py) when a translated
    product permalink is (re)activated — so a late-published translation gets the
    matching version permalink for that language (ADR-028 §2).
    """
    from django.contrib.contenttypes.models import ContentType as _ContentType
    from django.db import transaction

    from catalog.models import Product

    store = page_version.product.store
    product_ct = _ContentType.objects.get_for_model(Product)
    version_ct = _page_version_content_type()

    product_permalinks = list(
        Permalink.objects.for_store(store).filter(
            content_type=product_ct, object_id=page_version.product_id, is_active=True,
        )
    )

    with transaction.atomic():
        for pp in product_permalinks:
            target_slug = f"{pp.slug}-{page_version.slug_suffix}"
            existing = (
                Permalink.objects.for_store(store)
                .filter(content_type=version_ct, object_id=page_version.pk, lang=pp.lang)
                .first()
            )
            if existing is None:
                Permalink(
                    store=store,
                    content_type=version_ct,
                    object_id=page_version.pk,
                    lang=pp.lang,
                    slug=target_slug,
                    is_active=True,
                    auto_created=True,
                    trigger=PermalinkTrigger.MANUAL,
                ).save()
                continue

            was_active = existing.is_active
            old_slug = existing.slug
            changed = False
            if not existing.is_active:
                existing.is_active = True
                changed = True
            if existing.slug != target_slug:
                existing.slug = target_slug
                changed = True
            if changed:
                existing.save()
                if was_active and old_slug != target_slug:
                    from permalinks.redirects import create_slug_redirect
                    create_slug_redirect(store, old_slug, target_slug, pp.lang)


def deactivate_page_version_permalinks(page_version) -> None:
    """
    Deactivate every active Permalink for `page_version` and write a 302
    (temporary) SlugRedirect to the primary product URL in the same language
    (ADR-028 §2 "version deactivated" — reversible, matches UF-005's "falls back
    to normal product page").

    Called from catalog/signals.py when ProductPageVersion.is_active transitions
    True → False.
    """
    from django.contrib.contenttypes.models import ContentType as _ContentType
    from django.db import transaction

    from catalog.models import Product
    from permalinks.redirects import create_slug_redirect

    store = page_version.product.store
    product_ct = _ContentType.objects.get_for_model(Product)
    version_ct = _page_version_content_type()

    version_permalinks = list(
        Permalink.objects.for_store(store).filter(
            content_type=version_ct, object_id=page_version.pk, is_active=True,
        )
    )

    with transaction.atomic():
        for vp in version_permalinks:
            primary = (
                Permalink.objects.for_store(store)
                .filter(content_type=product_ct, object_id=page_version.product_id, lang=vp.lang, is_active=True)
                .first()
            )
            old_slug = vp.slug
            vp.is_active = False
            vp.save()
            if primary is not None:
                create_slug_redirect(
                    store, old_slug, primary.slug, vp.lang,
                    redirect_type=RedirectType.TEMPORARY,
                    trigger=RedirectTrigger.PAGE_VERSION_CHANGE,
                )


def delete_page_version_permalinks(page_version) -> None:
    """
    Deactivate every active Permalink for `page_version` and write a 301
    (permanent) SlugRedirect to the primary product URL in the same language
    (ADR-028 §2 "version deleted" — permanent, so Pinterest link equity
    consolidates onto the primary URL instead of 404ing, §XII).

    Called from catalog/signals.py post_delete on ProductPageVersion. Uses
    cross_store_unsafe() because the row is already gone by post_delete time in
    some Django versions' signal timing guarantees are not relied upon here —
    the queries below run against Permalink (a different model), which is safe
    to scope normally via for_store(store) since `store` is captured from the
    (still-populated, pre-delete-cascade) in-memory instance.
    """
    from django.contrib.contenttypes.models import ContentType as _ContentType
    from django.db import transaction

    from catalog.models import Product
    from permalinks.redirects import create_slug_redirect

    store = page_version.product.store
    product_ct = _ContentType.objects.get_for_model(Product)
    version_ct = _page_version_content_type()

    version_permalinks = list(
        Permalink.objects.for_store(store).filter(
            content_type=version_ct, object_id=page_version.pk, is_active=True,
        )
    )

    with transaction.atomic():
        for vp in version_permalinks:
            primary = (
                Permalink.objects.for_store(store)
                .filter(content_type=product_ct, object_id=page_version.product_id, lang=vp.lang, is_active=True)
                .first()
            )
            old_slug = vp.slug
            vp.is_active = False
            vp.save()
            if primary is not None:
                create_slug_redirect(
                    store, old_slug, primary.slug, vp.lang,
                    redirect_type=RedirectType.PERMANENT,
                    trigger=RedirectTrigger.PAGE_VERSION_CHANGE,
                )


def rename_page_version_permalinks_for_language(store, product_id, lang, old_product_slug, new_product_slug) -> None:
    """
    When a product's slug changes (default-language rename, or a translated-slug
    rename/late-hint-update) in `lang`, rename every active version permalink in
    that language from f"{old_product_slug}-{suffix}" to f"{new_product_slug}-{suffix}"
    and write the matching SlugRedirect (ADR-028 §2 "product slug changed").

    Called from permalinks/signals.py handle_slug_change (Product sender) and
    from catalog/signals.py _sync_translation_permalink (translated slug rename).

    Defensive: a version permalink whose slug does not start with
    f"{old_product_slug}-" is skipped (should not happen — belt-and-braces
    against a state drift rather than a crash).
    """
    from catalog.models import ProductPageVersion
    from permalinks.redirects import create_slug_redirect

    version_ct = _page_version_content_type()
    version_ids = list(
        ProductPageVersion.objects.for_store(store).filter(product_id=product_id).values_list('pk', flat=True)
    )
    if not version_ids:
        return

    prefix = f"{old_product_slug}-"
    version_permalinks = Permalink.objects.for_store(store).filter(
        content_type=version_ct, object_id__in=version_ids, lang=lang, is_active=True,
    )
    for vp in version_permalinks:
        if not vp.slug.startswith(prefix):
            continue
        suffix = vp.slug[len(prefix):]
        old_slug = vp.slug
        new_slug = f"{new_product_slug}-{suffix}"
        if old_slug == new_slug:
            continue
        vp.slug = new_slug
        vp.save()
        create_slug_redirect(store, old_slug, new_slug, lang)


def resolve_storefront_path(request, slug):
    """
    Map a request URL slug to the correct storefront view (TICKET-029 Phase 2).

    The ONLY place that dispatches Permalink content_type → view function.
    Called by storefront.views.page_view.

    Algorithm:
      1. Extract lang from request.locale.language.lang_code (fallback: 'en').
      2. Extract the clean path from request.locale.path (language prefix already
         stripped by LocaleMiddleware); fall back to the raw slug parameter.
      3. Call permalinks.resolver.resolve(store, lang, path_slug).
      4. On ResolvedRedirect: return the appropriate HttpResponse redirect.
      5. On ResolvedPage: load the target object and call the matching view.
      6. On None or unknown content type: return None (caller raises Http404).

    Late imports are used throughout to avoid circular imports with storefront.views.

    Args:
        request: the Django request (must have .store and .locale set by middleware).
        slug:    raw URL path captured by the <path:slug> URL pattern (before lang
                 prefix stripping).  Used only as a fallback when request.locale is
                 not available.

    Returns:
        HttpResponse — a fully rendered page or redirect, or
        None         — signals a 404 to the caller.
    """
    from django.http import HttpResponsePermanentRedirect, HttpResponseRedirect
    from permalinks.resolver import resolve, ResolvedRedirect

    store = getattr(request, "store", None)
    if store is None:
        return None

    locale = getattr(request, "locale", None)
    lang = "en"
    path_slug = slug
    # lang_prefix is "/fr" for path-prefixed languages, "" for root languages (Fix 7).
    lang_prefix = ""
    if locale is not None:
        lang_obj = getattr(locale, "language", None)
        if lang_obj is not None:
            lang = getattr(lang_obj, "lang_code", "en")
            if getattr(lang_obj, "use_path_prefix", False):
                lang_code_val = getattr(lang_obj, "lang_code", "")
                if lang_code_val:
                    lang_prefix = f"/{lang_code_val}"
        locale_path = getattr(locale, "path", None)
        if locale_path is not None:
            # Fix 2: strip both leading and trailing slash so "my-slug/" matches
            # the Permalink slug "my-slug" in the DB.  Sitemaps emit trailing-slash
            # URLs; browsers send them; the DB stores slugs without slashes.
            path_slug = locale_path.lstrip("/").rstrip("/")

    # Fix 3: an empty path_slug means the request is at the language root (e.g. /fr/).
    # Route directly to home_view — the Permalink table has no empty-slug row.
    if not path_slug:
        from storefront.views import home_view
        return home_view(request)

    result = resolve(store, lang, path_slug)

    if result is None:
        return None

    if isinstance(result, ResolvedRedirect):
        # Fix 7: compose the redirect target with the language prefix and a trailing
        # slash so "/fr/old-slug/" correctly redirects to "/fr/new-slug/" (not "/new-slug/").
        # Also preserve the query string so UTM parameters survive slug-change redirects.
        target = f"{lang_prefix}/{result.new_slug}/"
        qs = request.META.get("QUERY_STRING", "")
        if qs:
            target = f"{target}?{qs}"
        if result.http_status == 301:
            return HttpResponsePermanentRedirect(target)
        return HttpResponseRedirect(target)

    # ResolvedPage — dispatch by content type.
    content_type = result.content_type
    if content_type is None:
        # Special pages with no backing object (e.g. HOME) are served by home_view.
        return None

    model_name = content_type.model  # e.g. 'product', 'collection'

    if model_name == "product":
        from catalog.models import Product

        try:
            product = Product.objects.for_store(store).get(pk=result.object_id)
        except Product.DoesNotExist:
            return None
        from storefront.views import product_page

        return product_page(request, product)

    if model_name == "collection":
        from catalog.models import Collection

        try:
            collection = Collection.objects.for_store(store).get(pk=result.object_id)
        except Collection.DoesNotExist:
            return None
        from storefront.views import collection_page

        return collection_page(request, collection)

    if model_name == "productpageversion":
        from catalog.models import Product, ProductPageVersion

        try:
            version = (
                ProductPageVersion.objects.for_store(store)
                .select_related("product")
                .get(pk=result.object_id)
            )
        except ProductPageVersion.DoesNotExist:
            return None

        product = version.product

        # Defensive 302 to the primary product URL (ADR-028 §2 "belt-and-braces" —
        # the redirect row should normally already exist via the deactivate/delete
        # signal, this only covers a data-consistency drift).
        if (not version.is_active) or product.status != Product.STATUS_ACTIVE:
            primary_ct = ContentType.objects.get_for_model(Product)
            primary = (
                Permalink.objects.for_store(store)
                .filter(content_type=primary_ct, object_id=product.pk, lang=lang, is_active=True)
                .first()
            )
            if primary is None:
                return None
            target = f"{lang_prefix}/{primary.slug}/"
            qs = request.META.get("QUERY_STRING", "")
            if qs:
                target = f"{target}?{qs}"
            return HttpResponseRedirect(target)

        from storefront.views import product_page

        return product_page(request, product, page_version=version)

    if model_name == "staticpage":
        from pages.models import StaticPage

        try:
            page = StaticPage.objects.for_store(store).get(pk=result.object_id)
        except StaticPage.DoesNotExist:
            return None
        from storefront.views_pages import static_page_view

        return static_page_view(request, page)

    # Unknown content type — treat as 404.
    return None
