"""
Overlay theme registry (ADR-027 D10, TICKET-034).

A code registry, not DB rows: OVERLAY_THEMES maps a stable string key to the
partial template that renders that layout, a human-readable name, and a
schematic SVG preview — all three shown together in the admin "SELECT A
THEME" grid (spec-review finding admin-015-02/03: a visual preview grid, not
a plain dropdown). Adding a theme = adding a template + one preview SVG under
engagement/static/engagement/previews/ + one entry here — no migration
required (registry pattern, per the system prompt).

LeadCaptureCampaign.overlay_theme_key is validated against this registry in
clean() (loud at admin save, §XV-1). If a stored key later disappears from the
registry (a theme template removed in a deploy), LeadCaptureOverlayProvider.render()
falls back to DEFAULT_OVERLAY_THEME_KEY with an error log — the storefront must
never 500 over a removed overlay skin (slot contract: render() never raises).
"""

DEFAULT_OVERLAY_THEME_KEY = "centered_light"

# key -> {
#   "template": <partial path under storefront/templates/>,
#   "name": <admin label>,
#   "preview": <static-relative path to a schematic SVG wireframe of the
#              layout, rendered via engagement.widgets.OverlayThemeSelect>,
# }
OVERLAY_THEMES = {
    "centered_light": {
        "template": "storefront/partials/overlay_themes/centered_light.html",
        "name": "Centered (light)",
        "preview": "engagement/previews/centered_light.svg",
    },
    "photo_left_dark": {
        "template": "storefront/partials/overlay_themes/photo_left_dark.html",
        "name": "Photo left (dark)",
        "preview": "engagement/previews/photo_left_dark.svg",
    },
    "photo_right_accent": {
        "template": "storefront/partials/overlay_themes/photo_right_accent.html",
        "name": "Photo right (accent)",
        "preview": "engagement/previews/photo_right_accent.svg",
    },
    "top_banner": {
        "template": "storefront/partials/overlay_themes/top_banner.html",
        "name": "Top banner",
        "preview": "engagement/previews/top_banner.svg",
    },
}


def overlay_theme_choices():
    """Return (key, name) choice tuples for the admin ModelForm, in registry order."""
    return [(key, meta["name"]) for key, meta in OVERLAY_THEMES.items()]
