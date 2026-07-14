"""
Reviews signals — schedule a review-request email on each new OrderFulfillment.

Wired in ReviewsConfig.ready() (reviews/apps.py).
"""

from datetime import timedelta

from django.db.models.signals import post_save
from django.dispatch import receiver
from django.utils import timezone


@receiver(post_save, sender="orders.OrderFulfillment")
def schedule_review_request(sender, instance, created, **kwargs):
    """
    Create a ReviewRequest row when a new OrderFulfillment is saved.

    Uses get_or_create so re-processing an existing fulfillment is idempotent.
    scheduled_at = now + store.review_request_delay_days (Order has no fulfilled_at).
    """
    if not created:
        return

    from reviews.models import ReviewRequest

    order = instance.order
    delay = order.store.review_request_delay_days
    # Use for_store() to satisfy ADR-001 §4; also pass store= explicitly so
    # the create path sets the FK (get_or_create extracts it from kwargs, not
    # from the queryset filter).
    ReviewRequest.objects.for_store(order.store).get_or_create(
        order=order,
        store=order.store,
        defaults={
            "scheduled_at": timezone.now() + timedelta(days=delay),
        },
    )
