"""
Admin registrations for the permalinks app (TICKET-016).

Both Permalink and SlugRedirect are registered on super_admin_site only.
Store admins have no access — redirects are auto-managed by the slug_changed
signal and are not manually creatable in the store-admin UI (DECIDED ADR-005 §6).

Super-admins can inspect, toggle is_active on Permalink rows, and bulk-delete
SlugRedirect rows (e.g. after a bulk import cleanup).
"""

from django.contrib import admin

from permalinks.models import Permalink, SlugRedirect
from webecom.admin import super_admin_site


@admin.register(Permalink, site=super_admin_site)
class PermalinkAdmin(admin.ModelAdmin):
    list_display = (
        "store", "lang", "slug", "content_type", "object_id", "is_active", "auto_created",
        "created_at",
    )
    list_filter = ("lang", "is_active", "auto_created", "store")
    search_fields = ("slug",)
    readonly_fields = ("content_type", "object_id", "auto_created", "trigger", "created_at")
    raw_id_fields = ("store",)


@admin.register(SlugRedirect, site=super_admin_site)
class SlugRedirectAdmin(admin.ModelAdmin):
    list_display = (
        "store", "from_lang", "from_slug", "to_slug", "to_lang",
        "redirect_type", "is_active", "auto_created", "created_at",
    )
    list_filter = ("redirect_type", "from_lang", "is_active", "auto_created", "store")
    search_fields = ("from_slug", "to_slug")
    readonly_fields = ("auto_created", "trigger", "created_at")
    raw_id_fields = ("store",)
