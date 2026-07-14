"""
SecurityBadge — the "badge designer" model (ADR-030, TICKET-046).

One row per configured badge (a store may run several — e.g. one for the
product page, a different one for checkout — the screenshot's list view
shows multiple named, independently-active badges, not a singleton).

Every invariant below is enforced loudly in `clean()` (design-pattern-ideas
§XV-1: an unrecognized preset_key, an unrecognized location, or a malformed
heading/locations payload must fail loudly at save time, never silently
render nothing or render something unintended in production).
"""

import re

from django.core.exceptions import ValidationError
from django.db import models

from core.models import StoreOwnedModel
from media.validators import validate_image_file

from badges.badge_presets import BADGE_PRESETS

HEX_COLOR_RE = re.compile(r"^#[0-9a-fA-F]{6}$")

MAX_HEADING_SEGMENTS = 6
MAX_SEGMENT_TEXT_LENGTH = 40

MIN_LOCATION_WIDTH_PX = 100
MAX_LOCATION_WIDTH_PX = 800

CUSTOM_PRESET_KEY = "custom"

# Field-local, narrower than settings.ALLOWED_IMAGE_TYPES (ADR-030 D2): this
# field renders next to the payment button on every product/cart/checkout
# page of the store — a high-trust rendering zone — so image/svg+xml is
# excluded here even if the platform's global allow-list setting is ever
# changed to include it elsewhere. An uploaded SVG can carry inline
# <script>/event-handler payloads.
CUSTOM_IMAGE_ALLOWED_CONTENT_TYPES = ["image/jpeg", "image/png", "image/webp"]


def validate_custom_image_not_svg(file):
    """
    Reject SVG uploads for SecurityBadge.custom_image regardless of the
    platform-wide `settings.ALLOWED_IMAGE_TYPES` (ADR-030 D2). Independent,
    field-local allow-list — see module docstring for why.

    `file` may be the raw uploaded file (has .content_type directly — the
    form-submission case) OR a `FieldFile`/`ImageFieldFile` wrapping one
    (the case when a model's own `full_clean()` re-validates an
    already-assigned-but-not-yet-saved field: Django's FileField descriptor
    wraps the raw upload in a FieldFile, which does not proxy
    `.content_type` from the file it wraps). Drilling into `.file` handles
    both without changing behavior for either.
    """
    content_type = getattr(file, "content_type", None)
    if content_type is None:
        content_type = getattr(getattr(file, "file", None), "content_type", None)
    if content_type == "image/svg+xml":
        raise ValidationError(
            "SVG uploads are not allowed for security badge images. "
            "Use PNG, JPEG, or WebP instead."
        )
    if content_type and content_type not in CUSTOM_IMAGE_ALLOWED_CONTENT_TYPES:
        raise ValidationError(
            f"Unsupported image type {content_type!r} for security badge images. "
            f"Allowed: {', '.join(CUSTOM_IMAGE_ALLOWED_CONTENT_TYPES)}."
        )


class TrustBadgeLocation(models.TextChoices):
    PRODUCT = "product", "Product page"
    CART = "cart", "Cart"
    CHECKOUT = "checkout", "Checkout"


def _validate_heading_segments(segments):
    """
    heading_segments_json shape: a list of ≤6 {"text": str, "color": "#rrggbb"}
    objects (ADR-030 D2) — generalizes CommerceHQ's 3 hardcoded English words
    into a translatable, store-authored, arbitrary-length structure.
    """
    if not isinstance(segments, list):
        raise ValidationError({"heading_segments_json": "Must be a list of segment objects."})
    if len(segments) > MAX_HEADING_SEGMENTS:
        raise ValidationError({
            "heading_segments_json": f"At most {MAX_HEADING_SEGMENTS} segments are supported.",
        })
    for i, seg in enumerate(segments):
        if not isinstance(seg, dict):
            raise ValidationError({
                "heading_segments_json": f"Segment {i} must be an object with 'text' and 'color'.",
            })
        text = seg.get("text", "")
        color = seg.get("color", "")
        if not isinstance(text, str) or not text:
            raise ValidationError({
                "heading_segments_json": f"Segment {i}: 'text' must be a non-empty string.",
            })
        if len(text) > MAX_SEGMENT_TEXT_LENGTH:
            raise ValidationError({
                "heading_segments_json": (
                    f"Segment {i}: 'text' exceeds {MAX_SEGMENT_TEXT_LENGTH} characters."
                ),
            })
        if not isinstance(color, str) or not HEX_COLOR_RE.match(color):
            raise ValidationError({
                "heading_segments_json": (
                    f"Segment {i}: 'color' must be a hex color like '#088650'."
                ),
            })


