"""
Admin widget for SecurityBadge.preset_key (ADR-030 D5).

Mirrors engagement.widgets.OverlayThemeSelect exactly (ADR-027 D10 precedent):
same forms.RadioSelect subclass, same "one real <label><input type=radio>>
per card, CSS grid instead of <ul>" technique, same create_option() override
attaching each preset's preview SVG path to the option. Only the registry
(badges.badge_presets.BADGE_PRESETS instead of engagement.themes.OVERLAY_THEMES)
and the extra "custom" choice differ.

BadgePresetSelect is presentation only — it still posts the same "preset_key"
name with the same registry keys as values (plus the literal "custom"), and
still round-trips through the exact same validation in SecurityBadge.clean().
"""

from django import forms

from badges.badge_presets import BADGE_PRESETS


class BadgePresetSelect(forms.RadioSelect):
    """
    Renders preset_key choices as a grid of preview cards, with one extra
    "custom" card (no preview image — an upload icon instead) that reveals
    the custom_image field via badges/js/badge_preset_toggle.js.
    """

    template_name = "badges/widgets/badge_preset_select.html"
    option_template_name = "badges/widgets/badge_preset_option.html"

    class Media:
        css = {"all": ("badges/css/badge_preset_select.css",)}
        js = ("badges/js/badge_preset_toggle.js",)

    def create_option(self, name, value, label, selected, index, subindex=None, attrs=None):
        option = super().create_option(name, value, label, selected, index, subindex=subindex, attrs=attrs)
        # value is "" for the unselected blank choice some ChoiceField setups
        # add, and "custom" is a valid value with no registry entry — both
        # simply get no preview image (the option template shows a plain
        # upload placeholder card instead).
        preset = BADGE_PRESETS.get(value, {})
        option["preview"] = preset.get("preview", "")
        return option
