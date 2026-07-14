from django.apps import AppConfig


class CatalogConfig(AppConfig):
    name = 'catalog'
    default_auto_field = 'django.db.models.BigAutoField'

    def ready(self):
        import catalog.signals  # noqa: F401 — registers signal receivers
        import catalog.ai_jobs  # noqa: F401 — registers translation job type in aijobs registry
