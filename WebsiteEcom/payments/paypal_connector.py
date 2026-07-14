"""
PayPal Orders API v2 connector (TICKET-019).

Implements the PaymentProcessor interface (payments/processor.py) using the
PayPal Orders API v2 (AUTHORIZE intent + manual capture — the two-stage model
required for the post-purchase upsell funnel, ADR-007 §2).

Delayed-capture support confirmed: PayPal Orders API v2 supports AUTHORIZE intent
(authorize without capturing) followed by a separate capture call, satisfying the
ADR-007 §2 requirement.  supports_delayed_capture = True.

Design notes:
- client_id comes from ProcessorAccount.client_id (plain CharField — not secret).
- client_secret comes from ProcessorAccount.api_secret (EncryptedCharField).
- Amounts are in the smallest currency unit (cents) everywhere in this codebase;
  PayPal Orders API v2 uses decimal strings (e.g. "12.50" for 1250 cents).
  Conversion: cents → decimal = amount / 100, formatted to 2 decimal places.
- create_off_session_charge is a documented stub for Phase 1 — PayPal Vault API
  requires additional merchant onboarding beyond standard PayPal credentials.
- create_customer returns email as processor_customer_id — PayPal does not have
  a "create customer" endpoint; the email is the identifier for Vault lookups.
- verify_webhook uses PayPal's webhook signature verification API (not HMAC) —
  the sig_header argument is a dict of PayPal-specific HTTP headers.
- All public methods return result dataclasses and never raise; exceptions are
  caught, logged, and returned as success=False / healthy=False results.
"""

import json
import logging
import time
from datetime import timedelta
from decimal import Decimal

from .paypal_client import PayPalClient
from .processor import (
    ChargeResult,
    CustomerResult,
    HealthResult,
    PaymentIntentResult,
    PaymentProcessor,
    RefundResult,
)

logger = logging.getLogger('payments.paypal')


