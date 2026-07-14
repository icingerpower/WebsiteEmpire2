"""
Cart service: get/create cart, add/remove/update items, apply discounts, compute totals.

Public API:
  get_or_create_cart(store, session_key) -> Cart
  add_item(cart, variant_id, quantity=1) -> CartItem
  remove_item(cart, variant_id) -> None
  update_quantity(cart, variant_id, quantity) -> CartItem | None
  compute_totals(cart) -> dict
  apply_discount_code(cart, code_str) -> DiscountCode

Design notes:
- All CartItem reads go through .for_store() to enforce store isolation (ADR-001 §4).
- compute_totals() is advisory — it reads but does NOT write. The actual atomic coupon
  decrement happens inside begin_checkout (ADR-002 §4, §XV-5).
- apply_discount_code() validates the code and sets cart.discount_code. This is advisory
  (the cart-apply check is for UX); checkout re-validates under select_for_update.
"""

from decimal import Decimal

from .models import Cart, CartItem, CartStatus


# ---------------------------------------------------------------------------
# Order void
# ---------------------------------------------------------------------------

def void_pending_order(order) -> bool:
    """
    Atomically cancel a stale PENDING_PAYMENT order.

    Called by the void_stale_pending_orders GC task (ADR-015 §3).
    Delegates to orders.service.void_pending_order which owns the full
    cancellation logic: charge closure, discount restore, inventory restore,
    CheckoutState cleanup, and best-effort Stripe PI void.

    Stripe PI void is best-effort (logged on failure, never re-raised).

    Returns
    -------
    True if this call voided the order.
    False if the order was already cancelled/confirmed by a concurrent call, or
    if the processor reports the PaymentIntent already succeeded/is processing
    (MEDIUM-2 guard, CHECKOUT_BATCH_2_AUDIT.md) — callers must not count this as
    a void.
    """
    from orders.service import void_pending_order as _void_order
    return _void_order(order)


# ---------------------------------------------------------------------------
# Cart lifecycle
# ---------------------------------------------------------------------------

def get_or_create_cart(store, session_key: str) -> Cart:
    """
    Return the active Cart for this store+session, creating one if needed.

    Uses a plain try/get + conditional-update + save pattern rather than
    get_or_create because get_or_create on the scoped manager requires passing
    store= twice, and because we need distinct handling for ABANDONED carts.

    Session-key lifecycle:
    - CONVERTED carts have their session_key rotated to "converted-{pk}" by
      begin_checkout (ADR-016). They are therefore unreachable here by the
      original session_key, so no special handling is needed for them.
    - ABANDONED carts keep their original session_key (no rotation). When a
      shopper returns and adds to the cart, we re-activate them rather than
      trying to create a new cart, which would violate the unique_together
      constraint on (store, session_key) and raise IntegrityError.

    Reactivation UPDATE is conditional on status=ABANDONED to guard against the
    race where two concurrent requests both see no ACTIVE cart and both try to
    reactivate: the second UPDATE matches 0 rows, which is harmless — the cart
    is already ACTIVE from the first request.
    """
    # Fast path: ACTIVE cart already exists.
    try:
        return Cart.objects.for_store(store).get(
            session_key=session_key, status=CartStatus.ACTIVE
        )
    except Cart.DoesNotExist:
        pass

    # Shopper returned after abandonment: re-activate the existing cart rather
    # than creating a new one (which would collide on the unique_together constraint).
    try:
        cart = Cart.objects.for_store(store).get(
            session_key=session_key, status=CartStatus.ABANDONED
        )
        Cart.objects.for_store(store).filter(
            pk=cart.pk, status=CartStatus.ABANDONED
        ).update(status=CartStatus.ACTIVE)
        cart.status = CartStatus.ACTIVE
        return cart
    except Cart.DoesNotExist:
        pass

    # No ACTIVE or ABANDONED cart exists for this session — create one fresh.
    cart = Cart(store=store, session_key=session_key)
    cart.save()
    return cart


# ---------------------------------------------------------------------------
# Item operations
# ---------------------------------------------------------------------------

def add_item(cart: Cart, variant_id: int, quantity: int = 1, page_version_id: int | None = None) -> CartItem:
    """
    Add quantity units of variant_id to the cart.

    If the variant is already in the cart, the quantity is incremented and the
    EXISTING row's page_version_id is left untouched — "first-touch per line"
    (TICKET-042 / ADR-028 §1: a line re-added from a different page version
    keeps its first stamp; documented behavior, part of the report's
    "known attribution limits").
    unit_price is snapshotted from ProductVariant.price at call time.

    Args:
        page_version_id: soft ID of the ProductPageVersion the add-to-cart
            originated from (TICKET-042 / ADR-028 §1/§6), or None for the
            primary product page. Callers (cart/views.py add_to_cart) MUST
            validate this belongs to the same store+product before passing it
            here — this function trusts its caller and does not re-validate
            (never trust unvalidated public input into a stamp, Safety gate).

    Raises catalog.models.ProductVariant.DoesNotExist if the variant is not found
    in this store.
    """
    from catalog.models import InventoryMode, ProductVariant

    variant = ProductVariant.objects.for_store(cart.store).get(pk=variant_id)

    # Modes that prevent adding to cart at all.
    # FIXED_QTY is allowed here; quantity is validated + decremented atomically at checkout.
    # PRESALE, NO_TRACKING, and FAKE_SERVER are all allowed.
    if variant.inventory_mode == InventoryMode.SOLD_OUT:
        raise ValueError("This item is currently sold out.")
    if variant.inventory_mode == InventoryMode.ASK_WHEN_AVAILABLE:
        raise ValueError("This item is available on request only — contact us.")
    if variant.inventory_mode == InventoryMode.QUOTATION:
        raise ValueError("This item requires a custom quote — contact us.")

    existing = (
        CartItem.objects.for_store(cart.store)
        .filter(cart=cart, variant=variant)
        .first()
    )
    if existing is not None:
        existing.quantity += quantity
        existing.save(update_fields=['quantity'])
        return existing

    item = CartItem(
        store=cart.store,
        cart=cart,
        variant=variant,
        quantity=quantity,
        unit_price=variant.price,
        page_version_id=page_version_id,
    )
    item.save()
    return item


