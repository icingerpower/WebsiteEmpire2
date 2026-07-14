"""
Storefront template tags (ADR-012, D3, D9, TICKET-029 Phase 1).

{% theme_partial "name" %}
    Renders a theme-overridable partial.  Looks up templates in the order:
      1. <theme_key>/partials/<name>.html  (theme-specific override)
      2. storefront/partials/<name>.html   (shared fallback)

    Only names listed in THEME_OVERRIDABLE_PARTIALS are accepted — any other
    name raises TemplateSyntaxError at render time (§XV-1, loud not silent).

    The whitelist is a frozen constant; adding names requires an ADR amendment
    and a CI check update.

{% render_slot "slot_name" %}
    Renders all enabled SlotProviders registered for slot_name via slots.register().

    Always emits <div data-slot="slot_name">…</div> even when empty — callers
    (T035 popup JS, tests) can rely on the wrapper being present (TH-045).

    In preview mode, slots in PREVIEW_SUPPRESSED_SLOTS render empty (no provider
    output) but the wrapper is still emitted (D8).
"""

import json
import logging

from django import template
from django.template.loader import select_template
from django.utils.safestring import mark_safe

logger = logging.getLogger(__name__)

register = template.Library()


# ---------------------------------------------------------------------------
# Whitelist (ADR-012 D3) — additions require an ADR amendment + CI check update
# ---------------------------------------------------------------------------

THEME_OVERRIDABLE_PARTIALS = frozenset(
    {
        "hero",          # home hero composition
        "theme_band",    # home theme-specific band (B2B: benefits; Fashion: lookbook; General: trust)
        "product_badge", # sale/new badge treatment on product cards
        "footer_band",   # decorative footer strip above the functional footer
    }
)

# Slot categories suppressed under preview mode (ADR-012 D8).
#
# "chat_launcher" is included here (added 2026-07-11, ADR-024 addendum LOW
# note) so preview suppression does not depend solely on
# ChatWidgetSlotProvider.render() checking theme_ctx.is_preview itself —
# render_slot() now skips calling the provider at all during preview.
# ChatWidgetSlotProvider.render() keeps its own theme_ctx.is_preview check too
# (defense in depth: it must still refuse to render if ever invoked outside
# render_slot()); both layers are tested independently.
PREVIEW_SUPPRESSED_SLOTS = frozenset({"consent", "pixels", "overlay", "social_proof", "chat_launcher"})


# ---------------------------------------------------------------------------
# {% theme_partial "name" %}
# ---------------------------------------------------------------------------


@register.simple_tag(takes_context=True)
def theme_partial(context, name):
    """
    Render a theme-overridable partial (ADR-012 D3).

    Usage:
        {% load storefront_tags %}
        {% theme_partial "hero" %}

    The partial name must be in THEME_OVERRIDABLE_PARTIALS — unknown names raise
    TemplateSyntaxError (§XV-1: loud, not fallback).

    Raises:
        TemplateSyntaxError: if name is not in THEME_OVERRIDABLE_PARTIALS.
    """
    if name not in THEME_OVERRIDABLE_PARTIALS:
        raise template.TemplateSyntaxError(
            "theme_partial received unknown partial name {!r}. "
            "Allowed names: {}. "
            "Adding a name requires an ADR-012 amendment.".format(
                name, sorted(THEME_OVERRIDABLE_PARTIALS)
            )
        )

    theme_ctx = context.get("theme_ctx")
    key = getattr(theme_ctx, "key", "general")

    tmpl = select_template(
        [
            f"{key}/partials/{name}.html",
            f"storefront/partials/{name}.html",
        ]
    )
    return mark_safe(tmpl.render(context.flatten()))


# ---------------------------------------------------------------------------
# {% render_slot "slot_name" %}
# ---------------------------------------------------------------------------


@register.simple_tag(takes_context=True)
def render_slot(context, slot_name):
    """
    Render all enabled SlotProviders for the given slot (ADR-012 D9).

    Usage:
        {% load storefront_tags %}
        {% render_slot "pixels" %}

    Always emits <div data-slot="<slot_name>">…</div> so downstream JS and
    tests can rely on the wrapper being present (TH-045).

    In preview mode, PREVIEW_SUPPRESSED_SLOTS render no provider output but
    still emit the wrapper (D8).
    """
    from storefront.slots import get_providers

    request = context.get("request")
    store = getattr(request, "store", None)
    theme_ctx = context.get("theme_ctx")
    is_preview = getattr(theme_ctx, "is_preview", False)

    parts = [f'<div data-slot="{slot_name}">']

    if not (is_preview and slot_name in PREVIEW_SUPPRESSED_SLOTS):
        providers = get_providers(slot_name)
        for provider in providers:
            try:
                if store is not None and provider.is_enabled(store):
                    rendered = provider.render(context)
                    if rendered:
                        parts.append(rendered)
            except Exception:
                logger.exception(
                    "SlotProvider %r raised during render_slot(%r); skipping.",
                    getattr(provider, "key", repr(provider)),
                    slot_name,
                )

    parts.append("</div>")
    return mark_safe("".join(parts))


