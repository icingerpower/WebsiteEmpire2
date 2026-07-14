"""
Checkout service: converts a Cart into an Order and creates a PaymentIntent.

Public API:
  begin_checkout(cart, email, shipping_address, ...) -> dict

The returned dict contains:
  order_id           int — Django PK of the created Order
  payment_intent_id  str — processor-side intent identifier ('' for zero-total)
  client_secret      str — Stripe client_secret (or PayPal approval URL) for frontend
  processor_type     str — 'stripe' | 'paypal' | 'none' (zero-total) | …
  approval_flow      str — 'sdk' | 'redirect' (ADR-020 D7); absent for zero-total

Raises CheckoutError for all user-visible failure modes.
Raises DuplicateCheckoutError (subclass) when the cart was already submitted.

Design decisions (ADR-002, ADR-006, ADR-007, ADR-015, TICKET-010-CART):
- @transaction.atomic: the entire checkout rolls back atomically on any failure
  (order creation, discount decrement, payment intent, OrderCharge all in one unit).
- Idempotency key (UF-I): SHA-256 of session_key + sorted (variant_id, qty) pairs.
  Pre-check catches most duplicates. A try/except IntegrityError with a nested
  savepoint catches the concurrent double-submit race window (B3 T010-CART fix).
  Both idempotency error paths now raise DuplicateCheckoutError (carries the existing
  order) so the pay view can redirect to the existing thank-you page (EC-007, ADR-015 §7).
- Discount application order (ADR-002 §3 / B3):
  coupon first (applies to subtotal, atomically decrements times_used via apply_coupon),
  free-shipping coupon: zeroes shipping_amount only (OF-006, ADR-015 §5 step 3b),
  gift card second on the remainder (atomic balance decrement via apply_gift_card,
  requires the order reference — called after order save). One gift card per order
  (DECIDED UF-H). Gift card can cover shipping too (UF-009, ADR-015 §5 step 7).
- Shipping rate (ADR-015 §5 step 2b / SM-006 / OF-002):
  shipping_rate_id is re-validated server-side against resolve_shipping_rates for
  the cart's destination country — the client is never trusted for price or eligibility.
  When shipping_rate_id is None (digital goods, tests, zero-total), shipping_amount=0.
- Zero-total guard (ADR-015 §5 step 5c / 16:G2):
  when order.total == 0 after all discounts, skip PaymentIntent creation entirely
  (Stripe rejects amount=0 manual-capture intents). Create a ZERO_TOTAL/CAPTURED
  charge row and set payment_status=PAID immediately. Return processor_type='none'.
- PaymentIntent is created with capture_method='manual' so checkout only authorizes
  (ADR-007 §2 — the capture happens after the post-purchase upsell window, or
  immediately for non-upsell orders — 20:P1, see below).
- capture_window_expires_at: if an active ONE_CLICK_FUNNEL campaign exists for this
  store, uses campaign.capture_window_minutes (ADR-011 Q5 / Fix 4); otherwise falls
  back to connector.max_capture_delay (Stripe 7 days, PayPal 2 days — ADR-020 D8).
  Either way the window is clamped to connector.max_capture_delay so a PayPal order
  is never scheduled for capture past its 3-day honor period. This field is set for
  BOTH funnel and non-funnel orders — for non-funnel orders it now serves purely as
  the capture_window_watchdog reconciliation fallback (20:P1 below), not the primary
  capture trigger.
- capture_immediately (20:P1, DECIDED 2026-07-10): set to True when no funnel
  campaign applies (funnel_campaign is None). The payment-confirmation path
  (storefront.views_checkout._finalize_payment_return; payments.webhook_views's
  Stripe amount_capturable_updated handler; payments.paypal_webhook_views's
  PAYMENT.AUTHORIZATION.CREATED handler) calls
  campaigns.tasks.capture_original_charge_now(order) for these orders instead of
  waiting days for the watchdog. Funnel orders are unaffected — their ORIGINAL
  charge stays AUTHORIZED until upsell accept/decline or watchdog expiry, exactly
  as before. Zero-total orders never reach this code (step 5c returns earlier) and
  keep the field's False default — unaffected.
- OrderCharge row created for every intent, enabling per-charge refund targeting
  (ADR-007 §7) and the multi-charge upsell model (Phase 2).
- Cart.status is set to CONVERTED after the OrderCharge row is saved, not before,
  so a rolled-back transaction leaves the cart in ACTIVE state.
- order.total after gift card application (not the advisory value) is used as the
  PaymentIntent amount — avoids sending a stale total if gift card balance changed
  between cart-apply and checkout.
"""

import hashlib
import logging
import uuid
from datetime import timedelta
from decimal import Decimal

from django.db import IntegrityError, transaction
from django.db.models import F
from django.utils import timezone

