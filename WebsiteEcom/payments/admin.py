"""
Payments admin — all payment models registered on super_admin_site only (ADR-006-R).

Payment models are platform-global and owned by Organization; they belong on the
/superadmin/ surface, not the per-store /admin/.

Security: credential fields (api_key, api_secret, webhook_secret) are readonly —
they are never editable directly via the admin form.  Credentials must be written
via a dedicated secure input flow (TICKET-018+).  Readonly fields display the
decrypted plaintext value (via EncryptedCharField.from_db_value), so access to
/superadmin/ must be restricted to trusted super-admin users only.

New in TICKET-017R:
  - RoutingRule registration is REMOVED (model dropped).
  - DecisionLog admin updated for revised schema.
  - OrgPool + OrgPoolMember inline added.
  - OrganizationRule admin with shadow-warning surfacing in save_model.
  - PaymentMethod + ProcessorOption inline added.
  - ProcessorSplitCounter read-only list (debugging aid).
"""

from django import forms
from django.contrib import admin, messages

from core.choices import COUNTRY_CHOICES
from core.fields import JsonMultipleChoiceField
from webecom.admin import super_admin_site

from .models import (
    DecisionLog,
    OrgPool,
    OrgPoolMember,
    OrganizationRule,
    PaymentMethod,
    ProcessorAccount,
    ProcessorOption,
    ProcessorSplitCounter,
)


# ---------------------------------------------------------------------------
# ProcessorAccount
# ---------------------------------------------------------------------------


class ProcessorAccountAdminForm(forms.ModelForm):
    """
    Custom form for ProcessorAccountAdmin.

    supported_countries uses FilteredSelectMultiple (dual-list widget) backed by a
    fixed country vocabulary — admins pick from the list rather than typing raw codes.
    An empty selection means "all countries" (per the model field's help_text).
    """

    supported_countries = JsonMultipleChoiceField(
        choices=COUNTRY_CHOICES,
        label="Supported countries",
    )

    class Meta:
        model = ProcessorAccount
        fields = "__all__"


