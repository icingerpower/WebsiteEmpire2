"""
Admin registrations for the aijobs app (TICKET-014).

All AI job models are registered with super_admin_site only.
AI job orchestration is a platform-level concern, not per-store.
Store admins access job status through the review queue (TICKET-015).
"""

from django.contrib import admin

from core.admin_widgets import StoreScopedForeignKeyRawIdWidget
from webecom.admin import super_admin_site

from aijobs.models import (
    AiJob,
    AiJobMetric,
    AiJobOutput,
    AiJobRun,
    AiJobValidation,
    StoreAiQuota,
)


@admin.register(AiJob, site=super_admin_site)
class AiJobAdmin(admin.ModelAdmin):
    list_display = (
        "id",
        "store",
        "job_type",
        "status",
        "priority",
        "retry_count",
        "created_at",
    )
    list_filter = ("status", "job_type", "requires_human_review")
    search_fields = ("target_model", "target_id", "store__subdomain")
    readonly_fields = ("created_at", "updated_at", "retry_count")
    raw_id_fields = ("store",)
    ordering = ("-priority", "created_at")


@admin.register(AiJobRun, site=super_admin_site)
class AiJobRunAdmin(admin.ModelAdmin):
    list_display = (
        "id",
        "job",
        "model_id",
        "status",
        "prompt_tokens",
        "completion_tokens",
        "total_cost_usd",
        "started_at",
    )
    list_filter = ("status", "runner")
    search_fields = ("job__id", "model_id", "store__subdomain")
    readonly_fields = ("started_at",)
    raw_id_fields = ("store", "job")

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        'job' targets aijobs.AiJob (StoreOwnedModel); use the safe raw-id
        widget so label_and_url_for_value() doesn't crash via the raising
        default manager the instant this field re-renders with a bound value
        (RAW-ID-WIDGET-ISOLATION, core/admin_widgets.py). 'store' is not a
        StoreOwnedModel and needs no change.
        """
        if db_field.name == "job":
            kwargs["widget"] = StoreScopedForeignKeyRawIdWidget(
                db_field.remote_field, self.admin_site, using=kwargs.get("using"),
            )
        return super().formfield_for_foreignkey(db_field, request, **kwargs)


@admin.register(AiJobOutput, site=super_admin_site)
class AiJobOutputAdmin(admin.ModelAdmin):
    list_display = ("id", "run", "field_id", "is_accepted", "created_at")
    list_filter = ("is_accepted",)
    search_fields = ("run__id", "field_id", "store__subdomain")
    readonly_fields = ("created_at",)
    raw_id_fields = ("store", "run")

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        'run' targets aijobs.AiJobRun (StoreOwnedModel); use the safe raw-id
        widget (RAW-ID-WIDGET-ISOLATION, core/admin_widgets.py) — see
        AiJobRunAdmin.formfield_for_foreignkey for the full rationale.
        """
        if db_field.name == "run":
            kwargs["widget"] = StoreScopedForeignKeyRawIdWidget(
                db_field.remote_field, self.admin_site, using=kwargs.get("using"),
            )
        return super().formfield_for_foreignkey(db_field, request, **kwargs)


@admin.register(AiJobValidation, site=super_admin_site)
class AiJobValidationAdmin(admin.ModelAdmin):
    list_display = ("id", "run", "check_name", "passed", "severity", "created_at")
    list_filter = ("severity", "passed", "check_name")
    search_fields = ("run__id", "check_name", "store__subdomain")
    readonly_fields = ("created_at",)
    raw_id_fields = ("store", "run")

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        'run' targets aijobs.AiJobRun (StoreOwnedModel); use the safe raw-id
        widget (RAW-ID-WIDGET-ISOLATION, core/admin_widgets.py) — see
        AiJobRunAdmin.formfield_for_foreignkey for the full rationale.
        """
        if db_field.name == "run":
            kwargs["widget"] = StoreScopedForeignKeyRawIdWidget(
                db_field.remote_field, self.admin_site, using=kwargs.get("using"),
            )
        return super().formfield_for_foreignkey(db_field, request, **kwargs)


@admin.register(AiJobMetric, site=super_admin_site)
class AiJobMetricAdmin(admin.ModelAdmin):
    list_display = (
        "period_date",
        "store_id",
        "job_type",
        "model_id",
        "run_count",
        "prompt_tokens",
        "completion_tokens",
        "total_cost_usd",
    )
    list_filter = ("job_type", "period_date")
    search_fields = ("store_id", "job_type", "model_id")
    ordering = ("-period_date",)


@admin.register(StoreAiQuota, site=super_admin_site)
class StoreAiQuotaAdmin(admin.ModelAdmin):
    list_display = (
        "store",
        "monthly_budget_usd",
        "current_month_spent_usd",
        "current_month",
        "is_budget_enforced",
    )
    list_filter = ("is_budget_enforced",)
    search_fields = ("store__subdomain",)
    raw_id_fields = ("store",)
