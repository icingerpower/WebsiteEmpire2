"""
Pixel event payload builder — single source of truth (ADR-022 D4).

Provider templates only FORMAT these values; they never recompute money,
currency, or content-id rules. Every builder returns a plain dict consumed
uniformly by PixelProvider.render_event() (pixels/registry.py) — the payload
shape is intentionally identical across canonical events so the slot bridge
never needs per-event special-casing.

Payload rules (ADR-022 D4):
  - value: string, 2 decimal places, built from Decimal — NEVER float (money
    rule). Purchase uses order.total; other events use the displayed/cart
    price.
  - currency: ISO 4217. Purchase uses order.currency; every other event uses
    store.default_currency (AC-161: transactions always charge store
    currency; display-only conversion never changes pixel currency).
  - content_ids: MUST equal the future catalog-feed (T033) item IDs — see
    catalog_item_id() below, frozen for both consumers.
  - num_items / content_name: best-effort optional fields; their absence
    must never break rendering (templates guard with defaults).
"""

from decimal import Decimal


def catalog_item_id(product, variant=None) -> str:
    """
    Frozen convention (ADR-022 D4) shared by pixels AND the future T033
    catalog feeds. Returns str(product.pk) for a product-level ID, or
    "{product.pk}_{variant.pk}" when a variant is known.

    MUST NOT change without an ADR amendment — both consumers depend on
    stability for Meta/TikTok dynamic retargeting (a content_ids mismatch
    silently breaks retargeting, per the ADR-022 Risks section).
    """
    if variant is not None:
        return f"{product.pk}_{variant.pk}"
    return str(product.pk)


def _format_money(value) -> str:
    """Format a Decimal money value as a fixed 2-decimal-place string (never float)."""
    return f"{Decimal(value):.2f}"


def build_view_content_payload(*, product, variant, price: Decimal, currency: str) -> dict:
    """Payload for the 'view_content' canonical event (product page render)."""
    return {
        "value": _format_money(price),
        "currency": currency,
        "content_ids": [catalog_item_id(product, variant)],
        "content_name": getattr(product, "title", "") or "",
        "num_items": 1,
    }


def build_add_to_cart_payload(
    *, product, variant, price: Decimal, currency: str, quantity: int = 1
) -> dict:
    """
    Payload for the 'add_to_cart' canonical event, embedded server-side in the
    product page's #pixel-product-payload JSON block (ADR-022 D5 JS bridge).
    """
    return {
        "value": _format_money(Decimal(price) * quantity),
        "currency": currency,
        "content_ids": [catalog_item_id(product, variant)],
        "content_name": getattr(product, "title", "") or "",
        "num_items": quantity,
    }


def build_initiate_checkout_payload(
    *, content_ids, value: Decimal, currency: str, num_items: int
) -> dict:
    """Payload for the 'initiate_checkout' canonical event (first checkout GET)."""
    return {
        "value": _format_money(value),
        "currency": currency,
        "content_ids": list(content_ids),
        "num_items": num_items,
    }


def build_purchase_payload(order) -> dict:
    """
    Payload for the 'purchase' canonical event (first PAID thank-you render).

    event_id is the platform-side dedup key (ADR-022 D4/D6, AC-110): stable
    and order-derived so even a double-fire that slips past the FiredPixel
    claim is deduped platform-side, and a future CAPI follow-up can reuse it.
    transaction_id is GA4's native dedup field (str(order.pk)).

    content_ids/num_items are best-effort: OrderItem.product_variant is
    nullable (SET_NULL — the catalog row may have been deleted since the
    order was placed), so items with no surviving variant are skipped rather
    than breaking the whole payload.

    Uses order.items.for_store(order.store) rather than order.items.all() —
    OrderItem is a StoreOwnedModel, and the reverse FK manager only bypasses
    the unscoped-query guard when a store-scoped prefetch cache is already
    populated (ADR-001 §4); this function has no such guarantee about its
    caller, so it scopes explicitly.
    """
    items = list(order.items.for_store(order.store).select_related('product_variant__product'))
    content_ids = [
        catalog_item_id(item.product_variant.product, item.product_variant)
        for item in items
        if item.product_variant_id is not None
    ]
    return {
        "value": _format_money(order.total),
        "currency": order.currency,
        "content_ids": content_ids,
        "content_name": "",
        "num_items": sum(item.quantity for item in items),
        "event_id": f"order-{order.pk}-purchase",
        "transaction_id": str(order.pk),
    }
