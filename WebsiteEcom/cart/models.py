"""
Cart models: Cart, CartItem, CheckoutStep, CheckoutState.

Design decisions (ADR-001, ADR-002, ADR-015, TICKET-010-CART):
- Both models extend StoreOwnedModel (ADR-001 §4 — store FK on every store-scoped model).
- Cart is keyed by Django session_key (40-char string, one cart per store per session).
- CartItem.unit_price is a snapshot taken at add-to-cart time; price changes in the
  catalog do NOT retroactively update cart items (checkout re-validates stale prices).
- Cart.expires_at enforces the 30-day session lifetime (DECIDED UF-E).
- Cart.status tracks the lifecycle: active → converted (checkout) or abandoned.
- discount_code FK on Cart is advisory (cart-apply is UX-only); the atomic decrement
  happens inside the checkout transaction (ADR-002 §4 / §XV-5).
- unique_together on (store, session_key) for Cart and on (cart, variant) for CartItem
  prevents duplicate cart rows and duplicate line items at the DB layer.
- CheckoutState (ADR-015 §2) holds explicit checkout progress for one cart. One row
  per cart, created lazily on the first checkout POST (never on GET). The step enum
  is the FURTHEST step reached; earlier sections stay editable. The order FK is set
  by begin_checkout() and is the anchor for the failed-payment retry view.
"""

from datetime import timedelta

from django.db import models
from django.utils import timezone

from core.models import StoreOwnedModel


def _default_cart_expires_at():
    """30-day cart session lifetime (DECIDED UF-E)."""
    return timezone.now() + timedelta(days=30)


class CartStatus(models.TextChoices):
    ACTIVE = 'active', 'Active'
    CONVERTED = 'converted', 'Converted'
    ABANDONED = 'abandoned', 'Abandoned'


class Cart(StoreOwnedModel):
    """
    A shopping cart tied to one store session.

    Invariants:
    - session_key is the Django session key (max 40 chars); unique per store.
    - customer_email is populated when the customer enters their email at checkout,
      enabling abandoned-cart recovery (Phase 2).
    - discount_code is set at cart-apply time (advisory); re-validated and atomically
      consumed inside begin_checkout (ADR-002 §4).
    - status transitions: ACTIVE → CONVERTED (on successful checkout) or ABANDONED
      (detected by the abandoned-checkout campaign, Phase 2).
    - expires_at defaults to 30 days from creation (DECIDED UF-E); the abandoned-checkout
      campaign respects this lifetime.
    """

    session_key = models.CharField(max_length=40, db_index=True)
    customer_email = models.EmailField(blank=True, default='')
    discount_code = models.ForeignKey(
        'discounts.DiscountCode',
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name='carts',
        help_text='Coupon applied to this cart. Re-validated atomically at checkout.',
    )
    gift_card = models.ForeignKey(
        'discounts.DiscountCode',
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name='cart_gift_card_set',
        help_text=(
            'Gift card applied to this cart (B3 stacking field). '
            'Applied on the coupon remainder at checkout. '
            'One gift card per order — DECIDED UF-H, ADR-002 §3.'
        ),
    )
    currency = models.CharField(max_length=3, default='USD')
    status = models.CharField(
        max_length=20,
        choices=CartStatus.choices,
        default=CartStatus.ACTIVE,
    )
    expires_at = models.DateTimeField(default=_default_cart_expires_at)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def __str__(self):
        return f"Cart {self.session_key[:8]}… ({self.store})"

    class Meta:
        verbose_name = 'cart'
        verbose_name_plural = 'carts'
        unique_together = [('store', 'session_key')]
        indexes = [
            models.Index(fields=['store', 'status']),
            models.Index(fields=['store', 'updated_at']),
        ]


