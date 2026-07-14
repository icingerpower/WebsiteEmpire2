"""
Emails admin registrations (TICKET-022, TICKET-040).

EmailTemplateAdmin — store_admin_site + super_admin_site
    Store admins can create and edit per-store email template overrides.
    Super admins see all stores' templates and can manage platform defaults.

SentEmailAdmin — store_admin_site + super_admin_site
    Read-only audit log. No add or change permissions on either site.
    Provides list + search for support workflows.

SenderDomainAdmin — store_admin_site + super_admin_site (TICKET-040, ADR-034)
    Store admins register a custom sender domain, see the copy-friendly DNS
    records to publish, and click "Verify" to (re)start the DNS check.
    Super admins additionally get a "Mark verified" manual override — a
    spoofing-adjacent, audited, super-admin-only bypass of the DNS check
    (ADR-034 "Risks" / Option B3). Every use writes a
    SenderDomainManualVerification row.
"""

from django.contrib import admin, messages
from django.core.exceptions import PermissionDenied
from django.utils.html import format_html

from core.admin_widgets import StoreScopedForeignKeyRawIdWidget
from stores.permissions import check_module_access
from webecom.admin import store_admin_site, super_admin_site

from .models import EmailTemplate, SenderDomain, SenderDomainManualVerification, SentEmail


# ---------------------------------------------------------------------------
# EmailTemplate admin
# ---------------------------------------------------------------------------

class EmailTemplateAdminBase(admin.ModelAdmin):
    list_display = ('template_id', 'subject', 'is_active', 'store')
    list_filter = ('template_id', 'is_active')
    search_fields = ('template_id', 'subject')
    ordering = ('store', 'template_id')
    fieldsets = [
        (
            'Template',
            {
                'fields': ['store', 'template_id', 'subject', 'is_active'],
            },
        ),
        (
            'Body',
            {
                'fields': ['body_html', 'body_text'],
                'description': (
                    'Rendered as Django templates. Available variables depend on the '
                    'template_id — see the platform documentation.'
                ),
            },
        ),
    ]


@admin.register(EmailTemplate, site=store_admin_site)
class EmailTemplateAdmin(EmailTemplateAdminBase):
    """Store-scoped email template admin."""

    # ADR-033 D3c DECIDED (human 2026-07-11, ADR-033 D7 item 6): no dedicated "emails" row; store-level template
    # overrides sit under Settings.
    module_key = "settings"

    def get_queryset(self, request):
        if getattr(request, 'store', None):
            return EmailTemplate.objects.for_store(request.store)
        return EmailTemplate.objects.none()

    def save_model(self, request, obj, form, change):
        if not change and getattr(request, 'store', None):
            obj.store = request.store
        super().save_model(request, obj, form, change)

    def get_form(self, request, obj=None, **kwargs):
        """Remove the store field — it is always set to request.store."""
        kwargs.setdefault('exclude', [])
        kwargs['exclude'] = list(kwargs['exclude']) + ['store']
        return super().get_form(request, obj, **kwargs)


@admin.register(EmailTemplate, site=super_admin_site)
class EmailTemplateSuperAdmin(EmailTemplateAdminBase):
    """Cross-store email template admin for super admins."""
    raw_id_fields = ('store',)

    def get_queryset(self, request):
        return EmailTemplate.objects.cross_store_unsafe()


# ---------------------------------------------------------------------------
# SentEmail admin — read-only on both sites
# ---------------------------------------------------------------------------

class SentEmailAdminBase(admin.ModelAdmin):
    list_display = ('template_id', 'recipient_email', 'subject', 'status', 'sent_at', 'store')
    list_filter = ('template_id', 'status', 'sent_at')
    search_fields = ('recipient_email', 'subject', 'template_id')
    date_hierarchy = 'sent_at'
    ordering = ('-sent_at',)
    readonly_fields = (
        'store',
        'template_id',
        'recipient_email',
        'subject',
        'order',
        'sent_at',
        'status',
        'error_message',
    )

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False


@admin.register(SentEmail, site=store_admin_site)
class SentEmailAdmin(SentEmailAdminBase):
    """Store-scoped sent email log (read-only)."""

    module_key = "settings"

    def get_queryset(self, request):
        if getattr(request, 'store', None):
            return SentEmail.objects.for_store(request.store)
        return SentEmail.objects.none()


