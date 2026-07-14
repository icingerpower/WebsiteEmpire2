"""
Currency selection endpoint (ADR-023 §4).

POST /currency/set/ (view name 'storefront:set-display-currency', registered
in storefront/urls.py before the <path:slug> catch-all) is the ONLY code path
that writes request.session['display_currency'] — never middleware. See
currency/session.py's module docstring and ADR-023 §4 for the sf_lang
unconditional-write bug class (ADR-015 addendum) this deliberately avoids by
construction.

Body: {currency: <code>, next: <redirect path>}.
- Validates `code` against the store's enabled currencies (or the store's own
  default_currency, which needs no StoreCurrencySetting row — ADR-023 §2).
- On success: writes the session key, 302s to `next` (falls back to '/' if
  `next` is missing/unsafe — standard open-redirect guard, same as Django's
  own LoginView).
- On failure (unknown/disabled code): redirects back to `next` WITHOUT
  changing the session — never errors, per the never-break-the-page-for-
  money-adjacent-features principle running through ADR-023.
"""

from django.http import HttpResponseRedirect
from django.utils.http import url_has_allowed_host_and_scheme
from django.views.decorators.http import require_POST

from currency.models import StoreCurrencySetting
from currency.session import SESSION_KEY


def _safe_next(request, raw_next: str) -> str:
    """Return raw_next if it is a safe, same-host redirect target, else '/'."""
    if raw_next and url_has_allowed_host_and_scheme(
        raw_next, allowed_hosts={request.get_host()}, require_https=request.is_secure()
    ):
        return raw_next
    return "/"


@require_POST
def set_display_currency(request):
    next_url = _safe_next(request, request.POST.get("next", ""))
    code = (request.POST.get("currency") or "").strip()
    store = getattr(request, "store", None)

    if store is not None and code:
        is_valid = code == store.default_currency or (
            StoreCurrencySetting.objects.for_store(store)
            .filter(currency__code=code, currency__is_active=True, is_enabled=True)
            .exists()
        )
        if is_valid:
            request.session[SESSION_KEY] = code

    return HttpResponseRedirect(next_url)
