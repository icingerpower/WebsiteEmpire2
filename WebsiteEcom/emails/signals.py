"""
Email signal handlers (TICKET-022).

order_paid_send_confirmation
    Listens to post_save on Order.
    Fires when payment_status transitions TO 'paid'.
    Idempotency guards:
      1. _old_payment_status cached by orders.signals pre_save handler must not
         already be 'paid' (fast transition check).
      2. SentEmail.objects.filter(order=order, template_id='order_confirmation')
         must not exist (belt-and-suspenders: survives signal re-delivery).

    Context sent to the template:
      order — dict with order_number, items (list), totals, currency
      address — dict built from order.shipping_address JSON snapshot
      customer_name — display name for greeting

fulfillment_send_shipped
    Listens to post_save on OrderFulfillment.
    Fires on creation (created=True) when tracking_number is set.
    Idempotency: SentEmail check on (order, 'order_shipped') prevents
    re-sending when the fulfillment is saved a second time (e.g. status update).

    Context includes tracking_number, carrier, tracking_url, and the order
    items from the fulfillment's line_items_json.
"""

import logging

from django.db.models.signals import post_save
from django.dispatch import receiver

from orders.models import Order, OrderFulfillment, PaymentStatus

logger = logging.getLogger(__name__)


@receiver(post_save, sender=Order)
def order_paid_send_confirmation(sender, instance, created, **kwargs):
    """Send order confirmation email on the →PAID payment_status transition."""
    # Guard 1: must be a transition TO paid (not already paid before this save).
    old_status = getattr(instance, '_old_payment_status', None)
    if instance.payment_status != PaymentStatus.PAID:
        return
    if old_status == PaymentStatus.PAID:
        return

    # Guard 2: idempotency — don't re-send if a SentEmail already exists.
    from emails.models import SentEmail

    if SentEmail.objects.cross_store_unsafe().filter(
        order=instance,
        template_id='order_confirmation',
        status='sent',
    ).exists():
        logger.debug(
            "order_paid_send_confirmation: already sent for order %s — skipping",
            instance.pk,
        )
        return

    try:
        _send_order_confirmation(instance)
    except Exception:
        # Signal handlers must not propagate exceptions — they would abort
        # the save transaction and corrupt the order state.
        logger.exception(
            "order_paid_send_confirmation: unexpected error for order %s",
            instance.pk,
        )


@receiver(post_save, sender=OrderFulfillment)
def fulfillment_send_shipped(sender, instance, created, **kwargs):
    """Send shipped email when a fulfillment with tracking is first created."""
    # Only send on creation, not on subsequent status updates.
    if not created:
        return
    # Only send if a tracking number is available.
    if not instance.tracking_number:
        return

    # Idempotency: skip if already sent for this order.
    from emails.models import SentEmail

    order = instance.order
    if SentEmail.objects.cross_store_unsafe().filter(
        order=order,
        template_id='order_shipped',
        status='sent',
    ).exists():
        logger.debug(
            "fulfillment_send_shipped: already sent for order %s — skipping",
            order.pk,
        )
        return

    try:
        _send_order_shipped(instance)
    except Exception:
        logger.exception(
            "fulfillment_send_shipped: unexpected error for fulfillment %s",
            instance.pk,
        )


# ---------------------------------------------------------------------------
# Context builders and senders
# ---------------------------------------------------------------------------

def _build_order_context(order):
    """
    Build the template context for an order.

    All fields are coerced to safe types: Decimal → str (for JSON safety and
    template rendering), None → '' (AC-183).

    OrderItem uses StoreScopedManager — must use cross_store_unsafe() here
    since the signal handler does not have a request-scoped store object.
    """
    from orders.models import OrderItem

    items = []
    for item in OrderItem.objects.cross_store_unsafe().filter(order=order):
        items.append({
            'title': item.product_name or '',
            'variant': item.variant_title or '',
            'sku': item.sku or '',
            'quantity': item.quantity,
            'unit_price': str(item.unit_price),
            'line_total': str(item.line_total),
        })

    addr = order.shipping_address or {}
    address = {
        'name': addr.get('name') or '',
        'line1': addr.get('line1') or '',
        'line2': addr.get('line2') or '',
        'city': addr.get('city') or '',
        'state': addr.get('state') or '',
        'postal_code': addr.get('postal_code') or '',
        'country': addr.get('country') or '',
    }

    return {
        'order': {
            'order_number': order.order_number,
            'items': items,
            'subtotal': str(order.subtotal),
            'discount_amount': str(order.discount_amount),
            'shipping_amount': str(order.shipping_amount),
            'total': str(order.total),
            'currency': order.currency,
        },
        'address': address,
        'customer_name': order.customer_name or order.customer_email,
    }


def _send_order_confirmation(order):
    """Build context and call send_transactional_email for order_confirmation."""
    from emails.service import send_transactional_email

    context = _build_order_context(order)
    send_transactional_email(
        template_id='order_confirmation',
        recipient=order.customer_email,
        context=context,
        store=order.store,
        order=order,
    )


def _send_order_shipped(fulfillment):
    """Build context and call send_transactional_email for order_shipped."""
    from emails.service import send_transactional_email

    order = fulfillment.order
    order_context = _build_order_context(order)

    context = {
        **order_context,
        'tracking': {
            'number': fulfillment.tracking_number or '',
            'carrier': fulfillment.carrier or '',
            'url': fulfillment.tracking_url or '',
        },
    }

    send_transactional_email(
        template_id='order_shipped',
        recipient=order.customer_email,
        context=context,
        store=order.store,
        order=order,
    )


def send_welcome_email(customer, store):
    """
    Send a welcome email to a newly created Customer.

    Called from customers/signals.py or from the customer creation service.
    Not triggered by a post_save signal on Customer to keep coupling minimal —
    callers that know a customer was truly just created invoke this explicitly.
    """
    from emails.service import send_transactional_email

    context = {
        'customer_name': customer.full_name,
        'customer_email': customer.email,
    }
    send_transactional_email(
        template_id='welcome',
        recipient=customer.email,
        context=context,
        store=store,
    )
