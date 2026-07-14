"""
SecurityBadgeSlotProvider — bridges badges.SecurityBadge rows into the
already-declared ADR-012 "security_badge" slot (ADR-030 D4).

Location signal is explicit, never sniffed (ADR-022 D5's rule, reused here):
each of the three views (product_page / cart_page / checkout_view) sets
context["trust_badge_location"] to "product" / "cart" / "checkout" before
rendering its template. render_slot()'s own signature is frozen and shared by
every slot in the platform — this provider reads the plain context key rather
than requiring a new keyword argument on render_slot() itself.

A missing/unrecognized location renders nothing and logs a warning (a
developer-error case — a location wired to the slot without setting the key —
never a shopper-visible failure, §XV-1: loud in logs, safe on the page).
"""

import logging

from django.utils.safestring import mark_safe

from storefront.slots import SlotProvider, register

from badges.badge_presets import BADGE_PRESETS, accepted_families
from badges.models import SecurityBadge, TrustBadgeLocation

logger = logging.getLogger(__name__)


def _render_one(row, loc_cfg, request) -> str:
    """
    Render one active+location-enabled SecurityBadge row.

    Raises on template errors — the caller (render()) isolates one row's
    failure from the others, matching the PixelsSlotProvider precedent.
    """
    from django.template.loader import render_to_string

    context = {
        "request": request,
        "row": row,
        "width_px": loc_cfg.get("width_px", 200),
        # Self-verifying "secure connection" claim (§1.1, §3) — gated here,
        # not by admin configuration, so the claim is true by construction
        # even for a store whose custom-domain TLS verification is pending.
        "is_secure": request.is_secure(),
        # Truthful payment-network derivation (§3) — never a fixed list.
        "accepted_families": accepted_families(),
    }

    if row.preset_key == "custom":
        template_name = "storefront/partials/security_badges/custom_image.html"
    else:
        preset = BADGE_PRESETS[row.preset_key]
        template_name = preset["template"]

    return render_to_string(template_name, context, request=request)


class SecurityBadgeSlotProvider(SlotProvider):
    slot = "security_badge"
    key = "security_badge"
    name = "Security badges"
    required_settings: list = []

    def is_enabled(self, store) -> bool:
        """
        Always True — gating lives per-row/per-location inside render(), the
        same pattern as PixelsSlotProvider (ADR-022 D3) / SecurityBadgeSlotProvider's
        own single query below.
        """
        return True

    def render(self, context) -> str:
        request = context.get("request")
        store = getattr(request, "store", None)
        location = context.get("trust_badge_location")

        if store is None:
            # No store on the request (e.g. non-storefront paths) — nothing to
            # render, and not a developer-error case worth logging.
            return ""

        if location not in TrustBadgeLocation.values:
            if location is not None:
                # A location wired to the slot without setting the key is a
                # developer error to fix, not a shopper-visible failure
                # (§XV-1: loud in logs, safe on the page).
                logger.warning(
                    "SecurityBadgeSlotProvider: unrecognized trust_badge_location %r "
                    "(store=%s) — rendering nothing.",
                    location, store.pk,
                )
            return ""

        rows = SecurityBadge.objects.for_store(store).filter(is_active=True).order_by("created_at")

        parts = []
        for row in rows:
            loc_cfg = (row.locations_json or {}).get(location)
            if not loc_cfg or not loc_cfg.get("enabled"):
                continue
            try:
                parts.append(_render_one(row, loc_cfg, request))
            except Exception:
                logger.exception(
                    "SecurityBadge row=%s failed to render for location=%r; skipping.",
                    row.pk, location,
                )
        return mark_safe("".join(parts))

    def validate_settings(self, store) -> list:
        # Optional decorative feature — never blocks launch readiness (ADR-030 D4).
        return []


register(SecurityBadgeSlotProvider())
