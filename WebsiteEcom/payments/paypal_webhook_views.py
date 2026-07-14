"""
PayPal webhook endpoint (TICKET-019, T020R).

Receives PayPal event notifications, verifies the PayPal signature headers
via the PayPal signature verification API, and dispatches to event-specific
handlers.

Security notes:
- @csrf_exempt is safe here because PayPal webhooks are verified via PayPal's
  signature verification API (using the webhook_secret/webhook_id stored on
  ProcessorAccount), not via Django session cookies.
- The view returns 200 even when no active account is configured — this
  prevents PayPal from marking the endpoint as failed and scheduling retries.
- Returns 400 only on signature verification failure.
- All state-changing handlers use @transaction.atomic + select_for_update()
  to prevent double-processing races (ADR-002 §4).
- H1 account binding: every event is dispatched with the ProcessorAccount
  whose webhook id verified the signature, and every handler checks
  order.processor_account_id == verified_account.pk before mutating.  A
  mismatch (including an order with no pinned account) is logged as a
  security warning and ignored — same policy as the Stripe endpoint
  (see payments/webhook_views.py module docstring for the threat model).

PayPal Orders API v2 event → handler mapping (authorize-then-capture, ADR-007):
  PAYMENT.AUTHORIZATION.CREATED  → _handle_authorization_created
  PAYMENT.AUTHORIZATION.VOIDED   → _handle_authorization_voided  (FAILED + restore)
  PAYMENT.CAPTURE.COMPLETED      → _handle_capture_completed     (PAID + accrue)
  PAYMENT.CAPTURE.DENIED         → _handle_capture_denied        (FAILED + restore)
  PAYMENT.CAPTURE.REVERSED       → _handle_capture_reversed      (REFUNDED)
  CHECKOUT.ORDER.APPROVED        → _handle_order_approved        (authorize —
                                    reconciliation fallback for the return-view
                                    authorize call, ADR-020 D3)

20:P1 (DECIDED 2026-07-10) — immediate capture for non-funnel orders:
_handle_authorization_created triggers campaigns.tasks.capture_original_charge_now
for orders with no funnel campaign (Order.capture_immediately=True) — the async
fallback for a buyer who approves then closes the tab before the return view
runs. Split into an inner @transaction.atomic DB-write half and an outer
capture-trigger half so the outbound PayPal capture call never runs while the
order row lock is held (see the function's own docstring). The
capture_window_watchdog remains the reconciliation fallback if this inline call
fails; capture_window_expires_at is left set and unchanged either way.
"""

import logging
from decimal import Decimal

from django.db import transaction
from django.http import HttpResponse, HttpResponseBadRequest
from django.utils import timezone
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_POST

from .models import ProcessorAccount, ProcessorType
# Shared H1 account-binding check — logs security warnings on the
# 'payments.webhook' logger so all binding violations land in one channel.
from .webhook_views import _order_bound_to_account

logger = logging.getLogger('payments.paypal')


def _enqueue_gift_card_campaigns(order_pk) -> None:
    """
    ADR-029 D4 — thin indirection so the lambda passed to transaction.on_commit
    stays a one-liner and tests can patch this single seam (shared with the
    Stripe handler in webhook_views.py).
    """
    from discounts.tasks import process_gift_card_campaigns
    process_gift_card_campaigns.delay(order_pk)