# ---------------------------------------------------------------------------
# {% analytics_beacon %} — inline page-view + add-to-cart beacon (TH-043)
# ---------------------------------------------------------------------------

# Beacon endpoint (analytics app, mounted at /_analytics/ in webecom/urls.py).
_BEACON_URL = "/_analytics/beacon/"


def _resolve_consent(request):
    """
    ADR-025 D4: resolve the shopper's consent state for the beacon's own
    objection toggle. Imported locally + wrapped in try/except ImportError
    per the ADR-025 "Rollback strategy" fail-closed shim — if the `consent`
    app/package is ever removed entirely, the beacon must keep behaving
    exactly as it does today (exempt-by-default, never objected), so the
    fallback stand-in reports am_objected=False and allows("analytics")=False
    (matching CATEGORY_NECESSARY-only semantics; only relevant if a store
    also sets beacon_requires_consent, which itself becomes unreachable when
    the app is gone — _beacon_requires_consent() below also fails closed to
    False in that case).
    """
    try:
        from consent.state import get_consent
    except ImportError:
        from dataclasses import dataclass

        @dataclass(frozen=True)
        class _FailClosedConsent:
            am_objected: bool = False

            def allows(self, category: str) -> bool:
                return category == "necessary"

        return _FailClosedConsent()
    return get_consent(request)


def _beacon_requires_consent(store) -> bool:
    """
    ADR-025 D4: per-store conservative override demoting the beacon into the
    "analytics" consent category (ConsentSettings.beacon_requires_consent,
    default False). Fails closed to False (today's unchanged behaviour) if
    the `consent` app/package is absent.
    """
    try:
        from consent.models import get_or_create_consent_settings
    except ImportError:
        return False
    return get_or_create_consent_settings(store).beacon_requires_consent


@register.simple_tag(takes_context=True)
def analytics_beacon(context):
    """
    Render an inline <script> that fires a page_view beacon on every page load.

    Also fires an add_to_cart beacon on product pages when ?added=1 is present
    in the query string (set by the cart-add redirect to signal a successful add).

    Security: all values injected into the <script> block are server-side
    constants (integers and model-validated ASCII strings), serialized via
    json.dumps.  No user-submitted data is ever placed in this block.

    The beacon endpoint (/_analytics/beacon/) is csrf_exempt; the payload is
    JSON, not a form POST.  No CSRF token is needed.

    Suppressed (returns empty string) when request.store is None — avoids
    sending store_id=None events from the admin / error-handler paths.

    ADR-025 D4 (CNIL audience-measurement exemption): also suppressed when
    the shopper has explicitly objected (get_consent(request).am_objected —
    the panel's "Audience measurement" toggle, defaulted ON) or when this
    store's ConsentSettings.beacon_requires_consent=True and "analytics" is
    not currently granted. The normal case (undecided, no objection) still
    renders the beacon — that is the entire point of the exemption: it must
    keep measuring while a shopper has not yet decided, unlike the two
    consent-requiring pixel categories.
    """
    request = context.get("request")
    store = getattr(request, "store", None)
    if store is None:
        return mark_safe("")

    consent = _resolve_consent(request)
    if consent.am_objected:
        return mark_safe("")
    if _beacon_requires_consent(store) and not consent.allows("analytics"):
        return mark_safe("")

    # page_type: set by each view ("home", "product", "collection", etc.).
    # Defaults to "page" when the view omits it (defensive — should not happen).
    page_type = str(context.get("page_type") or "page")

    # object_id: PK of the content object (product, collection, …) or empty string.
    content_object = context.get("content_object")
    object_id = str(int(content_object.pk)) if content_object is not None else ""

    # lang_code from the resolved locale (ADR-008 RequestLocale).
    lang = ""
    locale = getattr(request, "locale", None)
    if locale is not None:
        language = getattr(locale, "language", None)
        if language is not None:
            lang = str(getattr(language, "lang_code", "") or "")

    store_id = int(store.pk)  # Ensure integer before embedding in script

    # page_version_id (TICKET-042, ADR-028 §6): set only on a page-version
    # render — product_page passes page_version=None on the primary page.
    # content_object stays the PRODUCT even on a version render (ADR-028 §6:
    # "product_id (soft column, as today)"), so object_id above is unaffected;
    # this is purely additive to properties.
    page_version = context.get("page_version")
    page_version_id = int(page_version.pk) if page_version is not None else None

    # product_id (TICKET-042, ADR-028 §6): a real int (not the display string
    # `object_id`) — populated on product pages so the nightly aggregation job
    # (analytics/management/commands/aggregate_metrics.py) can key
    # page_version_views by product_id, previously always NULL for page_view
    # events (ASSUMPTION, LOW: this is the first feature to need a
    # product-keyed aggregation).
    product_id_int = (
        int(content_object.pk)
        if (content_object is not None and page_type == "product")
        else None
    )

    page_view_properties = {
        "page_type": page_type,
        "object_id": object_id,
        "lang": lang,
    }
    if page_version_id is not None:
        page_view_properties["page_version_id"] = page_version_id

    # Build the events list — serialized safely via json.dumps.
    events = [
        {
            "event_type": "page_view",
            "product_id": product_id_int,
            "properties": page_view_properties,
        }
    ]

    # Add-to-cart event: fired when the cart-add endpoint redirects back with ?added=1.
    # Only on product pages; object_id is the product PK.
    if page_type == "product" and request.GET.get("added") == "1":
        atc_properties = {
            "product_id": object_id,
            "lang": lang,
        }
        if page_version_id is not None:
            atc_properties["page_version_id"] = page_version_id
        events.append(
            {
                "event_type": "add_to_cart",
                "product_id": product_id_int,
                "properties": atc_properties,
            }
        )

    # json.dumps produces valid JavaScript literals for these types (arrays / objects /
    # strings / integers).  Output is safe because all values are server-controlled.
    events_json = json.dumps(events)

    # Build the inline script.  store_id is an integer literal (no quoting needed).
    # Single-quotes used inside the script to avoid HTML attribute collisions.
    script_lines = [
        "<script>",
        "(function(){",
        "  var _s=(function(){",
        "    var k=sessionStorage.getItem('pa_sid');",
        "    if(!k){k=Math.random().toString(36).slice(2)+Date.now().toString(36);sessionStorage.setItem('pa_sid',k);}",
        "    return k;",
        "  })();",
        f"  var _d={{store_id:{store_id},session_id:_s,events:{events_json}}};",
        "  var _b=new Blob([JSON.stringify(_d)],{type:'application/json'});",
        f"  if(typeof navigator.sendBeacon==='function'){{navigator.sendBeacon('{_BEACON_URL}',_b);}}",
        f"  else if(window.fetch){{window.fetch('{_BEACON_URL}',{{method:'POST',headers:{{'Content-Type':'application/json'}},body:JSON.stringify(_d),keepalive:true}});}}",
        "})();",
        "</script>",
    ]
    return mark_safe("\n".join(script_lines))


