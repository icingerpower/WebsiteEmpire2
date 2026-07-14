"""
Storefront views — Phase 2 implementation (ADR-012 D5, TICKET-029).

View routing:
  home_view       — / : hero, featured collections, featured products, theme band.
  page_view       — /<path:slug> : permalink resolver → product_page | collection_page.
  product_page    — internal; called by resolve_storefront_path after object load.
  collection_page — internal; called by resolve_storefront_path after object load.
  cart_page       — /cart/ : session cart, line items, subtotal.
  search_view     — /search/?q= : product title search stub.
  custom_404_view — handler404; renders 404.html with HTTP 404 status.

Design invariants:
  - All DB queries are scoped to request.store (StoreScopedManager).
  - Lang code comes from request.locale.language.lang_code; never from resolve_locale().
  - If no published translation exists for the locale → 404 (ML-002, MCP-002).
  - "All taxes included" note appears adjacent to every displayed price.
  - Core flows (add-to-cart, search) work as plain form POSTs without JavaScript.
  - JavaScript is progressive enhancement only (TH-063).
"""

import json
import logging
from decimal import Decimal

from django.core.paginator import Paginator
from django.core.serializers.json import DjangoJSONEncoder
from django.http import Http404
from django.template.response import TemplateResponse
from django.utils.translation import gettext_lazy as _
from django.utils.safestring import mark_safe

from catalog.models import (
    Collection,
    CollectionProduct,
    CollectionTranslation,
    InventoryMode,
    Product,
    ProductImage,
    ProductTranslation,
    ProductVariant,
    TranslationStatus,
)
from cart.models import Cart, CartItem, CartStatus
from permalinks.resolver import base_url

logger = logging.getLogger(__name__)

# Number of products shown per page on collection pages.
_COLLECTION_PAGE_SIZE = 24

# Number of featured products shown on the home page.
_HOME_FEATURED_PRODUCT_COUNT = 8

# Number of featured collections shown on the home page.
_HOME_FEATURED_COLLECTION_COUNT = 4

# Maximum page range links in collection pagination before collapsing.
_MAX_PAGE_LINKS = 10

# Sort choices for collection pages (querystring value → ORM annotation/order).
# gettext_lazy so the labels are translated when rendered (TH-044).
_SORT_LABELS = {
    "price_asc": _("Price: low to high"),
    "price_desc": _("Price: high to low"),
    "newest": _("Newest"),
}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _get_locale_lang(request) -> str:
    """
    Return the active language code from request.locale.

    Falls back to 'en' when LocaleMiddleware has not run (e.g. tests using
    RequestFactory without the full middleware stack).
    """
    locale = getattr(request, "locale", None)
    if locale is not None:
        lang_obj = getattr(locale, "language", None)
        if lang_obj is not None:
            return getattr(lang_obj, "lang_code", "en")
    return "en"


def _build_variant_options(variants) -> dict:
    """
    Build option groups for the variant selector.

    Returns an ordered dict of {option_name: [unique_values_in_order]}.
    Option names and values appear in the order first encountered across all variants,
    sorted by variant.position (ascending) — callers must pass variants pre-sorted.
    """
    option_groups: dict[str, list] = {}
    for variant in variants:
        options = variant.option_values_json
        if not isinstance(options, list):
            continue
        for option in options:
            name = option.get("option_name", "")
            value = option.get("value", "")
            if not name:
                continue
            if name not in option_groups:
                option_groups[name] = []
            if value and value not in option_groups[name]:
                option_groups[name].append(value)
    return option_groups


def _get_inventory_status(default_variant) -> dict:
    """
    Compute the inventory badge label and CSS class for the given variant.

    Returns a dict with 'label' (human-readable string) and 'class' (CSS class name).
    Used to render the inventory badge on the product page.
    """
    if default_variant is None:
        return {"label": "In stock", "class": "in-stock"}

    mode = default_variant.inventory_mode

    if mode == InventoryMode.SOLD_OUT:
        return {"label": "Sold out", "class": "sold-out"}

    if mode == InventoryMode.FIXED_QTY:
        qty = default_variant.quantity or 0
        if qty == 0:
            return {"label": "Sold out", "class": "sold-out"}
        if qty <= 5:
            return {"label": f"Low stock ({qty} left)", "class": "low-stock"}
        return {"label": "In stock", "class": "in-stock"}

    if mode == InventoryMode.PRESALE:
        date_str = (
            str(default_variant.presale_ships_at)
            if default_variant.presale_ships_at
            else "TBD"
        )
        return {"label": f"Pre-order (available {date_str})", "class": "presale"}

    if mode == InventoryMode.ASK_WHEN_AVAILABLE:
        return {"label": "Out of stock — notify me", "class": "ask-available"}

    if mode == InventoryMode.QUOTATION:
        return {"label": "Request a quote", "class": "quotation"}

    # NO_TRACKING, FAKE_SERVER — treat as in stock.
    return {"label": "In stock", "class": "in-stock"}


