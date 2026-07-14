"""
Stripe webhook endpoint (TICKET-018, B2 remediation).

Receives Stripe event notifications, verifies the Stripe-Signature header,
and dispatches to event-specific handlers.

Security notes:
- @csrf_exempt is safe here because Stripe webhooks are verified via HMAC
  signature (webhook_secret from ProcessorAccount), not via Django session.
- The view returns 200 even when no active account is configured — this
  avoids Stripe marking the endpoint as failed and scheduling retries.
- Returns 400 only on signature verification failure (prevents replay attacks).

B2 — Counter-restore on failed/released authorization (ADR-002 §4):
- _handle_payment_intent_failed and _handle_payment_intent_canceled are each
  wrapped in @transaction.atomic. They:
  1. Load the Order with select_for_update (prevents double-restore races).
  2. Transition payment_status only if PENDING (idempotent guard).
  3. Call restore_discount_on_failed_payment() within the same transaction so
     a released authorization restores the counter atomically.

H1 — Account binding (safety fix):
- Every event is dispatched together with the ProcessorAccount whose
  webhook_secret verified the signature (verified_account).
- Every handler that mutates financial state checks that the referenced
  Order (directly, or via the OrderCharge's order) is pinned to that same
  account: order.processor_account_id == verified_account.pk.  An order with
  NO pinned account also fails the check — a webhook must never mutate an
  order it cannot prove ownership of.
- Without this binding, a single leaked webhook secret from ANY store lets an
  attacker forge payment_intent.succeeded (order marked PAID without payment),
  implant attacker-chosen vault ids, or promote UPSELL charges cross-tenant.
- On violation: log a security warning and return WITHOUT acting.  Never raise
  — a 500 would make Stripe/PayPal retry and eventually disable the endpoint.
- Security warning log lines never include the raw payment-intent id or any
  secret — only account pks and a SHA-256 fingerprint of the intent id.
- payment_intent.succeeded additionally validates amount_received and currency
  against the Order before the PAID transition (forged-amount defense).
- Internal placeholder intent ids ("upsell:<order_pk>:<step_pk>", written by the
  upsell service before the real PI id is known) are rejected: a webhook event
  carrying such an id is forged or an internal-state leak, never a Stripe PI id.

20:P1 (DECIDED 2026-07-10) — immediate capture for non-funnel orders:
_handle_payment_intent_authorized (payment_intent.amount_capturable_updated) calls
campaigns.tasks.capture_original_charge_now(order) for orders with no funnel
campaign (Order.capture_immediately=True) — the async fallback for a buyer who
approves/confirms then closes the tab before the return view runs. The
capture_window_watchdog remains the reconciliation fallback if this inline call
fails; capture_window_expires_at is left set and unchanged either way.
"""

import hashlib
import logging

from django.db import transaction
from django.db.models import F
from django.http import HttpResponse, HttpResponseBadRequest
from django.utils import timezone
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_POST

from .models import ProcessorAccount, ProcessorType

logger = logging.getLogger('payments.webhook')


# ---------------------------------------------------------------------------
# H1 — Account-binding helpers (shared with paypal_webhook_views)
# ---------------------------------------------------------------------------

def _reference_fingerprint(reference: str) -> str:
    """
    Safe log fingerprint for a processor reference (PI id / capture id).

    Security warnings must never contain the raw id (it is an attacker-supplied
    value and a lookup key into financial state); the SHA-256 prefix is enough
    to correlate log lines without disclosing the id itself.
    """
    return hashlib.sha256(reference.encode('utf-8')).hexdigest()[:12]


def _order_bound_to_account(order, verified_account, event_label: str, reference: str) -> bool:
    """
    H1 core check: is `order` pinned to the ProcessorAccount that verified the
    webhook signature?

    Returns True when order.processor_account_id == verified_account.pk.
    Returns False (after logging a security warning) in every other case,
    including order.processor_account_id being None: an order without a pinned
    account cannot prove ownership, so a webhook must never mutate it.

    The caller must return without acting on False — never raise (a 500 makes
    the processor retry and eventually disable the endpoint).
    """
    if order.processor_account_id == verified_account.pk:
        return True
    logger.warning(
        'payments.webhook.account_binding_violation: %s references order %s '
        'pinned to processor_account %s, but the signature was verified by '
        'processor_account %s (reference fingerprint %s) — event ignored.',
        event_label,
        order.pk,
        order.processor_account_id,
        verified_account.pk,
        _reference_fingerprint(reference),
    )
    return False


