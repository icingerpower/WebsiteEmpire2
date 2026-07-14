"""
Theme resolution for the Pradize storefront (ADR-012, D4, TICKET-029 Phase 1).

resolve_theme(store, preview=None) → ThemeContext
    The ONLY code that decides which theme renders for a given request (§XV-4).
    Called by ThemeMiddleware once per storefront request; result is cached on
    request as request.theme_ctx.

resolve_tokens(theme_key, customization) → dict
    Merges theme defaults + customization overrides into a final token dict.
    Phase 1: returns only color_overrides from customization (tokens.json not yet
    present; Phase 4 loads and merges the full token file).

Token CSS whitelist (_TOKEN_WHITELIST):
    Only --p-* names in this frozenset are emitted in the :root block.
    Unknown names from customization are dropped and logged (§XV-1).

Fallback chain (TH-008):
    1. preview.theme (valid signed grant, store matches)
    2. store.theme FK (status=published, is_active=True)
    3. Platform default (is_default=True, is_active=True)
    4. settings.STOREFRONT_REFERENCE_THEME ('general') — never 500s; logs error + L11 note.
"""

import logging
from dataclasses import dataclass, field
from typing import Optional

from django.conf import settings

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Whitelist — only these CSS custom property names are emitted in :root
# ---------------------------------------------------------------------------

_TOKEN_WHITELIST = frozenset(
    {
        "--p-color-brand",
        "--p-color-accent",
        "--p-color-background",
        "--p-color-surface",
        "--p-color-text",
        "--p-color-muted-text",
        "--p-color-badge",
        "--p-color-announcement",
        "--p-font-heading",
        "--p-font-body",
        "--p-radius",
        "--p-header-layout",
        "--p-card-aspect-ratio",
    }
)

# Colour-override slot → CSS custom property mapping.
# customization_json["color_overrides"]["primary"] → --p-color-brand
_COLOR_SLOT_TO_TOKEN = {
    "primary": "--p-color-brand",
    "accent": "--p-color-accent",
    "background": "--p-color-background",
    "surface": "--p-color-surface",
    "text": "--p-color-text",
    "muted_text": "--p-color-muted-text",
    "badge": "--p-color-badge",
    "announcement": "--p-color-announcement",
}

# Default structural toggles (overridden per theme or per customization).
_STRUCTURAL_DEFAULTS = {
    "sticky_header": True,
    "breadcrumbs": True,
    "hover_second_image": False,
    "announcement_bar": False,
}

# Per-theme card aspect ratios (Phase 4: move these into tokens.json).
# b2b uses 1/1 (square) — matches --card-aspect-ratio in themes/b2b/theme.css.
_CARD_ASPECT_RATIO = {
    "general": "4:5",
    "fashion": "3:4",
    "b2b": "1/1",
}


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class PreviewGrant:
    """
    Decoded signed preview token payload (ADR-012 D8).

    Issued by the theme preview endpoint; verified by ThemeMiddleware.
    theme_id=None is valid when previewing with the store's existing theme.
    """

    store_id: int
    theme_id: int
    customization_id: Optional[int] = None


@dataclass(frozen=True)
class ThemeContext:
    """
    Immutable theme context set on request by ThemeMiddleware (ADR-012 D4).

    Exposed to templates via the storefront_context context processor as 'theme_ctx'.

    Fields:
      key             — theme source_ref (e.g. 'general'); used as CSS/template namespace.
      is_preview      — True when rendering under a signed preview grant (D8).
      tokens          — resolved token dict ({--p-color-brand: '#…', …}).
      token_css       — serialized :root { --p-*: value; } block for inline <style>.
      structural      — boolean toggles (sticky_header, breadcrumbs, hover_second_image, …).
      card_aspect_ratio — e.g. '4:5'; drives the product-card CSS.
      css_path        — static path to theme.css, e.g. 'storefront/themes/general/theme.css'.
      customization   — raw customization_json dict for the active (store, theme) pair.
      tokens_css_path — static path to a separate token CSS file (Phase 4).
                        Empty string when tokens are bundled into theme.css (current state).
    """

    key: str
    is_preview: bool
    tokens: dict
    token_css: str
    structural: dict
    card_aspect_ratio: str
    css_path: str
    customization: dict
    tokens_css_path: str = ""


# ---------------------------------------------------------------------------
# Token resolution
# ---------------------------------------------------------------------------


def _build_token_css(tokens: dict) -> str:
    """Serialize a token dict to a :root { --p-*: value; } block."""
    if not tokens:
        return ""
    props = "".join(f"{k}:{v};" for k, v in tokens.items())
    return f":root{{{props}}}"


def resolve_tokens(theme_key: str, customization: dict) -> dict:
    """
    Merge theme defaults + customization into a final, whitelist-filtered token dict.

    Resolution order (TH-022):
      1. per-slot color_overrides from customization (highest priority)
      2. selected preset/font_pair from customization (Phase 4)
      3. base token defaults from tokens.json (Phase 4)

    Phase 1 note: tokens.json files are not yet present; only color_overrides are
    resolved here.  Phase 4 will load the full token file and merge all layers.

    Unknown token names are dropped and logged (§XV-1 — no silent failures).
    """
    resolved: dict = {}

    color_overrides = customization.get("color_overrides", {})
    if isinstance(color_overrides, dict):
        for slot, hex_value in color_overrides.items():
            css_prop = _COLOR_SLOT_TO_TOKEN.get(slot)
            if css_prop is None:
                logger.warning(
                    "Unknown color override slot %r in theme customization; dropped.",
                    slot,
                )
                continue
            if css_prop not in _TOKEN_WHITELIST:
                logger.warning(
                    "Token %r not in whitelist; dropped from :root block.",
                    css_prop,
                )
                continue
            resolved[css_prop] = hex_value

    return resolved


