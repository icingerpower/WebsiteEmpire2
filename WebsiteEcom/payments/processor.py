"""
Abstract payment processor interface (ADR-006 §3, TICKET-018).

Every concrete processor must implement this interface.
All methods are synchronous; async wrappers come with the Celery integration (T020).
All monetary amounts are in the smallest currency unit (cents for USD/EUR, etc.).

Connector registry design: no special-casing at call sites — callers hold a
PaymentProcessor and invoke it without knowing whether it is Stripe, PayPal, or any
later addition.  The factory (payments/factory.py) is the only place that maps
processor_type to a concrete class.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from datetime import timedelta


@dataclass
class ChargeResult:
    success: bool
    processor_charge_id: str  # Stripe charge_id or PayPal transaction_id
    amount_captured: int       # actual captured amount in smallest currency unit (cents)
    error_message: str = ''
    # The PaymentIntent (or equivalent) ID returned by the processor after the charge.
    # Stripe: pi_… (the PaymentIntent that was created / confirmed).
    # PayPal: the order ID or capture ID used as the payment anchor.
    # Empty for failed charges. Used by the webhook handler (payment_intent.succeeded)
    # to look up and promote OrderCharge rows (T039 Phase 2).
    processor_payment_intent_id: str = ''


@dataclass
class PaymentIntentResult:
    success: bool
    payment_intent_id: str
    client_secret: str = ''
    status: str = ''           # 'requires_capture', 'succeeded', 'canceled', etc.
    error_message: str = ''
    customer_id: str = ''      # processor customer ID (Stripe cus_… / PayPal email)
    payment_method_id: str = ''  # vault PM ID (Stripe pm_… / empty until customer confirms)


@dataclass
class RefundResult:
    success: bool
    refund_id: str
    amount_refunded: int       # refunded amount in smallest currency unit
    error_message: str = ''


@dataclass
class CustomerResult:
    success: bool
    processor_customer_id: str
    error_message: str = ''


@dataclass
class HealthResult:
    healthy: bool
    latency_ms: float = 0.0
    error_message: str = ''


class PaymentProcessor(ABC):
    """
    Abstract base for all payment processor connectors (ADR-006 §3).

    Connector capabilities:
    - supports_delayed_capture: True if the processor supports auth-then-capture
      (required for the post-purchase upsell funnel, ADR-007 §2).  Stripe: True.
    - All methods return result dataclasses — never raise from connector code;
      failures are reported via success=False and error_message.
    - verify_webhook returns None on failure, never raises.
    """

    #: Must be True if the processor supports authorize-then-capture.
    #: Orders routed to a False processor get no post-purchase upsell funnel (ADR-007).
    supports_delayed_capture: bool = False

    #: How the buyer completes payment after create_payment_intent (ADR-020 D7).
    #: 'sdk'      — the client SDK confirms in-page (Stripe: Elements/confirmPayment).
    #: 'redirect' — the buyer is sent to a processor-hosted approval URL carried in
    #:              PaymentIntentResult.client_secret (PayPal: the approve/payer-action
    #:              link).  checkout_pay_post branches on this capability — never on
    #:              processor_type — so a future redirect-style connector needs no
    #:              call-site changes (§IX).
    approval_flow: str = 'sdk'

    #: Upper bound on how long an authorization may sit before the capture-window
    #: watchdog must capture it (ADR-020 D8).  Stripe's authorization hold lasts up
    #: to 7 days; PayPal's is a 3-day honor period (max_capture_delay is set 1 day
    #: below that to leave headroom for a watchdog outage).  begin_checkout clamps
    #: the funnel/fallback capture window to this value so a PayPal order is never
    #: scheduled for capture after its honor period has lapsed (a silent revenue
    #: leak — §XV-1).
    max_capture_delay: timedelta = timedelta(days=7)

    def supports_off_session_charge(self) -> bool:
        """
        Return True if this processor account can issue off-session charges.

        Off-session charges are required for the upsell accept flow (ADR-011 Q3).
        Stripe returns True unconditionally (Stripe PaymentIntents support off-session
        charges via the vaulted pm_ identifier).
        PayPal returns self._account.paypal_vault_enabled (Vault onboarding required,
        ADR-011 Q3 — gated here to prevent a silent 100%-failure funnel §XV-1).

        Default False — new connectors opt in explicitly rather than silently failing.
        """
        return False

    @abstractmethod
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
        Create a payment intent.

        Default capture_method='manual' authorizes without capturing — required for
        the two-stage capture model (ADR-007 §2).  Pass 'automatic' for immediate
        capture (standard non-upsell orders, or off-session upsell charges).

        setup_future_usage=True signals vault opt-in for future off-session charges
        (ADR-011 Q4).  Stripe adds setup_future_usage='off_session'; PayPal adds
        vault payment_source fields.  The resulting PaymentIntentResult.customer_id
        and payment_method_id fields carry any processor IDs available at creation
        time — payment_method_id is typically empty until the customer confirms via
        the frontend SDK.

        return_url / cancel_url (ADR-020 D2): defaulted to '' so every existing call
        site and the Stripe connector are untouched.  PayPal uses them to populate
        payment_source.paypal.experience_context so the buyer is redirected back to
        this site after approving/cancelling on PayPal's site.  Stripe ignores both.

        shipping_address (20:P2, uncertainties doc): defaulted to None so every
        existing call site and the Stripe connector are untouched — Stripe never
        reads this kwarg (address collection lives in Stripe's own Elements/AVS
        flow). PayPal maps it onto purchase_units[0].shipping (Orders API v2
        shape: name + address_line_1/2, admin_area_2=city, admin_area_1=state,
        postal_code, country_code) and switches shipping_preference from
        'NO_SHIPPING' to 'SET_PROVIDED_ADDRESS' — carrying the address onto the
        PayPal transaction record strengthens PayPal Seller Protection eligibility
        for physical goods. None (the default) preserves the pre-20:P2 NO_SHIPPING
        payload exactly.
        """

    @abstractmethod
    def capture_payment_intent(
        self,
        payment_intent_id: str,
        amount_to_capture: int | None = None,
        idempotency_key: str = "",
    ) -> ChargeResult:
        """
        Capture a previously authorized payment intent.

        amount_to_capture=None captures the full authorized amount.
        Partial capture is used when declining some upsell items (ADR-007 §2).
        idempotency_key: when non-empty, passed to the processor so that repeated
        calls with the same key are safe (ADR-011 Q5 watchdog re-issue, §XV-6).
        """

    @abstractmethod
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
        Charge a saved payment method off-session (upsell step, ADR-007 §2).

        The payment method must have been vaulted during checkout via
        setup_future_usage=off_session (or PayPal equivalent).  Failure here
        must NOT affect the original order — callers are responsible for the
        failure handling described in ADR-007 §2.
        idempotency_key: when non-empty, passed to the processor to deduplicate
        concurrent or retried off-session charge attempts (ADR-011 Q1).
        """

    def retrieve_charge_by_idempotency_key(self, idempotency_key: str) -> ChargeResult | None:
        """
        Retrieve the outcome of a charge that may have succeeded before a
        network failure (H2 ambiguous-outcome recovery).

        Returns a ChargeResult when the processor can resolve the charge by its
        idempotency key (success=True if it actually went through), or None when
        the outcome cannot be resolved.  None means "still unknown": the caller
        must keep the charge in AMBIGUOUS_OUTCOME so the capture-window watchdog
        reconciles it later — None is never success and never a decline.

        Default: None (processor cannot resolve charges by idempotency key).
        StripeConnector overrides this using the PaymentIntent search API on
        metadata['idempotency_key'].
        """
        return None

    @abstractmethod
    def retrieve_payment_intent_status(self, payment_intent_id: str) -> str:
        """
        Retrieve the current capture status of a payment intent.

        Returns 'captured' when the intent is already captured at the processor
        (e.g. a lost-response scenario for a CAPTURE_IN_PROGRESS charge).
        Returns 'authorized' for all other states (pending, requires_capture, etc.).

        Used by the watchdog stuck-capture reconciliation pass (ADR-011 Q5 pass 2)
        to avoid re-issuing a capture that already succeeded at the processor.
        Never raises — failures return 'authorized' so the caller re-attempts capture.
        """

    def retrieve_payment_intent_client_secret(self, payment_intent_id: str) -> str:
        """
        Retrieve the client_secret for an existing PaymentIntent.

        Used by the failed-payment retry view (checkout_retry, PY-013 / EC-007) to
        re-surface the same PI's client_secret so the shopper can retry with a new
        card without creating a new intent.

        Default implementation returns '' (PayPal and other processors that have no
        PaymentIntent concept do not need to override this).
        StripeConnector overrides to call self._client.v1.payment_intents.retrieve().
        Never raises — on any error return '' so checkout_retry degrades gracefully.
        """
        return ''

    def void_payment_intent(self, payment_intent_id: str) -> None:
        """
        Cancel/void an uncaptured PaymentIntent at the processor.

        Called by void_pending_order (ADR-015 §3) when a PENDING, never-authorized
        order is cancelled by the GC task or the fast-path pay-view retry.  The intent
        has never been captured, so voiding it immediately releases the authorization
        hold on the cardholder's account.

        Callers MUST wrap this in try/except and log on failure — this is best-effort.
        The method must never raise.

        Default: no-op.  Connectors that support explicit cancellation should override:
          - Stripe: self._client.v1.payment_intents.cancel(payment_intent_id)
          - PayPal: void the authorization via the Orders API
            PATCH /v2/checkout/orders/{payment_intent_id} with status=VOIDED
        """

    def retrieve_payment_intent_raw_status(self, payment_intent_id: str) -> str:
        """
        Retrieve the raw processor status string for a PaymentIntent.

        Unlike retrieve_payment_intent_status() (which maps to 'authorized'/'captured'
        for the watchdog), this returns the raw processor status so callers can inspect
        Stripe-native values such as 'requires_payment_method' (declined), 'canceled',
        'requires_action' (3DS), etc.

        Used by checkout_retry to determine if the PI can be re-used for a retry.
        Default returns '' (processors with no equivalent concept — checkout_retry
        allows '' as a retryable state).  PayPalConnector overrides this with the
        full status mapping table (ADR-020 D4).
        Never raises — on any error return '' so checkout_retry degrades gracefully.
        """
        return ''

    @abstractmethod
    def refund(
        self,
        charge_id: str,
        amount: int | None = None,
        reason: str = 'requested_by_customer',
    ) -> RefundResult:
        """
        Issue a refund against a captured charge.

        amount=None issues a full refund.  Partial refunds are used for the
        multi-charge refund model (ADR-007 §7): upsell charge refunded first,
        then original capture.  Callers must not exceed Σcaptured − Σrefunded.
        """

    @abstractmethod
    def create_customer(
        self,
        email: str,
        name: str = '',
        metadata: dict | None = None,
    ) -> CustomerResult:
        """Create a processor-side customer record for future off-session charges."""

    @abstractmethod
    def verify_webhook(
        self,
        payload: bytes,
        sig_header: str,
        webhook_secret: str,
    ) -> dict | None:
        """
        Verify a webhook signature and return the parsed event as a plain dict.

        Returns None on any verification failure — never raises.
        The caller (stripe_webhook view) returns 400 on None and 200 on success.
        """

    @abstractmethod
    def health_check(self) -> HealthResult:
        """
        Probe liveness of this processor account.

        Used by the payment routing engine (T020) at evaluation time (ADR-006 §3).
        A failing health check causes the routing engine to fall through to the
        fallback rule automatically, without rule reconfiguration.
        """