class PayPalConnector(PaymentProcessor):
    """
    PayPal implementation of PaymentProcessor using Orders API v2.

    Supports authorize-then-capture (delayed capture), making PayPal-routed orders
    eligible for the post-purchase upsell funnel (ADR-007 §2).
    """

    supports_delayed_capture: bool = True

    #: Buyer completes payment via a 302 redirect to PayPal's approval page,
    #: not an in-page SDK confirm (ADR-020 D7). checkout_pay_post branches on
    #: this capability, never on processor_type.
    approval_flow: str = 'redirect'

    #: PayPal's funds-guarantee "honor period" is 3 days; capped 1 day inside
    #: that so the capture-window watchdog has headroom against a beat outage
    #: (ADR-020 D8). Replaces the old 7-day Stripe-shaped fallback, which was
    #: 4 days past the honor period for every non-funnel PayPal order.
    max_capture_delay: timedelta = timedelta(days=2)

    def supports_off_session_charge(self) -> bool:
        """
        PayPal supports off-session charges only when Vault onboarding is complete.

        Returns self._account.paypal_vault_enabled (ADR-011 Q3). When False,
        the upsell accept flow raises UpsellUnavailableError immediately rather
        than making a guaranteed-to-fail Vault API call (§XV-1 / ADR-011 Q3).
        """
        return bool(self._account.paypal_vault_enabled)

    def __init__(self, processor_account) -> None:
        """
        processor_account: a payments.models.ProcessorAccount instance.
        client_id is read from account.client_id (plain CharField).
        client_secret is read from account.api_secret (decrypted by EncryptedCharField).
        """
        self._account = processor_account
        self._client = PayPalClient(
            client_id=processor_account.client_id,
            client_secret=processor_account.api_secret,
            sandbox=processor_account.is_test_mode,
        )

    # ------------------------------------------------------------------
    # PaymentIntent — create (authorize) and capture
    # ------------------------------------------------------------------

    def create_payment_intent(
        self,
        amount: int,
        currency: str,
        customer_id: str,
        metadata: dict,
        capture_method: str = 'manual',
        setup_future_usage: bool = False,
        return_url: str = '',
        cancel_url: str = '',
        shipping_address: dict | None = None,
    ) -> PaymentIntentResult:
        """
        Create a PayPal Order with AUTHORIZE intent (authorize without capturing).

        The capture_method parameter is accepted for interface compatibility but
        PayPal Orders API v2 only supports manual capture via a separate POST —
        'automatic' is not honoured here; all PayPal orders authorize first.

        The returned client_secret is repurposed to carry the PayPal approve URL
        (or, when payment_source is present, the payer-action URL — see the
        rel-extraction note below).  The frontend must redirect the buyer to this
        URL; on return, checkout_paypal_return authorizes the order (ADR-020 D3).

        PayPal amounts are decimal strings; this method converts from cents.

        return_url / cancel_url (ADR-020 D2): when return_url is non-empty, both
        are placed in payment_source.paypal.experience_context so PayPal redirects
        the buyer back to this site after approving/cancelling.  When return_url
        is empty (defensive — e.g. a caller that hasn't been updated yet), no
        experience_context is sent, matching the pre-ADR-020 payload exactly.

        shipping_address (20:P2, DECIDED 2026-07-10 — supersedes the original
        hardcoded NO_SHIPPING): when a non-empty dict is supplied (the checkout-
        collected address, keys {name, line1, line2, city, state, postal_code,
        country} — see cart.models.CheckoutState / orders.models.Order snapshot
        shape), it is mapped onto purchase_units[0].shipping per the Orders API v2
        shape (name.full_name + address.address_line_1/2, admin_area_2=city,
        admin_area_1=state, postal_code, country_code) and
        experience_context.shipping_preference is set to 'SET_PROVIDED_ADDRESS' so
        PayPal locks the buyer to this address instead of collecting its own —
        carrying the address onto the PayPal transaction record is what PayPal
        Seller Protection eligibility for physical goods ties to. Blank keys are
        omitted from the mapped dicts (PayPal rejects empty-string address fields).
        shipping_address=None (the default) preserves the exact pre-20:P2 payload:
        shipping_preference='NO_SHIPPING', no shipping block.

        setup_future_usage=True adds PayPal Vault opt-in fields to the order
        payload, merged into the SAME payment_source.paypal block as
        experience_context (PayPal only accepts one payment_source.paypal object
        per order). ASSUMPTION: the vault payment_source structure used here
        follows the PayPal Orders API v2 Vault reference (store_in_vault=ON_SUCCESS,
        usage_type=MERCHANT).  This requires completed Vault merchant onboarding
        and is gated by paypal_vault_enabled=True on the ProcessorAccount.
        Vault token and customer ID for PayPal are only available after buyer
        approval (webhooks) — PaymentIntentResult.customer_id and
        payment_method_id are empty at creation time (T029 webhook handler).

        Approval-link extraction (ADR-020 D2, 20:A1): PayPal returns the buyer
        redirect link with rel='approve' on a bare order, but rel='payer-action'
        once a payment_source is supplied (which experience_context/vault always
        do here once return_url is set or setup_future_usage=True). Both rels are
        accepted so the redirect keeps working regardless of which payload shape
        PayPal used for this particular order.
        """
        try:
            purchase_unit: dict = {
                'amount': {
                    'currency_code': currency.upper(),
                    'value': f'{amount / 100:.2f}',
                },
                'custom_id': str(metadata.get('order_id', '')),
            }
            shipping_block = self._build_shipping_block(shipping_address)
            if shipping_block:
                purchase_unit['shipping'] = shipping_block
            payload: dict = {
                'intent': 'AUTHORIZE',
                'purchase_units': [purchase_unit],
            }
            paypal_payment_source: dict = {}
            if return_url:
                paypal_payment_source['experience_context'] = {
                    'return_url': return_url,
                    'cancel_url': cancel_url,
                    'user_action': 'PAY_NOW',
                    'shipping_preference': (
                        'SET_PROVIDED_ADDRESS' if shipping_block else 'NO_SHIPPING'
                    ),
                }
            if setup_future_usage:
                # ASSUMPTION: PayPal Vault API fields — store_in_vault=ON_SUCCESS
                # vaults the payment method on successful authorization.
                # Requires Vault merchant onboarding (gated by paypal_vault_enabled).
                # Fields per PayPal Orders API v2 Vault reference; untested until
                # onboarding is complete.
                paypal_payment_source['vault'] = {
                    'store_in_vault': 'ON_SUCCESS',
                    'usage_type': 'MERCHANT',
                }
            if paypal_payment_source:
                payload['payment_source'] = {'paypal': paypal_payment_source}
            data = self._client.post('/v2/checkout/orders', payload)
            order_id = data.get('id', '')
            approve_url = next(
                (
                    link['href']
                    for link in data.get('links', [])
                    if link.get('rel') in ('approve', 'payer-action')
                ),
                '',
            )
            return PaymentIntentResult(
                success=bool(order_id),
                payment_intent_id=order_id,
                client_secret=approve_url,  # carries the buyer approval URL
                status=data.get('status', ''),
                customer_id='',       # PayPal vault customer ID arrives via webhook (T029)
                payment_method_id='', # PayPal vault token ID arrives via webhook (T029)
            )
        except Exception as exc:
            logger.error('PayPal create_payment_intent failed: %s', exc)
            return PaymentIntentResult(
                success=False,
                payment_intent_id='',
                error_message=str(exc),
            )

    @staticmethod
    def _build_shipping_block(shipping_address: dict | None) -> dict:
        """
        Map a checkout-collected address dict onto PayPal Orders API v2's
        purchase_units[0].shipping shape (20:P2, DECIDED 2026-07-10).

        Input shape (cart.models.CheckoutState / orders.models.Order snapshot,
        ADR-002): {name, line1, line2, city, state, postal_code, country}.
        Output shape (PayPal Orders API v2):
          {'name': {'full_name': ...},
           'address': {'address_line_1': ..., 'address_line_2': ...,
                        'admin_area_2': ..., 'admin_area_1': ...,
                        'postal_code': ..., 'country_code': ...}}

        Returns {} when shipping_address is None/empty, or when it carries no
        non-blank fields at all — callers treat an empty dict as "no shipping
        block" (falls back to NO_SHIPPING). Blank individual keys are omitted
        from the mapped sub-dicts: PayPal rejects empty-string address fields
        rather than silently ignoring them.
        """
        if not shipping_address:
            return {}

        name = (shipping_address.get('name') or '').strip()
        address_fields = {
            'address_line_1': (shipping_address.get('line1') or '').strip(),
            'address_line_2': (shipping_address.get('line2') or '').strip(),
            'admin_area_2': (shipping_address.get('city') or '').strip(),
            'admin_area_1': (shipping_address.get('state') or '').strip(),
            'postal_code': (shipping_address.get('postal_code') or '').strip(),
            'country_code': (shipping_address.get('country') or '').strip(),
        }
        address = {k: v for k, v in address_fields.items() if v}

        block: dict = {}
        if name:
            block['name'] = {'full_name': name}
        if address:
            block['address'] = address
        return block

    def capture_payment_intent(
        self,
        payment_intent_id: str,
        amount_to_capture: int | None = None,
        idempotency_key: str = "",
    ) -> ChargeResult:
        """
        Capture an authorized PayPal order.

        Looks up the authorization ID from the order, then POSTs to
        /v2/payments/authorizations/{auth_id}/capture.

        amount_to_capture=None captures the full authorized amount (PayPal default
        when no amount body is sent).  Partial capture is supported for upsell
        item declines (ADR-007 §2).

        idempotency_key: when non-empty, sent as the PayPal-Request-Id header —
        PayPal's mechanism for idempotent capture retries (ADR-011 Q5 pass 2).

        PayPal amounts are decimal strings; this method converts from cents.
        final_capture=True signals that no further captures will be made on this
        authorization, preventing PayPal from allowing duplicate captures.
        """
        try:
            order = self._client.get(f'/v2/checkout/orders/{payment_intent_id}')
            auth_id = None
            for pu in order.get('purchase_units', []):
                for auth in pu.get('payments', {}).get('authorizations', []):
                    auth_id = auth.get('id')
                    break
                if auth_id:
                    break

            if not auth_id:
                return ChargeResult(
                    success=False,
                    processor_charge_id='',
                    amount_captured=0,
                    error_message='No authorization found on PayPal order',
                )

            capture_payload: dict = {'final_capture': True}
            if amount_to_capture is not None:
                capture_payload['amount'] = {
                    'value': f'{amount_to_capture / 100:.2f}',
                    'currency_code': order['purchase_units'][0]['amount']['currency_code'],
                }

            extra_headers: dict = {}
            if idempotency_key:
                extra_headers['PayPal-Request-Id'] = idempotency_key

            capture_data = self._client.post(
                f'/v2/payments/authorizations/{auth_id}/capture',
                capture_payload,
                extra_headers=extra_headers if extra_headers else None,
            )
            captured_value = capture_data.get('amount', {}).get('value', '0')
            captured_cents = int(Decimal(str(captured_value)) * 100)
            return ChargeResult(
                success=capture_data.get('status') == 'COMPLETED',
                processor_charge_id=capture_data.get('id', ''),
                amount_captured=captured_cents,
                processor_payment_intent_id=payment_intent_id,
            )
        except Exception as exc:
            logger.error('PayPal capture_payment_intent %s failed: %s', payment_intent_id, exc)
            return ChargeResult(
                success=False,
                processor_charge_id='',
                amount_captured=0,
                error_message=str(exc),
            )

    # ------------------------------------------------------------------
    # Off-session charge — upsell step (ADR-007 §2)
    # ------------------------------------------------------------------

    def create_off_session_charge(
        self,
        payment_method_id: str,
        amount: int,
        currency: str,
        customer_id: str,
        metadata: dict,
        idempotency_key: str = "",
    ) -> ChargeResult:
        """
        PayPal off-session charge — documented Phase 1 stub.

        PayPal Vault API requires additional merchant onboarding (vaulting agreement,
        advanced API access) beyond standard PayPal credentials.  This will be
        implemented in Phase 2 once the merchant has completed Vault onboarding.

        A failure here must NOT affect the original order (ADR-007 §2) — this stub
        returns a descriptive error_message so callers can handle it gracefully.

        idempotency_key: reserved for Phase 2 — will be sent as PayPal-Request-Id
        header on the merchant-initiated transaction.
        """
        masked = (customer_id[:3] + '***') if customer_id else ''
        logger.warning(
            'PayPal off-session charge requested but Vault API is not yet configured '
            '(Phase 2). payment_method_id=%s customer_id=%s amount=%d',
            payment_method_id,
            masked,
            amount,
        )
        return ChargeResult(
            success=False,
            processor_charge_id='',
            amount_captured=0,
            error_message=(
                'PayPal off-session charges require Vault API setup (Phase 2). '
                'The original order is not affected.'
            ),
        )

    def retrieve_payment_intent_status(self, payment_intent_id: str) -> str:
        """
        Retrieve the capture status of a PayPal order for the watchdog (ADR-020 D4).

        One GET /v2/checkout/orders/{payment_intent_id}; returns 'captured' when any
        capture on the order is 'COMPLETED', else 'authorized' (preserving the old
        stub's safe re-attempt bias — the watchdog's capture:{order_id} idempotency
        key already guards against double-capture, so 'authorized' is always a safe
        default even when the true state is a still-open authorization).
        Never raises — any error falls back to 'authorized' so the caller re-attempts.
        """
        try:
            order = self._client.get(f'/v2/checkout/orders/{payment_intent_id}')
        except Exception as exc:
            logger.error(
                'PayPal retrieve_payment_intent_status %s failed: %s',
                payment_intent_id, exc,
            )
            return 'authorized'

        captures = self._collect_captures(order)
        if any(c.get('status') == 'COMPLETED' for c in captures):
            return 'captured'
        return 'authorized'

    def retrieve_payment_intent_raw_status(self, payment_intent_id: str) -> str:
        """
        Retrieve the raw PayPal order/authorization/capture status, mapped onto the
        Stripe-shaped status strings consumed by checkout_payment_return,
        checkout_retry, and _pi_permits_void (ADR-020 D4 — exact mapping table in
        the ADR; each row here corresponds 1:1 to a row of that table).

        One GET /v2/checkout/orders/{payment_intent_id}; inspects the most-specific
        resource first (captures, then authorizations, then the order itself).
        Never raises — any error, or an unrecognized status, returns '' (every
        consumer already treats '' as "retryable, degrade gracefully" — the
        current/pre-ADR-020 behavior is the worst case, never a false 'succeeded').
        """
        try:
            order = self._client.get(f'/v2/checkout/orders/{payment_intent_id}')
        except Exception as exc:
            logger.error(
                'PayPal retrieve_payment_intent_raw_status %s failed: %s',
                payment_intent_id, exc,
            )
            return ''
        return self._map_order_to_raw_status(order)

    @staticmethod
    def _collect_captures(order: dict) -> list:
        """Flatten payments.captures across every purchase_unit on a PayPal order."""
        captures = []
        for pu in order.get('purchase_units', []) or []:
            captures.extend((pu.get('payments') or {}).get('captures', []) or [])
        return captures

    @staticmethod
    def _collect_authorizations(order: dict) -> list:
        """Flatten payments.authorizations across every purchase_unit on a PayPal order."""
        authorizations = []
        for pu in order.get('purchase_units', []) or []:
            authorizations.extend((pu.get('payments') or {}).get('authorizations', []) or [])
        return authorizations

    @classmethod
    def _map_order_to_raw_status(cls, order: dict) -> str:
        """
        Pure mapping function for the ADR-020 D4 table — no I/O, easily unit-tested
        row by row. See retrieve_payment_intent_raw_status for the raising/logging
        wrapper around this.
        """
        captures = cls._collect_captures(order)
        capture_statuses = {c.get('status') for c in captures}
        if capture_statuses & {'COMPLETED', 'REFUNDED', 'PARTIALLY_REFUNDED'}:
            return 'succeeded'
        if 'PENDING' in capture_statuses:
            return 'processing'
        # capture DECLINED/FAILED only (or no captures at all) falls through to
        # the authorization row below — a declined/failed capture does not mean
        # the authorization itself is gone.

        authorizations = cls._collect_authorizations(order)
        if authorizations:
            auth_status_map = {
                'CREATED': 'requires_capture',
                'PENDING': 'processing',
                'CAPTURED': 'succeeded',
                'PARTIALLY_CAPTURED': 'succeeded',
                'DENIED': 'requires_payment_method',
                'VOIDED': 'canceled',
                'EXPIRED': 'canceled',
            }
            return auth_status_map.get(authorizations[0].get('status', ''), '')

        order_status_map = {
            'CREATED': 'requires_payment_method',
            'SAVED': 'requires_payment_method',
            'PAYER_ACTION_REQUIRED': 'requires_action',
            'APPROVED': 'requires_confirmation',
            'VOIDED': 'canceled',
            'COMPLETED': 'succeeded',
        }
        return order_status_map.get(order.get('status', ''), '')

    def retrieve_payment_intent_client_secret(self, payment_intent_id: str) -> str:
        """
        Retrieve the PayPal approve/payer-action URL for an existing order so
        checkout_retry can re-serve it to the shopper (ADR-020 D4, 20:A2).

        Only returns a link when the order has not yet been authorized — order
        status CREATED, PAYER_ACTION_REQUIRED, or APPROVED-with-no-authorization-
        yet.  Any other state (already authorized/captured/voided) returns ''
        since re-approving would be meaningless or wrong.
        Best-effort: if PayPal has expired the approval link, the buyer lands
        back on the decline path and re-submits checkout (the fast-path void in
        checkout_pay_post covers the stale order) — this method never raises.
        """
        try:
            order = self._client.get(f'/v2/checkout/orders/{payment_intent_id}')
        except Exception as exc:
            logger.error(
                'PayPal retrieve_payment_intent_client_secret %s failed: %s',
                payment_intent_id, exc,
            )
            return ''

        status = order.get('status', '')
        has_authorization = bool(self._collect_authorizations(order))
        eligible = status in ('CREATED', 'PAYER_ACTION_REQUIRED') or (
            status == 'APPROVED' and not has_authorization
        )
        if not eligible:
            return ''

        return next(
            (
                link['href']
                for link in order.get('links', [])
                if link.get('rel') in ('approve', 'payer-action')
            ),
            '',
        )

    def void_payment_intent(self, payment_intent_id: str) -> None:
        """
        Void the authorization on a PayPal order, if one exists and is voidable
        (ADR-020 D5).

        GET the order, extract purchase_units[0].payments.authorizations[0]:
        - No authorization at all → nothing is held; log INFO and return (an
          unapproved/unauthorized PayPal order expires server-side on its own —
          Orders v2 has no supported "cancel order" call).
        - Authorization status CREATED or PENDING → POST
          /v2/payments/authorizations/{auth_id}/void (204 No Content on success,
          tolerated by PayPalClient.post — see ADR-020).
        - Any other authorization status (CAPTURED/DENIED/VOIDED/EXPIRED) →
          nothing voidable; log INFO and return.
        Never raises — this is a best-effort call; callers already wrap it and
        log on failure (orders/service.py _void_processor_intent_best_effort).
        """
        try:
            order = self._client.get(f'/v2/checkout/orders/{payment_intent_id}')
        except Exception as exc:
            logger.warning(
                'PayPal void_payment_intent: failed to GET order %s (best-effort): %s',
                payment_intent_id, exc,
            )
            return

        authorizations = self._collect_authorizations(order)
        if not authorizations:
            logger.info(
                'paypal.void.no_authorization: order %s has no authorization — '
                'nothing to void; it will expire on its own.',
                payment_intent_id,
            )
            return

        auth = authorizations[0]
        auth_id = auth.get('id', '')
        auth_status = auth.get('status', '')
        if auth_status not in ('CREATED', 'PENDING'):
            logger.info(
                'paypal.void.not_voidable: order %s authorization %s has status %s — '
                'nothing to void.',
                payment_intent_id, auth_id, auth_status,
            )
            return

        try:
            self._client.post(f'/v2/payments/authorizations/{auth_id}/void', {})
            logger.info(
                'paypal.void.authorization_voided: order %s authorization %s voided.',
                payment_intent_id, auth_id,
            )
        except Exception as exc:
            logger.warning(
                'PayPal void_payment_intent: void POST failed for order %s '
                'authorization %s (best-effort): %s',
                payment_intent_id, auth_id, exc,
            )

    def authorize_order(self, order_id: str, idempotency_key: str = '') -> dict:
        """
        POST /v2/checkout/orders/{order_id}/authorize (ADR-020 D3).

        Used exclusively by payments/paypal_service.py::ensure_authorized — the
        single resolution function shared by the PayPal return view and the
        CHECKOUT.ORDER.APPROVED webhook handler.  Kept on the connector (not in
        paypal_service.py) so all PayPal HTTP details — including the PayPal
        error-response shape — stay behind this class's boundary, matching every
        other connector method.

        idempotency_key, when non-empty, is sent as PayPal-Request-Id — PayPal's
        own idempotency mechanism; a concurrent return-view/webhook race for the
        same order collapses to a single authorization at PayPal.

        Returns a dict (never raises):
          'status'        — the authorization status on success ('CREATED',
                             'PENDING', 'DENIED'); '' on any failure.
          'error_name'     — PayPal's machine-readable error code on failure
                             (e.g. 'ORDER_ALREADY_AUTHORIZED', 'ORDER_NOT_APPROVED',
                             'INSTRUMENT_DECLINED'); '' on success or when the
                             error body could not be parsed.
          'error_message'  — human-readable description; '' on success.

        ASSUMPTION (20:A1-adjacent): PayPal's 4xx error body follows the Orders v2
        shape {'name': ..., 'details': [{'issue': ...}], 'message': ...} — the
        specific issue code lives in details[0].issue (e.g. ORDER_ALREADY_AUTHORIZED),
        falling back to the top-level 'name' when 'details' is absent.
        """
        extra_headers = {'PayPal-Request-Id': idempotency_key} if idempotency_key else None
        try:
            data = self._client.post(
                f'/v2/checkout/orders/{order_id}/authorize',
                {},
                extra_headers=extra_headers,
            )
        except Exception as exc:
            error_name = ''
            error_message = str(exc)
            response = getattr(exc, 'response', None)
            if response is not None:
                try:
                    body = response.json()
                    details = body.get('details') or []
                    error_name = details[0].get('issue', '') if details else body.get('name', '')
                    error_message = body.get('message', error_message)
                except Exception:
                    logger.warning(
                        'PayPal authorize_order %s: error response body could not be '
                        'parsed — error_name will be empty.',
                        order_id,
                    )
            logger.error(
                'PayPal authorize_order %s failed: error_name=%r error_message=%r',
                order_id, error_name, error_message,
            )
            return {'status': '', 'error_name': error_name, 'error_message': error_message}

        authorizations = self._collect_authorizations(data)
        auth_status = authorizations[0].get('status', '') if authorizations else ''
        return {'status': auth_status, 'error_name': '', 'error_message': ''}

    # ------------------------------------------------------------------
    # Refund
    # ------------------------------------------------------------------

    def refund(
        self,
        charge_id: str,
        amount: int | None = None,
        reason: str = 'requested_by_customer',
    ) -> RefundResult:
        """
        Issue a refund against a captured PayPal charge (capture ID).

        charge_id is the PayPal capture ID (returned as processor_charge_id by
        capture_payment_intent).  amount=None issues a full refund.

        Note: the currency_code in the partial-refund payload is currently
        hardcoded to 'USD' — a future ticket should pass currency through the
        OrderCharge row (TODO: currency resolution).
        """
        try:
            payload: dict = {}
            if amount is not None:
                payload['amount'] = {
                    'value': f'{amount / 100:.2f}',
                    'currency_code': 'USD',  # TODO: resolve from OrderCharge currency
                }
            refund_data = self._client.post(
                f'/v2/payments/captures/{charge_id}/refund',
                payload,
            )
            refunded_value = refund_data.get('amount', {}).get('value', '0')
            refunded_cents = int(Decimal(str(refunded_value)) * 100)
            return RefundResult(
                success=refund_data.get('status') == 'COMPLETED',
                refund_id=refund_data.get('id', ''),
                amount_refunded=refunded_cents,
            )
        except Exception as exc:
            logger.error('PayPal refund capture %s failed: %s', charge_id, exc)
            return RefundResult(
                success=False,
                refund_id='',
                amount_refunded=0,
                error_message=str(exc),
            )

    # ------------------------------------------------------------------
    # Customer
    # ------------------------------------------------------------------

    def create_customer(
        self,
        email: str,
        name: str = '',
        metadata: dict | None = None,
    ) -> CustomerResult:
        """
        PayPal has no "create customer" endpoint.

        Returns the email as the processor_customer_id — PayPal identifies buyers
        by email for Vault lookups.  No network call is made.
        """
        return CustomerResult(success=True, processor_customer_id=email)

    # ------------------------------------------------------------------
    # Webhook verification
    # ------------------------------------------------------------------

    def verify_webhook(
        self,
        payload: bytes,
        sig_header: str,
        webhook_secret: str,
    ) -> dict | None:
        """
        Verify a PayPal webhook using PayPal's signature verification API.

        Unlike Stripe (HMAC, local verification), PayPal requires a round-trip
        POST to /v1/notifications/verify-webhook-signature.

        sig_header is repurposed as a dict of PayPal HTTP headers:
          - PAYPAL-TRANSMISSION-ID
          - PAYPAL-TRANSMISSION-TIME
          - PAYPAL-CERT-URL
          - PAYPAL-AUTH-ALGO
          - PAYPAL-TRANSMISSION-SIG

        webhook_secret carries the PayPal Webhook ID (not a shared secret) —
        PayPal's verification endpoint requires the webhook ID, not a secret.

        Returns None on any failure — never raises.
        """
        try:
            event_body = json.loads(payload)
            verify_payload = {
                'transmission_id': self._extract_header(sig_header, 'PAYPAL-TRANSMISSION-ID'),
                'transmission_time': self._extract_header(sig_header, 'PAYPAL-TRANSMISSION-TIME'),
                'cert_url': self._extract_header(sig_header, 'PAYPAL-CERT-URL'),
                'auth_algo': self._extract_header(sig_header, 'PAYPAL-AUTH-ALGO'),
                'transmission_sig': self._extract_header(sig_header, 'PAYPAL-TRANSMISSION-SIG'),
                'webhook_id': webhook_secret,
                'webhook_event': event_body,
            }
            result = self._client.post(
                '/v1/notifications/verify-webhook-signature',
                verify_payload,
            )
            if result.get('verification_status') == 'SUCCESS':
                return event_body
            logger.warning(
                'PayPal webhook verification status: %s',
                result.get('verification_status'),
            )
            return None
        except Exception as exc:
            logger.warning('PayPal webhook verification failed: %s', exc)
            return None

    @staticmethod
    def _extract_header(sig_header, key: str) -> str:
        """
        Extract a PayPal header value from the sig_header dict.

        sig_header is typed str in the abstract interface but repurposed as a dict
        here to carry the multiple PayPal-specific HTTP headers.
        Returns '' if sig_header is not a dict or the key is absent.
        """
        if isinstance(sig_header, dict):
            return sig_header.get(key, '')
        return ''

    # ------------------------------------------------------------------
    # Health check
    # ------------------------------------------------------------------

    def health_check(self) -> HealthResult:
        """
        Verify PayPal credentials by calling the identity endpoint.

        A successful response confirms client_id and api_secret are valid and
        PayPal is reachable.  Used by the routing engine (T020) at evaluation
        time (ADR-006 §3).
        """
        try:
            start = time.monotonic()
            self._client.get('/v1/identity/openidconnect/userinfo?schema=openid')
            latency_ms = (time.monotonic() - start) * 1000
            return HealthResult(healthy=True, latency_ms=latency_ms)
        except Exception as exc:
            logger.error('PayPal health_check failed: %s', exc)
            return HealthResult(healthy=False, error_message=str(exc))
