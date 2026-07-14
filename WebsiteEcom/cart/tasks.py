"""
Celery tasks for the cart app (ADR-015 §3).

void_stale_pending_orders:
  GC beat task — runs every 6 hours (configured in webecom/settings/base.py).
  Finds CheckoutState rows that have not advanced to CONFIRMED within
  CHECKOUT_STALE_HOURS hours and cancels the linked PENDING orders.

  Safe to run concurrently — uses select_for_update() + atomic() per row.
  Idempotent — re-running on an already-voided or already-deleted row is a no-op.
  Runs cross-store via cross_store_unsafe() — this is an internal platform GC
  task, not a store-scoped operation.

  Delegates the full cancellation to cart.service.void_pending_order() which
  handles charge closure, discount restore, CheckoutState cleanup, and
  best-effort Stripe PI void (ADR-015 §3).
"""

import logging
from datetime import timedelta

from celery import shared_task
from django.conf import settings
from django.db import transaction
from django.utils import timezone

from cart.models import CheckoutState, CheckoutStep
from cart.service import void_pending_order
from orders.models import PaymentStatus

logger = logging.getLogger(__name__)


@shared_task(name="cart.tasks.void_stale_pending_orders")
def void_stale_pending_orders():
    """
    GC task: cancel PENDING orders whose CheckoutState has not advanced to
    CONFIRMED within CHECKOUT_STALE_HOURS hours.

    Safe to run concurrently — uses select_for_update() + atomic().
    Idempotent — re-running on an already-voided order is a no-op.

    Runs cross-store (all stores) using cross_store_unsafe() — this is an
    internal platform GC task, not a store-scoped operation.
    """
    stale_hours = getattr(settings, 'CHECKOUT_STALE_HOURS', 24)
    cutoff = timezone.now() - timedelta(hours=stale_hours)

    # Collect the PKs of stale CheckoutState rows outside any transaction.
    # Filters:
    #   - step is not CONFIRMED (confirmed checkouts have paid orders)
    #   - updated_at is older than the stale threshold
    #   - a linked order exists (order FK is set by begin_checkout)
    #   - that order is PENDING and has NOT been authorized (ADR-015 §3: authorized
    #     orders belong to the capture-window watchdog, never to this GC)
    stale_pks = list(
        CheckoutState.objects.cross_store_unsafe()
        .exclude(step=CheckoutStep.CONFIRMED)
        .filter(
            updated_at__lt=cutoff,
            order__isnull=False,
            order__payment_status=PaymentStatus.PENDING,
            order__authorized_at__isnull=True,
        )
        .values_list('pk', flat=True)
    )

    voided = 0
    for pk in stale_pks:
        with transaction.atomic():
            # Lock the CheckoutState row to prevent concurrent GC runs from
            # double-processing the same row.
            try:
                state = (
                    CheckoutState.objects.cross_store_unsafe()
                    .select_for_update()
                    .get(pk=pk)
                )
            except CheckoutState.DoesNotExist:
                # Already deleted by a concurrent run — skip.
                continue

            order = state.order
            if order is None:
                # order FK was cleared (SET_NULL) between the outer query and this lock.
                state.delete()
                continue

            # Delegate the full cancellation to void_pending_order.
            # The service uses a conditional UPDATE (PENDING → CANCELLED) so it is
            # safe even if a concurrent confirmation races past the outer lock.
            # The inner transaction.atomic() inside void_pending_order becomes a
            # savepoint; the Stripe PI void runs after the savepoint commits.
            try:
                if void_pending_order(order):
                    voided += 1
            except Exception:
                logger.exception(
                    "checkout.gc: exception voiding order %d — skipping",
                    order.pk,
                )

    logger.info(
        "checkout.gc.voided_pending: voided %d stale PENDING order(s)",
        voided,
    )
    return voided
