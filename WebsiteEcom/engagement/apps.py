from django.apps import AppConfig


class EngagementConfig(AppConfig):
    """
    Engagement app config (ADR-027, TICKET-034/TICKET-035).

    ready() registers LeadCaptureOverlayProvider and SocialProofProvider into
    the ADR-012 slot registry under the already-declared "overlay" and
    "social_proof" slots — the same self-registration pattern every other
    storefront integration app uses (pixels, consent, chat).
    """

    default_auto_field = "django.db.models.BigAutoField"
    name = "engagement"
    verbose_name = "Lead capture & social proof"

    def ready(self):
        import engagement.slot_providers  # noqa: F401 — registers into storefront.slots