def _build_product_slug_map(store, product_pks: list, lang: str) -> dict:
    """
    Fetch active Permalink slugs for a list of product PKs in one query.

    Returns a dict of {product_pk: slug_string}.
    Products with no active permalink for the given lang are absent from the dict.
    """
    from django.contrib.contenttypes.models import ContentType
    from permalinks.models import Permalink

    if not product_pks:
        return {}

    product_ct = ContentType.objects.get_for_model(Product)
    qs = Permalink.objects.for_store(store).filter(
        content_type=product_ct,
        object_id__in=product_pks,
        lang=lang,
        is_active=True,
    )
    return {p.object_id: p.slug for p in qs}


def _build_storefront_url(request, slug: str) -> str:
    """
    Build an absolute-path storefront URL for the given slug with the correct
    language prefix (Fix 4 — internal links broken on path-prefixed languages).

    For path-prefixed languages (e.g. /fr/), returns '/fr/slug/' (or '/fr/' for
    an empty slug).  For root languages, returns '/slug/' (or '/' for empty slug).

    This is the single place in views.py that composes storefront paths with the
    language prefix.  Template-layer consumers use the {% permalink_url %} tag.

    Args:
        request: the Django request (must have .locale set by LocaleMiddleware).
        slug:    pre-computed URL slug string (no leading or trailing slashes).
    """
    locale = getattr(request, "locale", None)
    lang_obj = getattr(locale, "language", None) if locale else None
    if lang_obj is not None and getattr(lang_obj, "use_path_prefix", False):
        lang_code = getattr(lang_obj, "lang_code", "")
        if lang_code:
            return f"/{lang_code}/{slug}/" if slug else f"/{lang_code}/"
    return f"/{slug}/" if slug else "/"


def _serialize_jsonld(data) -> str:
    """
    Serialize a JSON-LD structure (dict or list) to a safe string for embedding
    in a ``<script type="application/ld+json">`` tag.

    Uses DjangoJSONEncoder for Decimal/datetime support, then escapes the
    characters that could allow script injection (``<``, ``>``, ``&``) following
    the same strategy as Django's ``json_script`` filter / template tag.

    Returns a ``mark_safe`` string that can be rendered directly inside a
    ``<script>`` tag in a template.  Never raises — returns ``"{}"`` on error.
    """
    try:
        raw = json.dumps(data, cls=DjangoJSONEncoder, ensure_ascii=False)
        # Escape HTML-unsafe characters so the JSON cannot break out of the
        # <script> tag, following Django's _json_script_escapes convention.
        raw = raw.replace("&", "\\u0026").replace("<", "\\u003c").replace(">", "\\u003e")
        return mark_safe(raw)
    except (TypeError, ValueError):
        logger.exception("Failed to serialize JSON-LD data.")
        return mark_safe("{}")


# ---------------------------------------------------------------------------
# Public storefront views
# ---------------------------------------------------------------------------