logger = logging.getLogger('cart.checkout')


class CheckoutError(Exception):
    """
    Raised when checkout cannot proceed.

    Message is user-visible: keep it actionable and free of internal identifiers.
    """


class DuplicateCheckoutError(CheckoutError):
    """
    Raised when the submitted cart has already been converted to an order.

    Carries the existing Order so the pay view can redirect to its thank-you page
    instead of showing a generic error message (EC-007, ADR-015 §7).

    Usage:
        try:
            result = begin_checkout(...)
        except DuplicateCheckoutError as exc:
            order = exc.existing_order
            # redirect to order thank-you
    """

    def __init__(self, existing_order):
        super().__init__(
            f'This cart has already been submitted (order #{existing_order.order_number}). '
            'Please refresh the page.'
        )
        self.existing_order = existing_order


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _make_idempotency_key(cart_pk: int, items) -> str:
    """
    Build a globally unique idempotency key for this (cart, items) combination.

    Key = SHA-256 of "cart_pk|variant_id,qty:variant_id,qty:…" (sorted by variant_id).
    Identical carts re-submitted produce the same key, preventing double-orders on
    button-mash or network retry (UF-I).

    cart_pk is used instead of session_key (ADR-016) because session_key is rotated
    to "converted-{pk}" at checkout completion. Using cart_pk keeps the idempotency
    key stable across the rotation so duplicate detection continues to work correctly
    even if begin_checkout is called again on the already-converted cart.
    """
    parts = sorted((item.variant_id, item.quantity) for item in items)
    fingerprint = ':'.join(f'{vid},{qty}' for vid, qty in parts)
    raw = f'{cart_pk}|{fingerprint}'
    return hashlib.sha256(raw.encode('utf-8')).hexdigest()


def _generate_order_number() -> str:
    """
    Produce a human-readable, globally unique order number.

    Format: YYYYMMDD-XXXXXXXX  (date + 8 uppercase hex chars from UUID4)
    Fits within Order.order_number max_length=50.
    """
    date_prefix = timezone.now().strftime('%Y%m%d')
    unique_suffix = uuid.uuid4().hex[:8].upper()
    return f'{date_prefix}-{unique_suffix}'


# ---------------------------------------------------------------------------
# Shared discount arithmetic (PY-020/CK-021 — single source of truth)
#
# compute_checkout_totals() is the one place that combines coupon discount +
# shipping zeroing + gift card discount into a final total. begin_checkout uses
# it with the ATOMIC, already-validated discount amounts (from apply_coupon /
# apply_gift_card). The checkout sidebar preview (preview_checkout_totals) uses
# it with read-only, non-locking calculations so the shopper sees a total that
# cannot drift from what begin_checkout will actually charge — the formula
# itself is identical in both call sites, only the inputs differ.
# ---------------------------------------------------------------------------

def compute_checkout_totals(
    subtotal: Decimal,
    coupon_discount: Decimal,
    shipping_amount: Decimal,
    *,
    free_shipping: bool = False,
    gift_card_balance: Decimal | None = None,
) -> dict:
    """
    Pure arithmetic shared by begin_checkout (authoritative charge) and the
    checkout sidebar preview (display-only) — ADR-002 §3 apply order: the coupon
    reduces the subtotal (and zeroes shipping when free_shipping=True), then a
    gift card covers up to `gift_card_balance` of the remainder
    (post-coupon subtotal + shipping, floored at 0).

    coupon_discount must already be computed by the caller (via
    discounts.service.calculate_coupon_discount, directly or through the atomic
    apply_coupon()) — this function only combines the pieces, it does not know
    how to price a coupon.

    gift_card_balance=None means "no gift card in this checkout" — gift_card_discount
    stays 0. Passing a Decimal (even Decimal('0')) means a gift card is present and
    its discount is capped at min(balance, remainder).

    Returns {'shipping_amount', 'gift_card_discount', 'total'} (all Decimal).
    """
    effective_shipping = Decimal('0') if free_shipping else shipping_amount
    effective_remainder = max(subtotal - coupon_discount, Decimal('0')) + effective_shipping

    gift_card_discount = Decimal('0')
    if gift_card_balance is not None and effective_remainder > 0:
        gift_card_discount = min(gift_card_balance, effective_remainder)

    total = max(effective_remainder - gift_card_discount, Decimal('0'))

    return {
        'shipping_amount': effective_shipping,
        'gift_card_discount': gift_card_discount,
        'total': total,
    }


