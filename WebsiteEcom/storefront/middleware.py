"""
ThemeMiddleware — resolves and caches the active ThemeContext on every storefront
request (ADR-012 D4, D8, TICKET-029 Phase 1).

Runs after LocaleMiddleware (which sets request.store + request.locale).
Skips the same excluded prefixes as LocaleMiddleware so that admin / static /
webhook / beacon paths are not burdened with a DB query.

Preview mode (ADR-012 D8):
  - Entry: storefront URL + ?theme_preview=<signed_token>
    ThemeMiddleware verifies the token (django.core.signing), checks
    store_id binding, sets a __theme_preview cookie carrying the token.
  - Continuation: subsequent requests re-verify from the cookie.
  - Exit: token TTL expiry, or presence of ?theme_preview_exit param.
    Invalid / expired token → normal rendering (log at INFO; never 400/500).
  - Preview responses: Cache-Control: no-store (set here);
    noindex meta + suppressed slots are handled in templates / context.
"""

import logging

from django.conf import settings
from django.core import signing

from storefront.theme import PreviewGrant, resolve_theme

logger = logging.getLogger(__name__)

# Paths that should not incur a theme-resolution DB query.
_EXCLUDED_PREFIXES = (
    "/admin/",
    "/superadmin/",
    "/static/",
    "/media/",
    "/webhooks/",
    "/_analytics/",
)

_PREVIEW_COOKIE = "__theme_preview"
_PREVIEW_PARAM = "theme_preview"
_PREVIEW_EXIT_PARAM = "theme_preview_exit"
_PREVIEW_SALT = "theme-preview"


def _decode_preview_token(token: str, store) -> PreviewGrant | None:
    """
    Verify a signed preview token (ADR-012 D8).

    Returns a PreviewGrant if the token is valid, not expired, and bound to
    the current store.  Returns None on any error (invalid, expired, wrong store).
    Never raises.
    """
    ttl = getattr(settings, "STOREFRONT_PREVIEW_TTL", 7200)
    try:
        payload = signing.loads(token, salt=_PREVIEW_SALT, max_age=ttl)
    except signing.SignatureExpired:
        logger.info("Preview token expired for store pk=%s.", getattr(store, "pk", "?"))
        return None
    except signing.BadSignature:
        logger.info("Invalid preview token received.")
        return None

    if not isinstance(payload, dict):
        logger.info("Preview token payload is not a dict; ignored.")
        return None

    store_id = payload.get("store_id")
    theme_id = payload.get("theme_id")
    if store_id is None or theme_id is None:
        logger.info("Preview token missing store_id or theme_id; ignored.")
        return None

    # Store-binding check: a token for store A is invalid on store B (D8).
    if store is None or store_id != store.pk:
        logger.info(
            "Preview token store_id=%s does not match request store pk=%s.",
            store_id,
            getattr(store, "pk", "?"),
        )
        return None

    return PreviewGrant(
        store_id=store_id,
        theme_id=theme_id,
        customization_id=payload.get("customization_id"),
    )


class ThemeMiddleware:
    """
    Resolves request.theme_ctx (ThemeContext) for storefront paths.

    Placement: immediately after LocaleMiddleware in MIDDLEWARE (settings).
    Effect: every storefront view has request.theme_ctx available; the
    storefront_context context processor exposes it as 'theme_ctx' in templates.
    """

    def __init__(self, get_response):
        self._get_response = get_response

    def __call__(self, request):
        path = request.path_info
        for prefix in _EXCLUDED_PREFIXES:
            if path.startswith(prefix):
                return self._get_response(request)

        store = getattr(request, "store", None)
        preview_grant = self._resolve_preview_grant(request, store)
        request.theme_ctx = resolve_theme(store, preview=preview_grant)

        response = self._get_response(request)

        # Maintain the preview cookie across navigations (D8).
        if preview_grant is not None:
            raw_token = request.GET.get(_PREVIEW_PARAM) or request.COOKIES.get(_PREVIEW_COOKIE, "")
            ttl = getattr(settings, "STOREFRONT_PREVIEW_TTL", 7200)
            response.set_cookie(
                _PREVIEW_COOKIE,
                raw_token,
                max_age=ttl,
                httponly=True,
                samesite="Lax",
                secure=not getattr(settings, "DEBUG", False),
            )
            response["Cache-Control"] = "no-store"
        elif _PREVIEW_EXIT_PARAM in request.GET:
            response.delete_cookie(_PREVIEW_COOKIE)

        return response

    def _resolve_preview_grant(self, request, store) -> PreviewGrant | None:
        """
        Extract and verify a preview token from query string or cookie.

        Query-string param takes precedence over cookie (so a fresh token
        from the admin panel always wins even if an older cookie exists).
        """
        if _PREVIEW_EXIT_PARAM in request.GET:
            return None

        raw_token = request.GET.get(_PREVIEW_PARAM) or request.COOKIES.get(_PREVIEW_COOKIE)
        if not raw_token:
            return None

        return _decode_preview_token(raw_token, store)
