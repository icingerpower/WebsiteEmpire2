from django.apps import AppConfig


class CoreConfig(AppConfig):
    name = "core"

    def ready(self):
        """
        Connect the ADR-031 write-side M2M cross-store guard once the full
        app registry is loaded (models from every app must already be
        importable so local_many_to_many introspection sees every field).
        """
        from core.m2m_guard import connect_store_m2m_guards

        connect_store_m2m_guards()