@admin.register(ProcessorAccount, site=super_admin_site)
class ProcessorAccountAdmin(admin.ModelAdmin):
    """
    Super-admin view of payment processor credentials.

    Credentials are shown as decrypted plaintext in readonly mode — they are
    never writable through this form.  Assign credentials via a dedicated
    secure endpoint (TICKET-018+).

    PayPal no-Vault guard (ADR-011 Q3, T028):
    Saving with paypal_vault_enabled=False while any active Campaign for the same
    store uses paypal_capture_mode='delayed' shows a non-blocking admin WARNING.
    This prevents a misconfiguration where shoppers would see the upsell funnel but
    every accept attempt silently fails (§XV-1 invisible-failure class).
    """

    form = ProcessorAccountAdminForm
    list_display = [
        "organization",
        "processor_type",
        "display_name",
        "method_family",
        "is_active",
        "is_test_mode",
        "health_status",
        "health_source",
        "created_at",
    ]
    list_filter = ["processor_type", "method_family", "is_active", "is_test_mode", "health_status"]
    search_fields = ["display_name", "organization__name", "account_id", "client_id"]
    ordering = ["organization", "processor_type"]

    readonly_fields = [
        "api_key",
        "api_secret",
        "webhook_secret",
        "created_at",
        "updated_at",
    ]

    fieldsets = [
        (
            None,
            {
                "fields": [
                    "organization",
                    "processor_type",
                    "display_name",
                    "is_active",
                    "is_test_mode",
                ]
            },
        ),
        (
            "Method family & routing priority",
            {
                "fields": [
                    "method_family",
                    "supported_countries",
                    "settlement_currency",
                    "priority_within_family",
                    "may_be_primary",
                    "backup_only",
                ],
            },
        ),
        (
            "PayPal capture & vault (PayPal processor only)",
            {
                "fields": ["paypal_capture_mode", "paypal_vault_enabled"],
                "description": (
                    "PayPal only — ignored for all other processor types. "
                    "Delayed = authorize-then-capture (funnel-eligible when vault is enabled). "
                    "paypal_vault_enabled requires completed PayPal Vault onboarding. "
                    "WARNING: setting vault=False while active campaigns rely on delayed capture "
                    "will make the funnel silently fail for all PayPal-routed orders."
                ),
                "classes": ["collapse"],
            },
        ),
        (
            "Health state",
            {
                "fields": [
                    "health_status",
                    "health_source",
                    "health_checked_at",
                    "health_detail",
                ],
                "description": (
                    "Health probe result. UNKNOWN = not yet probed (still routable). "
                    "RED = excluded from routing. GREEN/YELLOW = routable (YELLOW sorts last)."
                ),
            },
        ),
        (
            "Routing identifiers (non-secret)",
            {
                "fields": ["account_id", "client_id"],
                "description": (
                    "Non-secret identifiers used in connector routing logic. "
                    "Safe to view and edit here."
                ),
            },
        ),
        (
            "Encrypted credentials (readonly)",
            {
                "fields": ["api_key", "api_secret", "webhook_secret"],
                "description": (
                    "Credentials are stored encrypted at rest (Fernet AES-128). "
                    "Shown here as decrypted plaintext — access is restricted to super-admins. "
                    "To update credentials, use the dedicated secure input flow (TICKET-018+)."
                ),
            },
        ),
        (
            "Timestamps",
            {
                "fields": ["created_at", "updated_at"],
                "classes": ["collapse"],
            },
        ),
    ]

    def save_model(self, request, obj, form, change):
        super().save_model(request, obj, form, change)

        # PayPal no-Vault guard (ADR-011 Q3 / §XV-1 invisible-failure prevention).
        # When paypal_vault_enabled=False and any active funnel Campaign for a store
        # in this organization uses paypal_capture_mode='delayed', shoppers could see
        # the upsell funnel but every accept attempt would silently fail.
        # Emit a non-blocking WARNING so the admin is aware — do not block the save.
        if (
            obj.processor_type == "paypal"
            and not obj.paypal_vault_enabled
            and obj.paypal_capture_mode == "delayed"
        ):
            try:
                from campaigns.models import Campaign, CampaignType

                # Check all stores belonging to this organization for active funnel campaigns.
                affected_store_ids = (
                    Campaign.objects.cross_store_unsafe()
                    .filter(
                        is_active=True,
                    )
                    .exclude(campaign_type=CampaignType.ABANDONED_CHECKOUT)
                    .filter(
                        store__organization=obj.organization,
                    )
                    .values_list("store_id", flat=True)
                    .distinct()
                )

                if affected_store_ids.exists():
                    self.message_user(
                        request,
                        (
                            "Warning: paypal_vault_enabled is False but this organization "
                            "has active funnel campaigns with delayed PayPal capture. "
                            "Shoppers who pay via PayPal will see the upsell funnel, but "
                            "every accept attempt will fail silently (UpsellUnavailableError). "
                            "Enable paypal_vault_enabled once PayPal Vault onboarding is "
                            "complete, or set paypal_capture_mode='immediate' to suppress "
                            "the funnel for PayPal orders."
                        ),
                        level=messages.WARNING,
                    )
            except Exception:
                # Never let the guard block the save — it is advisory only.
                pass


# ---------------------------------------------------------------------------
# OrgPool + OrgPoolMember inline
# ---------------------------------------------------------------------------


class OrgPoolMemberInline(admin.TabularInline):
    model = OrgPoolMember
    extra = 1
    fields = ["organization", "target_percent", "position"]
    ordering = ["position"]


@admin.register(OrgPool, site=super_admin_site)
class OrgPoolAdmin(admin.ModelAdmin):
    """
    Super-admin view of Organization pools used in stage-2 split rules.

    Inline shows pool members and their target_percent / position values.
    When the pool has an active percent_split rule, saving a member whose
    new total diverges from 100% is blocked by a pre_save signal.
    """

    list_display = ["name", "is_active", "created_at"]
    list_filter = ["is_active"]
    search_fields = ["name"]
    readonly_fields = ["created_at", "updated_at"]
    inlines = [OrgPoolMemberInline]


# ---------------------------------------------------------------------------
# OrganizationRule
# ---------------------------------------------------------------------------


