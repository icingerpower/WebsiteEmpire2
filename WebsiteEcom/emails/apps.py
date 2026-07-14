from django.apps import AppConfig


class EmailsConfig(AppConfig):
    name = "emails"
    verbose_name = "Emails"

    def ready(self):
        import emails.signals  # noqa: F401 — registers post_save handlers
