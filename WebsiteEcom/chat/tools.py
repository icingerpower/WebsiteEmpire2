"""
Read-only tools exposed to the chat model (ADR-024 Decision 3).

Exactly four tools, all bound (via build_tool_specs) to the request's store and
locale so the store id is NEVER a model-visible or model-supplied parameter
(ADR-024 Decision 3, AC-CHAT-03): search_products, get_product, list_collections,
get_store_policy.

Every handler is read-only — SELECT only, no .save()/.create()/.update()/.delete()
anywhere in this module (ADR-024 Decision 7, AC-CHAT-04). This is asserted by
chat/tests/test_tools.py via assertNumQueries-style write-count assertions.

Handlers return plain JSON-serializable dicts; chat/anthropic_client.py
json.dumps()s them into tool_result content blocks. All text returned here is
either raw catalog data or the result of chat/localization.py's fallback rule —
never text invented by this module (ADR-024 Decision 3: "Tool results are
rendered by our code from our rows").
"""

import logging

from catalog.models import InventoryMode, Product, ProductVariant
from catalog.models import Collection
from chat.localization import (
    absolute_permalink_url,
    localized_collection_fields,
    localized_product_fields,
    localized_static_page_fields,
)
from pages.models import StaticPageKind

logger = logging.getLogger(__name__)

MAX_SEARCH_RESULTS = 5

# ---------------------------------------------------------------------------
# Tool schemas (Anthropic Messages API `tools` format)
# ---------------------------------------------------------------------------

TOOL_SEARCH_PRODUCTS = {
    "name": "search_products",
    "description": (
        "Search this store's live product catalog by keyword. Returns at most "
        "5 matching active products with id, title, price range, in-stock flag, "
        "and absolute permalink. Use this to find candidate products before "
        "recommending anything — never invent a product that is not in the results."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "query": {
                "type": "string",
                "description": "Keywords to search for (product title/tags).",
            },
            "max_results": {
                "type": "integer",
                "description": "Maximum number of results to return (1-5).",
                "minimum": 1,
                "maximum": MAX_SEARCH_RESULTS,
            },
        },
        "required": ["query"],
    },
}

TOOL_GET_PRODUCT = {
    "name": "get_product",
    "description": (
        "Get full detail for one product by id, including every variant with its "
        "own price and stock status, and the absolute permalink. Use this before "
        "stating a specific price or stock claim about a product."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "product_id": {
                "type": "integer",
                "description": "The product id, as returned by search_products.",
            },
        },
        "required": ["product_id"],
    },
}

TOOL_LIST_COLLECTIONS = {
    "name": "list_collections",
    "description": (
        "List this store's published collections (names + absolute permalinks). "
        "Use this when the buyer asks to browse a category rather than search "
        "for a specific item."
    ),
    "input_schema": {
        "type": "object",
        "properties": {},
    },
}

_POLICY_KINDS = ("shipping", "refund", "terms", "contact")

TOOL_GET_STORE_POLICY = {
    "name": "get_store_policy",
    "description": (
        "Get this store's published policy text for one of: shipping, refund, "
        "terms, contact. Use this before stating anything about shipping times, "
        "return/refund rules, or how to contact the store — never state a policy "
        "detail that did not come from this tool. If the tool reports not found, "
        "direct the buyer to the store's contact form instead of guessing."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "kind": {
                "type": "string",
                "enum": list(_POLICY_KINDS),
                "description": "Which policy to fetch.",
            },
        },
        "required": ["kind"],
    },
}

ALL_TOOL_DEFINITIONS = [
    TOOL_SEARCH_PRODUCTS,
    TOOL_GET_PRODUCT,
    TOOL_LIST_COLLECTIONS,
    TOOL_GET_STORE_POLICY,
]


# ---------------------------------------------------------------------------
# Handlers — every query below is scoped via .for_store(store); read-only.
# ---------------------------------------------------------------------------


def _variant_in_stock(variant: ProductVariant) -> bool:
    """True unless the variant is explicitly sold out or a tracked quantity is 0."""
    if variant.inventory_mode == InventoryMode.SOLD_OUT:
        return False
    if variant.inventory_mode == InventoryMode.FIXED_QTY:
        return (variant.quantity or 0) > 0
    return True


def _product_summary(store, product, lang_code: str) -> dict:
    title, _description = localized_product_fields(store, product, lang_code)
    variants = list(
        ProductVariant.objects.for_store(store).filter(product=product, is_active=True)
    )
    if variants:
        prices = [v.price for v in variants]
        price_min, price_max = min(prices), max(prices)
        in_stock = any(_variant_in_stock(v) for v in variants)
    else:
        price_min = price_max = None
        in_stock = False
    return {
        "id": product.pk,
        "title": title,
        "price_min": str(price_min) if price_min is not None else None,
        "price_max": str(price_max) if price_max is not None else None,
        "currency": store.default_currency,
        "in_stock": in_stock,
        "permalink": absolute_permalink_url(store, product, lang_code),
    }


