from django.apps import AppConfig


class PagesConfig(AppConfig):
    default_auto_field = "django.db.models.BigAutoField"
    name = "pages"
    verbose_name = "Static Pages"

    def ready(self):
        # Import signal handlers so they connect on app startup (ADR-018 D1/D5).
        import pages.signals  # noqa: F401