# Internal placeholder prefix written by campaigns/upsell_service.py into
# OrderCharge.processor_payment_intent_id before the real PI id is known.
# A webhook event carrying this prefix is never a real Stripe PI id.
_UPSELL_PLACEHOLDER_PREFIX = 'upsell:'


def _enqueue_gift_card_campaigns(order_pk) -> None:
    """
    ADR-029 D4 — thin indirection so the lambda passed to transaction.on_commit
    stays a one-liner and tests can patch this single seam (shared with the
    PayPal handler in paypal_webhook_views.py).
    """
    from discounts.tasks import process_gift_card_campaigns
    process_gift_card_campaigns.delay(order_pk)


@csrf_exempt
@require_POST
def stripe_webhook(request):
    """
    Stripe webhook receiver.

    Tries each active Stripe ProcessorAccount's webhook_secret in turn until
    one verifies the Stripe-Signature header.  This correctly handles deployments
    with multiple active Stripe accounts (each with its own webhook secret).

    Returns:
    - 200 when no active account with a webhook_secret is configured (acknowledge
      to stop Stripe from retrying the event).
    - 400 when no account's secret verifies the signature.
    - 200 after successful event dispatch.
    """
    payload = request.body
    sig_header = request.headers.get('Stripe-Signature', '')

    accounts = list(
        ProcessorAccount.objects
        .filter(processor_type=ProcessorType.STRIPE, is_active=True)
        .exclude(webhook_secret='')
        .exclude(webhook_secret__isnull=True)
    )
    if not accounts:
        logger.warning(
            'Stripe webhook received but no active Stripe account with a '
            'webhook_secret is configured — acknowledging without processing.'
        )
        return HttpResponse(status=200)

    from .stripe_connector import StripeConnector
    event = None
    verified_account = None
    for acct in accounts:
        connector = StripeConnector(acct)
        event = connector.verify_webhook(payload, sig_header, acct.webhook_secret)
        if event is not None:
            verified_account = acct
            break

    if event is None:
        return HttpResponseBadRequest('Invalid signature')

    _dispatch_event(event, verified_account=verified_account)
    return HttpResponse(status=200)


# ---------------------------------------------------------------------------
# Event dispatcher
# ---------------------------------------------------------------------------

def _dispatch_event(event, *, verified_account) -> None:
    """
    Dispatch a verified Stripe event dict to the appropriate handler.

    verified_account (H1): the ProcessorAccount whose webhook_secret verified
    the Stripe-Signature header.  Every handler that mutates financial state
    checks the referenced Order/OrderCharge against this account before acting.
    Required keyword — no handler may run without a verified account.
    """
    event_type = event.get('type', '')
    logger.info('Stripe webhook event received: %s', event_type)

    handlers = {
        'payment_intent.succeeded': _handle_payment_intent_succeeded,
        'payment_intent.payment_failed': _handle_payment_intent_failed,
        'payment_intent.canceled': _handle_payment_intent_canceled,
        'payment_intent.amount_capturable_updated': _handle_payment_intent_authorized,
        'charge.refunded': _handle_charge_refunded,
    }
    handler = handlers.get(event_type)
    if handler is not None:
        handler(event, verified_account)
    else:
        logger.debug('Stripe webhook event %s: no handler registered, ignoring', event_type)


# ---------------------------------------------------------------------------
# Event handlers — Phase 1 minimal implementations
# ---------------------------------------------------------------------------