# ---------------------------------------------------------------------------
# {% language_switcher %} — renders active-language links in header (TH-140)
# ---------------------------------------------------------------------------


def _build_lang_switch_url(request, lang):
    """
    Build the URL for switching to lang from the current request.

    Uses request.locale.path (prefix-stripped by LocaleMiddleware) so we never
    manually parse the current language prefix out of request.path.

    For path-prefix languages (use_path_prefix=True): prepend /<lang_code>.
    For root languages (use_path_prefix=False): use the stripped path directly.

    Caveat: translated slugs that differ between languages will 404 on the
    target page — this is acceptable in Phase 1 (spec §TH-140, Phase 1 note).
    """
    locale = getattr(request, "locale", None)
    stripped = "/"
    if locale is not None:
        stripped = getattr(locale, "path", "/") or "/"

    if lang.use_path_prefix:
        return f"/{lang.lang_code}{stripped}"
    return stripped


@register.inclusion_tag("storefront/partials/language_switcher.html", takes_context=True)
def language_switcher(context):
    """
    Render the language-switcher partial (TH-140).

    Queries enabled StoreLanguage rows for the current store; renders nothing
    when the store has fewer than 2 active languages (no point showing a switcher
    with only one option).

    The inclusion template guards with {% if languages|length > 1 %} as a
    belt-and-suspenders safety check.
    """
    from stores.models import StoreLanguage

    request = context.get("request")
    store = getattr(request, "store", None)
    if not store:
        return {"languages": [], "current_lang": ""}

    languages = list(
        StoreLanguage.objects.filter(store=store, is_enabled=True).order_by(
            "-is_default", "lang_code"
        )
    )

    if len(languages) < 2:
        return {"languages": [], "current_lang": ""}

    # Resolve current lang code from locale (or fall back to empty string).
    current_lang_code = ""
    locale = getattr(request, "locale", None)
    if locale is not None:
        language = getattr(locale, "language", None)
        if language is not None:
            current_lang_code = str(getattr(language, "lang_code", "") or "")

    switch_links = [
        {
            "lang_code": lang.lang_code,
            "label": lang.lang_code.upper(),
            "url": _build_lang_switch_url(request, lang),
            "is_current": lang.lang_code == current_lang_code,
        }
        for lang in languages
    ]

    return {"languages": switch_links, "current_lang": current_lang_code}
