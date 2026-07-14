"""
Storefront app configuration (ADR-012, TICKET-029 Phase 1).

The storefront app owns:
  - Theme resolution + ThemeMiddleware (storefront/theme.py, storefront/middleware.py)
  - Slot registry + SlotProvider protocol (storefront/slots.py)
  - Base template + shared DOM contract (storefront/templates/storefront/)
  - URL dispatcher / catch-all (storefront/urls.py, storefront/views.py)
  - Context processor exposing theme_ctx (storefront/context_processors.py)

System checks registered in ready() validate startup invariants (§XV-1):
  - STOREFRONT_REFERENCE_THEME is set in settings.
"""

from django.apps import AppConfig
from django.core import checks
from django.conf import settings


@checks.register(checks.Tags.models)
def check_storefront_settings(app_configs, **kwargs):
    """
    Verify that the mandatory storefront settings are present at startup (§XV-1).
    Missing settings fail the deployment check loudly.
    """
    errors = []
    if not getattr(settings, "STOREFRONT_REFERENCE_THEME", ""):
        errors.append(
            checks.Error(
                "STOREFRONT_REFERENCE_THEME must be set in Django settings "
                "(e.g. 'general'). The storefront cannot resolve a fallback theme "
                "without it.",
                id="storefront.E001",
            )
        )
    return errors


class StorefrontConfig(AppConfig):
    name = "storefront"
    verbose_name = "Storefront"
    default_auto_field = "django.db.models.BigAutoField"
