"""
Storefront context processor (ADR-012 D4, TICKET-029 Phase 1, T029-mini-cart).

Exposes theme_ctx (ThemeContext) and mini-cart data to all templates rendered
for storefront paths.

ThemeMiddleware must run before the view so that request.theme_ctx is already set.
Falls back gracefully if ThemeMiddleware did not run (e.g. admin pages).

Mini-cart context (T029):
  cart_item_count  — total quantity of items across all line items (int).
  cart_items       — list of CartItem instances (select_related variant__product).
  cart_total       — subtotal as Decimal (sum of unit_price × quantity, no discounts).

Mini-cart variables always present in context (zero/empty when no cart exists),
so templates never need to guard for KeyError on missing variables.
Store isolation is enforced: cart is fetched via Cart.objects.for_store(store).

Currency context (ADR-023 §4, TICKET-031):
  display_currency — the shopper's resolved display currency code (ISO 4217),
                      via currency.session.resolve_display_currency() — the
                      single read path. Falls back to store.default_currency
                      when there is no request.store (e.g. admin pages).
"""

from decimal import Decimal

from storefront.theme import ThemeContext, _build_fallback_context

_MINI_CART_DEFAULTS = {
    "cart_item_count": 0,
    "cart_items": [],
    "cart_total": Decimal("0.00"),
}


def _get_mini_cart_context(request, store):
    """
    Return mini-cart context dict for the given request and store.

    Performs at most 2 DB queries (one for Cart, one for CartItems).
    Returns _MINI_CART_DEFAULTS on any failure path (no session, no cart, etc.)
    so the mini-cart template always has well-typed variables.

    Store isolation: cart is fetched with .for_store(store) — never unscoped.
    """
    from cart.models import Cart, CartItem, CartStatus

    session = getattr(request, "session", None)
    if session is None:
        return _MINI_CART_DEFAULTS

    session_key = getattr(session, "session_key", None)
    if not session_key:
        return _MINI_CART_DEFAULTS

    try:
        # status=ACTIVE guard (ADR-016): excludes CONVERTED carts (session_key
        # rotated by begin_checkout) and ABANDONED carts from the mini-cart
        # so a freshly-completed purchase shows an empty cart immediately.
        cart = Cart.objects.for_store(store).get(
            session_key=session_key, status=CartStatus.ACTIVE
        )
    except Cart.DoesNotExist:
        return _MINI_CART_DEFAULTS

    items = list(
        CartItem.objects.for_store(store)
        .filter(cart=cart)
        .select_related("variant__product")
    )

    cart_item_count = sum(item.quantity for item in items)
    cart_total = sum((item.line_total for item in items), Decimal("0.00"))

    return {
        "cart_item_count": cart_item_count,
        "cart_items": items,
        "cart_total": cart_total,
    }


def storefront_context(request):
    """
    Inject storefront theme context and mini-cart data into every template context.

    Returns:
        {
            "theme_ctx": ThemeContext — the active theme for this request.
                         Falls back to the reference theme if ThemeMiddleware
                         did not run (e.g. admin pages, test clients that bypass
                         middleware).
            "cart_item_count": int — total item quantity across all cart lines.
            "cart_items": list[CartItem] — line items with variant__product preloaded.
            "cart_total": Decimal — subtotal (unit_price × quantity, no discounts).
            "display_currency": str — resolved display currency code (ADR-023 §4).
        }
    """
    theme_ctx = getattr(request, "theme_ctx", None)
    if theme_ctx is None or not isinstance(theme_ctx, ThemeContext):
        # ThemeMiddleware skips admin/excluded paths; provide a safe fallback so
        # templates that include base.html from those paths don't AttributeError.
        from django.conf import settings
        key = getattr(settings, "STOREFRONT_REFERENCE_THEME", "general")
        theme_ctx = _build_fallback_context(key, is_preview=False)

    ctx = {"theme_ctx": theme_ctx}

    store = getattr(request, "store", None)
    if store is not None:
        ctx.update(_get_mini_cart_context(request, store))
        from currency.session import resolve_display_currency

        ctx["display_currency"] = resolve_display_currency(request, store)
    else:
        ctx.update(_MINI_CART_DEFAULTS)
        ctx["display_currency"] = ""

    return ctx