def _classify_cart_discounts(cart):
    """
    Resolve cart.discount_code / cart.gift_card into (coupon_obj, gift_card_obj)
    using the same legacy-compat classification begin_checkout has always used
    (B3 — ADR-002 §3): cart.discount_code is expected to hold a coupon, but a
    gift-card-type code stored there (pre-B3 data) is treated as the gift card.

    Raises CheckoutError if that would mean two gift cards on one order (UF-H) —
    i.e. cart.discount_code resolves to a gift card AND cart.gift_card is also set.
    """
    from discounts.models import DiscountType

    coupon_obj = None
    gift_card_obj = None

    raw_discount_code = cart.discount_code
    raw_gift_card = cart.gift_card

    if raw_discount_code is not None:
        if raw_discount_code.discount_type == DiscountType.COUPON:
            coupon_obj = raw_discount_code
        elif raw_discount_code.discount_type in (DiscountType.GIFT_CARD_MANUAL, DiscountType.GIFT_CARD_AUTO):
            gift_card_obj = raw_discount_code

    if raw_gift_card is not None:
        if gift_card_obj is not None:
            raise CheckoutError(
                'Only one gift card may be applied per order. '
                'Please remove one and try again.'
            )
        gift_card_obj = raw_gift_card

    return coupon_obj, gift_card_obj


def preview_checkout_totals(cart, subtotal: Decimal, shipping_rate=None) -> dict:
    """
    Display-only totals preview for the checkout sidebar (CK-020/CK-021, PY-020).

    Read-only: does NOT validate usage limits or per-email caps, does not lock
    rows, does not increment times_used, does not write a GiftCardTransaction.
    begin_checkout remains the sole authority at charge time (ADR-002 §4) — this
    function exists only so the sidebar does not show a stale, undiscounted total.
    It uses the exact same primitives begin_checkout uses (calculate_coupon_discount,
    DiscountCode.current_balance) combined through compute_checkout_totals(), so
    the two totals cannot silently drift apart.

    Gracefully degrades to "no discount" if the cart's discount FKs are in the
    (should-be-impossible-post-PY-021) two-gift-cards state, rather than raising
    into a page render.

    Returns a dict: subtotal, coupon_code, coupon_amount, gift_card_code,
    gift_card_amount, shipping_amount, total (all Decimal except the *_code strings).
    """
    from discounts.models import ValueType
    from discounts.service import calculate_coupon_discount

    shipping_amount = shipping_rate.price if shipping_rate is not None else Decimal('0')

    try:
        coupon_obj, gift_card_obj = _classify_cart_discounts(cart)
    except CheckoutError:
        coupon_obj, gift_card_obj = None, None

    coupon_code = ''
    coupon_amount = Decimal('0')
    free_shipping = False
    if coupon_obj is not None and coupon_obj.is_valid_now():
        coupon_code = coupon_obj.code
        coupon_amount = calculate_coupon_discount(coupon_obj, subtotal)
        free_shipping = coupon_obj.value_type == ValueType.FREE_SHIPPING

    gift_card_code = ''
    gift_card_balance = None
    if gift_card_obj is not None and gift_card_obj.is_valid_now():
        gift_card_code = gift_card_obj.code
        gift_card_balance = gift_card_obj.current_balance

    totals = compute_checkout_totals(
        subtotal, coupon_amount, shipping_amount,
        free_shipping=free_shipping,
        gift_card_balance=gift_card_balance,
    )

    return {
        'subtotal': subtotal,
        'coupon_code': coupon_code,
        'coupon_amount': coupon_amount,
        'gift_card_code': gift_card_code,
        'gift_card_amount': totals['gift_card_discount'],
        'shipping_amount': totals['shipping_amount'],
        'total': totals['total'],
    }


# ---------------------------------------------------------------------------
# Main checkout entry point
# ---------------------------------------------------------------------------

