"""
Connector factory — returns the appropriate PaymentProcessor for a ProcessorAccount.

This is the single place that maps processor_type to a concrete connector class.
No call site (checkout, routing engine, admin) should import a connector class
directly — always go through get_connector() so that new processors are added
by registering an entry here, without touching callers (ADR-006 §7).
"""

from .models import ProcessorType
from .processor import PaymentProcessor


def get_connector(processor_account) -> PaymentProcessor:
    """
    Instantiate and return the correct PaymentProcessor for the given account.

    Raises ValueError for unknown processor types.
    Raises NotImplementedError for processor types whose connector has not
    landed yet (e.g. PayPal — TICKET-019).
    """
    ptype = processor_account.processor_type

    if ptype == ProcessorType.STRIPE:
        from .stripe_connector import StripeConnector
        return StripeConnector(processor_account)

    if ptype == ProcessorType.PAYPAL:
        from .paypal_connector import PayPalConnector
        return PayPalConnector(processor_account)

    raise ValueError(f'Unknown processor type: {ptype!r}')