@csrf_exempt
@require_POST
def paypal_webhook(request):
    """
    PayPal webhook receiver.

    PayPal sends its signature as five separate HTTP headers (unlike Stripe's
    single Stripe-Signature header).  These are collected into a dict and
    passed to PayPalConnector.verify_webhook.

    webhook_secret on ProcessorAccount stores the PayPal Webhook ID (the
    identifier PayPal uses to look up the webhook configuration during
    signature verification — it is not a shared HMAC secret).

    Returns:
    - 200 when no active account is configured (acknowledge to stop retries).
    - 400 on signature verification failure.
    - 200 after successful event dispatch.
    """
    payload = request.body
    # PayPal sends signature fields as individual HTTP headers.
    sig_header = {
        'PAYPAL-TRANSMISSION-ID': request.headers.get('PAYPAL-Transmission-ID', ''),
        'PAYPAL-TRANSMISSION-TIME': request.headers.get('PAYPAL-Transmission-Time', ''),
        'PAYPAL-CERT-URL': request.headers.get('PAYPAL-Cert-URL', ''),
        'PAYPAL-AUTH-ALGO': request.headers.get('PAYPAL-Auth-Algo', ''),
        'PAYPAL-TRANSMISSION-SIG': request.headers.get('PAYPAL-Transmission-Sig', ''),
    }

    accounts = list(
        ProcessorAccount.objects
        .filter(processor_type=ProcessorType.PAYPAL, is_active=True)
        .exclude(webhook_secret='')
        .exclude(webhook_secret__isnull=True)
    )
    if not accounts:
        logger.warning(
            'PayPal webhook received but no active PayPal account with a '
            'webhook_secret (webhook ID) is configured — acknowledging without processing.'
        )
        return HttpResponse(status=200)

    from .factory import get_connector
    event = None
    verified_account = None
    for acct in accounts:
        connector = get_connector(acct)
        result = connector.verify_webhook(payload, sig_header, acct.webhook_secret)
        if result is not None:
            event = result
            verified_account = acct
            break

    if event is None:
        return HttpResponseBadRequest('Invalid signature')

    _dispatch_paypal_event(event, verified_account=verified_account)
    return HttpResponse(status=200)


# ---------------------------------------------------------------------------
# Event dispatcher
# ---------------------------------------------------------------------------

def _dispatch_paypal_event(event: dict, *, verified_account) -> None:
    """
    Dispatch a verified PayPal event dict to the appropriate handler.

    verified_account (H1): the ProcessorAccount whose webhook id verified the
    PayPal signature headers.  Every handler checks the referenced Order
    against this account before acting.  Required keyword — no handler may
    run without a verified account.
    """
    event_type = event.get('event_type', '')
    resource = event.get('resource', {})
    logger.info('PayPal webhook event received: %s', event_type)

    handlers = {
        'PAYMENT.AUTHORIZATION.CREATED': _handle_authorization_created,
        'PAYMENT.AUTHORIZATION.VOIDED': _handle_authorization_voided,
        'PAYMENT.CAPTURE.COMPLETED': _handle_capture_completed,
        'PAYMENT.CAPTURE.DENIED': _handle_capture_denied,
        'PAYMENT.CAPTURE.REVERSED': _handle_capture_reversed,
        'CHECKOUT.ORDER.APPROVED': _handle_order_approved,
    }
    handler = handlers.get(event_type)
    if handler is not None:
        handler(resource, verified_account)
    else:
        logger.debug(
            'PayPal webhook event %s: no handler registered, ignoring', event_type
        )


# ---------------------------------------------------------------------------
# Order ID resolution
# ---------------------------------------------------------------------------