@transaction.atomic
def _handle_payment_intent_succeeded(event, verified_account) -> None:
    """
    Mark the matching Order as PAID when Stripe confirms payment_intent.succeeded.

    The Order is looked up by processor_payment_intent_id (indexed).
    cross_store_unsafe() is correct here: webhook events are not scoped to a
    store — the intent ID is globally unique and identifies the order directly.
    Precisely BECAUSE the lookup is cross-store, the H1 account-binding check
    below is mandatory: the order must be pinned to the account that verified
    the signature, or the event is ignored.

    @transaction.atomic + select_for_update() prevent the double-accrual race:
    two concurrent deliveries of the same event both block on the row lock;
    the second delivery sees payment_status == PAID and skips all side-effects.

    Forged-amount defense (H1 step 4): before the PAID transition, the event's
    amount_received and currency must match the Order's total (in cents) and
    currency.  A mismatch is logged as a security warning and ignored.  Note:
    this assumes full capture of the authorized amount — the codebase never
    issues partial captures today; if partial capture is introduced, this
    check must compare against the captured OrderCharge amount instead.

    Volume accrual (ADR-006-R §1.1): after marking the order PAID, increment
    current_month_volume on the routing organization using an F() expression
    (never read-modify-write).  Month rollover is handled by an atomic conditional
    UPDATE: if volume_month != current month, reset volume to this order's total.
    """
    pi = event.get('data', {}).get('object', {})
    pi_id = pi.get('id', '')
    if not pi_id:
        logger.warning('payment_intent.succeeded event missing payment intent ID')
        return

    from django.utils import timezone
    from orders.models import Order, PaymentStatus
    from stores.models import Organization

    try:
        order = (
            Order.objects.cross_store_unsafe()
            .select_for_update()
            .select_related('organization')
            .get(processor_payment_intent_id=pi_id)
        )
    except Order.DoesNotExist:
        # The PI may belong to a UPSELL off-session charge rather than the original
        # checkout authorization (T039 Phase 2).  Try the OrderCharge path.
        logger.debug(
            'No order found for PaymentIntent %s — trying UPSELL OrderCharge lookup',
            pi_id,
        )
        _promote_upsell_charge_captured(pi_id, verified_account, pi=pi)
        return

    # H1: the order must be pinned to the account that verified the signature.
    # A mismatch means the event was signed with ANOTHER store's webhook secret
    # (leaked secret / rogue insider) — ignore without acting.
    if not _order_bound_to_account(order, verified_account, 'payment_intent.succeeded', pi_id):
        return

    # H1 step 4 — forged-amount defense: the event's amount/currency must match
    # the order before ANY financial mutation (vault write-back included).
    expected_cents = int(order.total * 100)
    amount_received = pi.get('amount_received')
    event_currency = (pi.get('currency') or '')
    if amount_received != expected_cents or event_currency.upper() != (order.currency or '').upper():
        logger.warning(
            'payments.webhook.amount_mismatch: payment_intent.succeeded for order %s '
            'carries amount_received=%r currency=%r but the order expects %s %s '
            '(reference fingerprint %s) — event ignored.',
            order.pk,
            amount_received,
            event_currency,
            expected_cents,
            order.currency,
            _reference_fingerprint(pi_id),
        )
        return

    # FR-W2 (T039): persist the vault/payment-method identifiers for upsell eligibility.
    # Write processor_payment_method_id and processor_customer_id only when currently
    # empty — duplicate deliveries and pre-populated fields are no-ops.
    pm_id = pi.get('payment_method', '')
    cus_id = pi.get('customer', '')
    if pm_id and not order.processor_payment_method_id:
        Order.objects.cross_store_unsafe().filter(
            pk=order.pk,
            processor_payment_method_id='',
        ).update(processor_payment_method_id=pm_id)
        logger.info(
            'payment_intent.succeeded: wrote processor_payment_method_id for order %s '
            '(PI %s)',
            order.pk, pi_id,
        )
    if cus_id and not order.processor_customer_id:
        Order.objects.cross_store_unsafe().filter(
            pk=order.pk,
            processor_customer_id='',
        ).update(processor_customer_id=cus_id)
        logger.info(
            'payment_intent.succeeded: wrote processor_customer_id for order %s '
            '(PI %s)',
            order.pk, pi_id,
        )

    # MEDIUM-2 (CHECKOUT_BATCH_2_AUDIT.md): the order may have been voided (GC
    # task or fast-path void) between PI confirmation and this webhook delivery
    # — inventory and discount counters were already restored on the void. A
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
    # refund-then-succeeded delivery (charge.refunded arrives before
    # payment_intent.succeeded) must never flip a REFUNDED order back to PAID
    # — that would accrue volume, fire purchase analytics, and enqueue
    # gift-card issuance for an order that is already refunded (and whose
    # gift-card void/flag hook already ran with nothing to void). Same
    # loud-warning / manual-reconciliation posture as the CANCELLED guard above.
    if order.payment_status == PaymentStatus.REFUNDED:
        logger.warning(
            'payments.webhook.refunded_order_payment: order %s received successful '
            'payment after refund — manual reconciliation required (out-of-order '
            'webhook delivery: charge.refunded arrived before payment_intent.succeeded)',
            order.pk,
        )
        return

    already_paid = order.payment_status == PaymentStatus.PAID
    if not already_paid:
        order.payment_status = PaymentStatus.PAID
        order.save(update_fields=['payment_status', 'updated_at'])
        logger.info('Order %s updated to PAID for PaymentIntent %s', order.pk, pi_id)

        # Accrue monthly volume on the routing organization (ADR-006-R §1.1).
        # Only on first PAID transition — duplicate delivery must not double-count.
        if order.organization_id:
            _accrue_volume(order)

        # Server-side purchase event (T012 — eliminates client-beacon gap for purchase
        # attribution). Only on first PAID transition — duplicate delivery must not
        # double-count revenue. Wrapped in try/except: analytics is best-effort.
        try:
            from analytics.events import record_purchase
            record_purchase(order)
        except Exception:
            logger.warning(
                'analytics.events.record_purchase failed for order %s (non-fatal)',
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
                'suppress_abandoned_checkout failed for order %s (non-fatal)',
                order.pk,
                exc_info=True,
            )

        # Automated gift-card campaigns (ADR-029, TICKET-045 — D4). Enqueued via
        # transaction.on_commit so the task never observes an uncommitted (or
        # rolled-back) PAID row; only on the first PAID transition — duplicate
        # webhook delivery must not double-enqueue (the task is idempotent
        # anyway via CampaignReward, but this avoids the redundant enqueue).
        transaction.on_commit(
            lambda order_pk=order.pk: _enqueue_gift_card_campaigns(order_pk)
        )
    else:
        logger.debug(
            'Order %s already PAID for PaymentIntent %s — skipping status update',
            order.pk, pi_id,
        )