def search_products(store, lang_code: str, query: str = "", max_results: int = MAX_SEARCH_RESULTS) -> dict:
    query = (query or "").strip()
    try:
        capped = max(1, min(int(max_results or MAX_SEARCH_RESULTS), MAX_SEARCH_RESULTS))
    except (TypeError, ValueError):
        capped = MAX_SEARCH_RESULTS

    if not query:
        return {"results": []}

    products = list(
        Product.objects.for_store(store)
        .filter(status=Product.STATUS_ACTIVE, title__icontains=query)
        .order_by("title")[:capped]
    )
    return {"results": [_product_summary(store, p, lang_code) for p in products]}


def get_product(store, lang_code: str, product_id: int | None = None) -> dict:
    if product_id is None:
        return {"error": "not_found"}
    try:
        product = Product.objects.for_store(store).get(pk=product_id, status=Product.STATUS_ACTIVE)
    except (Product.DoesNotExist, ValueError, TypeError):
        # ValueError/TypeError: a non-integer product_id from the model — never 500.
        # .for_store(store) already excludes cross-store ids (AC-CHAT-03).
        return {"error": "not_found"}

    title, description = localized_product_fields(store, product, lang_code)
    variants = list(
        ProductVariant.objects.for_store(store)
        .filter(product=product, is_active=True)
        .order_by("position")
    )
    return {
        "id": product.pk,
        "title": title,
        "description": description,
        "permalink": absolute_permalink_url(store, product, lang_code),
        "variants": [
            {
                "id": v.pk,
                "title": v.title,
                "price": str(v.price),
                "currency": store.default_currency,
                "in_stock": _variant_in_stock(v),
            }
            for v in variants
        ],
    }


def list_collections(store, lang_code: str) -> dict:
    collections = list(
        Collection.objects.for_store(store).filter(is_published=True).order_by("title")
    )
    results = []
    for collection in collections:
        title, _description = localized_collection_fields(store, collection, lang_code)
        results.append({
            "title": title,
            "permalink": absolute_permalink_url(store, collection, lang_code),
        })
    return {"results": results}


def get_store_policy(store, lang_code: str, kind: str = "", store_chat_settings=None) -> dict:
    kind = (kind or "").strip().lower()
    if kind not in _POLICY_KINDS:
        return {"error": "unknown_kind"}

    page = None
    if store_chat_settings is not None:
        page = {
            "shipping": store_chat_settings.shipping_policy_page,
            "refund": store_chat_settings.refund_policy_page,
            "terms": store_chat_settings.terms_policy_page,
            "contact": store_chat_settings.contact_page,
        }.get(kind)

        # Security audit H2 (defense-in-depth): the admin now scopes these FKs
        # to the request store (chat/admin.py formfield_for_foreignkey), but
        # nothing at the DB level stops a cross-store pk from being persisted
        # by a data migration, shell/bulk write, or a future admin bug. Never
        # trust the FK's store implicitly — re-check it here and treat a
        # mismatch exactly like an unconfigured policy (no error detail is
        # leaked to the shopper; the misconfiguration is logged for an admin
        # to investigate).
        if page is not None and getattr(page, "store_id", None) != store.pk:
            logger.warning(
                "get_store_policy: %s_policy_page (pk=%s) belongs to store %s, "
                "not the requesting store %s — treating as not configured.",
                kind, page.pk, page.store_id, store.pk,
            )
            page = None

    if page is None and kind == "contact":
        # Fallback: any published contact-kind StaticPage for the store, in the
        # absence of an explicit designation (see StoreChatSettings docstring).
        from pages.models import StaticPage

        page = (
            StaticPage.objects.for_store(store)
            .filter(kind=StaticPageKind.CONTACT, is_published=True)
            .first()
        )

    if page is None or not page.is_published:
        return {"error": "not_found", "kind": kind}

    title, body = localized_static_page_fields(store, page, lang_code)
    return {"kind": kind, "title": title, "body": body}


# ---------------------------------------------------------------------------
# Binding: build (definition, handler) pairs closed over (store, lang_code)
# ---------------------------------------------------------------------------


def build_tool_specs(store, lang_code: str, store_chat_settings=None) -> list[dict]:
    """
    Return [{"definition": <tool schema>, "handler": callable(input: dict) -> dict}, ...]
    for all four tools, closed over `store` and `lang_code` so the model never
    supplies or sees the store id (ADR-024 Decision 3).
    """
    return [
        {
            "definition": TOOL_SEARCH_PRODUCTS,
            "handler": lambda tool_input: search_products(
                store, lang_code,
                query=tool_input.get("query", ""),
                max_results=tool_input.get("max_results", MAX_SEARCH_RESULTS),
            ),
        },
        {
            "definition": TOOL_GET_PRODUCT,
            "handler": lambda tool_input: get_product(
                store, lang_code, product_id=tool_input.get("product_id"),
            ),
        },
        {
            "definition": TOOL_LIST_COLLECTIONS,
            "handler": lambda tool_input: list_collections(store, lang_code),
        },
        {
            "definition": TOOL_GET_STORE_POLICY,
            "handler": lambda tool_input: get_store_policy(
                store, lang_code,
                kind=tool_input.get("kind", ""),
                store_chat_settings=store_chat_settings,
            ),
        },
    ]
