"""
Order signal handlers.

update_customer_stats: atomically increments Customer.total_orders and
Customer.total_spent when an Order transitions into PaymentStatus.PAID.

Design notes:
- Uses pre_save to cache the previous payment_status on the instance so
  post_save can detect the exact transition without a second DB query.
- Uses F() expressions in Customer.objects.update() for atomicity — no
  read-modify-write race condition.
- Fires only on the →PAID transition (old_status != PAID) to prevent
  double-counting when a PAID order is saved again for unrelated reasons.
- Also sets is_verified_buyer=True on the first paid order.
"""

from django.db.models.signals import post_save, pre_save
from django.dispatch import receiver

from orders.models import Order, PaymentStatus


@receiver(pre_save, sender=Order)
def _cache_old_payment_status(sender, instance, **kwargs):
    """
    Store the current DB value of payment_status on the instance before save.

    This lets update_customer_stats detect the →PAID transition without a
    second DB query after the save completes.  The attribute is named with a
    leading underscore to signal it is a transient in-memory cache only.
    """
    if instance.pk:
        old = (
            Order.objects.cross_store_unsafe()
            .filter(pk=instance.pk)
            .values_list('payment_status', flat=True)
            .first()
        )
        instance._old_payment_status = old
    else:
        instance._old_payment_status = None


@receiver(post_save, sender=Order)
def update_customer_stats(sender, instance, created, **kwargs):
    """
    Atomically increment Customer.total_orders and total_spent on →PAID transition.

    Guards:
    - customer_id must be set (guest orders have no Customer row to update).
    - instance.payment_status must be PAID.
    - _old_payment_status must not already be PAID (prevents double-counting
      when a PAID order is re-saved for e.g. fulfillment_status changes).
    """
    if not instance.customer_id:
        return
    old_status = getattr(instance, '_old_payment_status', None)
    if instance.payment_status != PaymentStatus.PAID:
        return
    if old_status == PaymentStatus.PAID:
        return

    from django.db.models import F

    from customers.models import Customer

    Customer.objects.cross_store_unsafe().filter(pk=instance.customer_id).update(
        total_orders=F('total_orders') + 1,
        total_spent=F('total_spent') + instance.total,
        is_verified_buyer=True,
    )