def _accrue_volume(order) -> None:
    """
    Increment current_month_volume on the order's Organization atomically.

    Uses an F() expression to avoid read-modify-write races under concurrent webhooks.
    Month rollover: if volume_month differs from today's month, reset volume to this
    order's total (conditional UPDATE → update 0 rows → fallback reset UPDATE).

    The order.total field is the post-gift-card, post-coupon amount in the order currency.
    Currency conversion to a platform accounting currency is PENDING (Part E ASSUMPTION).
    """
    from stores.models import Organization

    now = timezone.now()
    period_start = now.replace(
        day=1, hour=0, minute=0, second=0, microsecond=0
    ).date()

    # Atomic increment when the month has NOT rolled over.
    updated = Organization.objects.filter(
        pk=order.organization_id,
        volume_month=period_start,
    ).update(current_month_volume=F('current_month_volume') + order.total)

    if not updated:
        # Month rolled over or first order this month: reset volume to this order's total.
        Organization.objects.filter(pk=order.organization_id).update(
            current_month_volume=order.total,
            volume_month=period_start,
        )
        logger.info(
            'Organization %s volume reset for period %s (order %s total=%s)',
            order.organization_id, period_start, order.pk, order.total,
        )


@transaction.atomic
def _handle_payment_intent_failed(event, verified_account) -> None:
    """
    Mark the matching Order as FAILED and restore discount counters when Stripe
    reports payment_intent.payment_failed (B2 — ADR-002 §4).

    Only transitions from PENDING — avoids overwriting a PAID status that may
    have been set by a successful retry before this failure event was delivered.

    H1: the order must be pinned to verified_account, or the event is ignored
    (a forged failure event would otherwise cancel a victim store's pending
    order and restore its discount counters).

    The @transaction.atomic wrapper ensures the status update and the discount
    restore (times_used decrement + gift card balance reversal) happen atomically:
    if restore fails, the FAILED status is not persisted, and Stripe will retry.
    """
    pi = event.get('data', {}).get('object', {})
    pi_id = pi.get('id', '')
    if not pi_id:
        logger.warning('payment_intent.payment_failed event missing payment intent ID')
        return

    from discounts.service import restore_discount_on_failed_payment, restore_stock_on_failed_payment
    from orders.models import Order, PaymentStatus

    try:
        order = (
            Order.objects.cross_store_unsafe()
            .select_for_update()
            .get(processor_payment_intent_id=pi_id)
        )
    except Order.DoesNotExist:
        logger.debug(
            'No order found for PaymentIntent %s on payment_intent.payment_failed — '
            'ignoring (may have been created by a different webhook endpoint)',
            pi_id,
        )
        return

    if not _order_bound_to_account(order, verified_account, 'payment_intent.payment_failed', pi_id):
        return

    if order.payment_status != PaymentStatus.PENDING:
        logger.debug(
            'Order %s is already %s — skipping FAILED transition for PaymentIntent %s '
            '(already PAID or already FAILED, no action needed)',
            order.pk, order.payment_status, pi_id,
        )
        return

    order.payment_status = PaymentStatus.FAILED
    order.save(update_fields=['payment_status', 'updated_at'])

    restore_discount_on_failed_payment(order)
    restore_stock_on_failed_payment(order)

    logger.info(
        'Order %s transitioned to FAILED and discounts restored for PaymentIntent %s',
        order.pk, pi_id,
    )


