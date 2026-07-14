"""
Stripe payment processor connector (TICKET-018).

Uses the Stripe Python SDK v15+ (StripeClient.v1.* namespace — deprecated
top-level client attributes replaced by the v1 namespace in stripe-python 5.x).

Instantiate with a ProcessorAccount; credentials are decrypted transparently
by EncryptedCharField.  Never pass a raw API key directly — always go through
ProcessorAccount so that test-mode vs live-mode is enforced at the model level.

All amounts are in the smallest currency unit (cents for USD/EUR).
All public methods return result dataclasses and never raise, with ONE
deliberate exception (H2 safety fix): create_off_session_charge raises
AmbiguousChargeOutcome on a network/connection failure, because in that case
the charge outcome is UNKNOWN (the processor may have executed the request
before the connection dropped).  Definitive processor declines still return
success=False and never raise.
"""

import logging
import time

import stripe

from .exceptions import AmbiguousChargeOutcome
from .processor import (
    ChargeResult,
    CustomerResult,
    HealthResult,
    PaymentIntentResult,
    PaymentProcessor,
    RefundResult,
)

logger = logging.getLogger('payments.stripe')


class StripeConnector(PaymentProcessor):
    """
    Stripe implementation of the PaymentProcessor interface.

    Supports authorize-then-capture (required for the upsell capture-window,
    ADR-007 §2).  The routing engine (T020) checks supports_delayed_capture
    before enrolling an order into the post-purchase funnel.
    """

    supports_delayed_capture: bool = True

    def supports_off_session_charge(self) -> bool:
        """Stripe always supports off-session charges via the vaulted pm_ identifier."""
        return True

    def __init__(self, processor_account) -> None:
        """
        processor_account: a payments.models.ProcessorAccount instance.
        The API key is read from account.api_key, decrypted by EncryptedCharField.
        """
        self._account = processor_account
        self._client = stripe.StripeClient(processor_account.api_key)

    # ------------------------------------------------------------------
    # PaymentIntent — authorize (manual capture) and capture
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
        Create a Stripe PaymentIntent.

        capture_method='manual' (default) authorizes without capturing — this is
        the two-stage capture model required for the upsell funnel (ADR-007 §2).
        Pass capture_method='automatic' for immediate-capture standard orders.

        confirm=False — the client completes confirmation via the Stripe.js SDK
        (required for 3DS/SCA).  The client_secret from the result is passed to
        the frontend for this purpose.

        setup_future_usage=True adds setup_future_usage='off_session' to the
        Stripe params so the confirmed PaymentIntent vaults the payment method
        for future off-session charges (ADR-011 Q4).  Requires customer_id to be
        a valid Stripe cus_… ID — pass '' to skip vaulting even when True.
        The intent.customer is echoed back as PaymentIntentResult.customer_id;
        intent.payment_method is empty at creation time (confirm=False) and will
        be populated after the customer confirms via Stripe.js.

        return_url / cancel_url (ADR-020 D2): accepted for interface compatibility
        with PayPalConnector but ignored — Stripe's own redirect flow (3DS) uses
        payment_return_url from the frontend confirmPayment() call instead.

        shipping_address (20:P2): accepted for interface compatibility with
        PayPalConnector but ignored — Stripe collects/validates address data via
        its own Elements/AVS flow, not via the PaymentIntent creation params.
        """
        try:
            params: dict = {
                'amount': amount,
                'currency': currency.lower(),
                'customer': customer_id,
                'capture_method': capture_method,
                'metadata': metadata,
                'confirm': False,
            }
            if setup_future_usage and customer_id:
                params['setup_future_usage'] = 'off_session'
            intent = self._client.v1.payment_intents.create(params=params)
            return PaymentIntentResult(
                success=True,
                payment_intent_id=intent.id,
                client_secret=intent.client_secret or '',
                status=intent.status,
                customer_id=str(intent.customer) if intent.customer else '',
                payment_method_id=str(intent.payment_method) if intent.payment_method else '',
            )
        except stripe.StripeError as exc:
            logger.error('Stripe create_payment_intent failed: %s', exc)
            return PaymentIntentResult(
                success=False,
                payment_intent_id='',
                error_message=str(exc),
            )

    def capture_payment_intent(
        self,
        payment_intent_id: str,
        amount_to_capture: int | None = None,
        idempotency_key: str = "",
    ) -> ChargeResult:
        """
        Capture a previously authorized PaymentIntent.

        amount_to_capture=None captures the full authorized amount.
        idempotency_key: when non-empty, passed as Stripe-Idempotency-Key so that
        repeated capture attempts with the same key are deduplicated by Stripe
        (ADR-011 Q5 watchdog re-issue, §XV-6).
        """
        try:
            params: dict = {}
            if amount_to_capture is not None:
                params['amount_to_capture'] = amount_to_capture
            stripe_options: dict = {}
            if idempotency_key:
                stripe_options['idempotency_key'] = idempotency_key
            intent = self._client.v1.payment_intents.capture(
                payment_intent_id,
                params=params if params else None,
                options=stripe_options if stripe_options else None,
            )
            charge_id = intent.latest_charge or ''
            return ChargeResult(
                success=intent.status == 'succeeded',
                processor_charge_id=str(charge_id),
                amount_captured=intent.amount_received,
                processor_payment_intent_id=payment_intent_id,
            )
        except stripe.StripeError as exc:
            logger.error('Stripe capture_payment_intent %s failed: %s', payment_intent_id, exc)
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
        Charge a saved payment method off-session (upsell, ADR-007 §2).

        Uses capture_method='automatic' so the upsell amount is captured
        immediately on accept — consistent with AC-141.  A failure here must
        NOT affect the original order; callers must capture the original auth
        first (or already have captured it) before calling this method.
        idempotency_key: when non-empty, passed as Stripe-Idempotency-Key so that
        concurrent or retried charges with the same key are deduplicated by Stripe
        (ADR-011 Q1 off-session idempotency, §XV-6).

        Error contract (H2 safety fix):
        - stripe.APIConnectionError (timeout / network failure) → raises
          AmbiguousChargeOutcome.  The charge may or may not have been executed
          by Stripe before the connection dropped; the caller MUST NOT treat
          this as a decline.  Recovery: retrieve_charge_by_idempotency_key().
        - Any other stripe.StripeError → definitive decline; returns
          ChargeResult(success=False).
        """
        try:
            stripe_options: dict = {}
            if idempotency_key:
                stripe_options['idempotency_key'] = idempotency_key
            intent = self._client.v1.payment_intents.create(
                params={
                    'amount': amount,
                    'currency': currency.lower(),
                    'customer': customer_id,
                    'payment_method': payment_method_id,
                    'off_session': True,
                    'confirm': True,
                    'capture_method': 'automatic',
                    'metadata': metadata,
                },
                options=stripe_options if stripe_options else None,
            )
            charge_id = intent.latest_charge or ''
            return ChargeResult(
                success=intent.status == 'succeeded',
                processor_charge_id=str(charge_id),
                amount_captured=intent.amount_received,
                processor_payment_intent_id=intent.id,
            )
        except stripe.APIConnectionError as exc:
            # AMBIGUOUS: the network failed mid-call; Stripe may or may not have
            # charged the customer.  Never report this as a decline (H2).
            logger.error(
                'Stripe create_off_session_charge network failure — outcome '
                'UNKNOWN (idempotency_key=%s): %s',
                idempotency_key, exc,
            )
            raise AmbiguousChargeOutcome(
                f'Network error during charge — outcome unknown: {exc}',
                idempotency_key=idempotency_key,
            ) from exc
        except stripe.StripeError as exc:
            # DEFINITIVE decline: Stripe responded; the charge did not go through.
            logger.error('Stripe create_off_session_charge failed: %s', exc)
            return ChargeResult(
                success=False,
                processor_charge_id='',
                amount_captured=0,
                error_message=str(exc),
            )

    def retrieve_charge_by_idempotency_key(self, idempotency_key: str) -> ChargeResult | None:
        """
        Retrieve the outcome of an off-session charge that may have succeeded
        before a network failure (H2 recovery path).

        Looks the PaymentIntent up via Stripe's search API on the
        metadata['idempotency_key'] field — the upsell service always writes the
        idempotency key into the intent metadata (campaigns/upsell_service.py),
        which is what makes this recovery possible.

        Returns:
        - ChargeResult(success=True, …) with the real PaymentIntent id when the
          intent exists and its status is 'succeeded'.
        - ChargeResult(success=False, …) when the intent exists but did not
          succeed (definitive non-success at the processor).
        - None when no intent with this key exists yet on Stripe's side, or when
          the lookup itself fails.  None means "still unknown" — the caller must
          keep the charge in AMBIGUOUS_OUTCOME for the watchdog to reconcile;
          it must never be interpreted as success or as a decline.
        """
        if not idempotency_key:
            return None
        # Escape single quotes for the Stripe search query language (defensive —
        # our keys are internally generated and contain no quotes).
        safe_key = idempotency_key.replace("'", "\\'")
        try:
            found = self._client.v1.payment_intents.search(
                params={'query': f"metadata['idempotency_key']:'{safe_key}'"}
            )
            intents = list(found.data or [])
        except stripe.StripeError as exc:
            # Lookup failed — the outcome is STILL unknown.  Returning None keeps
            # the charge in AMBIGUOUS_OUTCOME for later reconciliation; this is an
            # explicit "unknown", not a silenced failure (§XIII).
            logger.error(
                'Stripe retrieve_charge_by_idempotency_key failed '
                '(idempotency_key=%s): %s',
                idempotency_key, exc,
            )
            return None

        if not intents:
            return None

        intent = intents[0]
        charge_id = intent.latest_charge or ''
        succeeded = intent.status == 'succeeded'
        return ChargeResult(
            success=succeeded,
            processor_charge_id=str(charge_id),
            amount_captured=intent.amount_received if succeeded else 0,
            processor_payment_intent_id=intent.id,
            error_message='' if succeeded else f'PaymentIntent status is {intent.status!r}',
        )

    def retrieve_payment_intent_status(self, payment_intent_id: str) -> str:
        """
        Retrieve the capture status of a Stripe PaymentIntent.

        Returns 'captured' when intent.status == 'succeeded' (funds captured).
        Returns 'authorized' for all other states (requires_capture, canceled, etc.).
        Never raises — failures fall back to 'authorized' so the caller re-attempts.
        """
        try:
            intent = self._client.v1.payment_intents.retrieve(payment_intent_id)
            if intent.status == 'succeeded':
                return 'captured'
            return 'authorized'
        except stripe.StripeError as exc:
            logger.error(
                'Stripe retrieve_payment_intent_status %s failed: %s',
                payment_intent_id, exc,
            )
            return 'authorized'

    def retrieve_payment_intent_client_secret(self, payment_intent_id: str) -> str:
        """
        Retrieve the client_secret for an existing Stripe PaymentIntent.

        Used by the failed-payment retry view (PY-013 / EC-007) to re-surface the
        same PI so the shopper can retry with a new card without a new intent.
        Returns '' on any Stripe error so checkout_retry degrades gracefully.
        """
        try:
            intent = self._client.v1.payment_intents.retrieve(payment_intent_id)
            return intent.client_secret or ''
        except stripe.StripeError as exc:
            logger.error(
                'Stripe retrieve_payment_intent_client_secret %s failed: %s',
                payment_intent_id, exc,
            )
            return ''

    def retrieve_payment_intent_raw_status(self, payment_intent_id: str) -> str:
        """
        Retrieve the raw Stripe PaymentIntent status string.

        Returns the Stripe-native status (e.g. 'requires_payment_method', 'canceled',
        'requires_action', 'requires_capture', 'succeeded') so that checkout_retry
        can branch on declined vs authorized vs completed without an internal mapping.

        Different from retrieve_payment_intent_status() which maps to 'authorized'/
        'captured' for the upsell capture watchdog.
        Returns '' on any Stripe error (checkout_retry treats '' as retryable).
        """
        try:
            intent = self._client.v1.payment_intents.retrieve(payment_intent_id)
            return intent.status or ''
        except stripe.StripeError as exc:
            logger.error(
                'Stripe retrieve_payment_intent_raw_status %s failed: %s',
                payment_intent_id, exc,
            )
            return ''

    def void_payment_intent(self, payment_intent_id: str) -> None:
        """
        Cancel an uncaptured Stripe PaymentIntent (ADR-015 §3 best-effort void).

        Called by void_pending_order when the GC task cancels a stale PENDING_PAYMENT
        order.  The intent was never captured, so cancellation immediately releases the
        authorization hold on the cardholder's account.

        Only cancels when the PI is in a cancellable state (requires_payment_method,
        requires_capture, requires_confirmation, requires_action, processing).
        Stripe returns an error on already-succeeded / already-canceled PIs, which
        we swallow since the order DB state is the source of truth.
        Never raises — callers wrap in try/except but this method itself is safe.
        """
        try:
            self._client.v1.payment_intents.cancel(payment_intent_id)
        except stripe.StripeError as exc:
            logger.warning(
                'Stripe void_payment_intent %s failed (best-effort): %s',
                payment_intent_id, exc,
            )

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
        Issue a refund against a captured Stripe charge.

        amount=None issues a full refund.  For the multi-charge refund model
        (ADR-007 §7), callers should target OrderCharge rows and issue separate
        refund calls per charge (upsell charge first, then original capture).
        """
        try:
            params: dict = {'charge': charge_id, 'reason': reason}
            if amount is not None:
                params['amount'] = amount
            refund = self._client.v1.refunds.create(params=params)
            return RefundResult(
                success=refund.status == 'succeeded',
                refund_id=refund.id,
                amount_refunded=refund.amount,
            )
        except stripe.StripeError as exc:
            logger.error('Stripe refund charge %s failed: %s', charge_id, exc)
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
        """Create a Stripe Customer record for off-session reuse."""
        try:
            customer = self._client.v1.customers.create(
                params={
                    'email': email,
                    'name': name,
                    'metadata': metadata or {},
                }
            )
            return CustomerResult(success=True, processor_customer_id=customer.id)
        except stripe.StripeError as exc:
            logger.error('Stripe create_customer failed: %s', exc)
            return CustomerResult(
                success=False,
                processor_customer_id='',
                error_message=str(exc),
            )

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
        Verify the Stripe-Signature header and return the parsed event as a dict.

        Uses stripe.Webhook.construct_event (module-level — does not need an API
        key, only the webhook_secret for HMAC verification).  Returns None on any
        failure — never raises.

        The returned object is a stripe.Event (StripeObject subclass), which
        inherits from dict and supports .get() for key access.
        """
        try:
            event = stripe.Webhook.construct_event(payload, sig_header, webhook_secret)
            # StripeObject inherits from dict; callers use event.get('type') etc.
            return event  # type: ignore[return-value]
        except stripe.SignatureVerificationError:
            logger.warning('Stripe webhook signature verification failed')
            return None
        except Exception as exc:
            logger.error('Stripe webhook parse error: %s', exc)
            return None

    # ------------------------------------------------------------------
    # Health check
    # ------------------------------------------------------------------

    def health_check(self) -> HealthResult:
        """
        Probe Stripe liveness by fetching the account resource.

        A successful response confirms the API key is valid and Stripe is reachable.
        Used by the routing engine (T020) at evaluation time (ADR-006 §3).
        """
        try:
            start = time.monotonic()
            self._client.v1.accounts.retrieve_current()
            latency_ms = (time.monotonic() - start) * 1000
            return HealthResult(healthy=True, latency_ms=latency_ms)
        except stripe.AuthenticationError as exc:
            logger.error('Stripe health_check authentication failed: %s', exc)
            return HealthResult(healthy=False, error_message=str(exc))
        except stripe.StripeError as exc:
            logger.error('Stripe health_check failed: %s', exc)
            return HealthResult(healthy=False, error_message=str(exc))
