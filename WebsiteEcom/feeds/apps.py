from django.apps import AppConfig


class FeedsConfig(AppConfig):
    """
    Catalog feeds app config (ADR-026, TICKET-033).

    ready() performs the self-registration side effect the ADR requires:
    feeds.providers registers the Google Shopping and Facebook DPA
    FeedProvider instances into feeds.registry's module-level dict — the same
    registry pattern already proven by pixels/apps.py and storefront.slots
    (design-pattern-ideas §IX).

    feeds.signals wires the dirty-marking receivers (ADR-026 D3).
    """

    name = "feeds"
    verbose_name = "Catalog Feeds"
    default_auto_field = "django.db.models.BigAutoField"

    def ready(self):
        import feeds.providers  # noqa: F401 — self-registers into feeds.registry
        import feeds.signals  # noqa: F401 — registers dirty-marking signal receivers
