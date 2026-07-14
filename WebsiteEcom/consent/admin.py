"""
Store-admin registration for the consent app (ADR-025 D6).

Both ConsentSettings and ConsentRecord are StoreOwnedModel and belong on
store_admin_site only (mirrors chat/admin.py's split rationale — currency's
super_admin_site/store_admin_site split does not apply here since nothing in
this app is platform-global data).

ConsentSettings enabling/disabling flow (ADR-025 D6, mirrors chat/admin.py's
StoreChatSettingsAdminForm shape, INVERTED condition): chat requires a
checkbox ticked to allow is_enabled=True (opt-in); consent requires a
checkbox ticked to allow is_enabled=False (the only path off the
safe-by-default banner). Ticking it stamps non_eu_acknowledged_at +
non_eu_acknowledged_by in the same save() as disabling; re-enabling later
never clears those two fields — the acknowledgment audit trail is permanent
once given, exactly like chat's subprocessor_terms_accepted_at.

ConsentRecord is a read-only audit table (ADR-025 D6: "no edit/delete in
admin beyond the retention purge") — has_add_permission/has_change_permission/
has_delete_permission all return False; a superuser or store employee with
view permission can still browse/inspect rows via Django's built-in
view-only admin support.
"""

from django import forms
from django.contrib import admin
from django.utils import timezone

from pages.models import StaticPage
from webecom.admin import store_admin_site

from consent.models import ConsentRecord, ConsentSettings


class ConsentSettingsAdminForm(forms.ModelForm):
    non_eu_ack_confirm = forms.BooleanField(
        required=False,
        label="This store does not target EU/EEA visitors; I am responsible for tracker compliance",
        help_text=(
            "Required to disable the consent banner (ADR-025 D6). Existing pixels "
            "will otherwise block the launch checklist. This acknowledgment is "
            "permanent once given, even if the banner is re-enabled later."
        ),
    )

    class Meta:
        model = ConsentSettings
        fields = ("is_enabled", "beacon_requires_consent", "policy_page")

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        if self.instance and self.instance.pk and self.instance.non_eu_acknowledged_at:
            self.fields["non_eu_ack_confirm"].initial = True
            self.fields["non_eu_ack_confirm"].disabled = True
            self.fields["non_eu_ack_confirm"].help_text += (
                f" (acknowledged {self.instance.non_eu_acknowledged_at:%Y-%m-%d})"
            )

    def clean(self):
        cleaned = super().clean()
        already_acknowledged = bool(self.instance.pk and self.instance.non_eu_acknowledged_at)
        if (
            cleaned.get("is_enabled") is False
            and not already_acknowledged
            and not cleaned.get("non_eu_ack_confirm")
        ):
            raise forms.ValidationError(
                "You must acknowledge that this store does not target EU/EEA "
                "visitors before disabling the consent banner."
            )
        return cleaned


@admin.register(ConsentSettings, site=store_admin_site)
class ConsentSettingsAdmin(admin.ModelAdmin):
    # ADR-033 D3c DECIDED (human 2026-07-11, ADR-033 D7 item 6): no dedicated "consent" row; store-level
    # compliance configuration sits under Settings.
    module_key = "settings"

    form = ConsentSettingsAdminForm
    list_display = ("store", "is_enabled", "non_eu_acknowledged_at", "beacon_requires_consent", "policy_page")

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return ConsentSettings.objects.for_store(request.store)
        return ConsentSettings.objects.none()

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        store = getattr(request, "store", None)
        if store is not None and db_field.name == "policy_page":
            kwargs["queryset"] = StaticPage.objects.for_store(store)
        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def save_model(self, request, obj, form, change):
        if not change and getattr(request, "store", None):
            obj.store = request.store
        # Stamp acknowledgment in the SAME action as disabling; never clear
        # it afterwards even if is_enabled later flips back to True.
        if form.cleaned_data.get("non_eu_ack_confirm") and not obj.non_eu_acknowledged_at:
            obj.non_eu_acknowledged = True
            obj.non_eu_acknowledged_at = timezone.now()
            obj.non_eu_acknowledged_by = request.user
        super().save_model(request, obj, form, change)


@admin.register(ConsentRecord, site=store_admin_site)
class ConsentRecordAdmin(admin.ModelAdmin):
    """Read-only audit trail (ADR-025 D6) — no add/edit/delete in admin."""

    module_key = "settings"

    list_display = (
        "consent_id", "store", "action", "analytics", "marketing",
        "am_objected", "policy_version", "lang", "source", "created_at",
    )
    list_filter = ("action", "source", "analytics", "marketing")
    search_fields = ("consent_id",)
    readonly_fields = [f.name for f in ConsentRecord._meta.fields]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return ConsentRecord.objects.for_store(request.store)
        return ConsentRecord.objects.none()

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False
