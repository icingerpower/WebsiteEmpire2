"""
Session token minting/verification for the sales-assistant chat (ADR-024
Decision 2, Decision 4).

The browser never sees or supplies ChatSession.session_key directly — it holds
a signed token wrapping it (django.core.signing), so a tampered or forged token
fails verification before any DB lookup happens. Expiry is NOT encoded in the
token itself (no TimestampSigner max_age) — it is derived from
ChatSession.last_activity_at at request time (settings.CHAT_SESSION_TTL_SECONDS),
because a session should stay alive for as long as the visitor is actively
chatting, not merely for a fixed time since the token was minted.
"""

import secrets

from django.core import signing

_SALT = "chat.session"


def mint_session_key() -> str:
    return secrets.token_urlsafe(32)


def sign_session_token(session_key: str) -> str:
    return signing.dumps(session_key, salt=_SALT)


def verify_session_token(token: str) -> str | None:
    """Return the session_key encoded in `token`, or None if the token is
    missing/malformed/tampered. Never raises — callers treat None as "invalid session"."""
    if not token:
        return None
    try:
        return signing.loads(token, salt=_SALT)
    except signing.BadSignature:
        return None
