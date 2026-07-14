"""
Minimal PayPal Orders API v2 HTTP client.
Handles OAuth2 token refresh automatically.
No SDK dependency — uses requests (already a project dependency).

Thread-safe: token is stored in Django's cache (not instance state), so
multiple workers serving the same client_id share a single token.
"""

import logging

import requests
from django.core.cache import cache

logger = logging.getLogger('payments.paypal')

PAYPAL_LIVE_BASE = 'https://api-m.paypal.com'
PAYPAL_SANDBOX_BASE = 'https://api-m.sandbox.paypal.com'


class PayPalClient:
    """
    Thread-safe PayPal API client with token caching.

    Credentials:
    - client_id: PayPal app Client ID (plain text, from ProcessorAccount.client_id).
    - client_secret: PayPal app Secret (encrypted at rest, from ProcessorAccount.api_secret).

    OAuth2 tokens are cached per client_id prefix using Django's cache backend.
    The cache key is intentionally short (first 8 chars of client_id) — uniqueness
    is sufficient since client_id is typically a 80-char alphanumeric string.
    Tokens are cached for expires_in - 30 seconds to avoid using an expired token
    delivered in the last second of validity.
    """

    def __init__(self, client_id: str, client_secret: str, sandbox: bool = True) -> None:
        self._client_id = client_id
        self._client_secret = client_secret
        self._base_url = PAYPAL_SANDBOX_BASE if sandbox else PAYPAL_LIVE_BASE
        # Cache key uses first 8 chars of client_id for brevity; unique in practice.
        self._cache_key = f'paypal_token_{client_id[:8]}'

    def _get_access_token(self) -> str:
        """Return a cached access token, fetching a fresh one from PayPal if expired."""
        token = cache.get(self._cache_key)
        if token:
            return token
        resp = requests.post(
            f'{self._base_url}/v1/oauth2/token',
            data={'grant_type': 'client_credentials'},
            auth=(self._client_id, self._client_secret),
            timeout=10,
        )
        resp.raise_for_status()
        data = resp.json()
        token = data['access_token']
        expires_in = data.get('expires_in', 3600)
        # Subtract 30 s to avoid presenting an about-to-expire token.
        cache.set(self._cache_key, token, timeout=expires_in - 30)
        return token

    def _headers(self) -> dict:
        return {
            'Authorization': f'Bearer {self._get_access_token()}',
            'Content-Type': 'application/json',
        }

    def post(self, path: str, payload: dict, extra_headers: dict | None = None) -> dict:
        """POST to a PayPal API path with automatic token injection.

        extra_headers: optional dict merged into the request headers after the
        standard Authorization / Content-Type headers (e.g. PayPal-Request-Id
        for idempotency).

        204/empty-body tolerance (ADR-020): the authorize and void endpoints
        (/v2/checkout/orders/{id}/authorize on some paths, and always
        /v2/payments/authorizations/{id}/void) return 204 No Content on success.
        Calling resp.json() on an empty body raises requests.exceptions.JSONDecodeError
        on a *successful* call — that must not be mistaken for a failure, so an
        empty/204 response returns {} instead of attempting to decode it.
        """
        headers = self._headers()
        if extra_headers:
            headers.update(extra_headers)
        resp = requests.post(
            f'{self._base_url}{path}',
            json=payload,
            headers=headers,
            timeout=15,
        )
        resp.raise_for_status()
        if resp.status_code == 204 or not resp.content:
            return {}
        return resp.json()

    def get(self, path: str) -> dict:
        """GET from a PayPal API path with automatic token injection."""
        resp = requests.get(
            f'{self._base_url}{path}',
            headers=self._headers(),
            timeout=15,
        )
        resp.raise_for_status()
        return resp.json()
