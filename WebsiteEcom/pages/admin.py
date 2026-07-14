"""
Store-admin registrations for the pages app (ADR-018 D1/D3, T029-SP).

- StaticPageAdmin: store-scoped CRUD + a read-only translation-status inline
  (mirrors catalog.admin's ProductAdmin / TranslationStatusInline pattern).
- ContactMessageAdmin: inbox — status editable at "pages" full access, read-only
  at limited (the inbox is a workflow, not a log, mirroring QuotationRequestAdmin).
- Module permission gate: stores.permissions.StoreModulePermissionMixin,
  auto-applied at registration (ADR-033 D3b), module_key="pages" (frozen —
  ADR-033 D1a keeps the shipped key; the AF-002 display label is "CMS").
"""

from django import forms
from django.contrib import admin
from django.utils import timezone

from core.admin import StoreOwnedInlineMixin
from pages.models import ContactMessage, StaticPage, StaticPageTranslation
from stores.permissions import check_module_access
from webecom.admin import store_admin_site


# ---------------------------------------------------------------------------
# StaticPage admin
# ---------------------------------------------------------------------------

class StaticPageAdminForm(forms.ModelForm):
    class Meta:
        model = StaticPage
        fields = "__all__"
        widgets = {
            "body": forms.Textarea(attrs={"rows": 16}),
        }


class StaticPageTranslationInline(StoreOwnedInlineMixin, admin.TabularInline):
    """
    Read-only inline showing existing StaticPageTranslation rows — mirrors
    catalog.admin.TranslationStatusInline. Editors use the dedicated
    StaticPageTranslationAdmin form to actually edit a translation.
    """

    model = StaticPageTranslation
    extra = 0
    can_delete = False
    fields = ["lang_code", "status", "ai_job", "is_manually_edited", "updated_at"]
    readonly_fields = ["lang_code", "status", "ai_job", "is_manually_edited", "updated_at"]
    verbose_name = "translation status"
    verbose_name_plural = "translation statuses"

    def has_add_permission(self, request, obj=None):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def is_manually_edited(self, obj):
        return obj.manually_edited_at is not None

    is_manually_edited.boolean = True
    is_manually_edited.short_description = "Manually edited"

    def get_queryset(self, request):
        # StoreOwnedInlineMixin supplies cross_store_unsafe(); chain the
        # extra select_related() this inline needs on top of it.
        return super().get_queryset(request).select_related("ai_job")


@admin.register(StaticPage, site=store_admin_site)
class StaticPageAdmin(admin.ModelAdmin):
    module_key = "pages"

    form = StaticPageAdminForm
    list_display = ["title", "kind", "is_published", "show_in_header", "show_in_footer", "nav_position", "updated_at"]
    list_filter = ["kind", "is_published"]
    search_fields = ["title", "slug"]
    prepopulated_fields = {"slug": ("title",)}
    readonly_fields = ["published_at", "created_at", "updated_at"]
    ordering = ["nav_position", "-created_at"]
    inlines = [StaticPageTranslationInline]

    fieldsets = [
        (
            "Basic Info",
            {"fields": ["title", "slug", "kind", "is_published", "published_at"]},
        ),
        (
            "Content",
            {"fields": ["body"]},
        ),
        (
            "Navigation",
            {"fields": ["show_in_header", "show_in_footer", "nav_position"]},
        ),
        (
            "SEO",
            {"fields": ["seo_title", "seo_description"], "classes": ["collapse"]},
        ),
        (
            "Meta",
            {"fields": ["created_at", "updated_at"], "classes": ["collapse"]},
        ),
    ]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return StaticPage.objects.for_store(request.store)
        return StaticPage.objects.none()

    def save_model(self, request, obj, form, change):
        if not change and getattr(request, "store", None):
            obj.store = request.store
        if not obj.published_at and obj.is_published:
            obj.published_at = timezone.now()
        super().save_model(request, obj, form, change)

    def save_formset(self, request, form, formset, change):
        instances = formset.save(commit=False)
        for instance in instances:
            if hasattr(instance, "store_id") and not instance.store_id:
                instance.store = form.instance.store
            instance.save()
        formset.save_m2m()
        for obj_to_delete in formset.deleted_objects:
            obj_to_delete.delete()


@admin.register(StaticPageTranslation, site=store_admin_site)
class StaticPageTranslationAdmin(admin.ModelAdmin):
    """
    Store-admin view for individual StaticPageTranslation records (mirrors
    catalog.admin.ProductTranslationAdmin — sets manually_edited_at on human save).
    """

    module_key = "pages"

    list_display = ["page", "lang_code", "status", "slug_hint", "manually_edited_at", "updated_at"]
    list_filter = ["status", "lang_code"]
    search_fields = ["page__title", "lang_code"]
    readonly_fields = ["ai_job", "created_at", "updated_at"]
    fields = [
        "page", "lang_code", "title", "body", "seo_title", "seo_description",
        "slug_hint", "status", "ai_job", "manually_edited_at", "created_at", "updated_at",
    ]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return StaticPageTranslation.objects.for_store(request.store)
        return StaticPageTranslation.objects.none()

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        Security audit F6: scope the `page` and `ai_job` FK dropdowns to the
        current store (mirrors campaigns/admin.py's formfield_for_foreignkey
        pattern). Without this, the field resolves against the raw
        StoreScopedManager base queryset, which raises IsolationError on this
        model form — or, if a manual add ever worked around that, would let a
        store admin pick another store's page/ai_job.
        """
        store = getattr(request, "store", None)
        if store is not None:
            if db_field.name == "page":
                from pages.models import StaticPage

                kwargs["queryset"] = StaticPage.objects.for_store(store)
            elif db_field.name == "ai_job":
                from aijobs.models import AiJob

                kwargs["queryset"] = AiJob.objects.for_store(store)
        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def save_model(self, request, obj, form, change):
        if not change and getattr(request, "store", None):
            obj.store = request.store
        obj.manually_edited_at = timezone.now()
        super().save_model(request, obj, form, change)


# ---------------------------------------------------------------------------
# ContactMessage admin — store-admin inbox (ADR-018 D3)
# ---------------------------------------------------------------------------

@admin.register(ContactMessage, site=store_admin_site)
class ContactMessageAdmin(admin.ModelAdmin):
    """
    Contact message inbox for the current store.

    status is editable at "pages" full access (the inbox is a workflow, not a log);
    "limited" access is read-only. No add permission — rows are created only by the
    public contact form.

    has_change_permission is deliberately overridden below (not left to the
    generic module_key mapping) so that "limited" access can still OPEN the
    read-only change form to inspect a message — only "status" becomes
    editable at "full" (see get_readonly_fields). This is a genuine
    app-specific business rule preserved via
    stores.permissions._find_override (ADR-033 D3b docstring).
    """

    module_key = "pages"

    list_display = ["email", "name", "subject", "status", "created_at"]
    list_filter = ["status"]
    search_fields = ["email", "name", "subject", "message"]
    date_hierarchy = "created_at"
    ordering = ["-created_at"]

    def get_readonly_fields(self, request, obj=None):
        base = ["store", "page", "name", "email", "subject", "message", "lang_code", "created_at"]
        if check_module_access(request, "pages", "full"):
            return base
        return base + ["status"]

    def has_add_permission(self, request):
        return False

    def has_delete_permission(self, request, obj=None):
        return False

    def has_change_permission(self, request, obj=None):
        return check_module_access(request, "pages", "limited")

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return ContactMessage.objects.for_store(request.store)
        return ContactMessage.objects.none()
