"""
POST /_consent/ — the only writer of the `pradize_consent` cookie (ADR-025 D2/D5).

Handler shape, exactly per the ADR:
  1. Validate the JSON payload (action/analytics/marketing/am_objected/source).
  2. Rate-limit (pages.antispam.rate_limit_exceeded — a real UI action, not a
     honeypot target: there is no hidden form field to bait a bot with here).
  3. INSERT ConsentRecord.
  4. Set-Cookie on the 204 response.

Insert-before-cookie ordering (D5/§XIII): if step 3 fails, the handler
returns 500 with NO Set-Cookie header — the banner persists client-side and
nothing is silently granted. CSRF is enforced by the standard
CsrfViewMiddleware (already global, no csrf_exempt here) — unlike the
beacon endpoint, this view has a real consequence (writing a proof row and a
cookie), so it must not be exempted.

No user-supplied string from the payload is ever echoed back into the
response body (204 No Content / plain error bodies only) — nothing here is
ever rendered into HTML (ADR-025 Tests-required #14).

F7 (docs/security/CONSENT_AUDIT.md): every response also refreshes the
`csrftoken` cookie via django.middleware.csrf.get_token() so its 364-day
clock is re-synced with each decision, keeping it ahead of the 180-day
consent cookie across repeated manage-panel re-saves, not just at the first
grant (which ConsentSlotProvider.render()'s F1 fix already covers).
"""

import json
import logging
import uuid

from django.conf import settings
from django.http import HttpResponse, HttpResponseBadRequest
from django.middleware.csrf import get_token
from django.views.decorators.http import require_POST

from pages.antispam import rate_limit_exceeded

from consent.models import ConsentAction, ConsentRecord, ConsentSource
from consent.policy import CONSENT_COOKIE_MAX_AGE, CONSENT_COOKIE_NAME, CONSENT_POLICY_VERSION
from consent.state import serialize_cookie_value

logger = logging.getLogger(__name__)

# Generous upper bound for a payload that is, at most, a handful of short
# fields — anything bigger is not a legitimate consent decision.
_MAX_BODY_BYTES = 4096

_VALID_ACTIONS = {choice.value for choice in ConsentAction}
_VALID_SOURCES = {choice.value for choice in ConsentSource}


def _resolve_lang(request) -> str:
    """
    Best-effort banner language, mirroring storefront_tags.analytics_beacon's
    lang extraction (ADR-008 RequestLocale). Falls back to
    settings.LANGUAGE_CODE when request.locale is absent (e.g. admin/API
    contexts, or a malformed/absent Host resolution).
    """
    locale = getattr(request, "locale", None)
    if locale is not None:
        language = getattr(locale, "language", None)
        if language is not None:
            lang_code = getattr(language, "lang_code", "") or ""
            if lang_code:
                return lang_code[:8]
    return (settings.LANGUAGE_CODE or "en")[:8]


@require_POST
def consent_post(request):
    store = getattr(request, "store", None)
    if store is None:
        return HttpResponseBadRequest("Unknown store.")

    if rate_limit_exceeded(request, "consent_post"):
        return HttpResponse(status=429)

    body = request.body or b""
    if len(body) > _MAX_BODY_BYTES:
        return HttpResponseBadRequest("Payload too large.")

    try:
        payload = json.loads(body.decode("utf-8")) if body else {}
    except (ValueError, UnicodeDecodeError):
        return HttpResponseBadRequest("Invalid JSON payload.")

    if not isinstance(payload, dict):
        return HttpResponseBadRequest("Invalid payload.")

    action = payload.get("action")
    source = payload.get("source")
    analytics = payload.get("analytics")
    marketing = payload.get("marketing")
    am_objected = payload.get("am_objected", False)

    if action not in _VALID_ACTIONS:
        return HttpResponseBadRequest("Invalid or missing 'action'.")
    if source not in _VALID_SOURCES:
        return HttpResponseBadRequest("Invalid or missing 'source'.")
    if not isinstance(analytics, bool) or not isinstance(marketing, bool) or not isinstance(am_objected, bool):
        return HttpResponseBadRequest("'analytics'/'marketing'/'am_objected' must be booleans.")

    # Defense in depth: accept_all/refuse_all/withdraw always force the
    # categories server-side — never trust a client-sent analytics=True on a
    # refuse_all/withdraw action (or vice versa on accept_all). Without this,
    # a crafted payload could write a ConsentRecord labeled "withdraw" that
    # actually records a grant, muddying the proof trail the record exists
    # for (audit F4).
    if action == ConsentAction.ACCEPT_ALL:
        analytics, marketing = True, True
    elif action in (ConsentAction.REFUSE_ALL, ConsentAction.WITHDRAW):
        analytics, marketing = False, False

    lang = _resolve_lang(request)
    user_agent = request.META.get("HTTP_USER_AGENT", "")[:256]
    consent_id = uuid.uuid4()

    # Insert-before-cookie (D5): a failed proof write must never result in a
    # cookie being set (fail-closed — the banner keeps showing).
    try:
        ConsentRecord.objects.for_store(store).create(
            store=store,
            consent_id=consent_id,
            analytics=analytics,
            marketing=marketing,
            am_objected=am_objected,
            action=action,
            policy_version=CONSENT_POLICY_VERSION,
            lang=lang,
            source=source,
            user_agent=user_agent,
        )
    except Exception:
        logger.exception(
            "Failed to write ConsentRecord for store=%s; refusing to set the "
            "consent cookie (fail-closed).",
            store.pk,
        )
        return HttpResponse(status=500)

    # F7 (docs/security/CONSENT_AUDIT.md): refresh csrftoken's clock on every
    # decision, not just on banner render (ConsentSlotProvider.render(), F1).
    # Without this, repeated manage-panel re-saves keep extending the consent
    # cookie's 180-day clock while csrftoken's 364-day clock (set once, at
    # first render) keeps ticking down from that original moment — after
    # enough re-saves the shopper can end up decided but csrftoken-less, and
    # a further withdraw/update POST 403s with no recovery path short of
    # consent re-expiry. Calling get_token() here re-sends csrftoken with a
    # fresh 364-day clock alongside the consent cookie on every decision,
    # restoring "csrftoken always outlives the consent cookie" across
    # repeated re-saves, not just at first grant.
    get_token(request)

    response = HttpResponse(status=204)
    response.set_cookie(
        CONSENT_COOKIE_NAME,
        serialize_cookie_value(
            consent_id.hex,
            analytics=analytics,
            marketing=marketing,
            am_objected=am_objected,
        ),
        max_age=CONSENT_COOKIE_MAX_AGE,
        samesite="Lax",
        # ASSUMPTION: mirrors the codebase's existing convention for
        # deciding cookie Secure-ness — webecom/settings/production.py sets
        # SESSION_COOKIE_SECURE = True statically for the production
        # environment rather than branching on request.is_secure() per
        # request (no other cookie-writing code in this codebase calls
        # request.is_secure()). Reusing that same settings flag keeps this
        # cookie's Secure behavior identical to the session cookie's,
        # environment-for-environment, with no new settings surface.
        secure=settings.SESSION_COOKIE_SECURE,
        httponly=True,
        path="/",
    )
    return response
