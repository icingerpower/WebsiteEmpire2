"""
Store-admin registration for the chat app (ADR-024 Decision 2, Decision 10).

Registered on webecom.admin.store_admin_site (per-store admin, ADR-001 §2) —
these are store-owned rows a store owner needs to see/manage (opt-in, budgets,
policy-page designation) and audit (sessions/messages for dispute review,
ADR-024 Risks). Follows the same get_queryset()-scoping pattern as every other
StoreOwnedModel ModelAdmin in this codebase (see pages/admin.py StaticPageAdmin).

StoreChatSettings enabling flow (AC-CHAT-02): the admin form requires the
sub-processor acceptance checkbox to be ticked before is_enabled can be saved
as True; ticking it stamps subprocessor_terms_accepted_at + accepted_by.
Unticking is_enabled later (disabling) never clears those two fields — the
acceptance audit trail is permanent once given.
"""

from django import forms
from django.contrib import admin
from django.utils import timezone

from chat.models import ChatMessage, ChatSession, StoreChatSettings
from core.admin import StoreOwnedInlineMixin
from pages.models import StaticPage
from webecom.admin import store_admin_site

_POLICY_PAGE_FK_FIELDS = (
    "shipping_policy_page", "refund_policy_page", "terms_policy_page", "contact_page",
)


class ChatMessageInline(StoreOwnedInlineMixin, admin.TabularInline):
    """
    ChatMessage IS a StoreOwnedModel but this inline previously had NO
    get_queryset() override — a latent isolation bug (same class as the one
    StoreOwnedInlineMixin fixes everywhere else), found while migrating all
    inlines onto the shared mixin (ADR-031 addendum, TICKET-051). Any
    GET/POST of a ChatSession change form with >=1 message would have 500'd
    on `self.model._default_manager.get_queryset()` (Django's
    InlineModelAdmin default) before this fix.
    """

    model = ChatMessage
    extra = 0
    readonly_fields = ("role", "content", "tool_trace_json", "created_at")
    can_delete = False
    fields = readonly_fields

    def has_add_permission(self, request, obj=None):
        return False


@admin.register(ChatSession, site=store_admin_site)
class ChatSessionAdmin(admin.ModelAdmin):
    # ADR-033 D3c DECIDED (human 2026-07-11, ADR-033 D7 item 6): no dedicated "chat" row in the 19-module
    # vocabulary; gated under the generic "apps" bucket like engagement/feeds.
    module_key = "apps"

    list_display = ("session_key", "locale", "message_count", "ended_reason", "cost_usd", "created_at")
    list_filter = ("ended_reason", "locale")
    readonly_fields = (
        "session_key", "locale", "created_at", "last_activity_at", "message_count",
        "prompt_tokens", "completion_tokens", "cost_usd", "ended_reason",
    )
    inlines = [ChatMessageInline]
    search_fields = ("session_key",)

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return ChatSession.objects.for_store(request.store)
        return ChatSession.objects.none()

    def has_add_permission(self, request):
        # Sessions are only ever created by the storefront endpoint (chat/views.py).
        return False


class StoreChatSettingsAdminForm(forms.ModelForm):
    subprocessor_terms_accepted = forms.BooleanField(
        required=False,
        label="I have read and accept the Anthropic sub-processor disclosure",
        help_text=(
            "Customer chat content is sent to Anthropic for processing. Your "
            "store's privacy policy must disclose this sub-processor before "
            "enabling chat (ADR-024 Decision 10)."
        ),
    )

    class Meta:
        model = StoreChatSettings
        fields = (
            "is_enabled", "model_id_override", "daily_message_budget",
            "shipping_policy_page", "refund_policy_page", "terms_policy_page", "contact_page",
        )

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        if self.instance and self.instance.pk and self.instance.subprocessor_terms_accepted_at:
            self.fields["subprocessor_terms_accepted"].initial = True
            self.fields["subprocessor_terms_accepted"].disabled = True
            self.fields["subprocessor_terms_accepted"].help_text += (
                f" (accepted {self.instance.subprocessor_terms_accepted_at:%Y-%m-%d})"
            )

    def clean(self):
        cleaned = super().clean()
        already_accepted = bool(self.instance.pk and self.instance.subprocessor_terms_accepted_at)
        if cleaned.get("is_enabled") and not already_accepted and not cleaned.get("subprocessor_terms_accepted"):
            raise forms.ValidationError(
                "You must accept the sub-processor disclosure before enabling chat."
            )
        return cleaned


@admin.register(StoreChatSettings, site=store_admin_site)
class StoreChatSettingsAdmin(admin.ModelAdmin):
    module_key = "apps"

    form = StoreChatSettingsAdminForm
    list_display = ("store", "is_enabled", "subprocessor_terms_accepted_at", "daily_message_budget")

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return StoreChatSettings.objects.for_store(request.store)
        return StoreChatSettings.objects.none()

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        Security audit H2: scope the four policy-page FK dropdowns to the
        current store (mirrors pages/admin.py StaticPageAdmin's
        formfield_for_foreignkey pattern). Without this, the field resolves
        against the raw StoreScopedManager base queryset, which raises
        IsolationError and makes the change/add form unable to render at all —
        and, if ever worked around, would let a store admin pick another
        store's StaticPage.
        """
        store = getattr(request, "store", None)
        if store is not None and db_field.name in _POLICY_PAGE_FK_FIELDS:
            kwargs["queryset"] = StaticPage.objects.for_store(store)
        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def save_model(self, request, obj, form, change):
        if not change and getattr(request, "store", None):
            obj.store = request.store
        # AC-CHAT-02: stamp acceptance in the SAME action as enabling; disabling
        # never clears the stamp (only is_enabled flips back to False).
        if form.cleaned_data.get("subprocessor_terms_accepted") and not obj.subprocessor_terms_accepted_at:
            obj.subprocessor_terms_accepted_at = timezone.now()
            obj.accepted_by = request.user
        super().save_model(request, obj, form, change)
