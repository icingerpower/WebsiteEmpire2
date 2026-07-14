"""
Orders domain service functions.

void_pending_order(order) — atomically cancel a PENDING, never-authorized order and
    restore all held resources (inventory, coupon counter, gift card balance).
    Called by:
      - The pay view fast path (ADR-015 §3): before re-submitting a cart after
        a decline, void the prior PENDING order so its resources are not double-held.
      - The GC beat task (void_stale_pending_orders, orders/tasks.py): cleanup of
        orders that were never confirmed within CHECKOUT_PENDING_ORDER_TTL.

Design (ADR-015 §3):
- Before touching the DB at all, the processor is consulted for the PaymentIntent's
  raw status (CHECKOUT_BATCH_2_AUDIT.md MEDIUM-2): if the PI already reports
  'succeeded' or 'processing', money has moved (or is moving) at the processor and
  the void is aborted outright — voiding locally would restore stock/discounts that
  the still-in-flight payment never re-consumes, and the eventual success webhook
  would then flip CANCELLED → PAID over already-released resources. A processor/
  network error is NOT treated as "do not void": an unreachable processor must not
  immortalize a stale order forever, so the void proceeds in that case.
- Conditional UPDATE (§XIII of design-pattern-ideas.txt): the status field is updated
  atomically from PENDING → CANCELLED with a WHERE clause so that a concurrent
  payment confirmation (arriving after the decision to void) wins cleanly.
  rows_updated == 0 means the order was already confirmed or cancelled — skip silently.
- FIXED_QTY inventory is restored via F() update (no read-modify-write gap).
- Coupon and gift card usage counters are restored via the existing
  discounts.service.restore_discount_on_failed_payment function (§XV-4 — one function,
  two callers; no discount-restore logic duplicated here).
- PaymentIntent void at the processor is best-effort and happens OUTSIDE the
  transaction: a failed void is logged but does not re-raise (the auth expires on its
  own within the processor's window; manual reconciliation handles edge cases).
- All AUTHORIZED/CAPTURE_IN_PROGRESS OrderCharge rows are marked FAILED inside the transaction.
- The CheckoutState row is deleted inside the transaction (no CANCELLED step exists).
"""

import logging

from django.db import transaction

logger = logging.getLogger('orders.service')


def void_pending_order(order) -> bool:
    """
    Atomically cancel a PENDING, never-authorized order and restore held resources.

    Parameters
    ----------
    order : Order instance (payment_status must be PENDING and authorized_at IS NULL
            for the void to proceed; otherwise the function is a no-op and returns False).

    Returns
    -------
    True if the order was voided by this call.
    False if the order was already confirmed, cancelled, or claimed by a concurrent void,
    OR if the processor reports the PaymentIntent already succeeded/is processing
    (MEDIUM-2 guard — see module docstring).

    Side effects (only when True is returned):
    - order.payment_status set to CANCELLED in the DB.
    - FIXED_QTY variant quantities restored.
    - Coupon times_used and gift card balance restored.
    - All AUTHORIZED / CAPTURE_IN_PROGRESS OrderCharge rows marked FAILED.
    - CheckoutState row for this order deleted (no CANCELLED step exists).
    - After the transaction: best-effort PaymentIntent void at the processor (logged on failure).
    """
    from catalog.models import InventoryMode
    from discounts.service import restore_discount_on_failed_payment
    from orders.models import Order, OrderCharge, PaymentStatus, ChargeStatus

    from django.db.models import F

    # MEDIUM-2 (CHECKOUT_BATCH_2_AUDIT.md): consult the processor BEFORE any DB
    # mutation. A 'succeeded'/'processing' PI means money has moved (or is
    # moving) at the processor — voiding now would restore stock/discounts that
    # the in-flight payment never re-consumes, racing the success webhook into
    # a CANCELLED → PAID transition over already-released resources. Abort.
    if not _pi_permits_void(order):
        return False

    # Atomic conditional UPDATE: PENDING → CANCELLED (only if still PENDING and never authorized).
    # rows_updated == 0 means a concurrent webhook or prior void already claimed this order.
    with transaction.atomic():
        rows_updated = (
            Order.objects.cross_store_unsafe()
            .filter(pk=order.pk, payment_status=PaymentStatus.PENDING, authorized_at__isnull=True)
            .update(payment_status=PaymentStatus.CANCELLED)
        )
        if rows_updated == 0:
            logger.debug(
                "void_pending_order: order %s already claimed or confirmed — skipping.",
                order.pk,
            )
            return False

        # Re-load inside the transaction so we have consistent state.
        order.refresh_from_db()

        # Restore FIXED_QTY inventory (one UPDATE per variant — no read-modify-write gap).
        from catalog.models import ProductVariant
        for item in order.items.cross_store_unsafe().select_related('product_variant').all():
            variant = item.product_variant
            if variant is None:
                continue
            if variant.inventory_mode == InventoryMode.FIXED_QTY:
                ProductVariant.objects.cross_store_unsafe().filter(pk=variant.pk).update(
                    quantity=F('quantity') + item.quantity
                )

        # Restore coupon times_used and gift card balance (ADR-015 §3 step 3, §XV-4).
        try:
            restore_discount_on_failed_payment(order)
        except Exception:
            # Non-fatal: log and continue. The order is already CANCELLED.
            logger.exception(
                "void_pending_order: restore_discount_on_failed_payment raised for order %s",
                order.pk,
            )

        # Mark any still-open charges FAILED (AUTHORIZED or CAPTURE_IN_PROGRESS,
        # any charge type — upsell charges in-flight also need to be closed).
        OrderCharge.objects.cross_store_unsafe().filter(
            order=order,
            status__in=[ChargeStatus.AUTHORIZED, ChargeStatus.CAPTURE_IN_PROGRESS],
        ).update(status=ChargeStatus.FAILED)

        # Clean up the CheckoutState for this order so the checkout UI does not
        # show a stale step after cancellation.  CheckoutStep has no CANCELLED
        # value, so deletion is the simplest correct action.
        from cart.models import CheckoutState  # local import — avoids a cart→orders circular dep
        CheckoutState.objects.cross_store_unsafe().filter(order=order).delete()

    logger.info(
        "void_pending_order: voided order=%s (checkout.gc.voided_pending)",
        order.pk,
    )

    # Best-effort PI void at the processor (outside the DB transaction — a failure
    # here is logged but does not re-raise; the auth expires on its own).
    _void_processor_intent_best_effort(order)

    return True


