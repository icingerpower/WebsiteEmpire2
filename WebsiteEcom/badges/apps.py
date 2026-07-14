from django.apps import AppConfig


class BadgesConfig(AppConfig):
    """
    Security badge designer app config (ADR-030, TICKET-046).

    ready() registers SecurityBadgeSlotProvider into the already-declared
    ADR-012 "security_badge" slot — the same self-registration pattern every
    other storefront integration app uses (pixels, consent, chat, engagement).
    """

    default_auto_field = "django.db.models.BigAutoField"
    name = "badges"
    verbose_name = "Security badges"

    def ready(self):
        import badges.slot_provider  # noqa: F401 — registers into storefront.slots
