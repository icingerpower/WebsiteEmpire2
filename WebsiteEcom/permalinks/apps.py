"""
AppConfig for the permalinks app.

ready() imports the signal module so that handle_slug_change is connected to
catalog.signals.slug_changed at startup.  Without this import the receiver
decorator never fires.
"""

from django.apps import AppConfig


class PermalinksConfig(AppConfig):
    default_auto_field = "django.db.models.BigAutoField"
    name = "permalinks"

    def ready(self):
        import permalinks.signals  # noqa: F401