def home_view(request):
    """
    Render the storefront home page (TH-080, TICKET-029 Phase 2).

    Loads:
      - Up to 4 published collections with translations for the request locale.
      - Up to 8 products from the first collection with translations.

    Hero and theme-band text come from StoreThemeCustomization.customization_json
    when available; templates fall back to static defaults otherwise.
    """
    store = getattr(request, "store", None)
    lang = _get_locale_lang(request)

    featured_collections = []
    featured_products = []

    if store is not None:
        # Load featured collections — up to 4 published, oldest-first.
        raw_collections = list(
            Collection.objects.for_store(store)
            .filter(is_published=True)
            .order_by("created_at")[: _HOME_FEATURED_COLLECTION_COUNT]
        )

        for collection in raw_collections:
            try:
                coll_trans = CollectionTranslation.objects.for_store(store).get(
                    collection=collection,
                    lang_code=lang,
                    status=TranslationStatus.PUBLISHED,
                )
            except CollectionTranslation.DoesNotExist:
                coll_trans = None

            # Build collection permalink slug.
            from django.contrib.contenttypes.models import ContentType
            from permalinks.models import Permalink

            coll_ct = ContentType.objects.get_for_model(Collection)
            coll_permalink = (
                Permalink.objects.for_store(store)
                .filter(
                    content_type=coll_ct,
                    object_id=collection.pk,
                    lang=lang,
                    is_active=True,
                )
                .first()
            )

            featured_collections.append(
                {
                    "collection": collection,
                    "translation": coll_trans,
                    "slug": coll_permalink.slug if coll_permalink else "",
                    "image": collection.image if collection.image else None,
                }
            )

        # Load featured products from the first published collection.
        if raw_collections:
            first_collection = raw_collections[0]
            memberships = list(
                CollectionProduct.objects.for_store(store)
                .filter(collection=first_collection)
                .select_related("product")
                .order_by("sort_key")[: _HOME_FEATURED_PRODUCT_COUNT]
            )
            product_pks = [m.product_id for m in memberships]
            slug_map = _build_product_slug_map(store, product_pks, lang)

            # Bulk-load translations, images, and default variants for all products.
            translation_map = {
                t.product_id: t
                for t in ProductTranslation.objects.for_store(store).filter(
                    product_id__in=product_pks,
                    lang_code=lang,
                    status=TranslationStatus.PUBLISHED,
                )
            }
            home_images_qs = (
                ProductImage.objects.for_store(store)
                .filter(product_id__in=product_pks)
                .order_by("display_order")
            )
            home_image_map: dict = {}
            for img in home_images_qs:
                if img.product_id not in home_image_map:
                    home_image_map[img.product_id] = img

            # Load one default variant per product (is_default=True first, then position).
            home_variants_qs = (
                ProductVariant.objects.for_store(store)
                .filter(product_id__in=product_pks, is_active=True)
                .order_by("product_id", "-is_default", "position")
            )
            home_variant_map: dict = {}
            for variant in home_variants_qs:
                if variant.product_id not in home_variant_map:
                    home_variant_map[variant.product_id] = variant

            for membership in memberships:
                product = membership.product
                if product.status != Product.STATUS_ACTIVE:
                    continue
                prod_trans = translation_map.get(product.pk)
                if prod_trans is None:
                    continue  # Skip products with no published translation.

                featured_products.append(
                    {
                        "product": product,
                        "translation": prod_trans,
                        "slug": slug_map.get(product.pk, ""),
                        "image": home_image_map.get(product.pk),
                        "default_variant": home_variant_map.get(product.pk),
                    }
                )

    # Load StoreThemeCustomization for hero/band text if available.
    customization = {}
    if store is not None:
        theme_ctx = getattr(request, "theme_ctx", None)
        if theme_ctx is not None:
            customization = theme_ctx.customization

    return TemplateResponse(
        request,
        "storefront/pages/home.html",
        {
            "featured_collections": featured_collections,
            "featured_products": featured_products,
            "customization": customization,
            # content_object=None: home page has no backing DB object.
            # base.html gates {% hreflang_tags %} on content_object being truthy
            # (Fix 1 — canonical/hreflang never rendered).
            "content_object": None,
            # page_type: used by analytics_beacon tag (TH-043).
            "page_type": "home",
        },
    )


def page_view(request, slug=""):
    """
    Catch-all slug dispatcher — routes to the correct page via PermalinkResolver.

    Phase 2: calls resolve_storefront_path which looks up the Permalink table and
    dispatches to product_page or collection_page based on content_type.

    Returns a redirect (301/302) for renamed slugs.
    Returns Http404 when no active permalink or redirect exists (TH-087).
    """
    from permalinks.registry import resolve_storefront_path

    result = resolve_storefront_path(request, slug)
    if result is None:
        raise Http404(f"Page not found: {slug!r}")
    return result


