"""
Store-admin registrations for the engagement app (ADR-027, admin-015).

Permission gating uses the "apps" module key (spec 04_admin_flows.md §6 module
grid: "Apps | Full access / No access" — a two-state module, unlike "orders"
which also has "limited"). Module permission gate: stores.permissions.
StoreModulePermissionMixin, auto-applied at registration by
webecom.admin.StoreAdminSite.register() (ADR-033 D3b) — these admins declare
`module_key = "apps"` and no longer define their own has_*_permission methods
(the generic view=limited/mutate=full mapping is equivalent to the old
level="full"-for-everything convention here, since "apps" only ever stores
"full" or "none" — there is no "limited" value to diverge on).
"""

from django import forms
from django.contrib import admin
from django.utils.translation import gettext_lazy as _

from core.admin import StoreOwnedInlineMixin
from webecom.admin import store_admin_site

from engagement.models import (
    LeadCaptureCampaign,
    LeadCaptureCampaignTranslation,
    LeadSignup,
    NAMED_SOCIAL_PROOF_MODES_ENABLED,
    SocialProofDisplayMode,
    SocialProofSettings,
)
from engagement.themes import overlay_theme_choices
from engagement.widgets import OverlayThemeSelect


# ---------------------------------------------------------------------------
# LeadCaptureCampaign — admin-015 campaign list + create/edit
# ---------------------------------------------------------------------------


class LeadCaptureCampaignAdminForm(forms.ModelForm):
    """Restricts overlay_theme_key to the registry (engagement.themes.OVERLAY_THEMES).

    Same field name, same registry-derived choices, same validation as a plain
    forms.ChoiceField — only the widget changes, to render the "SELECT A
    THEME" preview grid from the screenshots (admin-015-02/03) instead of a
    plain <select> (engagement.widgets.OverlayThemeSelect).
    """

    overlay_theme_key = forms.ChoiceField(choices=overlay_theme_choices, widget=OverlayThemeSelect)

    class Meta:
        model = LeadCaptureCampaign
        fields = "__all__"


class LeadCaptureCampaignTranslationInline(StoreOwnedInlineMixin, admin.TabularInline):
    """Read-only inline — mirrors pages.admin.StaticPageTranslationInline."""

    model = LeadCaptureCampaignTranslation
    extra = 0
    can_delete = False
    fields = ["lang_code", "status", "ai_job", "updated_at"]
    readonly_fields = ["lang_code", "status", "ai_job", "updated_at"]
    verbose_name = "translation status"
    verbose_name_plural = "translation statuses"

    def has_add_permission(self, request, obj=None):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def get_queryset(self, request):
        # StoreOwnedInlineMixin supplies cross_store_unsafe(); chain the
        # extra select_related() this inline needs on top of it.
        return super().get_queryset(request).select_related("ai_job")


@admin.register(LeadCaptureCampaign, site=store_admin_site)
class LeadCaptureCampaignAdmin(admin.ModelAdmin):
    module_key = "apps"

    form = LeadCaptureCampaignAdminForm
    list_display = [
        "name", "is_active", "visitors_count", "impressions_count",
        "conversion_rate_display", "conversions_count", "updated_at",
    ]
    list_filter = ["is_active"]
    search_fields = ["name"]
    readonly_fields = [
        "visitors_count", "impressions_count", "conversions_count",
        "created_at", "updated_at",
    ]
    inlines = [LeadCaptureCampaignTranslationInline]

    fieldsets = [
        ("General settings", {
            "fields": [
                "name", "is_active", "trigger", "trigger_delay_seconds",
                "mobile_trigger", "mobile_trigger_delay_seconds", "excluded_pages",
            ],
        }),
        ("Select a theme", {"fields": ["overlay_theme_key"]}),
        ("Content", {
            "fields": ["headline", "body", "cta_label", "dismiss_label"],
        }),
        ("Upon a successful sign-up", {
            "fields": ["post_signup_action", "success_message", "redirect_url"],
        }),
        ("Frequency capping", {
            "fields": ["cap_enabled", "cap_impressions", "cap_window_value", "cap_window_unit"],
        }),
        ("Tags for customers that signup", {"fields": ["signup_tags"]}),
        ("Reward", {"fields": ["reward_discount_code"]}),
        ("Stats", {
            "fields": ["visitors_count", "impressions_count", "conversions_count"],
            "classes": ["collapse"],
        }),
        ("Meta", {"fields": ["created_at", "updated_at"], "classes": ["collapse"]}),
    ]

    @admin.display(description="Conversion rate")
    def conversion_rate_display(self, obj):
        return f"{obj.conversion_rate:.0%}"

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return LeadCaptureCampaign.objects.for_store(request.store)
        return LeadCaptureCampaign.objects.none()

    def save_model(self, request, obj, form, change):
        if not change and getattr(request, "store", None):
            obj.store = request.store
        super().save_model(request, obj, form, change)

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        store = getattr(request, "store", None)
        if store is not None and db_field.name == "reward_discount_code":
            from discounts.models import DiscountCode, ProvenanceType

            # LOW-4b (routed spec-review finding): only offer codes issued
            # FOR lead capture — a merchant/campaign/sold code shared as a
            # reward here would break the admin-013/ADR-026 audit trail.
            # Model.clean() enforces this again as defense in depth.
            kwargs["queryset"] = DiscountCode.objects.for_store(store).filter(
                provenance=ProvenanceType.LEAD_CAPTURE
            )
        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def formfield_for_manytomany(self, db_field, request, **kwargs):
        store = getattr(request, "store", None)
        if store is not None and db_field.name == "excluded_pages":
            from permalinks.models import Permalink

            kwargs["queryset"] = Permalink.objects.for_store(store).filter(is_active=True)
        return super().formfield_for_manytomany(db_field, request, **kwargs)


