"""
Signed set-password token for the store employee invite flow (ADR-033 D4b,
TICKET-047, AF-108).

Reuses Django's stock PasswordResetTokenGenerator rather than inventing a new
scheme: its hash already includes the user's password hash and last_login,
so a token is automatically invalidated the moment the invitee sets a real
password (stops replay) or if their password otherwise changes — exactly the
"expired/reused-token" guarantee AF-108's flow needs, with zero new crypto.
"""

from django.contrib.auth.tokens import PasswordResetTokenGenerator
from django.utils.encoding import force_bytes
from django.utils.http import urlsafe_base64_decode, urlsafe_base64_encode

invite_token_generator = PasswordResetTokenGenerator()


def build_invite_token(user):
    """Return (uidb64, token) for the given user."""
    uidb64 = urlsafe_base64_encode(force_bytes(user.pk))
    token = invite_token_generator.make_token(user)
    return uidb64, token


def decode_uidb64(uidb64):
    """Return the decoded user pk (int), or None if malformed."""
    try:
        return int(urlsafe_base64_decode(uidb64).decode())
    except (TypeError, ValueError, OverflowError):
        return None
