"""
Orders app: Order, OrderItem, OrderFulfillment, OrderCharge, FiredPixel.

Design decisions (spec §4, ADR-002, ADR-004, design-pattern-ideas.txt):
- Two-axis state machine: payment_status and fulfillment_status are explicit enum
  fields — state is NEVER inferred from absence of data.
- idempotency_key is globally unique (not per-store) — prevents double-submit across
  any store for the same session+cart fingerprint.
- processor_account is a lazy string FK ('payments.ProcessorAccount') — the payments
  app does not exist yet; this is intentional.
- Snapshot rule: OrderItem copies product_name, variant_title, sku, unit_price at
  creation time. Historical display MUST NOT join through to the catalog (ADR-002).
- Attribution columns (UTM + referrer + landing_page) capture first-touch session
  data at order creation (ADR-004).
- FiredPixel uses unique_together as an idempotency guard: a pixel event for a given
  order fires at most once.
- customer FK is nullable to support guest checkout — set when a Customer row exists.
- organization FK records the merchant-of-record Organization set at routing time (ADR-006).
- OrderCharge.refunded_amount tracks the cumulative Decimal amount refunded (ADR-007 §7).
  Refund cap = amount_cents/100 − refunded_amount.
- FulfillmentRecordStatus is the per-fulfillment-row status (distinct from the
  order-level FulfillmentStatus which is derived / updated separately).
"""

from decimal import Decimal

from django.db import models

from core.models import StoreOwnedModel


class PaymentStatus(models.TextChoices):
    PENDING = 'pending', 'Pending'
    PAID = 'paid', 'Paid'
    PARTIALLY_REFUNDED = 'partially_refunded', 'Partially Refunded'
    REFUNDED = 'refunded', 'Refunded'
    FAILED = 'failed', 'Failed'
    CANCELLED = 'cancelled', 'Cancelled'


class FulfillmentStatus(models.TextChoices):
    NOT_SENT = 'not_sent', 'Not Sent'
    SENT_TO_FULFILLMENT = 'sent_to_fulfillment', 'Sent to Fulfillment'
    PARTIALLY_SHIPPED = 'partially_shipped', 'Partially Shipped'
    SHIPPED = 'shipped', 'Shipped'


class FulfillmentRecordStatus(models.TextChoices):
    """
    Per-fulfillment-row status (spec §4, T010).

    Distinct from the order-level FulfillmentStatus — this tracks the lifecycle
    of one fulfillment record (a shipment), not the aggregate fulfillment state
    of the whole order.  Partial fulfillment and PARTIALLY_SHIPPED derivation
    depend on line_items_json to determine what was actually shipped.
    """
    PENDING = 'pending', 'Pending'
    PARTIAL = 'partial', 'Partial'
    SHIPPED = 'shipped', 'Shipped'
    DELIVERED = 'delivered', 'Delivered'
    RETURNED = 'returned', 'Returned'


