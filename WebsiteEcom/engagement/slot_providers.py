"""
SlotProvider subclasses bridging engagement models to the ADR-012 slot
registry (ADR-027 D1): slot.overlay (T034) and slot.social_proof (T035).

Both are registered in EngagementConfig.ready() via storefront.slots.register()
(engagement/apps.py).
"""

import json
import logging

from django.middleware.csrf import get_token
from django.template.loader import render_to_string
from django.urls import reverse

from storefront.slots import SlotProvider, register

from engagement import service
from engagement.models import LeadCaptureCampaign, get_or_create_social_proof_settings
from engagement.themes import DEFAULT_OVERLAY_THEME_KEY, OVERLAY_THEMES

logger = logging.getLogger(__name__)


def _lang_code(request) -> str:
    """Best-effort current storefront language code (mirrors consent.slot_provider._lang_code)."""
    locale = getattr(request, "locale", None)
    lang_obj = getattr(locale, "language", None) if locale else None
    return getattr(lang_obj, "lang_code", "en") if lang_obj else "en"


def _json_script_escape(json_string: str) -> str:
    """
    Escape a JSON string so it is safe to interpolate inside an inline
    ``<script>`` block via ``{{ ... |safe }}``, following Django's
    ``_json_script_escapes`` / ``json_script`` convention (also used by
    ``pixels.registry._json_script_escape`` and
    ``storefront.views._serialize_jsonld``).

    Security audit F6: both ``config_json`` (below) and ``entries_json``
    embed store-admin-controlled data that ultimately derives from
    ``Permalink.slug`` — a plain ``CharField`` (not a ``SlugField``), so a
    hand-entered slug containing ``</script>`` would otherwise terminate the
    surrounding script tag and inject markup into every storefront page
    rendering the widget. ``json.dumps`` alone does not escape ``<``, ``>``,
    or ``&``.
    """
    return (
        json_string.replace("&", "\\u0026")
        .replace("<", "\\u003c")
        .replace(">", "\\u003e")
    )


class LeadCaptureOverlayProvider(SlotProvider):
    slot = "overlay"
    key = "lead_capture"
    name = "Lead capture overlay"
    required_settings: list = []

    def is_enabled(self, store) -> bool:
        return service.select_active_campaign(store) is not None

    def render(self, context) -> str:
        request = context.get("request")
        store = getattr(request, "store", None)
        if request is None or store is None:
            return ""

        campaign = service.select_active_campaign(store)
        if campaign is None:
            return ""

        lang = _lang_code(request)
        content = service.resolve_campaign_content(campaign, lang)
        excluded_paths = service.resolve_excluded_paths(store, campaign)

        theme_meta = OVERLAY_THEMES.get(campaign.overlay_theme_key)
        if theme_meta is None:
            logger.error(
                "LeadCaptureCampaign %s references unknown overlay_theme_key %r "
                "— falling back to %r.",
                campaign.pk, campaign.overlay_theme_key, DEFAULT_OVERLAY_THEME_KEY,
            )
            theme_meta = OVERLAY_THEMES[DEFAULT_OVERLAY_THEME_KEY]

        # Guarantee the csrftoken cookie is set before the popup's JS ever
        # needs to POST (mirrors ConsentSlotProvider.render()'s F1 fix).
        get_token(request)

        service.record_visitor(request, campaign)

        cap_window_seconds = campaign.cap_window_value * {
            "minutes": 60,
            "hours": 3600,
            "days": 86400,
        }[campaign.cap_window_unit]

        config = {
            "campaign_id": campaign.pk,
            "trigger": campaign.trigger,
            "trigger_delay_seconds": campaign.trigger_delay_seconds,
            "mobile_trigger": campaign.mobile_trigger,
            "mobile_trigger_delay_seconds": campaign.mobile_trigger_delay_seconds,
            "cap_enabled": campaign.cap_enabled,
            "cap_impressions": campaign.cap_impressions,
            "cap_window_seconds": cap_window_seconds,
            "excluded_paths": excluded_paths,
            "signup_url": reverse("engagement:overlay-signup"),
            "event_url": reverse("engagement:overlay-event"),
        }

        return render_to_string(
            theme_meta["template"],
            {
                "request": request,
                "theme_variant": campaign.overlay_theme_key,
                "has_photo": "photo" in campaign.overlay_theme_key,
                "headline": content["headline"],
                "body": content["body"],
                "cta_label": content["cta_label"],
                "dismiss_label": content["dismiss_label"],
                "config_json": _json_script_escape(json.dumps(config)),
            },
            request=request,
        )

    def validate_settings(self, store) -> list:
        errors = []
        for campaign in LeadCaptureCampaign.objects.for_store(store).filter(is_active=True):
            if campaign.post_signup_action == "go_to_url" and not campaign.redirect_url:
                errors.append(
                    f"(warning) Campaign {campaign.name!r} is active with 'Go to Url' "
                    "but has no redirect_url configured."
                )
        return errors


class SocialProofProvider(SlotProvider):
    slot = "social_proof"
    key = "recent_purchases"
    name = "Recent purchase notifications"
    required_settings: list = []

    def is_enabled(self, store) -> bool:
        return get_or_create_social_proof_settings(store).is_enabled

    def render(self, context) -> str:
        request = context.get("request")
        store = getattr(request, "store", None)
        if request is None or store is None:
            return ""

        settings_row = get_or_create_social_proof_settings(store)
        lang = _lang_code(request)
        entries = service.get_social_proof_entries(store, context, lang)
        if not entries:
            return ""

        return render_to_string(
            "storefront/partials/social_proof_toast.html",
            {
                "request": request,
                "entries": entries,
                "entries_json": _json_script_escape(json.dumps(entries)),
                "position": settings_row.position,
                "first_delay_seconds": settings_row.first_delay_seconds,
                "interval_seconds": settings_row.interval_seconds,
            },
            request=request,
        )

    def validate_settings(self, store) -> list:
        # Neither settings model participates in launch-readiness (ADR-027 D8:
        # "Required for launch? no" on every field).
        return []


register(LeadCaptureOverlayProvider())
register(SocialProofProvider())
