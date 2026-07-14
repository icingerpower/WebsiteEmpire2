"""
Custom exceptions for the campaigns upsell flow (ADR-011).

UpsellRateLimitError:   Raised when session.accept_attempts >= 5.
                        The caller (upsell_accept_view) returns HTTP 403.
                        No processor call is made.

UpsellUnavailableError: Raised when the upsell offer cannot be fulfilled —
                        e.g. PayPal vault not enabled, session in wrong state,
                        capture window expired.  The caller returns HTTP 410.

Both inherit from Exception; they are never silenced at call sites.
Catching and returning a fallback is a §XIII violation.
"""


class UpsellRateLimitError(Exception):
    """
    Raised when session.accept_attempts >= 5 (ADR-011 rate limit).

    The 6th and subsequent accept attempts for a session reach this error
    without any processor call.  The view returns HTTP 403.
    """


class UpsellUnavailableError(Exception):
    """
    Raised when the upsell offer is unavailable for the current session.

    Causes:
    - PayPal vault not enabled (paypal_vault_enabled=False)
    - PayPal immediate capture mode (funnel-ineligible)
    - Session not in INTERACTION state
    - Capture window expired (DB-time check inside the locked transaction)
    - No authorized original charge found
    - Processor does not support off-session charges

    The view returns HTTP 410 (Gone) so the storefront can hide the offer.
    """