def product_page(request, product, page_version=None):
    """
    Render a product detail page (TH-082, ML-002).

    Args:
        product: a Product instance already scoped to the current store.
        page_version: optional ProductPageVersion (TICKET-042, ADR-028). When set
            (and active), the image set is substituted for the version's own
            ordered images — everything else on the page is identical (same
            price, forms, translation, pixels view_content payload — ADR-028 §3
            / §8). Callers (permalinks.registry.resolve_storefront_path) only
            pass an active version; this view still guards defensively.

    Raises Http404 when:
      - The product is not active (defensive check; permalink should be inactive too).
      - No published ProductTranslation exists for the request locale (MCP-002).
    """
    store = getattr(request, "store", None)
    lang = _get_locale_lang(request)

    # Defensive: only active products should have active permalinks, but guard anyway.
    if product.status != Product.STATUS_ACTIVE:
        raise Http404(f"Product {product.slug!r} is not active.")

    # A page_version that is somehow inactive is treated as "no version" — the
    # resolver dispatch already 302s an inactive version before reaching here;
    # this is belt-and-braces (ADR-028 §2).
    if page_version is not None and not page_version.is_active:
        page_version = None

    # Require a published translation — no mixed-language pages (ML-002, MCP-002).
    try:
        translation = ProductTranslation.objects.for_store(store).get(
            product=product,
            lang_code=lang,
            status=TranslationStatus.PUBLISHED,
        )
    except ProductTranslation.DoesNotExist:
        raise Http404(
            f"No published translation for product {product.slug!r} in lang {lang!r}."
        )

    # Variants, sorted by position.
    variants = list(
        ProductVariant.objects.for_store(store)
        .filter(product=product, is_active=True)
        .order_by("position")
    )

    default_variant = next((v for v in variants if v.is_default), variants[0] if variants else None)

    option_groups = _build_variant_options(variants)
    inventory_status = _get_inventory_status(default_variant)

    # Images, sorted by display_order.
    # ADR-028 §3: a page_version substitutes its own ordered image set — "same
    # template, swapping only the image set". first_image (used for the gallery,
    # og:image, and the JSON-LD image list below) is derived from this same
    # `images` list, so it automatically becomes the version's own first image.
    if page_version is not None:
        images = [
            pvi.image for pvi in
            page_version.images.for_store(store).select_related("image").order_by("display_order")
        ]
    else:
        images = list(
            ProductImage.objects.for_store(store)
            .filter(product=product)
            .order_by("display_order")
        )
    first_image = images[0] if images else None

    # ── SEO canonical override (ADR-028 §4, META-004 exception) ────────────
    # A page_version render must canonicalize to the PRIMARY product URL in the
    # same language, never to itself (FM-C4 — the duplicate-content bug this
    # entire feature is built around). Resolved here (not in the template tag)
    # so the "no canonical at all on failure" rule can log with full context.
    # request.seo_canonical_path is read by permalinks_tags._resolve_canonical_target
    # (permalinks/templatetags/permalinks_tags.py canonical_url): "" means
    # "primary unresolvable — emit no canonical tag", never fall back to self.
    if page_version is not None:
        from django.contrib.contenttypes.models import ContentType
        from permalinks.models import Permalink as _Permalink

        primary_ct = ContentType.objects.get_for_model(Product)
        primary_permalink = (
            _Permalink.objects.for_store(store)
            .filter(content_type=primary_ct, object_id=product.pk, lang=lang, is_active=True)
            .first()
        )
        if primary_permalink is not None:
            request.seo_canonical_path = f"/{primary_permalink.slug}/"
        else:
            logger.error(
                "product_page: page_version=%s could not resolve a primary product "
                "permalink for product=%s lang=%r — emitting NO canonical tag "
                "(ADR-028 §4, never self-canonical on a version page).",
                page_version.pk, product.pk, lang,
            )
            request.seo_canonical_path = ""

    # Breadcrumb: Home > Collection (if any) > Product.
    # Use _build_storefront_url so paths are prefixed correctly on path-prefixed
    # language editions (Fix 4 — internal links broken on path-prefixed languages).
    breadcrumbs = [{"label": "Home", "url": _build_storefront_url(request, "")}]
    collection = None
    collection_translation = None

    membership = (
        CollectionProduct.objects.for_store(store)
        .filter(product=product)
        .select_related("collection")
        .first()
    )
    if membership is not None:
        collection = membership.collection
        try:
            collection_translation = CollectionTranslation.objects.for_store(store).get(
                collection=collection,
                lang_code=lang,
                status=TranslationStatus.PUBLISHED,
            )
        except CollectionTranslation.DoesNotExist:
            collection_translation = None

        # Find the collection's permalink slug.
        from django.contrib.contenttypes.models import ContentType
        from permalinks.models import Permalink

        coll_ct = ContentType.objects.get_for_model(Collection)
        coll_permalink = (
            Permalink.objects.for_store(store)
            .filter(
                content_type=coll_ct,
                object_id=collection.pk,
                lang=lang,
                is_active=True,
            )
            .first()
        )
        coll_label = (
            (collection_translation.title if collection_translation else "")
            or collection.title
        )
        breadcrumbs.append(
            {
                "label": coll_label,
                "url": _build_storefront_url(request, coll_permalink.slug) if coll_permalink else None,
            }
        )

    breadcrumbs.append({"label": translation.title or product.title, "url": None})

    # ── JSON-LD structured data (Fix 5) ─────────────────────────────────────
    # Build Product schema for Google rich results (price/availability snippets).
    # Serialized with _serialize_jsonld so user-controlled strings are safely
    # escaped — no manual string concatenation into script tags.
    locale = getattr(request, "locale", None)
    if page_version is not None:
        # FIX-1 (SEO review PAGE_VERSIONS_SEO_REVIEW.md, Rule 8): a version
        # page's structured-data offers.url must agree with the canonical
        # (the primary product URL) — pointing it at the version's own URL
        # was the one on-page signal disagreeing with the canonical.
        # request.seo_canonical_path was set a few lines above, in this same
        # view: "" means the primary permalink could not be resolved (omit
        # offers.url entirely — same fail-loud posture as the canonical tag,
        # never fall back to self); a real path means compose the absolute
        # primary URL with the same base_url() helper the canonical template
        # tag uses (permalinks_tags._resolve_canonical_target_url).
        if request.seo_canonical_path and locale is not None:
            product_canonical_url = base_url(locale.language) + request.seo_canonical_path
        else:
            product_canonical_url = None
    else:
        product_canonical_url = (
            request.build_absolute_uri(locale.path)
            if locale is not None
            else request.build_absolute_uri()
        )
    is_in_stock = inventory_status["class"] != "sold-out"
    currency_code = getattr(store, "default_currency", None) or "USD"

    product_schema: dict = {
        "@context": "https://schema.org",
        "@type": "Product",
        "name": translation.title,
        "description": translation.seo_description or "",
        "offers": {
            "@type": "Offer",
            "price": str(default_variant.price) if default_variant else "0",
            "priceCurrency": currency_code,
            "availability": (
                "https://schema.org/InStock"
                if is_in_stock
                else "https://schema.org/OutOfStock"
            ),
        },
    }
    if product_canonical_url:
        product_schema["offers"]["url"] = product_canonical_url
    if images:
        product_schema["image"] = [
            request.build_absolute_uri(img.image.url) for img in images[:5]
        ]

    # BreadcrumbList — emitted when at least one breadcrumb has a URL.
    crumbs_with_url = [c for c in breadcrumbs if c.get("url")]
    jsonld_items: list = [product_schema]
    if crumbs_with_url:
        breadcrumb_schema = {
            "@context": "https://schema.org",
            "@type": "BreadcrumbList",
            "itemListElement": [
                {
                    "@type": "ListItem",
                    "position": i,
                    "name": crumb["label"],
                    "item": request.build_absolute_uri(crumb["url"]),
                }
                for i, crumb in enumerate(crumbs_with_url, 1)
            ],
        }
        jsonld_items.append(breadcrumb_schema)

    product_jsonld_json = _serialize_jsonld(jsonld_items)

    # ── Pixel events (ADR-022 D4/D5) ────────────────────────────────────────
    # Explicit pixel_events context contract — PixelsSlotProvider never sniffs
    # 'product' from context, it only reads this key (design-pattern-ideas
    # §XV-1: implicit sniffing is how events silently stop firing after a
    # template refactor).
    #
    # view_content fires server-side on every product render. add_to_cart has
    # no page load to hang a server-side render off of (the add-to-cart form
    # submits via progressive-enhancement AJAX, see product.html's
    # extra_scripts block) — its payload is pre-built here and embedded as
    # JSON for the page's JS to dispatch as the 'pradize:pixels' CustomEvent
    # bridge on a successful add (frozen event name + payload schema).
    pixel_events = []
    pixel_product_payload_json = ""
    if default_variant is not None:
        from pixels.events import build_add_to_cart_payload, build_view_content_payload

        store_currency = getattr(store, "default_currency", "") or "USD"
        pixel_events.append((
            "view_content",
            build_view_content_payload(
                product=product,
                variant=default_variant,
                price=default_variant.price,
                currency=store_currency,
            ),
        ))
        # Add-to-cart bridge payload only when the normal add-to-cart form is
        # actually rendered (quotation/sold-out/ask-available modes render a
        # different form with no [data-testid="product-form"] element at all —
        # embedding the bridge script for a form that doesn't exist would be
        # dead code, and the script's own querySelector-by-selector-string
        # would otherwise appear in the page source even on those variants).
        # Per-unit payload — product.html's bridge script scales value by the
        # quantity actually submitted before dispatching the bridge event.
        if inventory_status["class"] not in ("quotation", "sold-out", "ask-available"):
            pixel_product_payload_json = _serialize_jsonld(
                build_add_to_cart_payload(
                    product=product,
                    variant=default_variant,
                    price=default_variant.price,
                    currency=store_currency,
                    quantity=1,
                )
            )

    return TemplateResponse(
        request,
        "storefront/pages/product.html",
        {
            "product": product,
            "translation": translation,
            "variants": variants,
            "default_variant": default_variant,
            "option_groups": option_groups,
            "images": images,
            "first_image": first_image,
            "inventory_status": inventory_status,
            "breadcrumbs": breadcrumbs,
            "collection": collection,
            # content_object: allows base.html {% hreflang_tags %} to render
            # (Fix 1 — canonical/hreflang never rendered). Stays the PRODUCT even
            # on a page_version render (ADR-028 §6: beacon object_id / product_id
            # is the product, never the version) — hreflang suppression for
            # version pages is via the separate `page_version` context key below
            # (base.html passes page_version|default:content_object into
            # {% hreflang_tags %}; ProductPageVersion.hreflang_exempt is always
            # True — ADR-028 §4, "no template-tag change").
            "content_object": product,
            # page_version / page_version_id (TICKET-042, ADR-028 §4 / §6):
            # None on the primary page. Consumed by base.html (hreflang
            # suppression), the analytics beacon tag (properties.page_version_id),
            # and the add-to-cart form's hidden field (cart/views.py add_to_cart
            # stamps CartItem.page_version_id after validating it belongs to this
            # store+product — never trusted blindly).
            "page_version": page_version,
            "page_version_id": page_version.pk if page_version is not None else None,
            # product_jsonld_json: safely serialized JSON-LD for <script> tag
            # (Fix 5 — no JSON-LD Product structured data).
            "product_jsonld_json": product_jsonld_json,
            # page_type: used by analytics_beacon tag (TH-043).
            "page_type": "product",
            # pixel_events / pixel_product_payload_json: ADR-022 D5 explicit
            # context contract consumed by PixelsSlotProvider + the product
            # page's add-to-cart bridge script. Identical on a page_version
            # render (ADR-028 §8 non-goal — no pixel payload change).
            "pixel_events": pixel_events,
            "pixel_product_payload_json": pixel_product_payload_json,
            # trust_badge_location: explicit, never-sniffed context key consumed
            # by badges.slot_provider.SecurityBadgeSlotProvider (ADR-030 D4).
            "trust_badge_location": "product",
        },
    )


