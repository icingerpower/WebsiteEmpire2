"""
Token issuance and consumption for CampaignSessionToken (ADR-011 Q2).

issue_token(session, purpose, ttl_seconds) -> raw:
    Generates a cryptographically random raw token, stores its SHA-256 hash in
    CampaignSessionToken, and returns the raw value.  The raw value is returned
    once and never persisted.  Never log the return value of this function.

consume_token(raw, purpose) -> CampaignSessionToken:
    Hashes the raw value and performs a constant-time lookup + atomic single-use
    claim via conditional UPDATE (SET used_at=NOW() WHERE used_at IS NULL AND
    expires_at > NOW()).  Returns the token row if successful.
    Raises CampaignSessionToken.DoesNotExist on any failure (invalid, expired,
    already consumed, purpose mismatch).

Design invariants (ADR-011 Q2 / ADR-007 §5):
- Raw tokens are NEVER stored, logged, sent to analytics, or included in
  DecisionLog rows.  Only the SHA-256 hex digest is persisted.
- consume_token uses a conditional UPDATE for single-use atomicity — two
  concurrent callers with the same raw token will each compute the same hash;
  only one UPDATE will have affected_rows==1.
- Purpose mismatch is rejected silently (treats as DoesNotExist) so a leaked
  thankyou-view email link cannot be used to trigger a charge.
"""

import hashlib
import secrets
from datetime import timedelta

from django.utils import timezone


def _hash_token(raw: str) -> str:
    """Return the SHA-256 hex digest of a raw token string."""
    return hashlib.sha256(raw.encode()).hexdigest()


def issue_token(
    session,
    purpose: str,
    ttl_seconds: int,
    offered_amount_cents: int | None = None,
) -> str:
    """
    Create a CampaignSessionToken and return the raw token value.

    The raw value is generated with secrets.token_urlsafe (256 bits of entropy)
    and returned to the caller.  Only the SHA-256 hash is persisted.

    Arguments:
        session:              CampaignSession to link the token to.
        purpose:              TokenPurpose value ('thankyou-view' or 'upsell-act').
        ttl_seconds:          Seconds until the token expires (e.g. 72*3600 for thankyou-view,
                              capture_window_minutes*60 for upsell-act).
        offered_amount_cents: Amount shown to the customer at token-issue time (M3 guard).
                              None = storewide_discount step or no charge; guard skipped.
                              When set, accept_upsell validates the current step amount
                              against this snapshot before Phase A begins.

    Returns the raw token string.  Never log this value.
    """
    from campaigns.models import CampaignSessionToken

    raw = secrets.token_urlsafe(32)  # 256 bits of entropy
    token_hash = _hash_token(raw)
    expires_at = timezone.now() + timedelta(seconds=ttl_seconds)

    CampaignSessionToken.objects.create(
        store=session.store,
        campaign_session=session,
        purpose=purpose,
        token_hash=token_hash,
        expires_at=expires_at,
        offered_amount_cents=offered_amount_cents,
    )
    return raw


def consume_token(raw: str, purpose: str):
    """
    Validate and atomically claim a single-use token.

    Computes SHA-256(raw) and performs a conditional UPDATE:
        UPDATE ... SET used_at=NOW() WHERE token_hash=<hash>
            AND purpose=<purpose>
            AND used_at IS NULL
            AND expires_at > NOW()

    Returns the CampaignSessionToken instance on success.
    Raises CampaignSessionToken.DoesNotExist if the token is:
    - not found (invalid raw value)
    - wrong purpose (prevents purpose escalation)
    - already consumed (used_at IS NOT NULL)
    - expired (expires_at <= NOW())

    Concurrent callers with the same raw token each compute the same hash;
    only one UPDATE has affected_rows==1 — the other raises DoesNotExist.
    """
    from campaigns.models import CampaignSessionToken

    token_hash = _hash_token(raw)
    now = timezone.now()

    # Atomic single-use claim.
    updated = (
        CampaignSessionToken.objects.cross_store_unsafe()
        .filter(
            token_hash=token_hash,
            purpose=purpose,
            used_at__isnull=True,
            expires_at__gt=now,
        )
        .update(used_at=now)
    )

    if updated != 1:
        raise CampaignSessionToken.DoesNotExist(
            "Token not found, expired, already consumed, or purpose mismatch."
        )

    return (
        CampaignSessionToken.objects.cross_store_unsafe()
        .select_related("campaign_session", "campaign_session__order",
                        "campaign_session__current_step", "store")
        .get(token_hash=token_hash)
    )