@transaction.atomic
def _handle_payment_intent_canceled(event, verified_account) -> None:
    """
    Mark the matching Order as CANCELLED and restore discount counters when Stripe
    reports payment_intent.canceled (B2 — ADR-002 §4, released authorization).

    A canceled PaymentIntent means the authorization was released without capture
    (e.g. the customer abandoned the checkout, or the upsell capture window expired
    and the order was auto-cancelled). The coupon usage counter and gift card balance
    that were committed at checkout must be restored in the same transaction.

    Only transitions from PENDING — avoids overwriting a PAID order.
    H1: the order must be pinned to verified_account, or the event is ignored.
    """
    pi = event.get('data', {}).get('object', {})
    pi_id = pi.get('id', '')
    if not pi_id:
        logger.warning('payment_intent.canceled event missing payment intent ID')
        return

    from discounts.service import restore_discount_on_failed_payment, restore_stock_on_failed_payment
    from orders.models import Order, PaymentStatus

    try:
        order = (
            Order.objects.cross_store_unsafe()
            .select_for_update()
            .get(processor_payment_intent_id=pi_id)
        )
    except Order.DoesNotExist:
        logger.debug(
            'No order found for PaymentIntent %s on payment_intent.canceled — ignoring',
            pi_id,
        )
        return

    if not _order_bound_to_account(order, verified_account, 'payment_intent.canceled', pi_id):
        return

    if order.payment_status != PaymentStatus.PENDING:
        logger.debug(
            'Order %s is already %s — skipping CANCELLED transition for PaymentIntent %s',
            order.pk, order.payment_status, pi_id,
        )
        return

    order.payment_status = PaymentStatus.CANCELLED
    order.save(update_fields=['payment_status', 'updated_at'])

    restore_discount_on_failed_payment(order)
    restore_stock_on_failed_payment(order)

    logger.info(
        'Order %s transitioned to CANCELLED and discounts restored for PaymentIntent %s',
        order.pk, pi_id,
    )


