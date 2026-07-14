"""
Signed token helpers for checkout (ADR-015 §6, §4).

Two token families:
  order-thankyou  — unguessable public_order_id in the thank-you URL.
                    Gives access to order details without exposing the PK.
                    No max_age by default (the URL can be bookmarked).
  checkout-retry  — signed order token in the retry URL.
                    Embedded in the abandoned-checkout recovery email (ADR-010).
                    max_age enforced by the caller (default CHECKOUT_RETRY_TOKEN_MAX_AGE).

Security model: tokens are signed with SECRET_KEY (django.core.signing); tampered
tokens raise BadSignature → caller returns 404, never an error page (no PII leak).
Both tokens include only the order PK — the store is re-verified via for_store() so
a token from store A cannot access an order from store B (always store-check after load).
"""

from django.core import signing

SALT_THANK_YOU = 'order-thankyou'
SALT_RETRY = 'checkout-retry'

# Default maximum age for retry tokens: 7 days (in seconds).
# The abandoned-checkout campaign sends within hours (ADR-010); 7 days is generous.
CHECKOUT_RETRY_TOKEN_MAX_AGE = 7 * 24 * 3600


def make_thank_you_token(order_id: int) -> str:
    """
    Return a signed token for the thank-you URL.

    Usage:
        token = make_thank_you_token(order.pk)
        url = reverse('storefront:order-thank-you', args=[token])
    """
    return signing.dumps({'order_id': order_id}, salt=SALT_THANK_YOU)


def read_thank_you_token(token: str) -> dict:
    """
    Decode a thank-you token. Raises signing.BadSignature on tamper.

    Returns {'order_id': int}.
    Never enforce max_age — thank-you URLs should be permanently bookmarkable.
    """
    return signing.loads(token, salt=SALT_THANK_YOU)


def make_retry_token(order_id: int) -> str:
    """
    Return a signed token for the checkout retry URL.

    Usage:
        token = make_retry_token(order.pk)
        url = reverse('storefront:checkout-retry', args=[token])
    """
    return signing.dumps({'order_id': order_id}, salt=SALT_RETRY)


def read_retry_token(token: str, max_age: int | None = CHECKOUT_RETRY_TOKEN_MAX_AGE) -> dict:
    """
    Decode a retry token. Raises signing.BadSignature or signing.SignatureExpired on invalid input.

    Returns {'order_id': int}.
    max_age: maximum token age in seconds (default CHECKOUT_RETRY_TOKEN_MAX_AGE).
             Pass None to skip age enforcement.
    """
    return signing.loads(token, salt=SALT_RETRY, max_age=max_age)