def _pi_permits_void(order) -> bool:
    """
    Consult the processor's raw PaymentIntent status before voiding (MEDIUM-2).

    Returns False (abort the void) only when the processor gives a definitive
    answer that money has moved or is moving: raw status 'succeeded' or
    'processing'. In that case the eventual success webhook must be the one to
    finalize the order — voiding now would restore stock/discounts that payment
    never re-consumes.

    Returns True (proceed with the void) when:
    - no PaymentIntent is attached to the order (nothing to check),
    - the PI is in any other/cancelable state, or
    - the processor is unreachable or errors out. A network/connector failure
      must not immortalize a stale order forever — the void proceeds and the PI
      cancel remains best-effort (see _void_processor_intent_best_effort).
    """
    if not order.processor_payment_intent_id or order.processor_account_id is None:
        return True

    try:
        from payments.factory import get_connector
        connector = get_connector(order.processor_account)
        raw_status = connector.retrieve_payment_intent_raw_status(
            order.processor_payment_intent_id
        )
    except Exception:
        logger.exception(
            "void_pending_order: failed to retrieve PI raw status for order %s "
            "(PI=%s) — proceeding with void (unreachable processor must not "
            "immortalize a stale order).",
            order.pk,
            order.processor_payment_intent_id,
        )
        return True

    if raw_status in ('succeeded', 'processing'):
        logger.warning(
            "void_pending_order: order %s PI %s is already '%s' at the processor "
            "— aborting void (money has moved or is moving; the success webhook "
            "will finalize the order).",
            order.pk,
            order.processor_payment_intent_id,
            raw_status,
        )
        return False

    return True


def _void_processor_intent_best_effort(order) -> None:
    """
    Best-effort PaymentIntent void/cancel at the processor (ADR-015 §3 step 4).

    Runs outside the DB transaction. Failure is logged, never raised.
    Only applicable when processor_payment_intent_id is set (non-empty).
    """
    if not order.processor_payment_intent_id:
        return
    if order.processor_account_id is None:
        return

    try:
        from payments.factory import get_connector
        connector = get_connector(order.processor_account)
        connector.void_payment_intent(order.processor_payment_intent_id)
        logger.info(
            "void_pending_order: PI %s cancelled at processor for order %s",
            order.processor_payment_intent_id,
            order.pk,
        )
    except Exception:
        logger.exception(
            "void_pending_order: best-effort PI void failed for order %s (PI=%s) — "
            "auth will expire naturally; manual reconciliation may be needed.",
            order.pk,
            order.processor_payment_intent_id,
        )
