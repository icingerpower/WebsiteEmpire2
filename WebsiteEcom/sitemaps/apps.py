"""
Sitemaps app (TICKET-026).

Provides per-domain /sitemap.xml index, per-language /sitemap-<lang>.xml child
sitemaps, and /robots.txt — all scoped to the store resolved from the Host header
by stores.middleware.LocaleMiddleware.
"""

from django.apps import AppConfig


class SitemapsConfig(AppConfig):
    default_auto_field = "django.db.models.BigAutoField"
    name = "sitemaps"
    verbose_name = "Sitemaps"