def _resolve_order_id(resource: dict) -> int | None:
    """
    Resolve a Pradize Order PK from a PayPal webhook resource dict.

    PayPal embeds our Order PK as custom_id in the purchase_unit when the
    PayPal order is created (PayPalConnector.create_payment_intent).  PayPal
    copies custom_id to authorization and capture resource objects delivered
    in webhooks.

    CHECKOUT.ORDER.APPROVED (ADR-020 D3) is the one exception: for this event
    the resource IS the PayPal order object itself, not an authorization/
    capture sub-resource — custom_id lives one level down, at
    purchase_units[0].custom_id, never at the top level. Checked as an
    additional (additive) source below; existing resources with a top-level
    custom_id are unaffected.

    Falls back to OrderCharge.processor_charge_id when custom_id is absent
    (e.g. for capture/auth IDs recorded at charge time).

    Returns None when neither lookup succeeds; callers should log and return.
    """
    custom_id = resource.get('custom_id', '')
    if not custom_id:
        purchase_units = resource.get('purchase_units') or []
        if purchase_units:
            custom_id = purchase_units[0].get('custom_id', '')
    if custom_id:
        try:
            return int(custom_id)
        except (ValueError, TypeError):
            logger.warning(
                'PayPal resource custom_id is not a valid int: %r', custom_id
            )

    # Fallback: look up OrderCharge by processor_charge_id (capture/auth resource ID).
    charge_id = resource.get('id', '')
    if charge_id:
        from orders.models import OrderCharge
        order_id = (
            OrderCharge.objects.cross_store_unsafe()
            .filter(processor_charge_id=charge_id)
            .values_list('order_id', flat=True)
            .first()
        )
        if order_id:
            return order_id

    logger.warning(
        'Cannot resolve Pradize Order from PayPal resource: '
        'custom_id=%r, resource_id=%r',
        resource.get('custom_id'),
        resource.get('id'),
    )
    return None


# ---------------------------------------------------------------------------
# Event handlers
# ---------------------------------------------------------------------------

def _handle_authorization_created(resource: dict, verified_account) -> None:
    """
    PAYMENT.AUTHORIZATION.CREATED — PayPal authorization was created.

    The buyer approved the payment; the authorization is held pending capture.
    Sets authorized_at on the Order if not already recorded (delegated to the
    @transaction.atomic inner function below).

    20:P1 (DECIDED 2026-07-10): for orders with no funnel campaign
    (Order.capture_immediately=True), this is the async fallback that triggers
    the immediate capture — the synchronous counterpart is
    storefront.views_checkout._finalize_payment_return (the return-view path via
    payments.paypal_service.ensure_authorized). Deliberately split into an inner
    @transaction.atomic function (DB write, select_for_update) and this outer
    function (network capture call) so the capture's outbound PayPal request
    never runs while the order row lock is held — the same "no select_for_update
    across a network call" constraint _handle_order_approved already documents
    for this module (ADR-020 D3).
    """
    order = _handle_authorization_created_atomic(resource, verified_account)
    if order is not None and order.capture_immediately:
        from campaigns.tasks import capture_original_charge_now
        capture_original_charge_now(order)


@transaction.atomic
def _handle_authorization_created_atomic(resource: dict, verified_account):
    """
    DB-only half of PAYMENT.AUTHORIZATION.CREATED — see _handle_authorization_created.

    Returns the Order on success (whether or not authorized_at was newly set —
    the caller's capture trigger is itself idempotent) so the caller can decide
    whether to fire the 20:P1 immediate-capture call outside this transaction.
    Returns None when there is no order to act on (unresolved order_id, order not
    found, or the H1 account-binding check fails) — the caller must not attempt
    a capture in any of those cases.
    """
    order_id = _resolve_order_id(resource)
    if order_id is None:
        return None

    from orders.models import Order

    order = (
        Order.objects.cross_store_unsafe()
        .select_for_update()
        .filter(pk=order_id)
        .first()
    )
    if order is None:
        logger.warning(
            'No order found for PayPal authorization %s', resource.get('id')
        )
        return None

    if not _order_bound_to_account(
        order, verified_account, 'PAYMENT.AUTHORIZATION.CREATED', resource.get('id', '') or ''
    ):
        return None

    if not order.authorized_at:
        order.authorized_at = timezone.now()
        order.save(update_fields=['authorized_at', 'updated_at'])
        logger.info(
            'Order %s authorized_at set for PayPal authorization %s',
            order.pk, resource.get('id'),
        )
    else:
        logger.debug(
            'Order %s already has authorized_at — skipping for PayPal authorization %s',
            order.pk, resource.get('id'),
        )

    return order


