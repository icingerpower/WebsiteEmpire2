"""
{% consent_manage_button %} — the footer "Cookie preferences" re-open control
(ADR-025 D5, TICKET-049).

Mirrors pages_tags.static_page_links: an inclusion_tag that reads request.store
and returns a small context dict, rather than a context processor (no existing
context-processor precedent in this codebase queries a per-store settings row —
storefront_context only exposes theme_ctx, ADR-012 D4).

Suppressed (renders nothing) in two cases, both deliberate:
  - consent is disabled for the store (ConsentSettings.is_enabled=False) — there
    is no panel to reopen.
  - theme preview (theme_ctx.is_preview) — the consent slot itself renders
    nothing under preview (PREVIEW_SUPPRESSED_SLOTS, storefront_tags.py), so a
    visible "Cookie preferences" button with no panel behind it would be a
    dead control (ASSUMPTION: not explicitly stated in ADR-025 D5, but follows
    directly from D5's "unhides the ALREADY-RENDERED panel markup" contract —
    there is nothing to unhide in preview).
"""

from django import template

register = template.Library()


@register.inclusion_tag("storefront/partials/consent_manage_button.html", takes_context=True)
def consent_manage_button(context):
    """
    Usage in templates::

        {% load consent_tags %}
        {% consent_manage_button %}
    """
    request = context.get("request")
    store = getattr(request, "store", None) if request else None
    if store is None:
        return {"enabled": False}

    theme_ctx = context.get("theme_ctx")
    if getattr(theme_ctx, "is_preview", False):
        return {"enabled": False}

    from consent.models import get_or_create_consent_settings

    return {"enabled": get_or_create_consent_settings(store).is_enabled}