@admin.register(OrganizationRule, site=super_admin_site)
class OrganizationRuleAdmin(admin.ModelAdmin):
    """
    Super-admin view of two-stage routing rules.

    save_model surfaces _shadow_warnings from OrganizationRule.clean() as
    admin-level WARNING messages so the operator is alerted without being blocked.
    """

    list_display = [
        "stage",
        "name",
        "priority_within_stage",
        "is_active",
        "outcome_organization",
        "outcome_org_pool",
        "org_pool",
        "allocation_type",
    ]
    list_filter = ["stage", "is_active", "allocation_type"]
    search_fields = ["name", "outcome_organization__name", "outcome_org_pool__name"]
    ordering = ["stage", "priority_within_stage", "pk"]
    readonly_fields = ["created_at", "updated_at"]

    fieldsets = [
        (
            None,
            {
                "fields": ["stage", "name", "is_active", "priority_within_stage"],
            },
        ),
        (
            "Stage-1: Geography conditions",
            {
                "fields": ["conditions_json", "outcome_organization", "outcome_org_pool"],
                "description": (
                    "For stage-1 rules: set conditions_json (countries/areas/currencies/amounts) "
                    "and exactly one of outcome_organization or outcome_org_pool."
                ),
                "classes": ["collapse"],
            },
        ),
        (
            "Stage-2: Allocation",
            {
                "fields": ["org_pool", "allocation_type"],
                "description": (
                    "For stage-2 rules: set the org_pool this rule governs and the allocation_type. "
                    "conditions_json must be {} (pool-bound, not order-attribute-bound)."
                ),
                "classes": ["collapse"],
            },
        ),
        (
            "Fallback chain",
            {
                "fields": ["fallback_rule"],
                "description": (
                    "Explicit next rule when this rule's outcome is ineligible. "
                    "NULL = fall through to the platform default org. Must not create a cycle."
                ),
                "classes": ["collapse"],
            },
        ),
        (
            "Timestamps",
            {
                "fields": ["created_at", "updated_at"],
                "classes": ["collapse"],
            },
        ),
    ]

    def save_model(self, request, obj, form, change):
        super().save_model(request, obj, form, change)
        for warning in getattr(obj, "_shadow_warnings", []):
            self.message_user(request, warning, messages.WARNING)


# ---------------------------------------------------------------------------
# PaymentMethod + ProcessorOption inline
# ---------------------------------------------------------------------------


class ProcessorOptionInline(admin.TabularInline):
    model = ProcessorOption
    extra = 1
    fields = ["processor_account", "position"]
    ordering = ["position"]


@admin.register(PaymentMethod, site=super_admin_site)
class PaymentMethodAdmin(admin.ModelAdmin):
    """
    Super-admin view of checkout-visible payment method families.

    Inline shows ProcessorOption rows (which processor accounts back this method,
    and in which order for backup_chain strategy).
    """

    list_display = [
        "method_family",
        "displayed_label",
        "is_enabled",
        "strategy",
        "hide_when_no_route",
        "allow_store_local_disable",
    ]
    list_filter = ["is_enabled", "strategy"]
    search_fields = ["method_family", "displayed_label"]
    inlines = [ProcessorOptionInline]


# ---------------------------------------------------------------------------
# ProcessorSplitCounter — read-only debugging list
# ---------------------------------------------------------------------------


@admin.register(ProcessorSplitCounter, site=super_admin_site)
class ProcessorSplitCounterAdmin(admin.ModelAdmin):
    """
    Super-admin read-only view of split counters (debugging aid).

    These counters are written by the routing engine — do not modify manually.
    """

    list_display = [
        "org_pool",
        "organization",
        "payment_method",
        "processor_account",
        "period",
        "count",
    ]
    list_filter = ["period", "org_pool", "payment_method"]
    ordering = ["-period", "org_pool", "payment_method"]

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False


# ---------------------------------------------------------------------------
# DecisionLog — read-only audit log
# ---------------------------------------------------------------------------


@admin.register(DecisionLog, site=super_admin_site)
class DecisionLogAdmin(admin.ModelAdmin):
    """
    Super-admin read-only view of payment routing decisions (ADR-006-R §1.7).

    This is an immutable audit log — add/change/delete are disabled.
    Every route_payment() call writes exactly one row here.
    """

    list_display = [
        "order_id",
        "store_id",
        "chosen_organization",
        "chosen_processor_account",
        "reason",
        "over_cap",
        "buyer_country",
        "method_family",
        "decided_at",
    ]
    list_filter = ["reason", "over_cap", "method_family", "chosen_organization"]
    search_fields = ["reason", "chosen_organization__name", "buyer_country"]
    ordering = ["-decided_at"]
    readonly_fields = [
        "order_id",
        "store_id",
        "decided_at",
        "evaluated_rules_json",
        "chosen_organization",
        "chosen_processor_account",
        "matched_rule",
        "reason",
        "over_cap",
        "buyer_country",
        "method_family",
        "parent_decision",
    ]

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False
