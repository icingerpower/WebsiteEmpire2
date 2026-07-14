"""
Shipping admin registrations.

Platform-level models (ShippingCarrier, ShippingZone) are on super_admin_site only.
Store-level models (ShippingRate, ShippingRateException) are on both admin sites.
In the store admin, get_queryset uses .for_store(request.store) to enforce tenant isolation.
"""

from django.contrib import admin

from core.admin import StoreOwnedInlineMixin
from webecom.admin import store_admin_site, super_admin_site

from .models import ShippingCarrier, ShippingRate, ShippingRateException, ShippingZone


# ---------------------------------------------------------------------------
# Platform-level: super admin only
# ---------------------------------------------------------------------------


@admin.register(ShippingCarrier, site=super_admin_site)
class ShippingCarrierAdmin(admin.ModelAdmin):
    list_display = ["name", "is_active", "tracking_url_template"]
    list_filter = ["is_active"]
    search_fields = ["name"]


@admin.register(ShippingZone, site=super_admin_site)
class ShippingZoneAdmin(admin.ModelAdmin):
    list_display = ["name", "is_catch_all_display", "is_active"]
    list_filter = ["is_active"]
    search_fields = ["name"]
    fields = ["name", "country_codes", "service_description", "is_active"]

    @admin.display(boolean=True, description="All Countries")
    def is_catch_all_display(self, obj):
        return obj.is_catch_all


# ---------------------------------------------------------------------------
# Store-level: both admin sites
# ---------------------------------------------------------------------------


class ShippingRateExceptionInline(StoreOwnedInlineMixin, admin.TabularInline):
    """
    ShippingRateException IS a StoreOwnedModel but this inline previously had
    NO get_queryset() override — a latent INLINE-FORMSET-PK-ISOLATION /
    missing-scoping bug (same class as the one this ticket's mixin fixes
    everywhere else), found while migrating all inlines onto
    StoreOwnedInlineMixin. Any GET/POST of a ShippingRate change form with
    >=1 existing exception row would have 500'd on
    `self.model._default_manager.get_queryset()` (Django's InlineModelAdmin
    default) before this fix.
    """

    model = ShippingRateException
    extra = 0
    fields = ["exception_type", "product", "collection", "override_price"]


class ShippingRateAdminBase(admin.ModelAdmin):
    list_display = ["name", "zone", "carrier", "rate_type", "price", "is_active"]
    list_filter = ["rate_type", "is_active", "zone"]
    search_fields = ["name"]
    inlines = [ShippingRateExceptionInline]


@admin.register(ShippingRate, site=store_admin_site)
class ShippingRateStoreAdmin(ShippingRateAdminBase):
    # ADR-033 D3c DECIDED (human 2026-07-11, ADR-033 D7 item 6): the 19-module grid has no dedicated shipping
    # row; store-level shipping config sits under Settings.
    module_key = "settings"

    def get_queryset(self, request):
        store = getattr(request, "store", None)
        if store is None:
            return self.model.objects.none()
        return self.model.objects.for_store(store)


@admin.register(ShippingRate, site=super_admin_site)
class ShippingRateSuperAdmin(ShippingRateAdminBase):
    list_display = ["name", "zone", "carrier", "rate_type", "price", "is_active", "store"]