# ---------------------------------------------------------------------------
# Theme resolution
# ---------------------------------------------------------------------------


def resolve_theme(store, preview: Optional[PreviewGrant] = None) -> ThemeContext:
    """
    Resolve the active ThemeContext for the given store (ADR-012 D4, TH-008).

    Fallback chain:
      1. preview.theme (if preview grant is valid and store_id matches)
      2. store.theme FK (status=published, is_active=True)
      3. Platform default Theme (is_default=True, is_active=True)
      4. settings.STOREFRONT_REFERENCE_THEME — never raises; logs error + L11 reminder.

    Any fallback past step 2 is logged at ERROR level for the launch-readiness
    checklist item L11 "Active theme selected".
    """
    from stores.models import Theme  # late import — avoids circular at module load

    theme = None
    is_preview = False

    # --- Step 1: preview grant ---
    if preview is not None and store is not None and preview.store_id == store.pk:
        try:
            theme = Theme.objects.get(pk=preview.theme_id, is_active=True)
            is_preview = True
        except Theme.DoesNotExist:
            logger.info(
                "Preview: theme_id=%s not found or inactive; falling through.",
                preview.theme_id,
            )
            theme = None

    # --- Step 2: store.theme FK ---
    if theme is None and store is not None:
        store_theme = getattr(store, "theme", None)
        if store_theme is not None:
            try:
                theme = Theme.objects.get(
                    pk=store_theme.pk, status="published", is_active=True
                )
            except Theme.DoesNotExist:
                logger.error(
                    "Store pk=%s has theme FK pk=%s but it is not published/active; "
                    "falling back to platform default (L11).",
                    getattr(store, "pk", "?"),
                    store_theme.pk,
                )
                theme = None

    # --- Step 3: platform default ---
    if theme is None:
        try:
            theme = Theme.objects.get(is_default=True, is_active=True)
            if store is not None:
                logger.error(
                    "Store pk=%s has no active published theme; using platform default (L11).",
                    getattr(store, "pk", "?"),
                )
        except Theme.DoesNotExist:
            theme = None

    # --- Step 4: reference key from settings (never 500s) ---
    if theme is None:
        reference_key = getattr(settings, "STOREFRONT_REFERENCE_THEME", "general")
        logger.error(
            "No active Theme row found (including platform default); falling back to "
            "reference key %r. Check launch-readiness item L11.",
            reference_key,
        )
        return _build_fallback_context(reference_key, is_preview=is_preview)

    # Load customization for (store, theme)
    customization_json = _load_customization(store, theme) if store is not None else {}

    # Resolve structural toggles
    structural = dict(_STRUCTURAL_DEFAULTS)
    custom_structural = customization_json.get("structural", {})
    if isinstance(custom_structural, dict):
        structural.update(
            {k: v for k, v in custom_structural.items() if isinstance(v, bool)}
        )

    tokens = resolve_tokens(theme.source_ref, customization_json)
    token_css = _build_token_css(tokens)
    card_ratio = _CARD_ASPECT_RATIO.get(theme.source_ref, "4:5")

    return ThemeContext(
        key=theme.source_ref,
        is_preview=is_preview,
        tokens=tokens,
        token_css=token_css,
        structural=structural,
        card_aspect_ratio=card_ratio,
        # Phase 4: tokens.css was renamed to theme.css (single stylesheet per theme).
        # Path is relative to the storefront app's static/ root.
        css_path=f"storefront/themes/{theme.source_ref}/theme.css",
        customization=customization_json,
        # tokens_css_path is now empty: theme.css contains both tokens and skin.
        tokens_css_path="",
    )


def _load_customization(store, theme) -> dict:
    """
    Load StoreThemeCustomization JSON for (store, theme), returning {} on miss.

    Uses the StoreScopedManager (.for_store()) to respect tenant isolation.
    """
    from stores.models import StoreThemeCustomization

    cust = StoreThemeCustomization.objects.for_store(store).filter(theme=theme).first()
    if cust is None:
        return {}
    return cust.customization_json if isinstance(cust.customization_json, dict) else {}


def _build_fallback_context(key: str, *, is_preview: bool) -> ThemeContext:
    """Build a minimal ThemeContext from a key string only (no DB row required)."""
    return ThemeContext(
        key=key,
        is_preview=is_preview,
        tokens={},
        token_css="",
        structural=dict(_STRUCTURAL_DEFAULTS),
        card_aspect_ratio=_CARD_ASPECT_RATIO.get(key, "4:5"),
        # Phase 4: tokens.css was renamed to theme.css (single stylesheet per theme).
        css_path=f"storefront/themes/{key}/theme.css",
        customization={},
        tokens_css_path="",
    )
