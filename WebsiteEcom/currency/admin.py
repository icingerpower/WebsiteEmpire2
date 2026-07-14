"""
Currency admin registrations (ADR-023 §3, TICKET-037).

Admin split, correcting `06_settings_requirements.md` §8's placement in favor
of ADR-001 §3b/§5 (currencies are platform-global, same tier as Themes and
Shipping Zones):

  /superadmin/  — CurrencyDefinition (master data), CurrencyRate (manual entry
                  or read-only display when source='api:ecb'),
                  CurrencyConverterSettings (auto-refresh + rounding mode).
  /admin/       — StoreCurrencySetting ONLY: a simple enable/disable checklist
                  against the platform's CurrencyDefinition list. No symbol/
                  rate editing here — exactly mirroring how a store *activates*
                  a platform-owned Theme without editing the theme's shared
                  definition (see shipping/admin.py's ShippingCarrier/
                  ShippingZone vs ShippingRate split for the precedent).

Permission boundary (ADR-023 §3 "Tests required"): CurrencyDefinition and
CurrencyRate are registered ONLY on super_admin_site — a store admin has no
admin-UI route to them at all (proven in currency/tests/test_admin.py
by asserting they are absent from store_admin_site's registry, mirroring this
ADR's own "prove absence of a writer" testing philosophy).
"""

from django import forms
from django.contrib import admin

from core.formsets import StoreSafeModelFormSet
from webecom.admin import store_admin_site, super_admin_site

from .models import (
    CurrencyConverterSettings,
    CurrencyDefinition,
    CurrencyRate,
    RateSourceChoices,
    StoreCurrencySetting,
)


# ---------------------------------------------------------------------------
# Platform-level: super admin only
# ---------------------------------------------------------------------------


class CurrencyRateInline(admin.StackedInline):
    """
    Edit a currency's rate directly from its CurrencyDefinition page.

    rate_to_reference is read-only once source='api:ecb' — an auto-fetched
    rate is edited by disabling auto-refresh (or waiting for the next run),
    not by hand-editing a value the refresh task will overwrite next cycle.
    """

    model = CurrencyRate
    extra = 0
    max_num = 1
    fields = ["rate_to_reference", "source", "updated_at", "previous_rate", "last_refresh_error"]
    readonly_fields = ["updated_at", "previous_rate", "last_refresh_error"]

    def get_readonly_fields(self, request, obj=None):
        readonly = list(self.readonly_fields)
        if obj is not None:
            rate = getattr(obj, "rate", None)
            if rate is not None and rate.source != RateSourceChoices.MANUAL:
                readonly.append("rate_to_reference")
        return readonly


@admin.register(CurrencyDefinition, site=super_admin_site)
class CurrencyDefinitionAdmin(admin.ModelAdmin):
    """
    show_code/code_placement (ADR-023 §2 addendum, human decision 2026-07-11)
    restore the admin-011 screenshot's "Code visibility" segmented control,
    independent of the pre-existing "Symbol visibility"-equivalent
    symbol_placement field.
    """

    list_display = [
        "code",
        "name",
        "symbol",
        "symbol_placement",
        "show_code",
        "code_placement",
        "decimal_places",
        "is_active",
    ]
    list_filter = ["is_active", "symbol_placement", "show_code"]
    search_fields = ["code", "name"]
    inlines = [CurrencyRateInline]


@admin.register(CurrencyRate, site=super_admin_site)
class CurrencyRateAdmin(admin.ModelAdmin):
    """
    Standalone rate list — the admin dashboard view of ADR-023 §1's staleness
    badge and per-currency error info ("last updated Xh ago", red when stale).
    """

    list_display = ["currency", "rate_to_reference", "source", "updated_at", "is_stale_display", "last_refresh_error"]
    list_filter = ["source"]
    search_fields = ["currency__code"]
    raw_id_fields = ["currency"]

    @admin.display(boolean=True, description="Stale")
    def is_stale_display(self, obj):
        return obj.is_stale

    def get_readonly_fields(self, request, obj=None):
        readonly = []
        if obj is not None and obj.source != RateSourceChoices.MANUAL:
            readonly.append("rate_to_reference")
        return readonly


class CurrencyConverterSettingsForm(forms.ModelForm):
    class Meta:
        model = CurrencyConverterSettings
        fields = ["auto_refresh_enabled", "rounding_mode", "stale_hours"]


@admin.register(CurrencyConverterSettings, site=super_admin_site)
class CurrencyConverterSettingsAdmin(admin.ModelAdmin):
    """
    Singleton admin (ADR-023 §2) — T037's "rate-source configuration and
    refresh cadence" screen. The changelist redirects straight to the single
    row's change form; add/delete are disabled so there is never more than
    one row to confuse an admin.
    """

    form = CurrencyConverterSettingsForm

    def has_add_permission(self, request):
        return not CurrencyConverterSettings.objects.exists()

    def has_delete_permission(self, request, obj=None):
        return False

    def changelist_view(self, request, extra_context=None):
        from django.shortcuts import redirect
        from django.urls import reverse

        obj = CurrencyConverterSettings.get_solo()
        return redirect(reverse("super_admin:currency_currencyconvertersettings_change", args=[obj.pk]))


# ---------------------------------------------------------------------------
# Store-level: store admin only (enable/disable checklist)
# ---------------------------------------------------------------------------


@admin.register(StoreCurrencySetting, site=store_admin_site)
class StoreCurrencySettingAdmin(admin.ModelAdmin):
    """
    Per-store enable/disable checklist against the platform's CurrencyDefinition
    list (ADR-023 §3). No symbol/rate editing here.
    """

    module_key = "currency_converter"

    list_display = ["currency", "is_enabled"]
    list_editable = ["is_enabled"]
    fields = ["currency", "is_enabled"]

    def get_queryset(self, request):
        store = getattr(request, "store", None)
        if store is None:
            return self.model.objects.none()
        return self.model.objects.for_store(store)

    def get_changelist_formset(self, request, **kwargs):
        """
        list_editable on a StoreOwnedModel changelist reuses BaseModelFormSet
        .add_fields() (same mechanism as an inline's formset) to build the
        hidden pk field for each existing row from the model's raw,
        unscoped default manager — INLINE-FORMSET-PK-ISOLATION (ADR-031
        addendum, TICKET-051). Without this override, the bulk-save POST
        that list_editable renders 500s with IsolationError. StoreSafeModelFormSet
        swaps that pk field's queryset for the changelist's own (this
        ModelAdmin's get_queryset(request), already scoped above).
        """
        kwargs.setdefault("formset", StoreSafeModelFormSet)
        return super().get_changelist_formset(request, **kwargs)

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        if db_field.name == "currency":
            kwargs["queryset"] = CurrencyDefinition.objects.filter(is_active=True)
        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def save_model(self, request, obj, form, change):
        store = getattr(request, "store", None)
        if store is not None:
            obj.store = store
        super().save_model(request, obj, form, change)