class Order(StoreOwnedModel):
    """
    A customer order.

    Invariants:
    - order_number is unique per store (unique_together with store).
    - idempotency_key is globally unique — set to session_id+cart_fingerprint hash by
      the checkout service before the first INSERT attempt. Re-submitting the same key
      returns the existing order without re-charging (ADR-002).
    - payment_status and fulfillment_status are both required; state is never inferred.
    - shipping_address and billing_address store address snapshots as dicts; keys:
      {name, line1, line2, city, state, postal_code, country}.
    - Monetary fields use 12-digit, 2-decimal-place Decimals to handle large currencies.
    - discount_code FK is nullable: orders without discounts set it to None.
    - processor_account FK is a lazy string ref to 'payments.ProcessorAccount', which
      does not exist yet. Django resolves it at query time once the model is registered.
    - capture_window_expires_at is set for upsell-eligible orders (ADR-007); null for
      standard orders.
    - Attribution columns capture first-touch UTM parameters from the customer's session
      at order creation (ADR-004). Blank when unavailable.
    - customer FK is nullable — guest orders have no Customer row at checkout; one may
      be created post-purchase during account creation.
    - organization FK is the merchant-of-record legal entity set by the routing engine
      (ADR-006). Null for stores without payment routing configured.
    - authorized_at / captured_at are set by the webhook service on payment events.
    - placed_at is the timestamp the customer confirmed the order (may differ from
      created_at for draft/abandoned-cart recovery orders).
    - card_last4 is for display only — never store full card numbers.
    - customer_local_hour (0–23) is the customer's local time-of-day at order placement,
      used for time-of-day analytics segmentation.
    """

    order_number = models.CharField(max_length=50)
    idempotency_key = models.CharField(max_length=255, unique=True)

    payment_status = models.CharField(
        max_length=30,
        choices=PaymentStatus.choices,
        default=PaymentStatus.PENDING,
    )
    fulfillment_status = models.CharField(
        max_length=30,
        choices=FulfillmentStatus.choices,
        default=FulfillmentStatus.NOT_SENT,
    )

    # Customer identity — FK to the Customer row (nullable for guest checkout).
    customer = models.ForeignKey(
        'customers.Customer',
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name='orders',
        help_text='Linked Customer record (null for guest checkout).',
    )

    customer_email = models.EmailField()
    customer_name = models.CharField(max_length=255, blank=True, default='')
    phone = models.CharField(max_length=50, blank=True, default='')

    shipping_address = models.JSONField(default=dict)
    billing_address = models.JSONField(default=dict)

    subtotal = models.DecimalField(max_digits=12, decimal_places=2, default=0)
    discount_amount = models.DecimalField(max_digits=12, decimal_places=2, default=0)
    shipping_amount = models.DecimalField(max_digits=12, decimal_places=2, default=0)
    total = models.DecimalField(max_digits=12, decimal_places=2, default=0)
    currency = models.CharField(max_length=3, default='USD')

    discount_code = models.ForeignKey(
        'discounts.DiscountCode',
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name='orders',
        help_text='Coupon applied to this order (ADR-002 §3 — coupon first).',
    )
    gift_card = models.ForeignKey(
        'discounts.DiscountCode',
        null=True,
        blank=True,
        on_delete=models.PROTECT,
        related_name='order_gift_card_set',
        help_text=(
            'Gift card applied to this order — one per order (DECIDED UF-H, ADR-002 §3). '
            'Applied on the coupon remainder. '
            'PROTECT prevents accidental gift-card deletion when historical orders reference it.'
        ),
    )

    # Merchant-of-record Organization set by the routing engine (ADR-006).
    organization = models.ForeignKey(
        'stores.Organization',
        null=True,
        blank=True,
        on_delete=models.PROTECT,
        related_name='orders',
        help_text='Merchant-of-record Organization for this order (set at routing time, ADR-006).',
    )

    notes = models.TextField(blank=True, default='')

    # Checkout-language snapshot: the buyer's browsing language at order creation.
    # Used to send emails and receipts in the correct language (U16-8, ADR-015 §5).
    checkout_language = models.CharField(
        max_length=10,
        blank=True,
        default='',
        help_text='Buyer browsing language at checkout (U16-8). Used for order emails.',
    )

    # Shipping snapshots — display-only; historical display must not join the rate table
    # (ADR-002 snapshot rule). FK is advisory (analytics/reporting only).
    shipping_method_name = models.CharField(
        max_length=100,
        blank=True,
        default='',
        help_text=(
            'Display name of the selected shipping method, snapshotted at order creation (TH-002). '
            'Never join to the rate table for historical display — use this snapshot instead.'
        ),
    )
    shipping_rate = models.ForeignKey(
        'shipping.ShippingRate',
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name='orders',
        help_text='FK to the selected ShippingRate (advisory — analytics/reporting only). SET_NULL on rate deletion.',
    )

    # Set for upsell-eligible orders; null for standard orders (ADR-007).
    capture_window_expires_at = models.DateTimeField(null=True, blank=True)

    # 20:P1 (DECIDED 2026-07-10): True when no funnel campaign applied at checkout —
    # the confirmation path (webhook / return-view) captures this order's ORIGINAL
    # charge immediately via campaigns.tasks.capture_original_charge_now instead of
    # waiting for the capture_window_watchdog to reach capture_window_expires_at.
    # capture_window_expires_at is still set and unchanged for these orders — it
    # remains the watchdog's reconciliation fallback if the inline capture fails.
    # False (default) for funnel orders and for zero-total orders (which never
    # reach the PaymentIntent step and are paid/captured through a separate path).
    capture_immediately = models.BooleanField(
        default=False,
        help_text=(
            'True when no funnel campaign applied at checkout — the order is '
            'captured immediately on payment confirmation rather than waiting for '
            'the capture-window watchdog (20:P1). capture_window_expires_at remains '
            'set as the watchdog fallback if the inline capture fails.'
        ),
    )

    # Lazy FK — 'payments.ProcessorAccount' does not exist yet.
    processor_account = models.ForeignKey(
        'payments.ProcessorAccount',
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name='orders',
    )

    # Processor identifiers — set at checkout; used for webhook lookups, captures,
    # and refunds (ADR-007 §4 processor pinning, TICKET-018).
    # processor_payment_intent_id: Stripe PaymentIntent ID (or PayPal authorization ID).
    #   Indexed for fast webhook lookup (payment_intent.succeeded / .payment_failed).
    # processor_charge_id: populated after capture; used for refund targeting.
    #   For multi-charge orders (upsell), each charge gets an OrderCharge row (ADR-007 §7);
    #   this field holds the ID of the original authorization capture.
    # processor_customer_id: Stripe cus_… / PayPal payer ID — persisted at checkout for
    #   off-session upsell charges. Blank for guest orders and non-vault PayPal.
    # processor_payment_method_id: Stripe pm_… / PayPal vault token ID — persisted at
    #   checkout authorization for funnel-eligible orders. Required for off-session upsell
    #   charges (ADR-011 Q4). Not a secret (unusable without merchant credentials).
    processor_customer_id = models.CharField(
        max_length=255,
        blank=True,
        default='',
        help_text=(
            'Stripe cus_… / PayPal payer ID set at checkout for off-session upsell charges '
            '(ADR-011 Q4). Blank for guest orders and non-vault processors.'
        ),
    )
    processor_payment_method_id = models.CharField(
        max_length=255,
        blank=True,
        default='',
        help_text=(
            'Stripe pm_… / PayPal vault token ID set at checkout authorization. '
            'Required for off-session upsell charges (ADR-011 Q4). '
            'Not a secret — unusable without merchant credentials. '
            'Excluded from admin list displays and logs.'
        ),
    )
    processor_payment_intent_id = models.CharField(
        max_length=255,
        blank=True,
        default='',
        db_index=True,
        help_text='Stripe PaymentIntent ID (or equivalent) set at checkout.',
    )
    processor_charge_id = models.CharField(
        max_length=255,
        blank=True,
        default='',
        help_text='Charge/transaction ID after capture; used for refund targeting.',
    )

    # Payment timestamps — set by the webhook service on payment lifecycle events.
    authorized_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text='Timestamp when payment was authorized by the processor.',
    )
    captured_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text='Timestamp when payment was captured by the processor.',
    )
    placed_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text=(
            'Timestamp when the customer confirmed the order. May differ from created_at '
            'for draft orders or abandoned-cart recoveries.'
        ),
    )

    # Card display field — last 4 digits only, for receipt / admin display.
    card_last4 = models.CharField(
        max_length=4,
        blank=True,
        default='',
        help_text='Last 4 digits of the card used (display only — never store full PAN).',
    )

    # Time-of-day analytics — the customer's local hour at order placement (0–23).
    customer_local_hour = models.SmallIntegerField(
        null=True,
        blank=True,
        help_text=(
            'Customer local hour (0–23) at order placement, derived from the timezone '
            'header or IP geo. Used for time-of-day analytics segmentation.'
        ),
    )

    # Attribution columns — first-touch UTM session capture (ADR-004).
    utm_source = models.CharField(max_length=255, blank=True, default='')
    utm_medium = models.CharField(max_length=255, blank=True, default='')
    utm_campaign = models.CharField(max_length=255, blank=True, default='')
    utm_term = models.CharField(max_length=255, blank=True, default='')
    utm_content = models.CharField(max_length=255, blank=True, default='')
    first_referrer = models.URLField(max_length=500, blank=True, default='')
    landing_page = models.CharField(max_length=500, blank=True, default='')

    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def __str__(self):
        return f"Order {self.order_number} ({self.customer_email})"

    class Meta:
        verbose_name = 'order'
        verbose_name_plural = 'orders'
        unique_together = [('store', 'order_number')]
        indexes = [
            models.Index(fields=['store', 'payment_status']),
            models.Index(fields=['store', 'created_at']),
            models.Index(fields=['idempotency_key']),
        ]


