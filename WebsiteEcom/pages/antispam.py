"""
Minimal anti-spam primitives for public storefront forms (ADR-018 D3).

No rate-limiting infrastructure exists elsewhere in the codebase (verified: analytics
ingest has none; no third-party throttle library in requirements.txt) — this is the
cache-based primitive, built with no new dependency.

One function, four call sites (design-pattern-ideas.txt §XV-4): contact POST, store-level
quotation POST, and retrofitted onto the two existing product-form endpoints
(notify_me_view, quotation_request_view in storefront/views_product_forms.py).

Client IP: REMOTE_ADDR only. The deployment must terminate proxies correctly (note for
the release checklist) — X-Forwarded-For is never parsed in application code (spoofable
by the client unless the edge proxy strips it).

LocMem (dev) is per-process best-effort under multiple workers; Redis (production) is
atomic. This is accepted as best-effort spam control, not a security boundary.
"""

from django.core.cache import cache

# Defaults: 5 submissions / IP / store / form-scope / hour.
DEFAULT_RATE_LIMIT = 5
DEFAULT_WINDOW_SECONDS = 3600

# Security audit F2: TextField message bodies (ContactMessage.message,
# QuotationRequest.message) have no DB-level max_length, so nothing stops an
# unbounded POST body from being persisted and then fanned out verbatim into
# notification emails. Capped in the view before .objects.create() — shared here
# so storefront/views_pages.py and storefront/views_product_forms.py can never
# drift to different caps for the same underlying abuse concern.
PUBLIC_FORM_MESSAGE_MAX_LENGTH = 5000

# Security audit F4: per-store, IP-independent daily budget on contact-notification
# email sends. rate_limit_exceeded() above is per (scope, store, IP) — an attacker
# spread across many source IPs is not slowed by it at all, and each submission
# already fans out to every full-access StoreEmployee (email-bomb amplifier).
# This is a SEPARATE counter, keyed on store only, incremented once per
# notification attempt (i.e. once per contact-form submission, not once per
# recipient — the budget bounds submissions that trigger a fan-out, which is
# where the amplification happens). Beyond the cap, the ContactMessage row is
# still saved (audit trail preserved); only the notification send is skipped.
CONTACT_NOTIFICATION_DAILY_CAP = 50
_CONTACT_NOTIFICATION_WINDOW_SECONDS = 86400


def _client_ip(request) -> str:
    return request.META.get("REMOTE_ADDR", "") or "unknown"


def rate_limit_exceeded(
    request,
    scope: str,
    *,
    limit: int = DEFAULT_RATE_LIMIT,
    window_seconds: int = DEFAULT_WINDOW_SECONDS,
) -> bool:
    """
    Return True when the caller has exceeded `limit` submissions for `scope` within
    `window_seconds`, keyed on (scope, store, client IP).

    cache.add() seeds the counter at 0 only if absent (first hit in the window sets
    the TTL); cache.incr() is atomic on Redis (production). LocMem in dev is
    per-process best-effort, which is acceptable for a spam gate (see module docstring).
    """
    store = getattr(request, "store", None)
    store_key = getattr(store, "pk", "no-store")
    key = f"antispam:{scope}:{store_key}:{_client_ip(request)}"

    cache.add(key, 0, window_seconds)
    try:
        count = cache.incr(key)
    except ValueError:
        # Key expired between add() and incr() under a race — treat as first hit.
        cache.add(key, 1, window_seconds)
        count = 1

    return count > limit


def honeypot_triggered(request, field: str = "hp_company") -> bool:
    """
    Return True when the hidden honeypot field is non-empty (a bot filled it in).

    Callers must return the NORMAL success response on True (silent drop + info log)
    — never reveal detection to the bot.

    Field name (security audit F7): NOT 'website'/'url'/'email2' — those are the
    textbook honeypot names that sophisticated spam bots explicitly skip when
    filling forms. 'hp_company' does not appear on public honeypot-name skip-lists
    at the time of this fix, so it is filled by naive field-filling bots while
    still being meaningless to a human (hidden + off-tab-order in the templates).
    """
    return bool(request.POST.get(field, "").strip())


def contact_notification_budget_exceeded(store) -> bool:
    """
    Return True once CONTACT_NOTIFICATION_DAILY_CAP notification attempts have
    already been counted for `store` today (security audit F4).

    Independent of rate_limit_exceeded()'s per-IP key — this counter is keyed on
    store only, so it still caps the fan-out even when submissions arrive from many
    distinct IPs. Call exactly once per submission, right before attempting to
    notify employees (storefront/views_pages.py _notify_full_access_employees).
    """
    store_key = getattr(store, "pk", "no-store")
    key = f"antispam:contact_notify_budget:{store_key}"

    cache.add(key, 0, _CONTACT_NOTIFICATION_WINDOW_SECONDS)
    try:
        count = cache.incr(key)
    except ValueError:
        # Key expired between add() and incr() under a race — treat as first hit.
        cache.add(key, 1, _CONTACT_NOTIFICATION_WINDOW_SECONDS)
        count = 1

    return count > CONTACT_NOTIFICATION_DAILY_CAP
