"""
PixelProvider registry (ADR-022 D3).

Concrete providers (pixels/providers.py) self-register into the module-level
_registry dict at import time (PixelsConfig.ready() imports pixels.providers,
which calls register() once per provider at module scope) — identical shape
to storefront.slots.

Frozen keys (must equal Pixel.provider choices AND FiredPixel.pixel_type
values — ADR-022 D1/D2): facebook, ga, tiktok, snapchat, pinterest. Adding a
sixth provider means one PixelProvider subclass + its two templates + one
register() call — never a call-site edit (design-pattern-ideas §IX).
"""

import json
import logging
import re

from django.template.loader import render_to_string
from django.utils.safestring import mark_safe

logger = logging.getLogger(__name__)

# key -> PixelProvider instance, in registration order.
_registry: dict[str, "PixelProvider"] = {}


def _json_script_escape(json_string: str) -> str:
    """
    Escape a JSON string so it is safe to interpolate inside an inline
    ``<script>`` block, following Django's ``_json_script_escapes`` /
    ``json_script`` convention (also used by
    ``storefront.views._serialize_jsonld``).

    ``json.dumps`` alone does not escape ``<``, ``>``, or ``&``, so a value
    containing e.g. ``</script>`` could terminate the surrounding script tag.
    """
    return (
        json_string.replace("&", "\\u0026")
        .replace("<", "\\u003c")
        .replace(">", "\\u003e")
    )


class PixelProvider:
    """
    Base class for a pixel/conversion-tracking platform integration.

    Subclasses set the class attributes below and register a single instance
    via register(). render_base()/render_event() are implemented once here —
    subclasses only declare *data* (template paths, id pattern, event map),
    never rendering logic, so behaviour stays identical across providers and
    provider #6 really is "one class" (ADR-022 D3/Risks).
    """

    #: Frozen registry key — must equal Pixel.provider and FiredPixel.pixel_type.
    key: str = ""
    #: Consent category this provider's tracking requires (ADR-025 D1) — one
    #: of "analytics" or "marketing". Fixed per-provider in code (rejected
    #: alternative: a store-configurable value via Pixel.config_json, which
    #: would let a store owner reclassify a marketing pixel as "analytics" to
    #: dodge the marketing toggle). Read by PixelsSlotProvider.render() /
    #: pixels.service.claim_purchase_pixels() via consent.state.get_consent()
    #: to gate rendering and purchase-claim consumption.
    consent_category: str = ""
    #: Human-readable name for the admin card title.
    name: str = ""
    #: Label for the pixel_id form field (e.g. "GA4 Measurement ID (G-XXXXXXXX)").
    id_label: str = "Pixel ID"
    #: Strict validation pattern for pixel_id — security boundary, not cosmetics
    #: (ADR-022 D2: the ID is injected into an inline <script>).
    id_pattern: "re.Pattern" = re.compile(r"^$")
    #: Template rendering the loader/init script + automatic page_view + the
    #: 'pradize:pixels' bridge listener (ADR-022 D5).
    base_template: str = ""
    #: Template rendering one non-add_to_cart canonical event as a native call.
    event_template: str = ""
    #: canonical event name -> native event name/string. A missing or falsy
    #: entry means this provider has no native equivalent for that canonical
    #: event (ADR-022 D4, e.g. Pinterest has no InitiateCheckout event) —
    #: render_event() must then return '' rather than emit a broken call.
    event_map: dict[str, str] = {}

    def render_base(
        self,
        pixel_id: str,
        page_url: str | None = None,
        consent_mode: dict | None = None,
    ) -> str:
        """
        Render the loader/init snippet + automatic page_view + bridge listener.

        page_url (PIX:P1, human decision 2026-07-11): an optional normalized,
        tokenless URL to report for THIS render's automatic page_view instead
        of letting the platform SDK auto-collect the browser's real
        document.location.href. Only the GA4 base template
        (pixels/templates/pixels/ga_base.html) currently consumes it, via
        gtag's documented `page_location` config override — GA4 is the only
        one of the five providers with a supported, documented client-side
        override for the automatically-collected page URL. The other four
        providers (Facebook, TikTok, Snapchat, Pinterest) ignore this
        parameter; their base snippets have no equivalent override and keep
        reading document.location.href verbatim (see docs/adr/ADR-022-pixel-
        integrations.md, dated addendum, for the honest residual-exposure
        statement and recommended follow-up).

        None (the default) means "no override" — every call site outside the
        thank-you page passes nothing, and existing base_template rendering is
        unchanged.

        consent_mode (ADR-025 D3 item 4): optional dict
        {"analytics_granted": bool, "marketing_granted": bool} describing the
        shopper's current consent state, passed uniformly by
        PixelsSlotProvider.render() for every row — exactly the same
        precedent as page_url above (passed to all five providers, consumed
        by only one). Only ga_base.html reads it, to emit a truthful
        `gtag('consent', 'default', …)` line before `gtag('config', …)`
        (Google Consent Mode v2). The other four providers' templates do not
        reference these keys, so passing them is a no-op there. None (the
        default) means "omit the consent-mode line" — used by call sites
        that have no consent context to report.
        """
        context = {"pixel_id": pixel_id, "page_url": page_url}
        if consent_mode is not None:
            context["consent_mode"] = consent_mode
        return render_to_string(self.base_template, context)

    def render_event(self, pixel_id: str, event: str, payload: dict) -> str:
        """
        Render one canonical event as this provider's native call.

        Returns '' when the canonical event has no native mapping for this
        provider (D4) — never emits a broken/empty native call.
        """
        native_event = self.event_map.get(event) or ""
        if not native_event:
            return ""
        content_ids = list(payload.get("content_ids") or [])
        context = {
            "pixel_id": pixel_id,
            "native_event": native_event,
            "payload": payload,
            # Pre-serialized server-side (PKs only — never user input) so
            # templates never need to build a JS array literal by hand.
            # Escaped with the same convention as Django's json_script
            # filter / storefront.views._serialize_jsonld: content_ids is
            # PK-derived only today, but this keeps the value inert against
            # a future user-influenced id (e.g. merchant SKU) containing
            # "</script>".
            "content_ids_json": mark_safe(_json_script_escape(json.dumps(content_ids))),
            "event_id": payload.get("event_id", ""),
            "transaction_id": payload.get("transaction_id", ""),
        }
        return render_to_string(self.event_template, context)

    def validate_settings(self, pixel_row) -> list[str]:
        """
        Return human-readable errors for the launch checklist (ADR-022 D8).

        Base implementation only checks the pixel_id format; PixelsSlotProvider
        additionally reports rows whose provider key has no registry entry.
        """
        errors = []
        if not self.id_pattern.match(pixel_row.pixel_id or ""):
            errors.append(
                f"{self.name}: pixel ID {pixel_row.pixel_id!r} does not match the "
                f"expected {self.id_label} format."
            )
        return errors


def register(provider: PixelProvider) -> None:
    """Register a PixelProvider instance. Call once at module import time."""
    if not provider.key:
        raise ValueError(f"PixelProvider {provider!r} has an empty 'key' attribute.")
    _registry[provider.key] = provider
    logger.debug("Registered pixel provider %r.", provider.key)


def get_provider(key: str) -> "PixelProvider | None":
    """Return the registered PixelProvider for key, or None if not registered."""
    return _registry.get(key)


def all_providers() -> list["PixelProvider"]:
    """Return all registered providers, in registration order."""
    return list(_registry.values())