@transaction.atomic
def _handle_authorization_voided(resource: dict, verified_account) -> None:
    """
    PAYMENT.AUTHORIZATION.VOIDED — authorization was voided before capture.

    Transitions Order to FAILED and restores discount counters (ADR-002 §4).
    Only transitions from PENDING — avoids overwriting a PAID status that may
    have been set by a successful capture before this void event was delivered.

    The @transaction.atomic wrapper ensures the status update and discount
    restore (times_used decrement + gift card balance reversal) are atomic.
    """
    order_id = _resolve_order_id(resource)
    if order_id is None:
        return

    from discounts.service import restore_discount_on_failed_payment, restore_stock_on_failed_payment
    from orders.models import Order, PaymentStatus

    order = (
        Order.objects.cross_store_unsafe()
        .select_for_update()
        .filter(pk=order_id)
        .first()
    )
    if order is None:
        logger.warning(
            'No order found for PayPal voided authorization %s', resource.get('id')
        )
        return

    if not _order_bound_to_account(
        order, verified_account, 'PAYMENT.AUTHORIZATION.VOIDED', resource.get('id', '') or ''
    ):
        return

    if order.payment_status != PaymentStatus.PENDING:
        logger.debug(
            'Order %s is already %s — skipping FAILED transition for voided authorization %s',
            order.pk, order.payment_status, resource.get('id'),
        )
        return

    order.payment_status = PaymentStatus.FAILED
    order.save(update_fields=['payment_status', 'updated_at'])
    restore_discount_on_failed_payment(order)
    restore_stock_on_failed_payment(order)
    logger.info(
        'Order %s transitioned to FAILED and discounts restored for voided authorization %s',
        order.pk, resource.get('id'),
    )