def collection_page(request, collection):
    """
    Render a collection listing page (TH-081, ML-002).

    Args:
        collection: a Collection instance already scoped to the current store.

    Raises Http404 when:
      - The collection is not published.
      - No published CollectionTranslation exists for the request locale (ML-002).

    Supports:
      - Sort control via ?sort= (price_asc, price_desc, newest).
      - Pagination via ?page= (24 products per page).
      - Products with no published translation for the locale are silently skipped.
    """
    store = getattr(request, "store", None)
    lang = _get_locale_lang(request)

    if not collection.is_published:
        raise Http404(f"Collection {collection.slug!r} is not published.")

    try:
        translation = CollectionTranslation.objects.for_store(store).get(
            collection=collection,
            lang_code=lang,
            status=TranslationStatus.PUBLISHED,
        )
    except CollectionTranslation.DoesNotExist:
        raise Http404(
            f"No published translation for collection {collection.slug!r} in lang {lang!r}."
        )

    sort = request.GET.get("sort", "")

    # Build base queryset of products in this collection.
    # CollectionProduct orders by sort_key by default (Meta.ordering).
    memberships_qs = (
        CollectionProduct.objects.for_store(store)
        .filter(collection=collection)
        .select_related("product")
    )

    if sort == "price_asc":
        from django.db.models import Min
        memberships_qs = memberships_qs.annotate(
            min_price=Min("product__variants__price")
        ).order_by("min_price")
    elif sort == "price_desc":
        from django.db.models import Min
        memberships_qs = memberships_qs.annotate(
            min_price=Min("product__variants__price")
        ).order_by("-min_price")
    elif sort == "newest":
        memberships_qs = memberships_qs.order_by("-product__created_at")
    else:
        memberships_qs = memberships_qs.order_by("sort_key")

    # Paginate memberships before loading translations (fewer queries).
    paginator = Paginator(memberships_qs, _COLLECTION_PAGE_SIZE)
    page_obj = paginator.get_page(request.GET.get("page", 1))

    # Build product items: load translation + permalink slug for each product in the page.
    product_pks_on_page = [m.product_id for m in page_obj.object_list]
    slug_map = _build_product_slug_map(store, product_pks_on_page, lang)

    # Load all published translations for this page in one query.
    translations_qs = (
        ProductTranslation.objects.for_store(store)
        .filter(
            product_id__in=product_pks_on_page,
            lang_code=lang,
            status=TranslationStatus.PUBLISHED,
        )
    )
    translation_map = {t.product_id: t for t in translations_qs}

    # Load one image per product (for product cards).
    images_qs = (
        ProductImage.objects.for_store(store)
        .filter(product_id__in=product_pks_on_page)
        .order_by("display_order")
    )
    # Keep only the first image per product.
    image_map: dict = {}
    for img in images_qs:
        if img.product_id not in image_map:
            image_map[img.product_id] = img

    # Load one default variant per product for the price display on product cards.
    # Order: is_default=True first, then ascending position — first row per product wins.
    variants_qs = (
        ProductVariant.objects.for_store(store)
        .filter(product_id__in=product_pks_on_page, is_active=True)
        .order_by("product_id", "-is_default", "position")
    )
    variant_map: dict = {}
    for variant in variants_qs:
        if variant.product_id not in variant_map:
            variant_map[variant.product_id] = variant

    products_page_items = []
    for membership in page_obj.object_list:
        product = membership.product
        if product.status != Product.STATUS_ACTIVE:
            continue
        prod_trans = translation_map.get(product.pk)
        if prod_trans is None:
            continue  # Skip products with no published translation (ML-002).
        products_page_items.append(
            {
                "product": product,
                "translation": prod_trans,
                "slug": slug_map.get(product.pk, ""),
                "image": image_map.get(product.pk),
                "default_variant": variant_map.get(product.pk),
            }
        )

    # Fix 6: a collection with no published translations for this locale should not
    # be served — return 404 so the sitemap and page-level noindex can never disagree.
    # (The sitemap already excludes thin collections; 404 on empty ensures no
    # indexable-but-empty page ever reaches crawlers.)
    if not products_page_items:
        raise Http404(
            f"No translated products for collection {collection.slug!r} in lang {lang!r}."
        )

    # Use _build_storefront_url for language-prefix-correct paths (Fix 4).
    breadcrumbs = [
        {"label": "Home", "url": _build_storefront_url(request, "")},
        {"label": translation.title or collection.title, "url": None},
    ]

    sort_options = [
        {"value": "", "label": _("Featured")},
        {"value": "price_asc", "label": _SORT_LABELS["price_asc"]},
        {"value": "price_desc", "label": _SORT_LABELS["price_desc"]},
        {"value": "newest", "label": _SORT_LABELS["newest"]},
    ]

    return TemplateResponse(
        request,
        "storefront/pages/collection.html",
        {
            "collection": collection,
            "translation": translation,
            "products_page_items": products_page_items,
            "page_obj": page_obj,
            "sort": sort,
            "sort_options": sort_options,
            "breadcrumbs": breadcrumbs,
            # content_object: allows base.html {% hreflang_tags %} to render
            # (Fix 1 — canonical/hreflang never rendered).
            "content_object": collection,
            # page_type: used by analytics_beacon tag (TH-043).
            "page_type": "collection",
        },
    )


