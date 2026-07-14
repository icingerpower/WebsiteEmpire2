from django.apps import AppConfig


class PixelsConfig(AppConfig):
    """
    Pixel integrations app config (ADR-022, TICKET-030).

    ready() performs the self-registration side effects the ADR requires:
      - pixels.providers registers the five PixelProvider instances into the
        pixels.registry module-level dict (same pattern as every other
        registry in this codebase).
      - pixels.slot_provider registers the single bridging PixelsSlotProvider
        into storefront.slots (ADR-012 D9).

    Both imports are side-effect-only (module-level register() calls) — the
    `noqa: F401` markers are intentional, not oversights.
    """

    name = "pixels"
    verbose_name = "Pixel Integrations"
    default_auto_field = "django.db.models.BigAutoField"

    def ready(self):
        import pixels.providers  # noqa: F401 — self-registers into pixels.registry
        import pixels.slot_provider  # noqa: F401 — registers into storefront.slots