@transaction.atomic
def _handle_capture_completed(resource: dict, verified_account) -> None:
    """
    PAYMENT.CAPTURE.COMPLETED — PayPal capture succeeded.

    Transitions Order to PAID, accrues monthly volume on the Organization
    (ADR-006-R §1.1), and fires a server-side purchase event (T012).

    Only transitions once — duplicate event delivery is idempotent: a second
    COMPLETED event for an already-PAID order is silently skipped to prevent
    double-counting volume.
    """
    order_id = _resolve_order_id(resource)
    if order_id is None:
        return

    from orders.models import Order, PaymentStatus

    order = (
        Order.objects.cross_store_unsafe()
        .select_for_update()
        .filter(pk=order_id)
        .first()
    )
    if order is None:
        logger.warning(
            'No order found for PayPal capture %s', resource.get('id')
        )
        return

    if not _order_bound_to_account(
        order, verified_account, 'PAYMENT.CAPTURE.COMPLETED', resource.get('id', '') or ''
    ):
        return

    if order.payment_status == PaymentStatus.PAID:
        logger.debug(
            'Order %s is already PAID — skipping for PayPal capture %s (idempotent)',
            order.pk, resource.get('id'),
        )
        return

    # MEDIUM-2 (CHECKOUT_BATCH_2_AUDIT.md): the order may have been voided (GC
    # task or fast-path void) between capture and this webhook delivery —
    # inventory and discount counters were already restored on the void. A
    # CANCELLED → PAID transition here would accrue revenue without
    # re-consuming those restored resources (oversell / under-counted coupon).
    # Refuse the transition and flag for manual reconciliation instead of
    # silently corrupting inventory/discount state.
    if order.payment_status == PaymentStatus.CANCELLED:
        logger.warning(
            'payments.webhook.cancelled_order_payment: order %s received successful '
            'payment after void — manual reconciliation required (refund the charge '
            'or restore the order)',
            order.pk,
        )
        return

    # W3-1 (WAVE3_AUDIT.md): out-of-order webhook delivery defense. A fast
    # reversal-then-capture delivery (PAYMENT.CAPTURE.REVERSED arrives before
    # PAYMENT.CAPTURE.COMPLETED) must never flip a REFUNDED order back to PAID
    # — same reasoning and posture as the CANCELLED guard above, and mirrors
    # the Stripe handler's refunded_order_payment guard
    # (payments/webhook_views.py::_handle_payment_intent_succeeded).
    if order.payment_status == PaymentStatus.REFUNDED:
        logger.warning(
            'payments.webhook.refunded_order_payment: order %s received successful '
            'payment after refund — manual reconciliation required (out-of-order '
            'webhook delivery: PAYMENT.CAPTURE.REVERSED arrived before '
            'PAYMENT.CAPTURE.COMPLETED)',
            order.pk,
        )
        return

    # H1 step 4 — forged-amount defense: the captured amount and currency must match
    # the Order before the PAID transition.  A partial capture (ops error or future
    # partial-capture path) must never mark the full order PAID.
    # Decimal arithmetic is mandatory — float('83.55') * 100 = 8354.999... → truncates
    # to 8354 instead of 8355, which would cause spurious mismatches after the M1 fix.
    amount_info = resource.get('amount', {})
    amount_value = amount_info.get('value', '0')
    event_currency = amount_info.get('currency_code', '')
    try:
        captured_cents = int(Decimal(str(amount_value)) * 100)
    except Exception:
        logger.warning(
            'payments.webhook.amount_parse_error: PAYMENT.CAPTURE.COMPLETED for order %s '
            'has unparseable amount value=%r — event ignored.',
            order.pk, amount_value,
        )
        return
    expected_cents = int(order.total * 100)
    if captured_cents != expected_cents or event_currency.upper() != (order.currency or '').upper():
        logger.warning(
            'payments.webhook.amount_mismatch: PAYMENT.CAPTURE.COMPLETED for order %s '
            'carries amount=%r currency=%r but the order expects %s %s '
            '(capture_id=%s) — event ignored.',
            order.pk,
            amount_value,
            event_currency,
            expected_cents,
            order.currency,
            resource.get('id', ''),
        )
        return

    order.payment_status = PaymentStatus.PAID
    order.save(update_fields=['payment_status', 'updated_at'])
    logger.info(
        'Order %s transitioned to PAID for PayPal capture %s',
        order.pk, resource.get('id'),
    )

    # Accrue monthly volume on the routing Organization (ADR-006-R §1.1).
    # Only on first PAID transition — duplicate delivery must not double-count.
    if order.organization_id:
        _accrue_paypal_volume(order)

    # Server-side purchase event (T012). Best-effort — analytics must never
    # block or roll back a payment status transition.
    try:
        from analytics.events import record_purchase
        record_purchase(order)
    except Exception:
        logger.warning(
            'analytics.events.record_purchase failed for PayPal order %s (non-fatal)',
            order.pk,
            exc_info=True,
        )

    # Suppress abandoned-checkout sessions (T021 — ADR-010 Q5).
    # Must run after PAID commit, never from a signal. Idempotent.
    # Wrapped in a nested savepoint: a DatabaseError inside suppression
    # would otherwise poison the outer @transaction.atomic and roll back
    # the PAID status transition (Safety M1 / ADR-010 Q5).
    try:
        with transaction.atomic():
            from campaigns.service import suppress_abandoned_checkout
            suppress_abandoned_checkout(order)
    except Exception:
        logger.warning(
            'suppress_abandoned_checkout failed for PayPal order %s (non-fatal)',
            order.pk,
            exc_info=True,
        )

    # Automated gift-card campaigns (ADR-029, TICKET-045 — D4). Enqueued via
    # transaction.on_commit so the task never observes an uncommitted (or
    # rolled-back) PAID row — the same enqueue point as the Stripe handler
    # (payments/webhook_views.py::_handle_payment_intent_succeeded).
    transaction.on_commit(
        lambda order_pk=order.pk: _enqueue_gift_card_campaigns(order_pk)
    )