@transaction.atomic
def begin_checkout(
    cart,
    email: str,
    shipping_address: dict,
    shipping_rate_id: int | None = None,
    method_family: str = 'card',
    phone: str = '',
    billing_address: dict | None = None,
    checkout_language: str = '',
    utm_data: dict | None = None,
    approval_return_url: str = '',
    approval_cancel_url: str = '',
) -> dict:
    """
    Validate cart, create Order + OrderItems, authorize payment, create OrderCharge.

    Parameters
    ----------
    cart              : Cart instance (must belong to an active store).
    email             : Customer email (normalized to lowercase inside get_or_create_customer).
    shipping_address  : Dict with keys {name, line1, line2, city, state, postal_code, country}.
    shipping_rate_id  : PK of the selected ShippingRate. Re-validated server-side against
                        resolve_shipping_rates for the cart's country (OF-002 / SM-006).
                        None = no shipping (digital goods / tests / zero-total coupon path).
    method_family     : Payment method family ('card', 'paypal', …). Passed to preview_route
                        and route_payment (ADR-015 §5).
    phone             : Customer phone number (optional, stored on Order).
    billing_address   : Billing address dict. None → copy of shipping_address (U16-5).
    checkout_language : Language code for order emails (U16-8). '' → caller should use
                        resolve_checkout_language(request) before calling this function.
    utm_data          : Optional dict with first-touch UTM fields (from the customer session).
    approval_return_url / approval_cancel_url (ADR-020 D2): absolute URLs to
                        storefront:checkout-paypal-return / -cancel, supplied by
                        checkout_pay_post via request.build_absolute_uri. Forwarded
                        to connector.create_payment_intent as return_url/cancel_url —
                        Stripe ignores them; PayPal puts them in the order's
                        experience_context so the buyer is redirected back here.
                        '' is safe for both connectors (no experience_context sent).

    Returns
    -------
    dict with:
      order_id, payment_intent_id, client_secret, processor_type, approval_flow.
      processor_type == 'none' and client_secret == '' for zero-total orders
      (no approval_flow key in that case — no connector was ever involved).
      approval_flow (ADR-020 D7) — connector.approval_flow ('sdk' or 'redirect'),
      read by checkout_pay_post to decide between the SDK-confirm path and a
      redirect to the processor-hosted approval URL.

    Raises CheckoutError for all user-facing failure conditions.
    Raises DuplicateCheckoutError when this cart was already converted.
    """
    from catalog.models import InventoryMode, ProductVariant
    from discounts.models import DiscountType, ValueType
    from discounts.service import apply_coupon, apply_gift_card, DiscountError
    from customers.service import get_or_create_customer
    from orders.models import (
        Order,
        OrderItem,
        OrderCharge,
        PaymentStatus,
        FulfillmentStatus,
        ChargeType,
        ChargeStatus,
    )
    from payments.routing import route_payment, preview_route
    from payments.factory import get_connector
    from .models import CartItem, CartStatus

    store = cart.store

    # ------------------------------------------------------------------
    # 1. Load and validate cart items
    # ------------------------------------------------------------------
    items = list(
        CartItem.objects.for_store(store)
        .filter(cart=cart)
        .select_related('variant__product')
    )

    if not items:
        raise CheckoutError('Your cart is empty.')

    for item in items:
        if not item.variant.is_active:
            raise CheckoutError(
                f'"{item.variant.product.title}" is no longer available. '
                'Please remove it from your cart.'
            )

    # Re-validate inventory mode: catches the race where a variant became
    # SOLD_OUT between add_item and checkout.
    for item in items:
        if item.variant.inventory_mode == InventoryMode.SOLD_OUT:
            raise CheckoutError(
                f'"{item.variant.product.title}" is no longer available. '
                'Please remove it from your cart.'
            )

    # FIXED_QTY: lock the variant row, confirm stock, then atomically decrement.
    # select_for_update serializes concurrent checkouts; the update uses F() so
    # no read-modify-write gap exists between the check and the write.
    # All of this is inside the outer @transaction.atomic, so the lock is held
    # until commit and an oversell is impossible.
    for item in items:
        if item.variant.inventory_mode == InventoryMode.FIXED_QTY:
            locked = (
                ProductVariant.objects.for_store(store)
                .select_for_update()
                .get(pk=item.variant_id)
            )
            available = locked.quantity if locked.quantity is not None else 0
            if available < item.quantity:
                raise CheckoutError(
                    f"Only {available} unit(s) of '{item.variant.title}' are available."
                )
            ProductVariant.objects.for_store(store).filter(pk=locked.pk).update(
                quantity=F('quantity') - item.quantity
            )

    # ------------------------------------------------------------------
    # 2. Compute subtotal
    # ------------------------------------------------------------------
    subtotal = sum(item.unit_price * item.quantity for item in items)

    # ------------------------------------------------------------------
    # 2b. Shipping resolution (ADR-015 §5 step 2b / SM-006 / OF-002)
    #
    #     When shipping_rate_id is provided, verify it against the server-resolved
    #     rates for the cart's destination country. The client is never trusted for
    #     price or eligibility — this re-resolution prevents a tampered rate_id from
    #     undercharging shipping.
    #     When shipping_rate_id is None, shipping_amount is 0 (digital goods or
    #     tests; the view is responsible for ensuring a valid rate is passed for
    #     physical-goods orders).
    # ------------------------------------------------------------------
    from shipping.service import resolve_shipping_rates

    shipping_amount = Decimal('0')
    shipping_rate = None
    shipping_method_name = ''

    if shipping_rate_id is not None:
        country_code = shipping_address.get('country', '')
        available_rates = resolve_shipping_rates(store, country_code)
        available_rate_ids = {r.pk for r in available_rates}
        if shipping_rate_id not in available_rate_ids:
            raise CheckoutError(
                'The shipping method is not available for your address. '
                'Please select a different shipping method and try again.'
            )
        # Find the rate object from the already-fetched list (avoid extra query)
        shipping_rate = next(r for r in available_rates if r.pk == shipping_rate_id)
        shipping_amount = shipping_rate.price
        shipping_method_name = shipping_rate.name

    # ------------------------------------------------------------------
    # 3. Classify discount codes (B3 — ADR-002 §3)
    #
    #    cart.discount_code: expected to be a coupon; treated as old-style
    #      gift card if it has a gift-card discount_type (backward compat).
    #      FREE_SHIPPING type: zeroes shipping_amount (OF-006, step 3b).
    #    cart.gift_card: the new dedicated gift card field.
    #    Raise CheckoutError if both resolve to gift cards (AC-043 / UF-H).
    # ------------------------------------------------------------------
    # Classification (coupon vs. gift card, legacy-compat, two-gift-card guard)
    # is shared with the sidebar preview via _classify_cart_discounts (PY-020).
    coupon_obj, gift_card_obj = _classify_cart_discounts(cart)
    coupon_discount = Decimal('0')

    if coupon_obj is not None:
        # Atomic: validate + increment times_used + per-email counter.
        # apply_coupon returns Decimal('0') for value_type=FREE_SHIPPING; usage
        # is still tracked so we call it unconditionally.
        try:
            coupon_discount = apply_coupon(coupon_obj, subtotal, email)
        except DiscountError as exc:
            raise CheckoutError(str(exc)) from exc
        # Free-shipping coupon (OF-006 / ADR-015 §5 step 3b): coupon_discount is
        # already 0 for this type; additionally zero out shipping via the shared
        # formula (compute_checkout_totals) so this matches the sidebar preview.
        shipping_amount = compute_checkout_totals(
            subtotal, coupon_discount, shipping_amount,
            free_shipping=(coupon_obj.value_type == ValueType.FREE_SHIPPING),
        )['shipping_amount']

    # ------------------------------------------------------------------
    # 4. Idempotency guard — prevent double-submit
    #
    #    Pre-check catches the common single-submit case without DB lock overhead.
    #    The unique constraint on Order.idempotency_key catches the concurrent
    #    race window (two requests pass the filter simultaneously). The savepoint
    #    around order.save() lets us catch IntegrityError without aborting the
    #    outer @transaction.atomic block (T010-CART double-submit fix).
    # ------------------------------------------------------------------
    idempotency_key = _make_idempotency_key(cart.pk, items)
    existing = (
        Order.objects.cross_store_unsafe()
        .filter(idempotency_key=idempotency_key)
        .first()
    )
    if existing is not None:
        raise DuplicateCheckoutError(existing)

    # ------------------------------------------------------------------
    # 5. Get or create customer record
    # ------------------------------------------------------------------
    get_or_create_customer(store, email)

    # ------------------------------------------------------------------
    # 5b. Pre-flight routing check (AC-134 server-side rejection)
    #
    #     preview_route is a pure read: no counter increments, no log writes.
    #     Uses the true routed amount: max(subtotal - coupon, 0) + shipping_amount
    #     (ADR-015 §5 — uses method_family instead of hardcoded 'card').
    #     Skipped for zero-total orders (approx_total_cents == 0) to avoid
    #     passing a zero-amount check to the routing engine.
    #     Fails open on routing infrastructure errors so a probe hiccup does
    #     not block checkout (preview_route returns True on exception).
    # ------------------------------------------------------------------
    approx_total_cents = int(
        compute_checkout_totals(subtotal, coupon_discount, shipping_amount)['total'] * 100
    )
    if approx_total_cents > 0:
        if not preview_route(
            store=store,
            country=shipping_address.get('country', ''),
            currency=cart.currency,
            total_cents=approx_total_cents,
            method_family=method_family,
        ):
            raise CheckoutError(
                'Payment is not available for your region. Please contact support.'
            )

    # ------------------------------------------------------------------
    # 6. Create Order
    #
    #    discount_amount and total reflect coupon + shipping at this point.
    #    Gift card discount is added after order.save() (step 7).
    #    New fields (ADR-015 §5): phone, checkout_language, billing_address,
    #    placed_at, customer_name, shipping_amount, shipping_method_name, shipping_rate.
    # ------------------------------------------------------------------
    order_number = _generate_order_number()
    now = timezone.now()
    order = Order(
        store=store,
        order_number=order_number,
        idempotency_key=idempotency_key,
        payment_status=PaymentStatus.PENDING,
        fulfillment_status=FulfillmentStatus.NOT_SENT,
        customer_email=email,
        customer_name=shipping_address.get('name', ''),
        phone=phone,
        shipping_address=shipping_address,
        billing_address=billing_address if billing_address is not None else dict(shipping_address),
        subtotal=subtotal,
        discount_amount=coupon_discount,
        shipping_amount=shipping_amount,
        shipping_method_name=shipping_method_name,
        shipping_rate=shipping_rate,
        total=compute_checkout_totals(subtotal, coupon_discount, shipping_amount)['total'],
        currency=cart.currency,
        discount_code=coupon_obj,
        gift_card=gift_card_obj,
        checkout_language=checkout_language,
        placed_at=now,
    )
    if utm_data:
        for field_name in (
            'utm_source', 'utm_medium', 'utm_campaign',
            'utm_term', 'utm_content',
            'first_referrer', 'landing_page',
        ):
            if field_name in utm_data:
                setattr(order, field_name, utm_data[field_name])

    try:
        with transaction.atomic():  # savepoint — lets us catch IntegrityError
            order.save()
    except IntegrityError:
        # Concurrent double-submit: two requests passed the pre-check above and
        # one of them won the INSERT race.  Re-raise as DuplicateCheckoutError with
        # the existing order so the caller can redirect to the thank-you page (EC-007).
        try:
            dup = Order.objects.cross_store_unsafe().get(
                idempotency_key=idempotency_key
            )
            raise DuplicateCheckoutError(dup) from None
        except Order.DoesNotExist:
            raise CheckoutError(
                'Order submission failed due to a conflict. Please try again.'
            ) from None

    # ------------------------------------------------------------------
    # 7. Apply gift card atomically (requires order reference)
    #
    #    Gift card applies to the full effective remainder: max(subtotal - coupon, 0)
    #    + shipping_amount, so a large gift card can cover shipping too (UF-009,
    #    ADR-015 §5). Atomically locks the gift card row, validates balance, and
    #    writes a GiftCardTransaction. If this raises, the entire @transaction.atomic
    #    block rolls back (order, coupon decrement, inventory decrement all undone).
    # ------------------------------------------------------------------
    gift_card_discount = Decimal('0')
    # Amount gift card can cover: post-coupon subtotal + shipping (shared formula).
    effective_remainder = compute_checkout_totals(subtotal, coupon_discount, shipping_amount)['total']
    if gift_card_obj is not None and effective_remainder > 0:
        try:
            gift_card_discount = apply_gift_card(
                gift_card_obj,
                effective_remainder,
                order=order,
            )
        except DiscountError as exc:
            raise CheckoutError(str(exc)) from exc

        # Update order totals with the actual (locked, atomic) gift card spend.
        order.discount_amount = coupon_discount + gift_card_discount
        order.total = compute_checkout_totals(
            subtotal, coupon_discount, shipping_amount,
            gift_card_balance=gift_card_discount,
        )['total']
        order.save(update_fields=['discount_amount', 'total'])

    # ------------------------------------------------------------------
    # 8. Create OrderItems (price snapshots)
    # ------------------------------------------------------------------
    for item in items:
        v = item.variant
        line_total = item.unit_price * item.quantity
        OrderItem(
            store=store,
            order=order,
            product_variant=v,
            product_name=v.product.title,
            variant_title=v.title,
            sku=v.sku,
            quantity=item.quantity,
            unit_price=item.unit_price,
            line_total=line_total,
            # TICKET-042 / ADR-028 §1 / §6: snapshot the soft page_version_id
            # stamp — survives ProductPageVersion deletion (never join to the
            # catalog for historical display, the OrderItem snapshot invariant).
            page_version_id=item.page_version_id,
        ).save()

    # ------------------------------------------------------------------
    # 5c. Zero-total guard (ADR-015 §5 step 5c / 16:G2)
    #
    #     Placed after gift card application and OrderItems creation so the order
    #     is fully constructed before the early return. Zero-total orders are paid
    #     immediately with a ZERO_TOTAL/CAPTURED charge row; no processor is involved.
    #     Stripe would reject an amount=0 manual-capture intent, so we skip PI
    #     creation entirely. processor_type='none' signals this to the pay view.
    # ------------------------------------------------------------------
    if order.total == Decimal('0'):
        order.payment_status = PaymentStatus.PAID
        order.captured_at = now
        order.save(update_fields=['payment_status', 'captured_at'])

        OrderCharge(
            store=store,
            order=order,
            charge_type=ChargeType.ZERO_TOTAL,
            status=ChargeStatus.CAPTURED,
            amount_cents=0,
            captured_at=now,
        ).save()

        cart.status = CartStatus.CONVERTED
        cart.session_key = f'converted-{cart.pk}'
        cart.save(update_fields=['status', 'session_key', 'updated_at'])

        logger.info(
            "Checkout zero-total: order=%s store=%s",
            order.order_number,
            store.pk,
        )

        return {
            'order_id': order.pk,
            'payment_intent_id': '',
            'client_secret': '',
            'processor_type': 'none',
        }

    # ------------------------------------------------------------------
    # 9. Route payment
    # ------------------------------------------------------------------
    processor_account = route_payment(order)
    if processor_account is None:
        raise CheckoutError(
            'No payment processor is available for this order. Please try again later.'
        )

    order.processor_account = processor_account
    order.save(update_fields=['processor_account'])

    # ------------------------------------------------------------------
    # 9b. Resolve funnel campaign (needed before intent creation for vault setup)
    #
    #     Resolved here so vault eligibility is known before the PaymentIntent
    #     is created.  capture_window_expires_at is computed below after the
    #     intent succeeds (needs now, which isn't meaningful to pin before the
    #     network call completes).
    # ------------------------------------------------------------------
    from campaigns.models import CampaignSession, CampaignType
    from campaigns.service import resolve_campaign_for_store
    from payments.models import ProcessorType

    funnel_campaign = resolve_campaign_for_store(store, CampaignType.ONE_CLICK_FUNNEL)

    # Vault is enabled for Stripe whenever a funnel campaign is active.
    # For PayPal: only when paypal_vault_enabled=True on the account (Vault onboarding required).
    vault_enabled = funnel_campaign is not None
    if vault_enabled and processor_account.processor_type == ProcessorType.PAYPAL:
        vault_enabled = bool(getattr(processor_account, 'paypal_vault_enabled', False))

    # ------------------------------------------------------------------
    # 9c. Create processor-side customer record for vault (when vault-eligible)
    #
    #     Stripe requires a cus_… ID on the PaymentIntent for setup_future_usage
    #     to vault the payment method.  PayPal returns the email as the identifier.
    #     On failure: degrade gracefully (no vault, no funnel upsell) rather than
    #     blocking checkout — the order is still valid.
    # ------------------------------------------------------------------
    connector = get_connector(processor_account)
    processor_customer_id = ''
    if vault_enabled:
        customer_result = connector.create_customer(email=email)
        if customer_result.success:
            processor_customer_id = customer_result.processor_customer_id
        else:
            logger.warning(
                "Checkout: processor customer creation failed for order %s — "
                "vault degraded (no off-session upsell). Error: %s",
                order.pk,
                customer_result.error_message,
            )
            vault_enabled = False

    # ------------------------------------------------------------------
    # 10. Create PaymentIntent (authorize, do not capture)
    #
    #     Use order.total — the post-gift-card amount — not a locally-scoped
    #     advisory value, so the authorized amount is always accurate.
    #     setup_future_usage=vault_enabled opts the intent into off-session
    #     vaulting (ADR-011 Q4) when a funnel campaign is active.
    #     shipping_address (20:P2, DECIDED 2026-07-10): forwarded so PayPal can
    #     carry the checkout-collected address onto its own transaction record
    #     (purchase_units[0].shipping + shipping_preference=SET_PROVIDED_ADDRESS)
    #     instead of the old hardcoded NO_SHIPPING — strengthens PayPal Seller
    #     Protection eligibility for physical goods. Stripe ignores this kwarg.
    # ------------------------------------------------------------------
    total_cents = int(order.total * 100)
    intent_result = connector.create_payment_intent(
        amount=total_cents,
        currency=cart.currency,
        customer_id=processor_customer_id,
        metadata={'order_id': order.pk, 'store_id': store.pk},
        capture_method='manual',  # ADR-007: two-stage capture
        setup_future_usage=vault_enabled,
        return_url=approval_return_url,
        cancel_url=approval_cancel_url,
        shipping_address=shipping_address,
    )
    if not intent_result.success:
        raise CheckoutError(
            f'Payment could not be authorized: {intent_result.error_message}'
        )

    # ------------------------------------------------------------------
    # 11. Compute capture window, pin intent + vault IDs on order
    #
    #     If an active ONE_CLICK_FUNNEL campaign exists for this store, use its
    #     capture_window_minutes (ADR-011 Q5 / Fix 4); otherwise fall back to the
    #     connector's own max_capture_delay (ADR-020 D8 — Stripe 7 days, PayPal
    #     2 days). Either way the window is clamped to max_capture_delay: a funnel
    #     window is always far below both caps (Campaign validator: 1-60 minutes),
    #     but the clamp is what keeps a non-funnel PayPal order's fallback window
    #     inside PayPal's 3-day honor period instead of the old hardcoded 7 days
    #     (a systematic decline/shortfall generator for every such order — §XV-1).
    #     processor_customer_id and processor_payment_method_id are written here
    #     alongside the intent ID to avoid a second save round-trip.
    #     payment_method_id is empty at this point for Stripe (confirm=False — the
    #     pm_xxx is set after the customer confirms via Stripe.js); it is populated
    #     by the payment_intent.succeeded webhook handler (T029).
    # ------------------------------------------------------------------
    # Defensive default for test doubles (bare MagicMock connectors used across
    # this codebase's checkout tests) that don't set max_capture_delay: the ADR's
    # own rationale for the class attribute's 7-day default — "unknown future
    # connectors behave like Stripe" — applies equally to an unconfigured mock.
    # Real connectors (StripeConnector/PayPalConnector) always carry a real
    # timedelta class attribute, so this never masks a real connector's value.
    max_capture_delay = connector.max_capture_delay
    if not isinstance(max_capture_delay, timedelta):
        max_capture_delay = timedelta(days=7)

    if funnel_campaign is not None:
        window = timedelta(minutes=funnel_campaign.capture_window_minutes)
    else:
        window = max_capture_delay
    capture_window_expires_at = now + min(window, max_capture_delay)

    # capture_immediately (20:P1, DECIDED 2026-07-10): no funnel campaign means
    # there is no upsell window to keep open — the confirmation path (webhook /
    # return-view, storefront.views_checkout / payments.webhook_views /
    # payments.paypal_webhook_views) captures this order right away via
    # campaigns.tasks.capture_original_charge_now instead of waiting for
    # capture_window_watchdog to reach capture_window_expires_at below.
    # capture_window_expires_at is still computed and saved unchanged — it
    # remains the watchdog's reconciliation fallback if the inline capture fails.
    order.processor_payment_intent_id = intent_result.payment_intent_id
    order.processor_customer_id = intent_result.customer_id
    order.processor_payment_method_id = intent_result.payment_method_id
    order.capture_window_expires_at = capture_window_expires_at
    order.capture_immediately = funnel_campaign is None
    order.save(update_fields=[
        'processor_payment_intent_id',
        'processor_customer_id',
        'processor_payment_method_id',
        'capture_window_expires_at',
        'capture_immediately',
    ])

    # ------------------------------------------------------------------
    # 12. Create OrderCharge row
    # ------------------------------------------------------------------
    OrderCharge(
        store=store,
        order=order,
        charge_type=ChargeType.ORIGINAL,
        status=ChargeStatus.AUTHORIZED,
        processor_payment_intent_id=intent_result.payment_intent_id,
        amount_cents=total_cents,
    ).save()

    # ------------------------------------------------------------------
    # 12b. Create CampaignSession (funnel path)
    #
    #      Runs inside a savepoint so a session-creation failure (e.g. race on
    #      the UniqueConstraint) does not roll back the outer transaction — the
    #      checkout has already authorized the payment and the order is valid.
    #      A failed session creation degrades to "no funnel"; the order is
    #      captured normally by the watchdog.
    #
    #      No upsell-act token is issued here.  serve_thank_you_step() in
    #      campaigns/service.py issues a fresh token at thank-you render time
    #      (ADR-011 Addendum 2026-07-06 — Option B).  Issuing a token here
    #      was dead code: the Stripe 3DS redirect discards any value embedded
    #      in the response, so the token was never reachable by the widget.
    # ------------------------------------------------------------------
    if funnel_campaign is not None and funnel_campaign.entry_step is not None:
        try:
            with transaction.atomic():  # savepoint — isolates any IntegrityError
                funnel_session = CampaignSession(
                    store=store,
                    order=order,
                    campaign=funnel_campaign,
                    campaign_type=funnel_campaign.campaign_type,
                    current_step=funnel_campaign.entry_step,
                    expires_at=capture_window_expires_at,
                )
                funnel_session.save()
        except Exception:
            logger.warning(
                "Checkout: funnel session creation failed for order %s (non-fatal, "
                "no upsell funnel for this order)",
                order.pk,
                exc_info=True,
            )

    # ------------------------------------------------------------------
    # 13. Mark cart as converted (last step — rolls back cleanly on failure)
    #
    #     session_key is rotated to "converted-{pk}" (ADR-016) so that
    #     get_or_create_cart can create a fresh ACTIVE cart for the same
    #     browser session without violating unique_together(store, session_key).
    #     cart.pk is guaranteed non-None here (cart was retrieved, not just
    #     instantiated). Rotation rolls back with the transaction on failure.
    # ------------------------------------------------------------------
    cart.status = CartStatus.CONVERTED
    cart.session_key = f'converted-{cart.pk}'
    cart.save(update_fields=['status', 'session_key', 'updated_at'])

    logger.info(
        "Checkout complete: order=%s intent=%s store=%s",
        order.order_number,
        intent_result.payment_intent_id,
        store.pk,
    )

    return {
        'order_id': order.pk,
        'payment_intent_id': intent_result.payment_intent_id,
        'client_secret': intent_result.client_secret,
        'processor_type': processor_account.processor_type,
        # ADR-020 D7 — checkout_pay_post branches on this capability, never on
        # processor_type, to route the buyer via SDK confirm or a 302 redirect.
        'approval_flow': connector.approval_flow,
    }