class CartItem(StoreOwnedModel):
    """
    A line item within a Cart.

    Invariants:
    - unit_price is a snapshot of ProductVariant.price at the time the item was added.
      The checkout service re-validates this price and may reject stale items.
    - unique_together on (cart, variant) prevents a variant appearing twice; callers
      must use add_item() from cart.service (which increments quantity if item exists)
      rather than inserting directly.
    - The store FK on CartItem must match cart.store — enforced by add_item().
    """

    cart = models.ForeignKey(
        Cart,
        on_delete=models.CASCADE,
        related_name='items',
    )
    variant = models.ForeignKey(
        'catalog.ProductVariant',
        on_delete=models.CASCADE,
        related_name='cart_items',
    )
    quantity = models.PositiveIntegerField(default=1)
    unit_price = models.DecimalField(
        max_digits=10,
        decimal_places=2,
        help_text='Snapshot of ProductVariant.price at add-to-cart time.',
    )
    # TICKET-042 / ADR-028 §1 / §6: soft ID (deliberately NOT an FK) of the
    # ProductPageVersion this line's add-to-cart originated from — attribution
    # must survive version deletion (ADR-004 soft-ID rule). NULL means the item
    # was added from the primary product page. cart.service.add_item() sets
    # this only on FIRST creation of the row; when the same SKU variant is
    # added again (quantity increment), the FIRST stamp is kept ("first-touch
    # per line", documented behavior — see add_item()).
    page_version_id = models.IntegerField(
        null=True, blank=True,
        help_text=(
            "Soft ID of the ProductPageVersion this line was added from (NULL = "
            "primary page). Not an FK — survives version deletion (ADR-028 §1)."
        ),
    )
    created_at = models.DateTimeField(auto_now_add=True)

    @property
    def line_total(self):
        """Total price for this line: unit_price × quantity."""
        return self.unit_price * self.quantity

    def __str__(self):
        return f"{self.quantity}× {self.variant} in cart {self.cart_id}"

    class Meta:
        verbose_name = 'cart item'
        verbose_name_plural = 'cart items'
        unique_together = [('cart', 'variant')]
        indexes = [
            models.Index(fields=['store', 'cart']),
        ]


class CheckoutStep(models.TextChoices):
    CONTACT   = 'contact',   'Contact'
    ADDRESS   = 'address',   'Shipping address'
    SHIPPING  = 'shipping',  'Shipping method'
    PAYMENT   = 'payment',   'Payment'
    CONFIRMED = 'confirmed', 'Confirmed'


class CheckoutState(StoreOwnedModel):
    """
    Server-side checkout progress for one cart (ADR-015 §2, spec 16 CO-007, CK-002).

    Invariants:
    - One row per cart (OneToOne). Created lazily on the first checkout POST
      (never on GET — a bot crawling /checkout/ must not create rows).
    - step is the FURTHEST step reached; earlier sections stay editable (CK-002).
      Editing the address clears shipping_rate and regresses step to ADDRESS.
    - order is set by begin_checkout() success and never cleared — it is the
      anchor for the failed-payment retry view (U16-2) after the cart converts.
    - shipping_rate is advisory: begin_checkout re-resolves it server-side inside
      the transaction (OF-002). SET_NULL: a deleted/disabled rate re-opens the
      shipping step, never 500s.
    - initiate_event_fired: EVT_INITIATE_CHECKOUT fires once per checkout session
      (CK-003) — insert-before-fire semantics (§X of design-pattern-ideas.txt).
    - billing_address: only populated when the "use different billing address"
      override is checked (U16-5); empty dict means "same as shipping address".
    """

    cart = models.OneToOneField(
        'cart.Cart',
        on_delete=models.CASCADE,
        related_name='checkout_state',
    )
    step = models.CharField(
        max_length=20,
        choices=CheckoutStep.choices,
        default=CheckoutStep.CONTACT,
    )
    email = models.EmailField(blank=True, default='')
    phone = models.CharField(max_length=50, blank=True, default='')
    newsletter_opt_in = models.BooleanField(default=False)
    # shipping_address matches Order snapshot shape: {name, line1, line2, city, state, postal_code, country}
    shipping_address = models.JSONField(default=dict)
    # billing_address only when override checkbox is checked (U16-5); empty = same as shipping
    billing_address = models.JSONField(default=dict)
    shipping_rate = models.ForeignKey(
        'shipping.ShippingRate',
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name='+',
    )
    order = models.ForeignKey(
        'orders.Order',
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name='+',
    )
    # Language snapshotted from the session at checkout entry (U16-3)
    checkout_language = models.CharField(max_length=10, blank=True, default='')
    initiate_event_fired = models.BooleanField(
        default=False,
        help_text='True after EVT_INITIATE_CHECKOUT has been fired once for this checkout session.',
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def __str__(self):
        return f"CheckoutState cart={self.cart_id} step={self.step}"

    class Meta:
        verbose_name = 'Checkout State'
        verbose_name_plural = 'Checkout States'