class OrderItemStatus(models.TextChoices):
    """
    Per-item lifecycle status for upsell provisional items (ADR-011 Q6).

    ACTIVE:          Item is confirmed — included in fulfillment triggers
                     and the confirmation email.
    PENDING_CAPTURE: Item is provisional (upsell charge submitted but not yet
                     confirmed by webhook). Excluded from fulfillment and emails
                     until promoted to ACTIVE by the PAYMENT.CAPTURE.COMPLETED
                     webhook (ADR-007 §6 provisional invisibility).
    VOIDED:          Charge was not captured (window expired, accept not reached).
    FAILED:          Upsell charge declined — item removed from the order.
    """
    ACTIVE = 'active', 'Active'
    PENDING_CAPTURE = 'pending_capture', 'Pending Capture'
    VOIDED = 'voided', 'Voided'
    FAILED = 'failed', 'Failed'


class OrderItem(StoreOwnedModel):
    """
    A line item within an order.

    Invariants:
    - All product/variant fields are SNAPSHOTS copied at order creation.
      Historical display must use these snapshot fields — NEVER join to the catalog.
    - product_variant FK is nullable (SET_NULL) because the original variant may be
      deleted from the catalog after the order was placed.
    - line_total = (unit_price − discount_per_unit) × quantity; computed by the
      checkout service and stored here (not recomputed on read).
    """

    order = models.ForeignKey(
        Order,
        on_delete=models.CASCADE,
        related_name='items',
    )
    product_variant = models.ForeignKey(
        'catalog.ProductVariant',
        null=True,
        on_delete=models.SET_NULL,
        related_name='order_items',
    )

    # Snapshot fields — do not follow FKs for historical display.
    product_name = models.CharField(max_length=512)
    variant_title = models.CharField(max_length=255, default='')
    sku = models.CharField(max_length=255, blank=True, default='')

    quantity = models.PositiveIntegerField(default=1)
    unit_price = models.DecimalField(max_digits=12, decimal_places=2)
    discount_amount = models.DecimalField(max_digits=12, decimal_places=2, default=0)
    line_total = models.DecimalField(max_digits=12, decimal_places=2)

    # TICKET-042 / ADR-028 §1 / §6: snapshot of CartItem.page_version_id at
    # order-creation time — same soft-ID, non-FK invariant as every other
    # OrderItem field (never join to the catalog for historical display).
    # Survives ProductPageVersion deletion; the results report displays
    # "(deleted #id)" for stamps whose version no longer exists.
    page_version_id = models.IntegerField(
        null=True, blank=True,
        help_text=(
            "Soft ID snapshot of CartItem.page_version_id at order creation "
            "(NULL = primary page). Not an FK (ADR-028 §1)."
        ),
    )

    # Upsell provisional-item lifecycle (ADR-011 Q6 / ADR-007 §6).
    # Existing rows default to 'active' — confirmed items are not retroactively affected.
    # PENDING_CAPTURE items are excluded from fulfillment triggers and the confirmation
    # email until promoted to ACTIVE by the charge-captured webhook.
    status = models.CharField(
        max_length=20,
        choices=OrderItemStatus.choices,
        default=OrderItemStatus.ACTIVE,
        help_text=(
            "Per-item lifecycle for upsell provisional items (ADR-011). "
            "ACTIVE = confirmed; PENDING_CAPTURE = provisional (excluded from fulfilment); "
            "VOIDED = window expired without capture; FAILED = upsell charge declined."
        ),
    )

    def __str__(self):
        return f"{self.quantity}x {self.product_name}"

    class Meta:
        verbose_name = 'order item'
        verbose_name_plural = 'order items'
        indexes = [
            models.Index(fields=['order']),
        ]