@admin.register(SentEmail, site=super_admin_site)
class SentEmailSuperAdmin(SentEmailAdminBase):
    """Cross-store sent email log (read-only)."""
    raw_id_fields = ('order',)

    def get_queryset(self, request):
        return SentEmail.objects.cross_store_unsafe()

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        'order' targets orders.Order (StoreOwnedModel). 'order' is always in
        SentEmailAdminBase.readonly_fields, so today this field never becomes
        a live form field and the widget is never actually instantiated by
        Django's own get_form() — but raw_id_fields still names it, and a
        future change that makes 'order' editable (or a permission change
        that lifts has_change_permission's False) would silently resurrect
        the RAW-ID-WIDGET-ISOLATION crash otherwise. Fixed defensively so
        that can never happen (core/admin_widgets.py).
        """
        if db_field.name == "order":
            kwargs["widget"] = StoreScopedForeignKeyRawIdWidget(
                db_field.remote_field, self.admin_site, using=kwargs.get("using"),
            )
        return super().formfield_for_foreignkey(db_field, request, **kwargs)


# ---------------------------------------------------------------------------
# SenderDomain admin (TICKET-040, ADR-034)
# ---------------------------------------------------------------------------

_DNS_RECORDS_DESCRIPTION = (
    "Add these records to your domain's DNS, then click \"Verify\". "
    "SPF: merge this into your domain's EXISTING SPF record if it has one — "
    "never replace it outright, or you may silently break your other outgoing "
    "mail. DMARC is informational guidance only; it is not checked by "
    "verification. "
    "v1 scope, stated honestly: a verified custom From-address and an SPF "
    "pass are fully supported today. Full custom-domain DKIM signing (this "
    "domain cryptographically signing every message it sends) additionally "
    "requires your platform contact to configure the outbound mail relay "
    "with this domain's key — an ops step, tracked in "
    "docs/runbooks/sender-domain-dkim-signing.md, not delivered by publishing "
    "this DNS record alone. Until that step is done, mail is platform-DKIM-"
    "aligned, not custom-domain-DKIM-signed — never described as \"fully "
    "DKIM-verified\" here."
)


class SenderDomainAdminBase(admin.ModelAdmin):
    """
    Shared behavior for both admin sites. dkim_private_key is deliberately
    NEVER listed in fields/fieldsets/readonly_fields anywhere in this class —
    it must never be renderable via any admin surface (EncryptedCharField
    already prevents plaintext leaking into logs/shell repr; this is the
    admin-layer half of that same guarantee).

    module_key = "settings" (ADR-033 D3b permission matrix, TICKET-052):
    matches the sibling EmailTemplateAdmin/SentEmailAdmin in this same file —
    sender-domain configuration is email-sending settings, gated the same
    way. Only takes effect on store_admin_site (StoreModulePermissionMixin is
    auto-mixed in there only, webecom/admin.py); super_admin_site is never
    matrix-gated, so this attribute is inert on SenderDomainSuperAdmin.
    """

    module_key = "settings"

    list_display = ('domain', 'store', 'status', 'verified_at', 'last_checked_at')
    list_filter = ('status',)
    search_fields = ('domain', 'store__name', 'store__subdomain')
    ordering = ('-created_at',)
    readonly_fields = (
        'status',
        'verified_at',
        'last_checked_at',
        'last_check_error',
        'verification_attempts',
        'spf_record_display',
        'dkim_record_display',
        'dmarc_record_display',
    )
    fieldsets = [
        (
            'Sender domain',
            {
                'fields': [
                    'domain', 'sender_local_part', 'status',
                    'verified_at', 'last_checked_at', 'last_check_error',
                    'verification_attempts',
                ],
            },
        ),
        (
            'DNS records to publish',
            {
                'fields': ['spf_record_display', 'dkim_record_display', 'dmarc_record_display'],
                'description': _DNS_RECORDS_DESCRIPTION,
            },
        ),
    ]
    actions = ['verify_domain']

    @admin.display(description="SPF (merge into existing TXT record)")
    def spf_record_display(self, obj):
        return format_html('<code>{}</code>', obj.expected_spf_record)

    @admin.display(description="DKIM")
    def dkim_record_display(self, obj):
        return format_html(
            'Name: <code>{}</code><br>Value: <code>{}</code>',
            obj.expected_dkim_record_name, obj.expected_dkim_record_value,
        )

    @admin.display(description="DMARC (informational only, not verified)")
    def dmarc_record_display(self, obj):
        return format_html(
            'Name: <code>{}</code><br>Value: <code>{}</code>',
            obj.expected_dmarc_record_name, obj.expected_dmarc_record_value,
        )

    @admin.action(description="Verify (run DNS check)")
    def verify_domain(self, request, queryset):
        """
        Store-owner self-serve verification (ADR-034 Option B1): transitions
        UNVERIFIED/FAILED/VERIFIED -> PENDING and enqueues the real DNS check.
        Synchronous in tests via CELERY_TASK_ALWAYS_EAGER.
        """
        from emails.tasks import verify_sender_domain

        started, skipped = 0, []
        for domain in queryset:
            try:
                domain.mark_pending()
            except ValueError:
                # Already PENDING — re-clicking Verify must not reset an
                # in-flight attempt budget.
                skipped.append(domain.domain)
                continue
            verify_sender_domain.delay(domain.pk)
            started += 1

        if started:
            self.message_user(request, f"Verification started for {started} domain(s).")
        if skipped:
            self.message_user(
                request,
                f"Skipped (already pending): {', '.join(skipped)}",
                level=messages.WARNING,
            )


@admin.register(SenderDomain, site=store_admin_site)
class SenderDomainAdmin(SenderDomainAdminBase):
    """
    Store-scoped sender domain admin. Mirrors StoreDomainStoreAdminAdmin
    (stores/admin.py, ADR-008) — SenderDomain is a plain Model, not
    StoreOwnedModel, so scoping is done explicitly here rather than via
    StoreScopedManager.for_store().
    """

    def get_queryset(self, request):
        qs = super().get_queryset(request)
        if not hasattr(request, 'store') or request.store is None:
            return qs.none()
        return qs.filter(store=request.store)

    def save_model(self, request, obj, form, change):
        if hasattr(request, 'store') and request.store:
            obj.store = request.store
        super().save_model(request, obj, form, change)

    def get_form(self, request, obj=None, **kwargs):
        """The store field is always set to request.store — never a form field."""
        kwargs.setdefault('exclude', [])
        kwargs['exclude'] = list(kwargs['exclude']) + ['store']
        return super().get_form(request, obj, **kwargs)

    def has_module_permission(self, request):
        """
        ADR-033 D3b: the extra "no resolved store" check is a genuine
        app-specific rule preserved via stores.permissions._find_override —
        it now ALSO requires the "settings" matrix grant (previously it
        granted visibility unconditionally, same fix already applied to
        stores.admin.StoreDomainStoreAdminAdmin).
        """
        if not check_module_access(request, self.module_key, "limited"):
            return False
        return hasattr(request, 'store') and request.store is not None


@admin.register(SenderDomain, site=super_admin_site)
class SenderDomainSuperAdmin(SenderDomainAdminBase):
    """
    Cross-store sender domain admin. Adds the "Mark verified" manual override
    (ADR-034 Option B3) — a spoofing-adjacent bypass of the DNS check,
    restricted to super-admins and audited via SenderDomainManualVerification.

    The super_admin_site itself already gates access to is_super_admin users
    only (webecom/admin.py:SuperAdminSite.has_permission), but the action
    re-checks explicitly (defense in depth — see SentEmailSuperAdmin's
    analogous comment on formfield_for_foreignkey for why this codebase
    prefers not to rely on a single permission check for a spoofing-surface
    action).
    """

    raw_id_fields = ('store',)
    actions = SenderDomainAdminBase.actions + ['force_verify_manual']

    def get_queryset(self, request):
        return SenderDomain.objects.all()

    @admin.action(description="Mark verified (manual override — bypasses DNS check, audited)")
    def force_verify_manual(self, request, queryset):
        if not request.user.is_super_admin:
            raise PermissionDenied("Manual sender-domain verification requires super-admin.")

        count = 0
        for domain in queryset:
            previous_status = domain.status
            domain.force_verify()
            SenderDomainManualVerification.objects.create(
                sender_domain=domain,
                performed_by=request.user,
                previous_status=previous_status,
            )
            count += 1

        self.message_user(
            request,
            f"{count} domain(s) manually marked VERIFIED (bypasses DNS check, audited).",
            level=messages.WARNING,
        )


@admin.register(SenderDomainManualVerification, site=super_admin_site)
class SenderDomainManualVerificationAdmin(admin.ModelAdmin):
    """
    Read-only audit log of every manual "Mark verified" override
    (ADR-034 "Risks"). No add/change/delete — rows are only ever created by
    SenderDomainSuperAdmin.force_verify_manual.
    """

    list_display = ('sender_domain', 'previous_status', 'performed_by', 'performed_at')
    list_filter = ('previous_status',)
    search_fields = ('sender_domain__domain',)
    date_hierarchy = 'performed_at'
    ordering = ('-performed_at',)
    readonly_fields = ('sender_domain', 'previous_status', 'performed_by', 'performed_at')
    raw_id_fields = ('sender_domain',)

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False
