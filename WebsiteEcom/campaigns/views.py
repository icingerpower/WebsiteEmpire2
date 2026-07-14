"""
Upsell accept/decline HTTP endpoints (ADR-011 / T028).

POST /campaigns/upsell/accept/  — upsell_accept_view
POST /campaigns/upsell/decline/ — upsell_decline_view

Both views:
- Require an authenticated store session (request.store set by HostResolutionMiddleware).
- Are CSRF-exempt: authentication is via the single-use upsell-act token in the JSON body.
- Accept JSON body: {"token": "<raw_token_value>"}
- Log only the SHA-256 hash of the token — never the raw value (ADR-011 / ADR-007 §5).
- Return JSON responses.

HTTP status codes:
    200  — success
    400  — malformed request (missing token key, non-JSON body)
    403  — rate limit exceeded (UpsellRateLimitError)
    410  — upsell unavailable (UpsellUnavailableError, expired window, etc.)
    422  — invalid / expired / consumed token (DoesNotExist)
"""

import hashlib
import json
import logging

from django.http import JsonResponse
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_POST

from campaigns.exceptions import UpsellRateLimitError, UpsellUnavailableError
from campaigns.upsell_service import accept_upsell, decline_upsell

logger = logging.getLogger("campaigns.views")


def _hash_for_log(raw_token: str) -> str:
    """Return a truncated SHA-256 prefix for safe logging (first 12 hex chars)."""
    return hashlib.sha256(raw_token.encode()).hexdigest()[:12]


@csrf_exempt
@require_POST
def upsell_accept_view(request):
    """
    POST /campaigns/upsell/accept/

    Accepts the current upsell offer for the session identified by the token.

    Request body (JSON): {"token": "<raw_upsell_act_token>"}

    Success:  HTTP 200, {"status": "accepted", "order_charge_id": <int>}
    Rate limit: HTTP 403, {"error": "rate_limit"}
    Unavailable: HTTP 410, {"error": "unavailable"}
    Invalid token: HTTP 422, {"error": "invalid_token"}
    """
    store = getattr(request, "store", None)
    if store is None:
        return JsonResponse({"error": "store_not_resolved"}, status=400)

    try:
        body = json.loads(request.body)
    except (json.JSONDecodeError, ValueError):
        return JsonResponse({"error": "invalid_json"}, status=400)

    if not isinstance(body, dict):
        return JsonResponse({"error": "invalid_body"}, status=400)

    raw_token = body.get("token", "")
    if not isinstance(raw_token, str) or len(raw_token) > 256:
        return JsonResponse({"error": "invalid_token"}, status=422)
    if not raw_token:
        return JsonResponse({"error": "missing_token"}, status=400)

    token_hash_prefix = _hash_for_log(raw_token)
    logger.info(
        "upsell.accept: attempt for store=%s token_hash_prefix=%s",
        store.pk, token_hash_prefix,
    )

    from campaigns.models import CampaignSessionToken

    try:
        result = accept_upsell(raw_token, store)
    except UpsellRateLimitError:
        logger.warning(
            "upsell.accept: rate limit for store=%s token_hash_prefix=%s",
            store.pk, token_hash_prefix,
        )
        return JsonResponse({"error": "rate_limit"}, status=403)
    except UpsellUnavailableError as exc:
        logger.info(
            "upsell.accept: unavailable for store=%s token_hash_prefix=%s: %s",
            store.pk, token_hash_prefix, exc,
        )
        return JsonResponse({"error": "unavailable"}, status=410)
    except CampaignSessionToken.DoesNotExist:
        logger.warning(
            "upsell.accept: invalid token for store=%s token_hash_prefix=%s",
            store.pk, token_hash_prefix,
        )
        return JsonResponse({"error": "invalid_token"}, status=422)
    except Exception:
        logger.exception(
            "upsell_accept_view: unexpected error for store=%s token_hash_prefix=%s",
            store.pk, token_hash_prefix,
        )
        return JsonResponse({"error": "server_error"}, status=500)

    return JsonResponse(result, status=200)


@csrf_exempt
@require_POST
def upsell_decline_view(request):
    """
    POST /campaigns/upsell/decline/

    Declines the current upsell offer for the session identified by the token.

    Request body (JSON): {"token": "<raw_upsell_act_token>"}

    Success:  HTTP 200, {"status": "declined"}
    Invalid token: HTTP 422, {"error": "invalid_token"}
    """
    store = getattr(request, "store", None)
    if store is None:
        return JsonResponse({"error": "store_not_resolved"}, status=400)

    try:
        body = json.loads(request.body)
    except (json.JSONDecodeError, ValueError):
        return JsonResponse({"error": "invalid_json"}, status=400)

    if not isinstance(body, dict):
        return JsonResponse({"error": "invalid_body"}, status=400)

    raw_token = body.get("token", "")
    if not isinstance(raw_token, str) or len(raw_token) > 256:
        return JsonResponse({"error": "invalid_token"}, status=422)
    if not raw_token:
        return JsonResponse({"error": "missing_token"}, status=400)

    token_hash_prefix = _hash_for_log(raw_token)
    logger.info(
        "upsell.decline: attempt for store=%s token_hash_prefix=%s",
        store.pk, token_hash_prefix,
    )

    from campaigns.models import CampaignSessionToken

    try:
        result = decline_upsell(raw_token, store)
    except CampaignSessionToken.DoesNotExist:
        logger.warning(
            "upsell.decline: invalid token for store=%s token_hash_prefix=%s",
            store.pk, token_hash_prefix,
        )
        return JsonResponse({"error": "invalid_token"}, status=422)
    except UpsellUnavailableError as exc:
        logger.info(
            "upsell.decline: unavailable for store=%s token_hash_prefix=%s: %s",
            store.pk, token_hash_prefix, exc,
        )
        return JsonResponse({"error": str(exc)}, status=410)
    except Exception:
        logger.exception(
            "upsell.decline: unexpected error for store=%s token_hash_prefix=%s",
            store.pk, token_hash_prefix,
        )
        return JsonResponse({"error": "server_error"}, status=500)

    return JsonResponse(result, status=200)