def _validate_locations(locations):
    """
    locations_json shape: {location_key: {"enabled": bool, "width_px": int}}
    — keys must be a subset of TrustBadgeLocation.values (ADR-030 D2).
    """
    if not isinstance(locations, dict):
        raise ValidationError({"locations_json": "Must be an object keyed by location."})
    valid_keys = set(TrustBadgeLocation.values)
    for key, cfg in locations.items():
        if key not in valid_keys:
            raise ValidationError({
                "locations_json": f"Unknown location {key!r}. Must be one of {sorted(valid_keys)}.",
            })
        if not isinstance(cfg, dict):
            raise ValidationError({"locations_json": f"locations_json[{key!r}] must be an object."})
        width_px = cfg.get("width_px", MIN_LOCATION_WIDTH_PX)
        if not isinstance(width_px, int) or isinstance(width_px, bool):
            raise ValidationError({
                "locations_json": f"locations_json[{key!r}].width_px must be an integer.",
            })
        if not (MIN_LOCATION_WIDTH_PX <= width_px <= MAX_LOCATION_WIDTH_PX):
            raise ValidationError({
                "locations_json": (
                    f"locations_json[{key!r}].width_px must be between "
                    f"{MIN_LOCATION_WIDTH_PX} and {MAX_LOCATION_WIDTH_PX}."
                ),
            })


class SecurityBadge(StoreOwnedModel):
    """
    A configured trust badge composition (ADR-030 D2).

    preset_key is either a key in badges.badge_presets.BADGE_PRESETS or the
    literal "custom" (store-uploaded image, custom_image required). Exactly
    one of {preset_key in BADGE_PRESETS, preset_key == "custom" + custom_image}
    may be true at a time — clean() enforces this loudly.
    """

    name = models.CharField(max_length=100, help_text="Admin label only, e.g. 'Safe checkout'.")
    is_active = models.BooleanField(default=True)

    # --- Badge combination (§1, §3) ---
    preset_key = models.CharField(
        max_length=40,
        help_text="Key into badges.badge_presets.BADGE_PRESETS, or 'custom'.",
    )
    custom_image = models.ImageField(
        upload_to="security_badges/%Y/%m/",
        blank=True,
        null=True,
        validators=[validate_image_file, validate_custom_image_not_svg],
        help_text="Used only when preset_key='custom'. Raster only (PNG/JPEG/WebP) — no SVG.",
    )

    # --- Refine style ---
    heading_enabled = models.BooleanField(default=True)
    # Ordered, per-segment coloring — store's own copy (§1.3), the platform
    # is not responsible for the truthfulness of this free text. blank=True
    # (matches engagement.LeadCaptureCampaign.signup_tags' JSONField
    # convention): an empty list is a valid state (heading_enabled=False, or
    # not configured yet) — Django's Field.blank check otherwise treats `[]`
    # as "cannot be blank" and full_clean()/admin saves would always fail.
    heading_segments_json = models.JSONField(
        default=list,
        blank=True,
        help_text='[{"text": "Guaranteed", "color": "#000000"}, ...] — at most 6 segments.',
    )

    border_enabled = models.BooleanField(default=True)
    border_color = models.CharField(max_length=7, default="#000000")
    border_width_px = models.PositiveSmallIntegerField(default=5)

    # --- Placement (screenshot "LOCATION" section) ---
    # blank=True — an empty dict (no location enabled yet) is a valid state,
    # same rationale as heading_segments_json above.
    locations_json = models.JSONField(
        default=dict,
        blank=True,
        help_text=(
            '{"product": {"enabled": false, "width_px": 200}, '
            '"cart": {"enabled": false, "width_px": 200}, '
            '"checkout": {"enabled": true, "width_px": 200}}'
        ),
    )

    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "security badge"
        verbose_name_plural = "security badges"
        indexes = [models.Index(fields=["store", "is_active"])]

    def __str__(self):
        return self.name

    def clean(self):
        super().clean()
        if self.preset_key != CUSTOM_PRESET_KEY and self.preset_key not in BADGE_PRESETS:
            raise ValidationError({"preset_key": f"Unknown preset_key {self.preset_key!r}."})
        if self.preset_key == CUSTOM_PRESET_KEY and not self.custom_image:
            raise ValidationError({
                "custom_image": "custom_image is required when preset_key is 'custom'.",
            })
        if self.preset_key != CUSTOM_PRESET_KEY and self.custom_image:
            raise ValidationError({
                "custom_image": (
                    "custom_image is only used when preset_key is 'custom' — clear the "
                    "upload or switch preset_key, do not leave a dangling unused upload."
                ),
            })
        _validate_heading_segments(self.heading_segments_json)
        _validate_locations(self.locations_json)

    def placement_summary(self) -> str:
        """
        Admin list-display helper: e.g. "Set on Cart and Checkout" — computed
        from locations_json, never stored (ADR-030 D5).
        """
        labels = {
            TrustBadgeLocation.PRODUCT: "Product",
            TrustBadgeLocation.CART: "Cart",
            TrustBadgeLocation.CHECKOUT: "Checkout",
        }
        enabled = [
            labels[loc]
            for loc in TrustBadgeLocation.values
            if (self.locations_json or {}).get(loc, {}).get("enabled")
        ]
        if not enabled:
            return "Not set on any page"
        if len(enabled) == 1:
            return f"Set on {enabled[0]}"
        return f"Set on {', '.join(enabled[:-1])} and {enabled[-1]}"