def cart_page(request):
    """
    Render the shopping cart page (TH-083).

    Reads the active cart from the session. Returns an empty-cart state when
    no session or no active cart exists.

    Template receives `items_ctx` — a list of enriched dicts
    {item: CartItem, image: ProductImage | None}.
    Product images are preloaded here to avoid unscoped lazy queries in the template
    (StoreScopedManager.IsolationError if accessed via reverse FK without for_store()).
    """
    store = getattr(request, "store", None)
    session_key = getattr(getattr(request, "session", None), "session_key", None)

    cart = None
    items_ctx: list = []
    subtotal = Decimal("0")

    if store is not None and session_key:
        cart = (
            Cart.objects.for_store(store)
            .filter(session_key=session_key, status=CartStatus.ACTIVE)
            .first()
        )
        if cart is not None:
            items = list(
                CartItem.objects.for_store(store)
                .filter(cart=cart)
                .select_related("variant", "variant__product")
                .order_by("created_at")
            )
            subtotal = sum(item.unit_price * item.quantity for item in items)

            # Preload one image per product — avoids unscoped lazy queries in template.
            product_pks = [item.variant.product_id for item in items]
            cart_images_qs = (
                ProductImage.objects.for_store(store)
                .filter(product_id__in=product_pks)
                .order_by("display_order")
            )
            cart_image_map: dict = {}
            for img in cart_images_qs:
                if img.product_id not in cart_image_map:
                    cart_image_map[img.product_id] = img

            # Preload permalink slugs for all cart products (ADR-005 §3:
            # never hardcode product.slug as a URL — use the Permalink table).
            lang = _get_locale_lang(request)
            slug_map = _build_product_slug_map(store, product_pks, lang)

            items_ctx = [
                {
                    "item": item,
                    "image": cart_image_map.get(item.variant.product_id),
                    # slug from Permalink table — not product.slug (may diverge after renames).
                    "slug": slug_map.get(item.variant.product_id, ""),
                }
                for item in items
            ]

    return TemplateResponse(
        request,
        "storefront/pages/cart.html",
        {
            "cart": cart,
            "items_ctx": items_ctx,
            "subtotal": subtotal,
            # page_type: used by analytics_beacon tag (TH-043).
            "page_type": "cart",
            # trust_badge_location: explicit, never-sniffed context key consumed
            # by badges.slot_provider.SecurityBadgeSlotProvider (ADR-030 D4).
            "trust_badge_location": "cart",
        },
    )


