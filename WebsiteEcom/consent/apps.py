from django.apps import AppConfig


class ConsentConfig(AppConfig):
    """
    Consent management app config (ADR-025, TICKET-048).

    ready() registers ConsentSlotProvider into the ADR-012 slot registry under
    the already-declared "consent" slot (ADR-012 D9 / TH-141) — the same
    self-registration pattern every other storefront integration app uses
    (pixels, chat). See consent/slot_provider.py.

    TICKET-048 scope: is_enabled()/validate_settings() only — render() is a
    placeholder pending TICKET-049 (banner/panel markup, CSS/JS, footer
    button, fr translations).
    """

    default_auto_field = "django.db.models.BigAutoField"
    name = "consent"
    verbose_name = "Consent management"

    def ready(self):
        import consent.slot_provider  # noqa: F401 — registers into storefront.slots
