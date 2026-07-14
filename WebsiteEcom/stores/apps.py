from django.apps import AppConfig


class StoresConfig(AppConfig):
    name = "stores"

    def ready(self):
        # Register signal handlers.  Import is deferred to ready() so that the
        # app registry is fully loaded before signals reference model classes.
        import stores.signals  # noqa: F401
