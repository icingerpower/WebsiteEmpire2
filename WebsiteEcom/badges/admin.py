"""
Store-admin registration for the badges app (ADR-030, TICKET-046).

Permission gating uses the "security_badges" module key — a new, DEDICATED
module row per AF-002's 18-module table ("Security Badge | Full access / No
access"), NOT a fallback into the generic "apps" bucket that engagement/feeds
use (those two are not named rows in the 18-module list; Security Badge
explicitly is — ADR-030 D5). Full access / No access only, no "Limited"
option.

Module permission gate: stores.permissions.StoreModulePermissionMixin, auto-
applied at registration by webecom.admin.StoreAdminSite.register() (ADR-033
D3b) — this admin no longer defines its own has_*_permission methods; it
just declares `module_key` below.
"""

from django import forms
from django.contrib import admin
from django.utils.html import format_html

from webecom.admin import store_admin_site

from media.admin_mixins import ImageThumbnailMixin

from badges.badge_presets import badge_preset_choices
from badges.models import CUSTOM_PRESET_KEY, SecurityBadge
from badges.widgets import BadgePresetSelect


class SecurityBadgeAdminForm(forms.ModelForm):
    """
    Restricts preset_key to the registry (badges.badge_presets.BADGE_PRESETS)
    plus the literal "custom" choice — same field name, same registry-derived
    choices + one extra value, same validation as a plain forms.ChoiceField,
    only the widget changes to render the "BADGE COMBINATION" preview grid
    (ADR-030 D5) instead of a plain <select>.
    """

    preset_key = forms.ChoiceField(
        choices=lambda: badge_preset_choices() + [(CUSTOM_PRESET_KEY, "Upload your own image")],
        widget=BadgePresetSelect,
    )

    class Meta:
        model = SecurityBadge
        fields = "__all__"
        help_texts = {
            "custom_image": (
                "You are responsible for ensuring you have the right to use any "
                "image you upload here, including third-party certification marks."
            ),
        }


@admin.register(SecurityBadge, site=store_admin_site)
class SecurityBadgeAdmin(ImageThumbnailMixin, admin.ModelAdmin):
    module_key = "security_badges"

    form = SecurityBadgeAdminForm
    thumbnail_field = "custom_image"

    list_display = [
        "status_dot", "name", "placement_summary_display", "thumbnail_or_dash", "updated_at",
    ]
    list_filter = ["is_active", "preset_key"]
    search_fields = ["name"]
    readonly_fields = ["created_at", "updated_at"]

    fieldsets = [
        ("General", {"fields": ["name", "is_active"]}),
        ("Badge combination", {"fields": ["preset_key", "custom_image"]}),
        ("Refine style", {
            "fields": [
                "heading_enabled", "heading_segments_json",
                "border_enabled", "border_color", "border_width_px",
            ],
        }),
        ("Location", {"fields": ["locations_json"]}),
        ("Meta", {"fields": ["created_at", "updated_at"], "classes": ["collapse"]}),
    ]

    @admin.display(description="")
    def status_dot(self, obj):
        color = "#28a745" if obj.is_active else "#999999"
        return format_html('<span style="color:{};">&#9679;</span>', color)

    @admin.display(description="Placement")
    def placement_summary_display(self, obj):
        return obj.placement_summary()

    @admin.display(description="Preview")
    def thumbnail_or_dash(self, obj):
        if obj.preset_key == CUSTOM_PRESET_KEY and obj.custom_image:
            return self.image_thumbnail(obj)
        return "—"

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return SecurityBadge.objects.for_store(request.store)
        return SecurityBadge.objects.none()

    def save_model(self, request, obj, form, change):
        if not change and getattr(request, "store", None):
            obj.store = request.store
        super().save_model(request, obj, form, change)