def _handle_payment_intent_authorized(event, verified_account) -> None:
    """
    Backfill Order.processor_payment_method_id when payment_intent.amount_capturable_updated
    fires (Stripe authorization succeeded, T039 Phase 2).

    This event carries the payment_method ID that was used at confirmation time.
    If the order was created before the customer confirmed (confirm=False, the standard
    checkout flow), processor_payment_method_id is blank at creation time and must be
    backfilled from this event so the upsell accept flow can issue off-session charges.

    H1: the order must be pinned to verified_account, or the event is ignored —
    a forged event here would implant an attacker-chosen pm_ id used later for
    off-session upsell charges.

    No transaction needed: the conditional UPDATE is a single atomic statement.
    If the order is not found, this PI belongs to a non-checkout flow and is silently
    ignored (warning-level, not error-level, since the PI may be from a direct SDK call).

    20:P1 (DECIDED 2026-07-10): this event fires the moment Stripe confirms the
    manual-capture PaymentIntent is authorized — i.e. exactly the payment-confirmation
    moment for a buyer who never returns to the browser tab (the synchronous
    counterpart is storefront.views_checkout._finalize_payment_return). For orders
    with no funnel campaign (order.capture_immediately=True), this is the async
    fallback that triggers the immediate capture via
    campaigns.tasks.capture_original_charge_now — idempotent against a concurrent
    capture already triggered by the return-view path.
    """
    pi = event.get('data', {}).get('object', {})
    payment_intent_id = pi.get('id', '')
    payment_method_id = pi.get('payment_method', '')

    if not payment_intent_id or not payment_method_id:
        logger.warning(
            'payment_intent.amount_capturable_updated: missing id or payment_method '
            '(pi_id=%r pm_id=%r) — ignoring',
            payment_intent_id,
            payment_method_id,
        )
        return

    from orders.models import Order

    # Conditional UPDATE: only backfills when processor_payment_method_id is blank.
    # Idempotent: two concurrent deliveries of the same event both attempt the same
    # UPDATE; the second finds 0 matching rows (already set) and is a no-op.
    try:
        order = (
            Order.objects.cross_store_unsafe()
            .get(processor_payment_intent_id=payment_intent_id)
        )
    except Order.DoesNotExist:
        logger.warning(
            'payment_intent.amount_capturable_updated: no order for PI %s '
            '(PI may belong to a non-checkout flow — ignoring)',
            payment_intent_id,
        )
        return

    if not _order_bound_to_account(
        order, verified_account, 'payment_intent.amount_capturable_updated', payment_intent_id
    ):
        return

    if not order.processor_payment_method_id:
        Order.objects.cross_store_unsafe().filter(
            pk=order.pk,
            processor_payment_method_id='',
        ).update(processor_payment_method_id=payment_method_id)
        logger.info(
            'payment_intent.amount_capturable_updated: backfilled processor_payment_method_id '
            'for order %s (PI %s)',
            order.pk,
            payment_intent_id,
        )
    else:
        logger.debug(
            'payment_intent.amount_capturable_updated: order %s already has '
            'processor_payment_method_id — no backfill needed (PI %s)',
            order.pk,
            payment_intent_id,
        )

    # 20:P1 — async fallback capture trigger. No lock is held here (no
    # select_for_update above), so it is safe to make the connector call inline.
    if order.capture_immediately:
        from campaigns.tasks import capture_original_charge_now
        capture_original_charge_now(order)