def remove_item(cart: Cart, variant_id: int) -> None:
    """Remove the cart item for the given variant. No-op if the item is not present."""
    CartItem.objects.for_store(cart.store).filter(
        cart=cart, variant_id=variant_id
    ).delete()


def update_quantity(cart: Cart, variant_id: int, quantity: int) -> CartItem | None:
    """
    Set the quantity for a cart item.

    quantity=0 removes the item (calls remove_item).
    Returns the updated CartItem, or None if removed.
    Raises ValueError if the item is not in the cart and quantity > 0.
    """
    if quantity == 0:
        remove_item(cart, variant_id)
        return None

    item = (
        CartItem.objects.for_store(cart.store)
        .filter(cart=cart, variant_id=variant_id)
        .first()
    )
    if item is None:
        raise ValueError(
            f"Variant {variant_id} is not in the cart. Use add_item() first."
        )
    item.quantity = quantity
    item.save(update_fields=['quantity'])
    return item


# ---------------------------------------------------------------------------
# Totals — advisory only (no DB writes)
# ---------------------------------------------------------------------------

def compute_totals(cart: Cart) -> dict:
    """
    Return a dict with subtotal, coupon_discount, gift_card_discount,
    discount_amount, total, and currency.

    This is a read-only, advisory calculation for display purposes.
    The actual atomic discount consumption happens inside begin_checkout.

    Discount application order (ADR-002 §3 / B3):
    - Coupon (from cart.discount_code if it is a coupon type) applied to subtotal.
    - Gift card (from cart.gift_card, OR cart.discount_code if it is a gift-card type)
      applied to the coupon remainder. Advisory amount = min(current_balance, remainder).
    """
    from discounts.models import DiscountType
    from discounts.service import calculate_coupon_discount

    items = list(
        CartItem.objects.for_store(cart.store).filter(cart=cart).select_related('variant')
    )
    subtotal = sum(
        item.unit_price * item.quantity for item in items
    ) or Decimal('0')

    coupon_discount = Decimal('0')
    gift_card_discount = Decimal('0')

    discount_code = cart.discount_code
    gift_card = cart.gift_card

    if discount_code:
        if discount_code.discount_type == DiscountType.COUPON:
            coupon_discount = calculate_coupon_discount(discount_code, subtotal)
        elif discount_code.discount_type in (
            DiscountType.GIFT_CARD_MANUAL,
            DiscountType.GIFT_CARD_AUTO,
        ):
            # Old-style: gift card stored in discount_code field
            remainder = subtotal - coupon_discount
            gift_card_discount = min(discount_code.current_balance, remainder)

    if gift_card:
        # New-style: dedicated gift card field (B3)
        remainder = subtotal - coupon_discount
        gift_card_discount = min(gift_card.current_balance, remainder)

    discount_amount = coupon_discount + gift_card_discount
    total = max(subtotal - discount_amount, Decimal('0'))
    return {
        'subtotal': subtotal,
        'coupon_discount': coupon_discount,
        'gift_card_discount': gift_card_discount,
        'discount_amount': discount_amount,
        'total': total,
        'currency': cart.currency,
    }


# ---------------------------------------------------------------------------
# Discount code
# ---------------------------------------------------------------------------

def apply_discount_code(cart: Cart, code_str: str):
    """
    Validate and attach a discount code to the cart.

    Routes the code to the correct cart field (B3 — ADR-002 §3):
    - Coupon type  → cart.discount_code
    - Gift card type → cart.gift_card

    Raises ValueError with a user-visible message on any validation failure.
    Returns the DiscountCode instance on success.

    This is advisory: cart-apply checks are for UX only. Checkout re-validates
    inside a select_for_update transaction (ADR-002 §4, §XV-5).
    """
    from discounts.models import DiscountCode, DiscountType

    code_str = code_str.strip().upper()
    try:
        dc = DiscountCode.objects.for_store(cart.store).get(code=code_str)
    except DiscountCode.DoesNotExist:
        raise ValueError(f"Discount code '{code_str}' was not found.")

    if not dc.is_valid_now():
        raise ValueError("This discount code is no longer valid.")

    if dc.discount_type == DiscountType.COUPON:
        cart.discount_code = dc
        cart.save(update_fields=['discount_code', 'updated_at'])
    else:
        # Gift card types (giftcard_manual / giftcard_auto) go to the dedicated field
        cart.gift_card = dc
        cart.save(update_fields=['gift_card', 'updated_at'])

    return dc
