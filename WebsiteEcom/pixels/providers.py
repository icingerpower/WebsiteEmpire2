"""
Concrete PixelProvider implementations (ADR-022 D1/D3/D4).

Five day-1 providers. Adding a sixth means: one subclass here (id_pattern +
event_map + template paths) + its two templates
(pixels/templates/pixels/<key>_base.html, <key>_event.html) + one register()
call at the bottom of this module — no call-site edits anywhere else
(design-pattern-ideas §IX).

Frozen keys — must equal pixels.models.Pixel.PixelProvider.values AND every
FiredPixel.pixel_type value ever written for these providers (ADR-022 D1/D2).
A test in pixels/tests/test_registry.py pins this equality.

Native event mapping (ADR-022 D4 sign-off table). A missing key means "no
native equivalent" — PixelProvider.render_event() returns '' for it rather
than emit a broken call (e.g. Pinterest has no InitiateCheckout event).
"""

import re

from pixels.registry import PixelProvider, register


class FacebookPixelProvider(PixelProvider):
    key = "facebook"
    consent_category = "marketing"
    name = "Meta / Facebook Pixel"
    id_label = "Pixel ID"
    id_pattern = re.compile(r"^\d{5,20}$")
    base_template = "pixels/facebook_base.html"
    event_template = "pixels/facebook_event.html"
    event_map = {
        "view_content": "ViewContent",
        "add_to_cart": "AddToCart",
        "initiate_checkout": "InitiateCheckout",
        "purchase": "Purchase",
    }


class GaPixelProvider(PixelProvider):
    key = "ga"
    consent_category = "analytics"
    name = "Google Analytics 4"
    id_label = "GA4 Measurement ID (G-XXXXXXXX)"
    # GA4 only — no Universal Analytics (ADR-022 D1: UA stopped processing
    # hits in 2023/2024; accepting UA-* would be an invisible failure).
    id_pattern = re.compile(r"^G-[A-Z0-9]{4,16}$")
    base_template = "pixels/ga_base.html"
    event_template = "pixels/ga_event.html"
    event_map = {
        # page_view is fired automatically by gtag('config', ...) in the base
        # snippet — no explicit mapping entry needed here.
        "view_content": "view_item",
        "add_to_cart": "add_to_cart",
        "initiate_checkout": "begin_checkout",
        "purchase": "purchase",
    }


class TiktokPixelProvider(PixelProvider):
    key = "tiktok"
    consent_category = "marketing"
    name = "TikTok Pixel"
    id_label = "Pixel ID"
    id_pattern = re.compile(r"^[A-Z0-9]{10,30}$")
    base_template = "pixels/tiktok_base.html"
    event_template = "pixels/tiktok_event.html"
    event_map = {
        "view_content": "ViewContent",
        "add_to_cart": "AddToCart",
        "initiate_checkout": "InitiateCheckout",
        "purchase": "CompletePayment",
    }


class SnapchatPixelProvider(PixelProvider):
    key = "snapchat"
    consent_category = "marketing"
    name = "Snapchat Pixel"
    id_label = "Pixel ID (UUID)"
    id_pattern = re.compile(
        r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
    )
    base_template = "pixels/snapchat_base.html"
    event_template = "pixels/snapchat_event.html"
    event_map = {
        "view_content": "VIEW_CONTENT",
        "add_to_cart": "ADD_CART",
        "initiate_checkout": "START_CHECKOUT",
        "purchase": "PURCHASE",
    }


class PinterestPixelProvider(PixelProvider):
    key = "pinterest"
    consent_category = "marketing"
    name = "Pinterest Tag"
    id_label = "Tag ID"
    id_pattern = re.compile(r"^\d{5,20}$")
    base_template = "pixels/pinterest_base.html"
    event_template = "pixels/pinterest_event.html"
    event_map = {
        # Pinterest overloads 'pagevisit' for view_content, with a line_items
        # parameter added by the template — this is deliberate, not a bug
        # (ADR-022 D4: Pinterest has no distinct ViewContent event).
        "view_content": "pagevisit",
        "add_to_cart": "addtocart",
        # No standard InitiateCheckout event for Pinterest (ADR-022 D4) —
        # deliberately absent from event_map; render_event() returns ''.
        "purchase": "checkout",
    }


register(FacebookPixelProvider())
register(GaPixelProvider())
register(TiktokPixelProvider())
register(SnapchatPixelProvider())
register(PinterestPixelProvider())