def search_view(request):
    """
    Simple product search page (TH-086).

    GET /search/?q=<query>
    Filters active products for the store where the primary title contains the query.
    Also returns products with a published translation whose title matches the query.
    Results are deduplicated by product PK.

    Phase 2 stub — full-text / faceted search is a Phase 5+ concern.
    """
    store = getattr(request, "store", None)
    lang = _get_locale_lang(request)
    q = request.GET.get("q", "").strip()

    results = []
    total = 0

    if store is not None and q:
        # Match on primary product title OR published translation title.
        from django.db.models import Q as DQ

        matched_by_title = (
            Product.objects.for_store(store)
            .filter(status=Product.STATUS_ACTIVE, title__icontains=q)
        )
        matched_by_translation = (
            Product.objects.for_store(store)
            .filter(
                status=Product.STATUS_ACTIVE,
                translations__lang_code=lang,
                translations__status=TranslationStatus.PUBLISHED,
                translations__title__icontains=q,
            )
        )
        # Union with distinct to avoid duplicates.
        product_qs = (matched_by_title | matched_by_translation).distinct()

        product_list = list(product_qs)
        total = len(product_list)

        product_pks = [p.pk for p in product_list]
        slug_map = _build_product_slug_map(store, product_pks, lang)

        translation_map = {
            t.product_id: t
            for t in ProductTranslation.objects.for_store(store).filter(
                product_id__in=product_pks,
                lang_code=lang,
                status=TranslationStatus.PUBLISHED,
            )
        }

        images_qs = (
            ProductImage.objects.for_store(store)
            .filter(product_id__in=product_pks)
            .order_by("display_order")
        )
        image_map: dict = {}
        for img in images_qs:
            if img.product_id not in image_map:
                image_map[img.product_id] = img

        # Load one default variant per product for price display on product cards.
        search_variants_qs = (
            ProductVariant.objects.for_store(store)
            .filter(product_id__in=product_pks, is_active=True)
            .order_by("product_id", "-is_default", "position")
        )
        search_variant_map: dict = {}
        for variant in search_variants_qs:
            if variant.product_id not in search_variant_map:
                search_variant_map[variant.product_id] = variant

        for product in product_list:
            results.append(
                {
                    "product": product,
                    "translation": translation_map.get(product.pk),
                    "slug": slug_map.get(product.pk, ""),
                    "image": image_map.get(product.pk),
                    "default_variant": search_variant_map.get(product.pk),
                }
            )

    return TemplateResponse(
        request,
        "storefront/pages/search.html",
        {
            "query": q,
            "results": results,
            "total": total,
            # page_type: used by analytics_beacon tag (TH-043).
            "page_type": "search",
        },
    )