@transaction.atomic
def _handle_capture_denied(resource: dict, verified_account) -> None:
    """
    PAYMENT.CAPTURE.DENIED — PayPal capture was denied.

    Transitions Order to FAILED and restores discount counters (ADR-002 §4).
    Only transitions from PENDING — avoids overwriting a PAID status that may
    have been set by a successful retry before this denial event was delivered.
    """
    order_id = _resolve_order_id(resource)
    if order_id is None:
        return

    from discounts.service import restore_discount_on_failed_payment, restore_stock_on_failed_payment
    from orders.models import Order, PaymentStatus

    order = (
        Order.objects.cross_store_unsafe()
        .select_for_update()
        .filter(pk=order_id)
        .first()
    )
    if order is None:
        logger.warning(
            'No order found for PayPal denied capture %s', resource.get('id')
        )
        return

    if not _order_bound_to_account(
        order, verified_account, 'PAYMENT.CAPTURE.DENIED', resource.get('id', '') or ''
    ):
        return

    if order.payment_status != PaymentStatus.PENDING:
        logger.debug(
            'Order %s is already %s — skipping FAILED transition for denied capture %s',
            order.pk, order.payment_status, resource.get('id'),
        )
        return

    order.payment_status = PaymentStatus.FAILED
    order.save(update_fields=['payment_status', 'updated_at'])
    restore_discount_on_failed_payment(order)
    restore_stock_on_failed_payment(order)
    logger.info(
        'Order %s transitioned to FAILED and discounts restored for denied capture %s',
        order.pk, resource.get('id'),
    )


@transaction.atomic
def _handle_capture_reversed(resource: dict, verified_account) -> None:
    """
    PAYMENT.CAPTURE.REVERSED — a completed capture was reversed (e.g. chargeback
    or PayPal buyer-protection reversal).

    Transitions Order to REFUNDED.  Only transitions from PAID — a reversal is
    meaningful only after a successful capture.  Full refund tracking (OrderCharge
    refunded_amount, PARTIALLY_REFUNDED) is deferred to a future ticket per
    ADR-007 §7.  Discount counters are NOT restored: a reversal is a post-payment
    event initiated by PayPal, not a pre-capture failure.
    """
    order_id = _resolve_order_id(resource)
    if order_id is None:
        return

    from orders.models import Order, PaymentStatus

    order = (
        Order.objects.cross_store_unsafe()
        .select_for_update()
        .filter(pk=order_id)
        .first()
    )
    if order is None:
        logger.warning(
            'No order found for PayPal reversed capture %s', resource.get('id')
        )
        return

    if not _order_bound_to_account(
        order, verified_account, 'PAYMENT.CAPTURE.REVERSED', resource.get('id', '') or ''
    ):
        return

    if order.payment_status == PaymentStatus.REFUNDED:
        logger.debug(
            'Order %s is already REFUNDED — skipping for PayPal reversed capture %s',
            order.pk, resource.get('id'),
        )
        return

    # W3-1 (WAVE3_AUDIT.md): this docstring already claimed "only transitions
    # from PAID", but no such guard previously existed in code. REFUNDED is
    # only reachable from PAID — an out-of-order or premature reversal event
    # must not flip a PENDING/FAILED/CANCELLED order straight to REFUNDED
    # (which would also open the REFUNDED->PAID gap this ticket closes, since
    # no gift cards would exist yet for the void/flag hook to act on). Refuse
    # and flag for manual reconciliation, mirroring the Stripe
    # charge.refunded handler (payments/webhook_views.py::_handle_charge_refunded).
    if order.payment_status != PaymentStatus.PAID:
        logger.warning(
            'payments.webhook.refund_before_paid: order %s received PAYMENT.CAPTURE.REVERSED '
            'while in status %s (expected PAID) — manual reconciliation required '
            '(capture %s)',
            order.pk, order.payment_status, resource.get('id'),
        )
        return

    order.payment_status = PaymentStatus.REFUNDED
    order.save(update_fields=['payment_status', 'updated_at'])
    logger.info(
        'Order %s transitioned to REFUNDED for PayPal reversed capture %s',
        order.pk, resource.get('id'),
    )

    # Void/flag campaign-issued gift cards (ADR-029 D9, human-approved
    # 2026-07-11). Wrapped in a nested savepoint: a DatabaseError here would
    # otherwise poison the outer @transaction.atomic and roll back the
    # REFUNDED transition (same non-fatal posture as suppress_abandoned_checkout,
    # ADR-010 Q5). PAYMENT.CAPTURE.REVERSED is always a full reversal of the
    # capture — there is no PayPal "partial reversed capture" concept — so no
    # partial-refund gate is needed here (unlike the Stripe charge.refunded
    # handler, which can carry partial amounts).
    try:
        with transaction.atomic():
            from discounts.service import void_or_flag_campaign_gift_cards_for_refund
            void_or_flag_campaign_gift_cards_for_refund(order)
    except Exception:
        logger.warning(
            'void_or_flag_campaign_gift_cards_for_refund failed for PayPal order %s (non-fatal)',
            order.pk,
            exc_info=True,
        )


