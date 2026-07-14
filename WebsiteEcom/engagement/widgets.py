"""
Admin widget for LeadCaptureCampaign.overlay_theme_key (spec-review finding
admin-015-02/03: the "SELECT A THEME" section in the screenshots is a visual
preview grid, not a plain dropdown).

OverlayThemeSelect is presentation only: it is still a ChoiceField-compatible
RadioSelect, still posts the same "overlay_theme_key" name with the same
registry keys as values, and still round-trips through the exact same
validation in engagement.admin.LeadCaptureCampaignAdminForm and
LeadCaptureCampaign.clean() (engagement/models.py). Only the rendering — one
card per registry entry with its schematic SVG preview (engagement/themes.py
OVERLAY_THEMES) instead of an <option> — changes.
"""

from django import forms

from engagement.themes import OVERLAY_THEMES


class OverlayThemeSelect(forms.RadioSelect):
    """Renders overlay_theme_key choices as a grid of preview cards.

    Templates (engagement/templates/engagement/widgets/) replace Django's
    default <ul>/<li> radio markup with a CSS grid of cards; each card is
    still a real <label><input type="radio" ...></label> pair, so keyboard
    navigation, screen readers and plain form POSTs work exactly as they did
    with the plain <select> this replaces.
    """

    template_name = "engagement/widgets/overlay_theme_select.html"
    option_template_name = "engagement/widgets/overlay_theme_option.html"

    class Media:
        css = {"all": ("engagement/css/overlay_theme_select.css",)}

    def create_option(self, name, value, label, selected, index, subindex=None, attrs=None):
        option = super().create_option(name, value, label, selected, index, subindex=subindex, attrs=attrs)
        # value is "" for the unselected blank choice some ChoiceField setups
        # add; OVERLAY_THEMES never contains that key, so .get(...) guards it.
        theme = OVERLAY_THEMES.get(value, {})
        option["preview"] = theme.get("preview", "")
        return option