class OrderFulfillment(StoreOwnedModel):
    """
    A fulfillment record for one or more items in an order.

    One order can produce multiple fulfillment records (e.g. split shipments).
    tracking_number and carrier are blank until the item leaves the warehouse.

    status uses FulfillmentRecordStatus (per-shipment lifecycle, distinct from the
    order-level FulfillmentStatus).

    line_items_json holds a list of {variant_id, quantity_shipped} dicts representing
    which order items — and how many units — this fulfillment covers. Required for
    partial fulfillment accounting and for deriving the order-level PARTIALLY_SHIPPED
    state (T010, spec §4).
    """

    order = models.ForeignKey(
        Order,
        on_delete=models.CASCADE,
        related_name='fulfillments',
    )
    tracking_number = models.CharField(max_length=255, blank=True, default='')
    carrier = models.CharField(max_length=100, blank=True, default='')
    tracking_url = models.URLField(max_length=500, blank=True, default='')
    shipped_at = models.DateTimeField(null=True, blank=True)
    notes = models.CharField(max_length=255, blank=True, default='')

    status = models.CharField(
        max_length=20,
        choices=FulfillmentRecordStatus.choices,
        default=FulfillmentRecordStatus.PENDING,
        help_text='Per-shipment lifecycle status (not the order-level fulfillment_status).',
    )
    line_items_json = models.JSONField(
        default=list,
        help_text=(
            'List of {variant_id, quantity_shipped} dicts identifying which items '
            'and quantities this fulfillment covers. Used for partial fulfillment tracking.'
        ),
    )

    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"Fulfillment {self.tracking_number or 'pending'} for {self.order}"

    class Meta:
        verbose_name = 'order fulfillment'
        verbose_name_plural = 'order fulfillments'


