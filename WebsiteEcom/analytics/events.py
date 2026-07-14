"""
Server-side analytics event helpers — called from the order/checkout layer.

Pixel dedup (FiredPixel) is the responsibility of the caller (orders app).
These helpers only ingest into the analytics DB.
"""

from .ingest import ingest_events


def record_initiate_checkout(store_id: int, session_id: str) -> None:
    """
    Record a server-side checkout_start event (EVT_INITIATE_CHECKOUT, CK-003).

    Called from checkout_view on the first GET /checkout/ after a CheckoutState
    exists.  The call site guards against double-firing via a conditional UPDATE on
    CheckoutState.initiate_event_fired (ADR-015 §2).

    Never raises — ingest_events swallows all ingest errors internally.
    """
    ingest_events(
        store_id=store_id,
        session_id=session_id or 'server',
        events=[{'event_type': 'checkout_start'}],
    )


def record_purchase(order) -> None:
    """
    Record a server-side purchase event at payment confirmation.

    Called from the payment webhook handler or order service after a successful
    charge. FiredPixel dedup is handled separately (see orders.models.FiredPixel).

    order must expose:
        store_id, pk, utm_source, utm_medium, utm_campaign, utm_term,
        utm_content, first_referrer, landing_page, total, currency, items.

    properties.page_version_ids (TICKET-042 / ADR-028 §6): distinct non-null
    CartItem->OrderItem page_version_id stamps of the order's items, sorted for
    determinism. Empty list when no item was added from a page-version URL.
    Uses cross_store_unsafe() — order is already pk-scoped, no cross-tenant risk
    (same pattern as orders.service.void_pending_order's order.items access).
    """
    page_version_ids = sorted({
        item.page_version_id
        for item in order.items.cross_store_unsafe().all()
        if item.page_version_id is not None
    })

    ingest_events(
        store_id=order.store_id,
        session_id='server',
        events=[{
            'event_type': 'purchase',
            'order_id': order.pk,
            'utm_source': order.utm_source,
            'utm_medium': order.utm_medium,
            'utm_campaign': order.utm_campaign,
            'utm_term': order.utm_term,
            'utm_content': order.utm_content,
            'referrer': order.first_referrer,
            'landing_page': order.landing_page,
            # value column: order total for top_country_revenue aggregation.
            'value': float(order.total),
            'properties': {
                'total': str(order.total),
                'currency': order.currency,
                'page_version_ids': page_version_ids,
            },
        }],
    )