def custom_404_view(request, exception=None):
    """
    Custom 404 handler (TH-087).

    Registered as handler404 in webecom/urls.py.
    Returns HTTP 404 — never a soft-200 (TH-087 binding rule).
    Renders storefront/pages/404.html with a search bar and featured-collection links.
    """
    store = getattr(request, "store", None)
    lang = _get_locale_lang(request)

    featured_collections = []
    if store is not None:
        raw_collections = list(
            Collection.objects.for_store(store)
            .filter(is_published=True)
            .order_by("created_at")[: _HOME_FEATURED_COLLECTION_COUNT]
        )
        for collection in raw_collections:
            try:
                coll_trans = CollectionTranslation.objects.for_store(store).get(
                    collection=collection,
                    lang_code=lang,
                    status=TranslationStatus.PUBLISHED,
                )
            except CollectionTranslation.DoesNotExist:
                coll_trans = None

            from django.contrib.contenttypes.models import ContentType
            from permalinks.models import Permalink

            coll_ct = ContentType.objects.get_for_model(Collection)
            coll_permalink = (
                Permalink.objects.for_store(store)
                .filter(
                    content_type=coll_ct,
                    object_id=collection.pk,
                    lang=lang,
                    is_active=True,
                )
                .first()
            )
            featured_collections.append(
                {
                    "collection": collection,
                    "translation": coll_trans,
                    "slug": coll_permalink.slug if coll_permalink else "",
                }
            )

    return TemplateResponse(
        request,
        "storefront/pages/404.html",
        {
            "featured_collections": featured_collections,
            # page_type: used by analytics_beacon tag (TH-043).
            "page_type": "404",
        },
        status=404,
    )