def _promote_upsell_charge_captured(pi_id: str, verified_account, pi: dict | None = None) -> None:
    """
    Promote a UPSELL OrderCharge to CAPTURED when payment_intent.succeeded fires for it.

    Called from _handle_payment_intent_succeeded when no Order row owns the PI ID —
    that means the PI belongs to an off-session UPSELL charge rather than the original
    checkout authorization.

    H1 protections:
    - Placeholder rejection: the upsell service seeds
      OrderCharge.processor_payment_intent_id with "upsell:<order_pk>:<step_pk>"
      before the real PI id is known.  A webhook event carrying such an id is
      forged (or an internal-state leak) — real Stripe PI ids never look like
      this — and is rejected before any lookup.  Without this, an attacker who
      can guess order/step pks would promote a never-charged UPSELL row.
    - Account binding: the charge's order must be pinned to verified_account,
      or the event is ignored.
    - charge_type guard: only UPSELL charges are promoted here.  An ORIGINAL
      charge sharing the same PI id (should not happen but defensive) is ignored.
    - Status whitelist: only AUTHORIZED, CAPTURE_IN_PROGRESS, or AMBIGUOUS_OUTCOME
      charges are promoted.  FAILED and REFUNDED charges are NOT promoted back to
      CAPTURED (FR-W1 guard).

    Session finalization (CONVERTED / DISMISSED) is owned exclusively by
    upsell_service.accept_upsell (Phase C) and the capture-window watchdog
    (campaigns/tasks.py).  This function does NOT advance the CampaignSession —
    advancing it here would break multi-step funnels: a step-1 webhook arriving
    while the shopper is viewing step 2 would force the session to CONVERTED and
    make the step-2 accept return 410 (§XV-4).

    FR-W1 (T039): writes processor_charge_id from the event's latest_charge field
    when available and when the charge row does not already have one.

    If no OrderCharge matches: the PI is from an unrelated flow (direct SDK,
    non-upsell payment). Log a warning (not an error) and return.
    """
    from orders.models import ChargeStatus, ChargeType, OrderCharge

    if pi_id.startswith(_UPSELL_PLACEHOLDER_PREFIX):
        logger.warning(
            'payments.webhook.placeholder_intent_rejected: payment_intent.succeeded '
            'carries an internal upsell placeholder id instead of a real PI id '
            '(reference fingerprint %s, verified account %s) — event ignored.',
            _reference_fingerprint(pi_id),
            verified_account.pk,
        )
        return

    try:
        charge = (
            OrderCharge.objects.cross_store_unsafe()
            .select_for_update()
            .select_related('order')
            .get(processor_payment_intent_id=pi_id, charge_type=ChargeType.UPSELL)
        )
    except OrderCharge.DoesNotExist:
        logger.warning(
            'payment_intent.succeeded: no Order or UPSELL OrderCharge found for PI %s '
            '— PI may belong to a non-upsell flow, ignoring',
            pi_id,
        )
        return

    if not _order_bound_to_account(
        charge.order, verified_account, 'payment_intent.succeeded (UPSELL charge)', pi_id
    ):
        return

    now = timezone.now()

    if charge.status != ChargeStatus.CAPTURED:
        # Build update kwargs; write processor_charge_id when the event supplies
        # latest_charge and the row does not already have one (FR-W1 / T039).
        update_kwargs = {
            'status': ChargeStatus.CAPTURED,
            'captured_at': now,
        }
        if pi is not None:
            latest_charge_id = pi.get('latest_charge', '')
            if isinstance(latest_charge_id, str) and latest_charge_id and not charge.processor_charge_id:
                update_kwargs['processor_charge_id'] = latest_charge_id

        # Whitelist-based conditional UPDATE: only promote pre-capture states.
        # FAILED and REFUNDED charges must not be promoted back to CAPTURED.
        # Idempotent: two concurrent events each try to update only matching rows;
        # exactly one succeeds when the first delivery wins the status race.
        updated = (
            OrderCharge.objects.cross_store_unsafe()
            .filter(
                pk=charge.pk,
                status__in=[
                    ChargeStatus.AUTHORIZED,
                    ChargeStatus.CAPTURE_IN_PROGRESS,
                    ChargeStatus.AMBIGUOUS_OUTCOME,
                ],
            )
            .update(**update_kwargs)
        )
        if updated:
            logger.info(
                'payment_intent.succeeded: UPSELL OrderCharge %s promoted to CAPTURED '
                '(order=%s pi=%s)',
                charge.pk,
                charge.order_id,
                pi_id,
            )
        else:
            logger.debug(
                'payment_intent.succeeded: UPSELL OrderCharge %s not promoted — '
                'status %s is not in the pre-capture whitelist (order=%s pi=%s)',
                charge.pk,
                charge.status,
                charge.order_id,
                pi_id,
            )
    else:
        logger.debug(
            'payment_intent.succeeded: UPSELL OrderCharge %s already CAPTURED — no-op '
            '(order=%s pi=%s)',
            charge.pk,
            charge.order_id,
            pi_id,
        )

    # FR-W1 (T039): promote provisional PENDING_CAPTURE OrderItems to ACTIVE when the
    # UPSELL charge is captured.  T039 does not create OrderItems (that is T029 scope);
    # this code ships now so T029 can plug in without touching the webhook handler.
    # Promotion is order-scoped (all PENDING_CAPTURE items on this order) because there
    # is currently no FK from OrderItem to OrderCharge.  T029 may add a more precise
    # link; update the filter here when that FK is available.
    #
    # Must NOT mark the Order PAID, accrue org volume, fire purchase analytics, or run
    # abandoned-checkout suppression — those are ORIGINAL-intent side effects only.
    try:
        from orders.models import OrderItem, OrderItemStatus
        promoted_items = (
            OrderItem.objects.cross_store_unsafe().filter(
                order_id=charge.order_id,
                status=OrderItemStatus.PENDING_CAPTURE,
            ).update(status=OrderItemStatus.ACTIVE)
        )
        if promoted_items:
            logger.info(
                'payment_intent.succeeded: promoted %d PENDING_CAPTURE OrderItem(s) '
                'to ACTIVE for order %s (UPSELL charge %s)',
                promoted_items,
                charge.order_id,
                charge.pk,
            )
    except Exception:
        logger.warning(
            'payment_intent.succeeded: OrderItem promotion failed for order %s '
            '(non-fatal — charge %s)',
            charge.order_id,
            charge.pk,
            exc_info=True,
        )