def _handle_order_approved(resource: dict, verified_account) -> None:
    """
    CHECKOUT.ORDER.APPROVED — buyer approved the PayPal order (ADR-020 D3).

    Reconciliation fallback for the return-view authorize call
    (storefront.views_checkout.checkout_paypal_return): covers the buyer who
    approves on PayPal's site and then closes the tab before returning here.

    Deliberately NOT wrapped in @transaction.atomic / select_for_update, unlike
    the sibling handlers in this module: paypal_service.ensure_authorized makes
    an outbound PayPal API call, and holding a row lock across that network call
    would block every other writer of this order for the round-trip (ADR-020 D3
    guard note — "no select_for_update held across the network call"). There is
    also nothing to protect with a lock: this handler makes no DB write itself,
    and ensure_authorized's own writer (PAYMENT.AUTHORIZATION.CREATED, handled
    elsewhere in this module) already uses the idempotent `WHERE authorized_at
    IS NULL` conditional UPDATE pattern.

    The returned status is intentionally ignored: there is no buyer to redirect
    from a webhook. A 'requires_payment_method' outcome (denied/not-approved) is
    just logged — the existing PAYMENT.AUTHORIZATION.VOIDED handler / GC task
    handle that corpse the same way they always have.
    """
    order_id = _resolve_order_id(resource)
    if order_id is None:
        return

    from orders.models import Order, PaymentStatus

    order = Order.objects.cross_store_unsafe().filter(pk=order_id).first()
    if order is None:
        logger.warning(
            'No order found for PayPal CHECKOUT.ORDER.APPROVED %s', resource.get('id')
        )
        return

    if not _order_bound_to_account(
        order, verified_account, 'CHECKOUT.ORDER.APPROVED', resource.get('id', '') or ''
    ):
        return

    if order.payment_status != PaymentStatus.PENDING or order.authorized_at is not None:
        logger.debug(
            'Order %s is %s (authorized_at=%s) — skipping authorize for '
            'CHECKOUT.ORDER.APPROVED %s',
            order.pk, order.payment_status, order.authorized_at, resource.get('id'),
        )
        return

    from .paypal_service import ensure_authorized
    status = ensure_authorized(order)
    logger.info(
        'CHECKOUT.ORDER.APPROVED: ensure_authorized(order=%s) returned status=%r',
        order.pk, status,
    )


# ---------------------------------------------------------------------------
# Volume accrual
# ---------------------------------------------------------------------------

def _accrue_paypal_volume(order) -> None:
    """
    Delegate monthly volume accrual to the shared helper from webhook_views.

    Reuses _accrue_volume (originally written for Stripe) to avoid duplicating
    the F()-based atomic increment + month-rollover logic (ADR-006-R §1.1).
    """
    from .webhook_views import _accrue_volume
    _accrue_volume(order)
