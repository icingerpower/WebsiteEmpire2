"""
Feed serving view (ADR-026 D2) — token check + FileResponse, nothing else.

Generation NEVER happens here — that is feeds/tasks.py's job exclusively
(FeedProvider.run() docstring: "called by tasks, never by views"). This view
only: resolves (store, domain, language) from request.locale (ML-004 single
resolver — set by stores.middleware.LocaleMiddleware), checks the per-store
secret token via hmac.compare_digest (constant-time, AC-213), and streams the
pre-generated file.

Multi-tenant isolation (AC-213 cross-tenant test) is the CONJUNCTION of two
things, both already in place before this view runs any of its own logic:
  1. Host -> store resolution (request.locale.store) — a request can only
     ever resolve to the ONE store that owns the requested domain.
  2. Every DB lookup below is .for_store(store) scoped.
A token minted for store A therefore can never open store B's feed: even if
somehow presented on store B's domain, it is compared only against store B's
own StoreFeedToken.token.
"""

from __future__ import annotations

import hmac
import os

from django.http import FileResponse, HttpResponse
from django.views.decorators.http import require_GET

from feeds.models import FeedTarget, FeedTargetStatus, StoreFeedToken
from feeds.registry import get_provider
from stores.models import StoreLanguage


@require_GET
def feed_view(request, provider_key: str, country_code: str, lang_code: str):
    """
    Serve /feeds/<provider_key>/<country_code>-<lang_code>.xml.

    Order of checks (AC-213): token FIRST — wrong/missing token is always a
    bare 403 with an empty body, before any lookup that could otherwise leak
    "this provider/country/lang combination exists or not" via a different
    status code.
    """
    locale = getattr(request, "locale", None)
    if locale is None:
        return HttpResponse(status=404)

    store = locale.store

    provided_token = request.GET.get("token", "")
    store_token = StoreFeedToken.objects.for_store(store).first()
    # Compare utf-8 bytes, not str: hmac.compare_digest raises TypeError on
    # non-ASCII strings ("comparing strings with non-ASCII characters is not
    # supported"), which would otherwise surface as an unhandled 500 instead
    # of a plain 403 for a garbage/non-ASCII ?token= value (security audit
    # F1). The stored token is always ASCII (secrets.token_urlsafe), so a
    # non-ASCII input simply compares unequal once both sides are bytes.
    if (
        not provided_token
        or store_token is None
        or not hmac.compare_digest(
            provided_token.encode("utf-8"), store_token.token.encode("utf-8")
        )
    ):
        return HttpResponse(status=403)

    if get_provider(provider_key) is None:
        return HttpResponse(status=404)

    try:
        store_language = StoreLanguage.objects.select_related("domain").get(
            domain=locale.domain, lang_code=lang_code, is_enabled=True
        )
    except StoreLanguage.DoesNotExist:
        return HttpResponse(status=404)

    try:
        target = FeedTarget.objects.for_store(store).get(
            provider=provider_key,
            store_language=store_language,
            country_code=country_code.upper(),
        )
    except FeedTarget.DoesNotExist:
        return HttpResponse(status=404)

    # PENDING (never generated) or ERROR with no file yet -> 503, beat will
    # catch up; an ERROR target with a previous good file keeps serving it
    # (ADR-026 D3 — a bad generation never blanks a live ad catalog).
    if (
        target.status == FeedTargetStatus.PENDING
        or not target.file_path
        or not os.path.exists(target.file_path)
    ):
        return HttpResponse(status=503, headers={"Retry-After": "1800"})

    return FileResponse(
        open(target.file_path, "rb"), content_type="application/xml; charset=utf-8"
    )