@transaction.atomic
def _handle_charge_refunded(event, verified_account) -> None:
    """
    charge.refunded — Stripe reports a refund (partial or full) against a charge.

    Full multi-charge refund tracking (OrderCharge.refunded_amount,
    PARTIALLY_REFUNDED status transitions per the multi-charge refund model of
    ADR-007 §7) remains deferred to a future ticket — this handler does not
    touch OrderCharge at all.

    What IS implemented here (ADR-029 D9, human-approved 2026-07-11 refund
    policy): on a FULL refund of the order's original charge, transition the
    order to REFUNDED (idempotent — only acts once, same pattern as every
    other handler in this module) using the H1 account-binding check this
    docstring previously flagged as mandatory once implemented, and then
    void/flag any gift cards a campaign issued for this order
    (discounts.service.void_or_flag_campaign_gift_cards_for_refund).  Partial
    refunds are logged only — D9: "no automatic voiding (v1)".

    Counter-restore for failed/canceled authorizations was implemented in
    B2+B3 via _handle_payment_intent_failed and _handle_payment_intent_canceled
    — unrelated to this handler.
    """
    charge = event.get('data', {}).get('object', {})
    charge_id = charge.get('id', '')
    pi_id = charge.get('payment_intent', '') or ''
    if not pi_id:
        logger.info(
            'Stripe charge.refunded event %s has no payment_intent — nothing to reconcile',
            charge_id,
        )
        return

    from orders.models import Order, PaymentStatus

    order = (
        Order.objects.cross_store_unsafe()
        .select_for_update()
        .filter(processor_payment_intent_id=pi_id)
        .first()
    )
    if order is None:
        logger.debug(
            'Stripe charge.refunded: no order for PaymentIntent %s (charge %s)',
            pi_id, charge_id,
        )
        return

    if not _order_bound_to_account(order, verified_account, 'charge.refunded', pi_id):
        return

    amount = charge.get('amount')
    amount_refunded = charge.get('amount_refunded')
    is_full_refund = bool(charge.get('refunded')) or (
        amount is not None and amount_refunded is not None and amount_refunded >= amount
    )
    if not is_full_refund:
        logger.info(
            'Stripe charge.refunded: partial refund for order %s (charge %s) — '
            'no automatic gift-card voiding (D9 v1)',
            order.pk, charge_id,
        )
        return

    if order.payment_status == PaymentStatus.REFUNDED:
        logger.debug(
            'Order %s already REFUNDED — skipping duplicate charge.refunded delivery (charge %s)',
            order.pk, charge_id,
        )
        return

    # W3-1 (WAVE3_AUDIT.md): REFUNDED is only reachable from PAID. Without this
    # whitelist, an out-of-order or otherwise premature charge.refunded event
    # could flip a PENDING/FAILED/CANCELLED order straight to REFUNDED before
    # it was ever PAID — a nonsensical state that also lets a later
    # payment_intent.succeeded flip it right back to PAID (the exact
    # REFUNDED->PAID gap this ticket closes) since no gift cards existed yet
    # for void_or_flag_campaign_gift_cards_for_refund to act on. Refuse and
    # flag for manual reconciliation, same posture as the CANCELLED guard in
    # _handle_payment_intent_succeeded.
    if order.payment_status != PaymentStatus.PAID:
        logger.warning(
            'payments.webhook.refund_before_paid: order %s received charge.refunded '
            'while in status %s (expected PAID) — manual reconciliation required '
            '(charge %s)',
            order.pk, order.payment_status, charge_id,
        )
        return

    order.payment_status = PaymentStatus.REFUNDED
    order.save(update_fields=['payment_status', 'updated_at'])
    logger.info('Order %s transitioned to REFUNDED for Stripe charge %s', order.pk, charge_id)

    # Void/flag campaign-issued gift cards (ADR-029 D9). Wrapped in a nested
    # savepoint: a DatabaseError here would otherwise poison the outer
    # @transaction.atomic and roll back the REFUNDED transition (same
    # non-fatal posture as suppress_abandoned_checkout, ADR-010 Q5).
    try:
        with transaction.atomic():
            from discounts.service import void_or_flag_campaign_gift_cards_for_refund
            void_or_flag_campaign_gift_cards_for_refund(order)
    except Exception:
        logger.warning(
            'void_or_flag_campaign_gift_cards_for_refund failed for order %s (non-fatal)',
            order.pk,
            exc_info=True,
        )
