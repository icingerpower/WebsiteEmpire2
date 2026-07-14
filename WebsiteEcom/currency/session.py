"""
Shopper currency selection + session persistence (ADR-023 §4).

SESSION_KEY is written by EXACTLY ONE view: currency.views.set_display_currency
(POST /currency/set/, registered in storefront/urls.py as
'storefront:set-display-currency'). No middleware ever writes this key — see
that view's docstring and ADR-023 §4 for why (the sf_lang unconditional-write
bug class, ADR-015 addendum).

resolve_display_currency() is the single read path, called from the storefront
context processor and the currency template tags — nowhere else.
"""

SESSION_KEY = "display_currency"


def resolve_display_currency(request, store) -> str:
    """
    Resolve the shopper's display currency for the given store (ADR-023 §4).

    Priority:
      1. session['display_currency'], IF it is still enabled for THIS store
         (a store admin may have disabled a currency after the shopper picked
         it, or the shopper's session may be carrying a choice made at a
         different store on the same domain/cookie).
      2. store.default_currency (identity — no conversion, never errors,
         never geo-guesses).

    Never raises — a missing/broken session or an unknown store falls back to
    store.default_currency exactly like a shopper who never picked anything.
    Tolerates a session-like test double that only implements a subset of the
    real SessionBase interface (e.g. exposing session_key but not .get()) —
    mirrors this module's sibling _get_mini_cart_context's defensive
    getattr() pattern in storefront/context_processors.py.
    """
    from currency.models import StoreCurrencySetting

    session = getattr(request, "session", None)
    session_get = getattr(session, "get", None)
    code = session_get(SESSION_KEY) if callable(session_get) else None

    if code:
        if code == store.default_currency:
            return code
        is_enabled = (
            StoreCurrencySetting.objects.for_store(store)
            .filter(currency__code=code, currency__is_active=True, is_enabled=True)
            .exists()
        )
        if is_enabled:
            return code

    return store.default_currency
