"""
Reviews admin — moderation screens for Review and ReviewRequest (TICKET-023).

Registered on both store_admin_site and super_admin_site.

Store-admin surface:
- ReviewAdmin: list with status filter tabs (PENDING/APPROVED/REJECTED/SPAM).
  Moderators can change status only; review content and metadata are read-only.
  save_model() stamps moderated_at / moderated_by when status changes.
- ReviewRequestAdmin: read-only view of scheduled/sent review emails.
  No add, no change — rows are created by the signal and updated by Celery.

Super-admin surface:
- ReviewSuperAdmin / ReviewRequestSuperAdmin: cross-store read via cross_store_unsafe().
"""

from django.contrib import admin
from django.utils import timezone

from reviews.models import Review, ReviewRequest
from webecom.admin import store_admin_site, super_admin_site


# ---------------------------------------------------------------------------
# Store-admin ReviewAdmin
# ---------------------------------------------------------------------------

@admin.register(Review, site=store_admin_site)
class ReviewAdmin(admin.ModelAdmin):
    module_key = "reviews"

    list_display = [
        "product",
        "reviewer_name",
        "rating",
        "status",
        "provenance",
        "is_verified_buyer",
        "submitted_at",
    ]
    list_filter = ["status", "provenance", "is_verified_buyer", "rating"]
    search_fields = ["reviewer_name", "reviewer_email", "title", "body"]
    readonly_fields = [
        "reviewer_email",
        "is_verified_buyer",
        "provenance",
        "ai_job",
        "submitted_at",
        "moderated_at",
        "moderated_by",
        # Content fields are read-only — moderators judge, not edit.
        "product",
        "order",
        "reviewer_name",
        "rating",
        "title",
        "body",
    ]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return Review.objects.for_store(request.store)
        return Review.objects.none()

    def save_model(self, request, obj, form, change):
        if change and "status" in form.changed_data:
            obj.moderated_at = timezone.now()
            obj.moderated_by = request.user
        super().save_model(request, obj, form, change)

    def save_related(self, request, form, formsets, change):
        """
        Assign obj.store = request.store for new reviews created via admin
        (unusual path — reviews are normally submitted via storefront or AI job).
        """
        if not form.instance.store_id and getattr(request, "store", None):
            form.instance.store = request.store
        super().save_related(request, form, formsets, change)

    def has_add_permission(self, request):
        # Moderators do not create reviews manually through this admin.
        return False

    def has_delete_permission(self, request, obj=None):
        # Prevent bulk-delete via the default action — status changes go through
        # save_model() so the moderation trail is always stamped.
        return False


# ---------------------------------------------------------------------------
# Store-admin ReviewRequestAdmin
# ---------------------------------------------------------------------------

@admin.register(ReviewRequest, site=store_admin_site)
class ReviewRequestAdmin(admin.ModelAdmin):
    module_key = "reviews"

    list_display = ["order", "scheduled_at", "is_sent", "sent_at"]
    list_filter = ["is_sent"]
    readonly_fields = ["order", "scheduled_at", "sent_at", "is_sent"]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return ReviewRequest.objects.for_store(request.store)
        return ReviewRequest.objects.none()

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False


# ---------------------------------------------------------------------------
# Super-admin ReviewSuperAdmin — cross-store read
# ---------------------------------------------------------------------------

@admin.register(Review, site=super_admin_site)
class ReviewSuperAdmin(admin.ModelAdmin):
    list_display = [
        "store",
        "product",
        "reviewer_name",
        "rating",
        "status",
        "provenance",
        "is_verified_buyer",
        "submitted_at",
    ]
    list_filter = ["status", "provenance", "is_verified_buyer", "rating", "store"]
    search_fields = ["reviewer_name", "reviewer_email", "title", "body"]
    readonly_fields = [
        "reviewer_email",
        "is_verified_buyer",
        "provenance",
        "ai_job",
        "submitted_at",
        "moderated_at",
        "moderated_by",
        "product",
        "order",
        "reviewer_name",
        "rating",
        "title",
        "body",
    ]

    def get_queryset(self, request):
        return Review.objects.cross_store_unsafe()

    def save_model(self, request, obj, form, change):
        if change and "status" in form.changed_data:
            obj.moderated_at = timezone.now()
            obj.moderated_by = request.user
        super().save_model(request, obj, form, change)

    def has_add_permission(self, request):
        return False


# ---------------------------------------------------------------------------
# Super-admin ReviewRequestSuperAdmin — cross-store read
# ---------------------------------------------------------------------------

@admin.register(ReviewRequest, site=super_admin_site)
class ReviewRequestSuperAdmin(admin.ModelAdmin):
    list_display = ["store", "order", "scheduled_at", "is_sent", "sent_at"]
    list_filter = ["is_sent", "store"]
    readonly_fields = ["store", "order", "scheduled_at", "sent_at", "is_sent"]

    def get_queryset(self, request):
        return ReviewRequest.objects.cross_store_unsafe()

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False