class ChargeType(models.TextChoices):
    ORIGINAL   = 'original',   'Original'
    UPSELL     = 'upsell',     'Upsell'
    ZERO_TOTAL = 'zero_total', 'Zero Total'


class ChargeStatus(models.TextChoices):
    AUTHORIZED = 'authorized', 'Authorized'
    CAPTURE_IN_PROGRESS = 'capture_in_progress', 'Capture In Progress'
    CAPTURED = 'captured', 'Captured'
    FAILED = 'failed', 'Failed'
    REFUNDED = 'refunded', 'Refunded'
    # H2 safety fix: the charge attempt ended in an UNKNOWN state (network
    # failure mid-call — the processor may or may not have charged the card).
    # NOT a failure: FAILED means the processor definitively declined.
    # AMBIGUOUS_OUTCOME rows are reconciled by the capture-window watchdog
    # (campaigns/tasks.py pass 5) via retrieve_charge_by_idempotency_key.
    AMBIGUOUS_OUTCOME = 'ambiguous_outcome', 'Ambiguous Outcome'


class OrderCharge(StoreOwnedModel):
    """
    One processor-level charge on an order (ADR-007 §3 / §7 / ADR-011 Q1, spec §9b).

    Invariants:
    - The original authorization creates one ORIGINAL/AUTHORIZED row at checkout.
    - Each accepted upsell step creates an additional UPSELL row via
      the off-session charge path (T028, ADR-011).
    - Refunds target individual OrderCharge rows: upsell charges first, then the
      original capture (multi-charge refund model, ADR-007 §7).
    - processor_payment_intent_id: the Stripe PaymentIntent (or PayPal auth) ID for
      this charge. Indexed for webhook lookups.
    - processor_charge_id: populated after capture via the webhook; used for refund
      targeting. Blank until captured. Partial unique constraint WHERE non-blank
      prevents a unique collision on empty strings.
    - amount_cents: integer minor-currency units sent to and from the processor
      (mirrors the connector interface which works exclusively in cents).
    - captured_at: set when the charge transitions to CAPTURED status.
    - refunded_amount: cumulative Decimal amount already refunded against this charge.
      Refund cap = (amount_cents / 100) − refunded_amount (ADR-007 §7).
      Stored as Decimal so financial reporting does not round to cents.
    - capture_claimed_at: set when the watchdog claims this charge for capture
      (AUTHORIZED → CAPTURE_IN_PROGRESS). Null when not yet claimed (ADR-011 Q1 / ADR-007 §6b).
    - campaign_step_id: soft reference (no FK — avoids campaigns→orders reverse dep)
      to the campaigns.CampaignStep.pk that produced this UPSELL charge. Null for
      ORIGINAL charges. Partial unique (order, campaign_step_id) WHERE non-null
      is the DB idempotency backstop for "one upsell charge per step per order" (ADR-011 Q1).
    - status=CAPTURE_IN_PROGRESS: the claimed-but-unconfirmed state the watchdog
      reconciles. State is NEVER inferred from absence (§VIII / ADR-011 Q1).
    """

    order = models.ForeignKey(
        Order,
        on_delete=models.CASCADE,
        related_name='charges',
    )
    charge_type = models.CharField(
        max_length=20,
        choices=ChargeType.choices,
        default=ChargeType.ORIGINAL,
    )
    status = models.CharField(
        max_length=30,
        choices=ChargeStatus.choices,
        default=ChargeStatus.AUTHORIZED,
    )
    processor_payment_intent_id = models.CharField(
        max_length=255,
        blank=True,
        default='',
        db_index=True,
        help_text='Stripe PaymentIntent ID (or PayPal authorization ID) for this charge.',
    )
    processor_charge_id = models.CharField(
        max_length=255,
        blank=True,
        default='',
        help_text='Charge/transaction ID after capture; used for refund targeting.',
    )
    amount_cents = models.IntegerField(
        default=0,
        help_text='Amount in smallest currency unit (cents), mirroring the processor interface.',
    )
    captured_at = models.DateTimeField(null=True, blank=True)
    refunded_amount = models.DecimalField(
        max_digits=10,
        decimal_places=2,
        default=Decimal('0.00'),
        help_text=(
            'Cumulative amount already refunded against this charge (ADR-007 §7). '
            'Refund cap = (amount_cents / 100) − refunded_amount.'
        ),
    )
    # Set when the watchdog atomically claims this charge for capture
    # (AUTHORIZED → CAPTURE_IN_PROGRESS). Used by the stuck-capture reconciliation
    # pass to detect charges in-flight for more than 10 minutes (ADR-011 Q5).
    capture_claimed_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text=(
            'Set when the watchdog claims this charge for capture '
            '(AUTHORIZED → CAPTURE_IN_PROGRESS). '
            'Null when not yet claimed. ADR-011 Q1 / ADR-007 §6b.'
        ),
    )
    # Dedup flag for the pass-4 breach alert (ADR-011 Q5).
    # Set to True after the first alert is emitted; suppresses subsequent
    # alerts for the same charge so operators see exactly one breach alert per charge.
    breach_alerted = models.BooleanField(
        default=False,
        help_text=(
            'Set to True after the first uncaptured-breach alert is emitted '
            'for this charge (ADR-011 Q5 pass 4). Prevents repeated alerts '
            'per charge on subsequent watchdog runs.'
        ),
    )
    # Soft reference to campaigns.CampaignStep.pk — no FK to avoid creating a
    # hard orders → campaigns dependency (campaigns → orders is the established
    # direction per ADR-010). Null for ORIGINAL charges.
    campaign_step_id = models.BigIntegerField(
        null=True,
        blank=True,
        help_text=(
            'Soft reference (no FK) to campaigns.CampaignStep.pk. '
            'Set on UPSELL charges; null for ORIGINAL. '
            'Partial unique (order, campaign_step_id) WHERE non-null prevents double charges '
            'per step per order (ADR-011 Q1 idempotency backstop).'
        ),
    )
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return (
            f"OrderCharge {self.charge_type}/{self.status} "
            f"{self.amount_cents}¢ for order {self.order_id}"
        )

    class Meta:
        verbose_name = 'order charge'
        verbose_name_plural = 'order charges'
        constraints = [
            # Partial unique: processor_charge_id must be unique when non-blank.
            # An unconditional unique constraint would collide on '' for pre-capture rows.
            models.UniqueConstraint(
                fields=['processor_charge_id'],
                condition=models.Q(processor_charge_id__gt=''),
                name='ordercharge_unique_processor_charge_id',
            ),
            # Partial unique: one UPSELL charge per (order, step). Idempotency backstop
            # for the accept transaction — if a retry creates a duplicate row, the DB
            # raises IntegrityError and the service recovers the existing row (ADR-011 Q1).
            models.UniqueConstraint(
                fields=['order', 'campaign_step_id'],
                condition=models.Q(campaign_step_id__isnull=False),
                name='ordercharge_unique_order_campaign_step',
            ),
        ]
        indexes = [
            models.Index(fields=['order']),
            models.Index(fields=['processor_payment_intent_id']),
            # Watchdog scans by status across all stores (cross_store_unsafe pattern).
            models.Index(fields=['status'], name='ordercharge_status_idx'),
        ]


class FiredPixel(StoreOwnedModel):
    """
    Idempotency record for pixel/conversion events fired at order time.

    Stored in the main transactional DB (not an analytics store) so that retries and
    page reloads never double-fire pixels for the same order.

    unique_together ('order', 'pixel_type', 'event') is the idempotency guard:
    the checkout service attempts INSERT and catches IntegrityError on duplicates.
    """

    order = models.ForeignKey(
        Order,
        on_delete=models.CASCADE,
        related_name='fired_pixels',
    )
    pixel_type = models.CharField(
        max_length=50,
        help_text="Pixel provider, e.g. 'facebook', 'ga', 'snapchat'.",
    )
    event = models.CharField(
        max_length=100,
        help_text="Conversion event name, e.g. 'purchase', 'add_to_cart'.",
    )
    fired_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"{self.pixel_type}/{self.event} for {self.order}"

    class Meta:
        verbose_name = 'fired pixel'
        verbose_name_plural = 'fired pixels'
        unique_together = [('order', 'pixel_type', 'event')]