@admin.register(LeadCaptureCampaignTranslation, site=store_admin_site)
class LeadCaptureCampaignTranslationAdmin(admin.ModelAdmin):
    module_key = "apps"

    list_display = ["campaign", "lang_code", "status", "updated_at"]
    list_filter = ["status", "lang_code"]
    search_fields = ["campaign__name", "lang_code"]
    readonly_fields = ["ai_job", "created_at", "updated_at"]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return LeadCaptureCampaignTranslation.objects.for_store(request.store)
        return LeadCaptureCampaignTranslation.objects.none()

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        store = getattr(request, "store", None)
        if store is not None:
            if db_field.name == "campaign":
                kwargs["queryset"] = LeadCaptureCampaign.objects.for_store(store)
            elif db_field.name == "ai_job":
                from aijobs.models import AiJob

                kwargs["queryset"] = AiJob.objects.for_store(store)
        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def save_model(self, request, obj, form, change):
        if not change and getattr(request, "store", None):
            obj.store = request.store
        super().save_model(request, obj, form, change)


@admin.register(LeadSignup, site=store_admin_site)
class LeadSignupAdmin(admin.ModelAdmin):
    """View-only audit/consent-proof table — mirrors CampaignIssuedCodeAdmin."""

    module_key = "apps"

    list_display = ["email", "campaign", "lang", "confirmed_at", "created_at"]
    list_filter = ["campaign", "lang"]
    search_fields = ["email"]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return LeadSignup.objects.for_store(request.store)
        return LeadSignup.objects.none()

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False


# ---------------------------------------------------------------------------
# SocialProofSettings — one row per store
# ---------------------------------------------------------------------------


class SocialProofSettingsAdminForm(forms.ModelForm):
    """
    ADR-027 D6 gating mechanism: while NAMED_SOCIAL_PROOF_MODES_ENABLED is
    False, display_mode only offers 'anonymous' as a selectable choice — the
    named modes exist in the schema (SocialProofDisplayMode) but are neither
    selectable nor rendered until the legal flag flips. The model's own
    clean() is the second, defense-in-depth layer (§XV-1: loud, not silent)
    for any row saved outside this form.
    """

    class Meta:
        model = SocialProofSettings
        fields = "__all__"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        if not NAMED_SOCIAL_PROOF_MODES_ENABLED:
            self.fields["display_mode"].choices = [
                (SocialProofDisplayMode.ANONYMOUS, SocialProofDisplayMode.ANONYMOUS.label)
            ]
            self.fields["display_mode"].help_text = _(
                "Named modes (first name / first name + city) are pending human/legal "
                "sign-off (ADR-027 D6) and are not selectable yet."
            )


@admin.register(SocialProofSettings, site=store_admin_site)
class SocialProofSettingsAdmin(admin.ModelAdmin):
    module_key = "apps"

    form = SocialProofSettingsAdminForm
    list_display = ["store", "is_enabled", "display_mode", "window_hours", "position"]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return SocialProofSettings.objects.for_store(request.store)
        return SocialProofSettings.objects.none()

    def save_model(self, request, obj, form, change):
        if not change and getattr(request, "store", None):
            obj.store = request.store
        super().save_model(request, obj, form, change)
