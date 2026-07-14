"""
PayPal buyer-approval authorization service (ADR-020 D3, TICKET-042 / T031-PP).

Single resolution function (design-pattern-ideas §XV-4): ensure_authorized(order)
is called synchronously by BOTH:
  - storefront.views_checkout.checkout_paypal_return (the buyer's browser redirect
    back from PayPal after approving) — for immediate thank-you confirmation.
  - payments.paypal_webhook_views._handle_order_approved (CHECKOUT.ORDER.APPROVED)
    — reconciliation fallback for a buyer who approves then closes the tab before
    the return view runs.

Race-free without a DB lock across the network call: both callers pass the same
PayPal-Request-Id (authorize:{order.pk}) to PayPalConnector.authorize_order, so a
concurrent return-view/webhook race collapses to a single authorization at PayPal
(PayPal's own idempotency mechanism, not ours) — see design-pattern-ideas §XIII.

ensure_authorized does NOT write Order.authorized_at itself: that happens via the
existing conditional UPDATE in storefront.views_checkout._finalize_payment_return
(return-view path) and the idempotent PAYMENT.AUTHORIZATION.CREATED webhook handler
(payments.paypal_webhook_views._handle_authorization_created) — both already use
`WHERE authorized_at IS NULL`; this module intentionally adds no third writer.

Never raises — every path returns a Stripe-shaped status string (possibly '').
"""

import logging

logger = logging.getLogger('payments.paypal')


def ensure_authorized(order) -> str:
    """
    Authorize an approved PayPal order. Idempotent. Returns a Stripe-shaped status.

    Steps (ADR-020 D3):
    1. Guard: the order's pinned account must be PayPal in 'delayed' capture mode
       (Orders API intent=AUTHORIZE) — anything else (Stripe, or a PayPal account
       configured for immediate capture) has nothing to authorize here; report its
       real status via the connector's standard raw-status lookup instead.
    2. Short-circuit: order.authorized_at already set → 'requires_capture'. This is
       the idempotent re-entry path (page refresh, or the losing side of a
       return-view/webhook race) — no second authorize call is issued.
    3. POST /v2/checkout/orders/{id}/authorize via
       PayPalConnector.authorize_order(idempotency_key=f'authorize:{order.pk}').
    4. Map the outcome:
       - authorization CREATED  → 'requires_capture'
       - authorization PENDING  → 'processing' (buyer under review / eCheck)
       - authorization DENIED   → 'requires_payment_method'
       - error_name ORDER_ALREADY_AUTHORIZED → someone else (the webhook or the
         return view) won the race; report the real current state.
       - error_name ORDER_NOT_APPROVED       → the buyer bailed before approving.
       - error_name INSTRUMENT_DECLINED      → recovery is re-approval; the retry
         view serves the approval link again via retrieve_payment_intent_client_secret.
       - any other error (network failure, unparseable body, unrecognized PayPal
         error code) → log a warning and fall back to the raw-status lookup, which
         itself never raises and returns '' as the ultimate fallback (fails closed
         onto the decline branch — today's pre-ADR-020 behavior).

    No DB row lock is held across the network call (design-pattern-ideas §XV-4):
    the caller-supplied `order` is read-only here; both callers already resolve it
    without select_for_update before invoking this function.
    """
    from .factory import get_connector
    from .models import ProcessorType

    account = order.processor_account
    if account is None:
        return ''

    connector = get_connector(account)

    if account.processor_type != ProcessorType.PAYPAL or account.paypal_capture_mode != 'delayed':
        # Not our concern here — Stripe, or a PayPal account routed for immediate
        # capture (intent=CAPTURE at create_payment_intent time, nothing to
        # authorize as a separate step). Report reality via the standard lookup.
        return connector.retrieve_payment_intent_raw_status(order.processor_payment_intent_id)

    if order.authorized_at:
        return 'requires_capture'

    result = connector.authorize_order(
        order.processor_payment_intent_id,
        idempotency_key=f'authorize:{order.pk}',
    )
    status = result.get('status', '')
    error_name = result.get('error_name', '')

    if status == 'CREATED':
        return 'requires_capture'
    if status == 'PENDING':
        return 'processing'
    if status == 'DENIED':
        return 'requires_payment_method'

    if error_name == 'ORDER_ALREADY_AUTHORIZED':
        return connector.retrieve_payment_intent_raw_status(order.processor_payment_intent_id)
    if error_name == 'ORDER_NOT_APPROVED':
        return 'requires_payment_method'
    if error_name == 'INSTRUMENT_DECLINED':
        return 'requires_payment_method'

    logger.warning(
        'paypal_service.ensure_authorized: authorize failed for order %s '
        '(error_name=%r, error_message=%r) — falling back to raw status lookup.',
        order.pk, error_name, result.get('error_message', ''),
    )
    return connector.retrieve_payment_intent_raw_status(order.processor_payment_intent_id)
