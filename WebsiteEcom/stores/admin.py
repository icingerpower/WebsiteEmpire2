"""
Admin registrations for the stores app.

Platform-level models (Store, Organization, Theme) are registered with super_admin_site.
StoreEmployee is registered with both admin sites:
  - super_admin_site: all employees across all stores (cross-tenant management).
  - store_admin_site: employees for the current store only (filtered by request.store).

StoreEmployee permission matrix UI + invite flow (ADR-033 D4, TICKET-047):
PermissionGridFormMixin renders the 19-module grid; StoreEmployeeStoreAdminForm
adds the Full name/Email/Phone invite fields (D4b); StoreEmployeeSuperAdminForm
keeps the raw user/store FKs and gains the same grid (D4c).
"""

from django import forms
from django.contrib import admin, messages
from django.db import transaction

from core.choices import CURRENCY_CHOICES
from core.fields import JsonMultipleChoiceField
from core.widgets import CsvListFormField, CsvListWidget
from stores.invite import find_conflicting_employee, resolve_invitee, send_store_invite_email
from stores.modules import MODULES, module_choices
from stores.permissions import check_module_access
from webecom.admin import store_admin_site, super_admin_site
from stores.models import Organization, ShippingCountry, Store, StoreDomain, StoreEmployee, StoreLanguage, Theme


class OrganizationAdminForm(forms.ModelForm):
    """
    Custom form for OrganizationAdmin.

    settlement_currencies uses FilteredSelectMultiple (dual-list widget) backed by a
    fixed currency vocabulary — admins pick from the list rather than typing raw codes.

    coverage_areas_json is still a free-form CsvListWidget (area tokens like EU/ROW
    do not map to a closed list used elsewhere in the form).
    """

    settlement_currencies = JsonMultipleChoiceField(
        choices=CURRENCY_CHOICES,
        label="Settlement currencies",
    )

    class Meta:
        model = Organization
        fields = "__all__"
        widgets = {
            "coverage_areas_json": CsvListWidget(
                attrs={"rows": 2, "placeholder": "EU, US, ROW"}
            ),
        }
        # ADR-031 Addendum 3 (D3a): CsvListWidget must pair with
        # CsvListFormField, not the stock JSONField — see core/widgets.py's
        # CsvListFormField docstring for the crash (and the pre-existing
        # display bug) this avoids.
        field_classes = {
            "coverage_areas_json": CsvListFormField,
        }


@admin.register(Organization, site=super_admin_site)
class OrganizationAdmin(admin.ModelAdmin):
    form = OrganizationAdminForm
    list_display = ("name", "is_default", "status", "registration_country", "created_at")
    list_filter = ("is_default", "status", "registration_country")
    search_fields = ("name", "legal_name", "display_name")
    readonly_fields = ("created_at",)
    fieldsets = [
        (
            None,
            {
                "fields": ["name", "is_default", "status"],
            },
        ),
        (
            "Routing identity (ADR-006-R §1.1)",
            {
                "fields": [
                    "legal_name",
                    "display_name",
                    "registration_country",
                    "statement_descriptor",
                    "settlement_currencies",
                    "coverage_areas_json",
                ],
                "description": (
                    "Legal entity details used in payment routing and buyer-facing communications. "
                    "statement_descriptor is the text shown on card statements (max 22 chars)."
                ),
            },
        ),
        (
            "Monthly cap (ADR-006-R §1.1)",
            {
                "fields": [
                    "monthly_threshold",
                    "current_month_volume",
                    "volume_month",
                ],
                "description": (
                    "Monthly volume cap for routing. NULL = no cap. "
                    "The default org must always have NULL monthly_threshold (DB constraint). "
                    "current_month_volume is incremented automatically — do not edit manually."
                ),
            },
        ),
        (
            "Timestamps",
            {
                "fields": ["created_at"],
                "classes": ["collapse"],
            },
        ),
    ]


@admin.register(Theme, site=super_admin_site)
class ThemeAdmin(admin.ModelAdmin):
    list_display = ("name", "is_active", "created_at")
    list_filter = ("is_active",)
    search_fields = ("name",)
    readonly_fields = ("created_at",)


@admin.register(Store, site=super_admin_site)
class StoreAdmin(admin.ModelAdmin):
    list_display = ("subdomain", "name", "primary_language", "default_currency", "is_active", "deleted_at")
    list_filter = ("is_active", "primary_language", "default_currency")
    search_fields = ("subdomain", "name", "custom_domain")
    readonly_fields = ("created_at", "updated_at", "deleted_at")
    raw_id_fields = ("organization", "theme")


class PermissionGridFormMixin(forms.ModelForm):
    """
    ADR-033 D4a (TICKET-047): renders one ChoiceField per stores.modules
    .MODULES module (perm__<key>), composed back into permissions_json in
    clean() — no per-module DB columns, the shipped JSONField storage stays
    exactly as-is (D2). Shared by BOTH the store-site and super-site
    StoreEmployee forms (D4c: "the super-admin site ... gains the same grid
    form"). RadioSelect widget: Orders renders 3 options, every other module
    renders 2 (module_choices() reads each module's own `levels` tuple).

    Toggling Full Access does NOT wipe permissions_json — switching back
    restores the previous grid (least-surprise; full_access is a read-time
    bypass in StoreEmployee.has_module_access, so the stored grid underneath
    is simply irrelevant while full_access=True, not destroyed).

    AF-C1 default for a NEW row: every module defaults to "none" (module
    .levels[-1] — "none" is always the last entry in stores.modules.MODULES's
    tuples) — the all-none default for new invitees.
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        stored = self.instance.permissions_json if self.instance and self.instance.pk else {}
        for module in MODULES:
            field_name = f"perm__{module.key}"
            self.fields[field_name] = forms.ChoiceField(
                choices=module_choices(module.key),
                initial=stored.get(module.key, module.levels[-1]),
                label=module.label,
                required=True,
                widget=forms.RadioSelect,
            )

    def clean(self):
        cleaned = super().clean()
        permissions_json = {}
        for module in MODULES:
            field_name = f"perm__{module.key}"
            if field_name in cleaned:
                permissions_json[module.key] = cleaned[field_name]
        self.instance.permissions_json = permissions_json
        return cleaned


class StoreEmployeeSuperAdminForm(PermissionGridFormMixin):
    class Meta:
        model = StoreEmployee
        fields = ("user", "store", "full_access", "is_active")


class StoreEmployeeSuperAdminAdmin(admin.ModelAdmin):
    """
    Super-admin view: all StoreEmployee rows across all stores.

    D5b/D5c: the super site ALLOWS demote/deactivate/delete of the last
    active full-access employee in a store (a super-admin can always
    re-grant access; hard-blocking the control plane creates unfixable
    states) but warns loudly via messages.warning so the action is
    deliberate. Self-escalation guard does not apply here — super-admins are
    exempt (StoreEmployee._validate_no_self_escalation) and this admin never
    threads an _acting_user onto the instance.
    """

    form = StoreEmployeeSuperAdminForm
    list_display = ("user", "store", "full_access", "is_active", "invited_at", "accepted_at", "last_login_at")
    list_filter = ("is_active", "full_access", "store")
    search_fields = ("user__email", "store__subdomain")
    readonly_fields = ("invited_at",)
    raw_id_fields = ("user", "store")

    def save_model(self, request, obj, form, change):
        """
        D5c: bypass StoreEmployee's lockout guard (which would otherwise
        raise on the store site) via _allow_lockout_transition, and warn
        instead when this save is the exact transition the guard exists to
        stop.
        """
        if change and obj.is_last_qualifying_transition():
            messages.warning(
                request,
                "This was the last active, full-access employee for "
                f"{obj.store}. The store now has zero full-access "
                "employees — you can always re-grant access here.",
            )
        obj._allow_lockout_transition = True
        super().save_model(request, obj, form, change)

    def delete_model(self, request, obj):
        if obj.would_be_last_active_full_access():
            messages.warning(
                request,
                "This was the last active, full-access employee for "
                f"{obj.store}. The store now has zero full-access "
                "employees — you can always re-grant access here.",
            )
        super().delete_model(request, obj)

    def delete_queryset(self, request, queryset):
        for obj in queryset:
            if obj.would_be_last_active_full_access():
                messages.warning(
                    request,
                    "This was the last active, full-access employee for "
                    f"{obj.store}. The store now has zero full-access "
                    "employees — you can always re-grant access here.",
                )
        super().delete_queryset(request, queryset)


super_admin_site.register(StoreEmployee, StoreEmployeeSuperAdminAdmin)


class _ActingUserFormMixin:
    """
    Threads request.user onto form.instance BEFORE full_clean() runs, so
    StoreEmployee.clean()'s self-escalation guard (ADR-033 D5a) can see who
    is making the change. Mirrors core/admin.py's StoreUniqueValidationFormMixin
    pattern (a per-request dynamic subclass, never a shared class attribute).
    """

    _acting_user = None

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.instance._acting_user = self._acting_user


class StoreEmployeeStoreAdminForm(PermissionGridFormMixin):
    """
    ADR-033 D4b (TICKET-047 invite flow): replaces the raw `user` FK widget
    with Full name / Email / Phone fields. `user` is resolved (existing-user
    link, or new-user creation with an unusable password) in
    StoreEmployeeStoreAdminAdmin.save_model, which has access to request —
    the form itself never touches auth.User creation.

    Email is fixed (disabled) once the row already has a linked user — no
    identity-swap surface once invited.

    Duplicate-invite check (T047 report gap fix): `user` is resolved from
    `email` server-side in StoreEmployeeStoreAdminAdmin.save_model, AFTER
    form validation — it is not a form field, so Django's automatic
    validate_unique() never runs the (user, store) UniqueConstraint. Without
    clean()'s explicit pre-check below, inviting an email that is already an
    employee of this store reached obj.save() and raised a raw, uncaught
    IntegrityError (HTTP 500) instead of a form error.
    """

    full_name = forms.CharField(label="Full name", max_length=255)
    email = forms.EmailField(label="Email")
    phone = forms.CharField(label="Phone", required=False, max_length=32)

    #: Threaded in by StoreEmployeeStoreAdminAdmin.get_form (same per-request
    #: dynamic-subclass pattern as _acting_user below) so clean() can check
    #: for a duplicate invite against the right store. None when the form is
    #: built outside that admin (e.g. directly in a shell/test) — clean()
    #: no-ops in that case.
    _store = None

    class Meta:
        model = StoreEmployee
        fields = ("full_access", "is_active")

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        instance = self.instance
        if instance and instance.pk:
            self.fields["full_name"].initial = instance.invited_name
            self.fields["phone"].initial = instance.invited_phone
            self.fields["email"].initial = instance.user.email if instance.user_id else ""
            self.fields["email"].disabled = True
        field_order = (
            ["full_name", "email", "phone", "full_access"]
            + [f"perm__{module.key}" for module in MODULES]
            + ["is_active"]
        )
        self.order_fields(field_order)

    def clean(self):
        """
        T047 report gap fix: reject a duplicate invite (same email, same
        store) with a friendly error on `email` BEFORE save_model() resolves/
        creates the User and hits the DB's unique_store_employee constraint.
        Only applies on creation — `email` is disabled (unchangeable) once
        the row is linked to a user, so there is nothing new to conflict
        with on edit.
        """
        cleaned = super().clean()
        if self.instance.pk is None and self._store is not None:
            email = cleaned.get("email")
            if email and find_conflicting_employee(email, self._store) is not None:
                self.add_error(
                    "email",
                    "This person is already an employee of this store.",
                )
        return cleaned


class StoreEmployeeStoreAdminAdmin(admin.ModelAdmin):
    """
    Store-admin view: only employees belonging to the current store (request.store).
    Scoped in get_queryset() so a store admin cannot see employees of other stores.

    module_key="employees" (ADR-033 D1c — a dedicated 19th matrix row, NOT
    folded into "settings"): has_add/change/delete/view/module_permission all
    fall out of the generic StoreModulePermissionMixin mapping (full_access
    bypasses).

    D4a list view: name, email, active status dot, last_login_at, invited_at.
    D4b invite flow: form = StoreEmployeeStoreAdminForm (full_name/email/phone
    replace the raw user FK); save_model resolves or creates the User and
    sends the "Store invitation" email on creation (never on edit).

    Security hardening (unchanged from TICKET-002, now module-key-driven):
    - store/user are excluded from the form (Meta.fields whitelist on
      StoreEmployeeStoreAdminForm) — store is always pinned to request.store,
      user is always resolved server-side in save_model (prevents a store-A
      admin from creating employees for store B, or spoofing a user pk).
    - D5a: no self-edit. has_change_permission/has_delete_permission deny when
      obj.user_id == request.user.pk (super-admins exempt — they never reach
      this admin anyway, only the store site). Defense in depth at the
      persistence boundary lives in StoreEmployee.clean() via _ActingUserFormMixin.
    - D5b: last-active-full-access demote/deactivate/delete is blocked — the
      demote/deactivate case raises ValidationError from
      StoreEmployee.clean()/save() (surfaced as a normal form error); delete
      is blocked explicitly in delete_model/delete_queryset below since
      Model.delete() never calls clean().
    """

    module_key = "employees"
    form = StoreEmployeeStoreAdminForm

    list_display = ("display_name", "email_display", "status_dot", "last_login_at", "invited_at")
    list_filter = ("is_active", "full_access")
    search_fields = ("user__email", "invited_name")
    readonly_fields = ("invited_at", "accepted_at")

    @admin.display(description="Name")
    def display_name(self, obj):
        return obj.invited_name or (obj.user.email if obj.user_id else "—")

    @admin.display(description="Email")
    def email_display(self, obj):
        return obj.user.email if obj.user_id else "—"

    @admin.display(description="Active", boolean=True)
    def status_dot(self, obj):
        return obj.is_active

    def get_queryset(self, request):
        qs = super().get_queryset(request)
        store = getattr(request, "store", None)
        if store is not None:
            return qs.filter(store=store)
        return qs.none()

    def get_form(self, request, obj=None, change=False, **kwargs):
        """
        Wrap the form to thread request.user in as the acting user (D5a) and
        request.store (T047 report gap fix: StoreEmployeeStoreAdminForm.clean()
        uses it to reject a duplicate invite before save_model runs).
        """
        form_class = super().get_form(request, obj, change=change, **kwargs)
        return type(
            form_class.__name__,
            (_ActingUserFormMixin, form_class),
            {
                "_acting_user": request.user,
                "_store": request.store,
                "__module__": form_class.__module__,
            },
        )

    def save_model(self, request, obj, form, change):
        """
        ADR-033 D4b invite flow: on creation, resolve the invitee's User
        (existing-user link, or new-user creation with an unusable password)
        from the form's full_name/email/phone fields, pin obj.store, persist,
        then send the "Store invitation" email — never on edit (editing only
        touches the matrix/active flag, not the invite).
        """
        if not change:
            obj.store = request.store
            obj.invited_name = form.cleaned_data["full_name"]
            obj.invited_phone = form.cleaned_data.get("phone", "")
            user, is_new_user = resolve_invitee(form.cleaned_data["email"])
            obj.user = user
            super().save_model(request, obj, form, change)
            send_store_invite_email(obj, is_new_user, request)
            return
        super().save_model(request, obj, form, change)

    def has_change_permission(self, request, obj=None):
        """D5a: no self-edit — a store employee can never edit their own row."""
        if obj is not None and obj.user_id == request.user.pk and not getattr(
            request.user, "is_super_admin", False
        ):
            return False
        return check_module_access(request, self.module_key, "full")

    def has_delete_permission(self, request, obj=None):
        """D5a (no self-delete) + D5b (no deleting the last full-access row)."""
        if obj is not None and obj.user_id == request.user.pk and not getattr(
            request.user, "is_super_admin", False
        ):
            return False
        return check_module_access(request, self.module_key, "full")

    def delete_model(self, request, obj):
        """D5b: block deleting the last active, full-access employee row."""
        with transaction.atomic():
            locked = StoreEmployee.objects.select_for_update().filter(store_id=obj.store_id)
            list(locked)  # force the lock
            if obj.would_be_last_active_full_access():
                self.message_user(
                    request,
                    "This store must always keep at least one active, "
                    "full-access employee. Promote or activate another "
                    "employee before deleting this one.",
                    level=messages.ERROR,
                )
                return
            super().delete_model(request, obj)

    def delete_queryset(self, request, queryset):
        """
        D5a/D5b: block bulk-deleting the actor's own row (Django's stock
        delete_selected action only calls has_delete_permission(obj=None)
        once for the whole action — it never re-checks per object — so the
        self-delete guard in has_delete_permission alone would not stop a
        bulk delete that happens to include the actor's own row) and block
        bulk-deleting the last active, full-access employee row.
        """
        if not getattr(request.user, "is_super_admin", False):
            if queryset.filter(user_id=request.user.pk).exists():
                self.message_user(
                    request,
                    "You cannot delete your own employee row — ask another "
                    "admin or a super-admin.",
                    level=messages.ERROR,
                )
                return
        store_ids = set(queryset.values_list("store_id", flat=True))
        with transaction.atomic():
            for store_id in store_ids:
                list(StoreEmployee.objects.select_for_update().filter(store_id=store_id))
            blocked = [obj for obj in queryset if obj.would_be_last_active_full_access()]
            if blocked:
                self.message_user(
                    request,
                    "This store must always keep at least one active, "
                    "full-access employee — the bulk delete was cancelled "
                    "because it would have removed the last one for at "
                    f"least one selected store ({blocked[0].store}).",
                    level=messages.ERROR,
                )
                return
            super().delete_queryset(request, queryset)


store_admin_site.register(StoreEmployee, StoreEmployeeStoreAdminAdmin)


# ---------------------------------------------------------------------------
# ADR-008: StoreDomain, StoreLanguage, ShippingCountry
# Minimal registrations — richer UI ships in TICKET-024.
# ---------------------------------------------------------------------------


class StoreDomainAdmin(admin.ModelAdmin):
    """
    StoreDomain admin — available on both sites (ADR-008 §3, TICKET-024).

    Columns: host, routing mode, primary badge, active toggle, language count.
    """

    list_display = ["host", "routing_mode", "is_primary", "is_active", "language_count", "created_at"]
    list_filter = ["is_primary", "is_active"]
    search_fields = ["host", "store__name", "store__subdomain"]
    readonly_fields = ["created_at"]
    raw_id_fields = ["store"]

    @admin.display(description="Routing")
    def routing_mode(self, obj):
        """
        Show the routing strategy used by this domain's StoreLanguage rows.

        If any StoreLanguage on this store uses path prefixes, show "Path prefix";
        otherwise show "Subdomain" (each language is served at a dedicated hostname).
        """
        uses_prefix = obj.store.languages.filter(use_path_prefix=True).exists()
        return "Path prefix" if uses_prefix else "Subdomain"

    @admin.display(description="Languages")
    def language_count(self, obj):
        """Count of StoreLanguage rows linked to this domain's store."""
        return obj.store.languages.count()


class StoreDomainStoreAdminAdmin(StoreDomainAdmin):
    """
    Store-admin view: only domains belonging to the current store (request.store).
    Scoped in get_queryset() so a store admin cannot see or edit domains of other stores.
    save_model() enforces the store FK on creation so a store admin cannot assign a
    domain to a different store.

    module_key="domains" (ADR-033 D3c, high confidence). has_module_permission
    is deliberately overridden below (not left to the generic module_key
    mapping) so the extra "no resolved store" check (hides the module from a
    super-admin browsing without a store context) keeps applying — a
    genuine app-specific rule preserved via stores.permissions._find_override
    (ADR-033 D3b docstring): it now ALSO requires the matrix grant, whereas
    before this ticket it granted visibility unconditionally.
    """

    module_key = "domains"

    def get_queryset(self, request):
        qs = super().get_queryset(request)
        if not hasattr(request, "store") or request.store is None:
            return qs.none()
        return qs.filter(store=request.store)

    def save_model(self, request, obj, form, change):
        if hasattr(request, "store") and request.store:
            obj.store = request.store
        super().save_model(request, obj, form, change)

    def has_module_permission(self, request):
        if not check_module_access(request, self.module_key, "limited"):
            return False
        return hasattr(request, "store") and request.store is not None


admin.register(StoreDomain, site=store_admin_site)(StoreDomainStoreAdminAdmin)
admin.register(StoreDomain, site=super_admin_site)(StoreDomainAdmin)


class StoreLanguageAdmin(admin.ModelAdmin):
    """
    StoreLanguage admin — per-language row in the merged General Settings + Domains screen
    (ADR-008 §3/§5, ML-010, TICKET-024).

    Columns: lang/domain/routing/enabled/target-countries/translation-coverage.
    translation_coverage shows % of published translations (products + collections)
    relative to the total catalog for this (store, lang_code) pair.
    """

    list_display = [
        "lang_code",
        "store",
        "domain",
        "use_path_prefix",
        "is_default",
        "is_enabled",
        "target_countries",
        "translation_coverage",
    ]
    list_filter = ["is_enabled", "is_default", "use_path_prefix"]
    search_fields = ["lang_code", "store__subdomain", "domain__host"]
    readonly_fields = ["created_at"]
    raw_id_fields = ["store", "domain"]

    @admin.display(description="Target Countries")
    def target_countries(self, obj):
        """
        ISO country codes declared as target markets for this (store, language) pair.

        Reads ShippingCountry rows linked to this StoreLanguage (ML-003/ML-010c).
        Returns a comma-separated list or '—' when none are configured.
        """
        codes = list(
            obj.shipping_countries.values_list("country_code", flat=True).order_by("country_code")
        )
        return ", ".join(codes) if codes else "—"

    @admin.display(description="Translation Coverage")
    def translation_coverage(self, obj):
        """
        Published translation % for this (store, lang_code) pair across products + collections.

        Format: "N/D (P%)" where N = published translations, D = total catalog objects.
        Uses cross_store_unsafe() narrowed to this store — no cross-tenant bleed.
        N+1 in admin is acceptable; annotate the queryset here if list performance becomes
        an issue.
        """
        from catalog.models import (
            Collection,
            CollectionTranslation,
            Product,
            ProductTranslation,
            TranslationStatus,
        )

        store = obj.store
        lang = obj.lang_code
        total = (
            Product.objects.for_store(store).count()
            + Collection.objects.for_store(store).count()
        )
        if total == 0:
            return "—"
        published = (
            ProductTranslation.objects.cross_store_unsafe().filter(
                store=store,
                lang_code=lang,
                status=TranslationStatus.PUBLISHED,
            ).count()
            + CollectionTranslation.objects.cross_store_unsafe().filter(
                store=store,
                lang_code=lang,
                status=TranslationStatus.PUBLISHED,
            ).count()
        )
        pct = round(published / total * 100)
        return f"{published}/{total} ({pct}%)"

    def save_model(self, request, obj, form, change):
        """
        Warn when is_enabled transitions True → False (ML-012).

        Disabling a published language causes LocaleMiddleware to return 410 Gone
        for all requests on that language's URL namespace. Store admins must see
        this warning so the action is intentional.
        """
        if change and "is_enabled" in form.changed_data:
            old = StoreLanguage.objects.get(pk=obj.pk)
            if old.is_enabled and not obj.is_enabled:
                from django.contrib import messages

                messages.warning(
                    request,
                    f"Language '{obj.lang_code}' has been disabled. "
                    "All URLs for this language will return HTTP 410 Gone until re-enabled "
                    "(per ML-012 — ensure this is intentional).",
                )
        super().save_model(request, obj, form, change)


class StoreLanguageStoreAdminAdmin(StoreLanguageAdmin):
    """
    Store-admin view: only StoreLanguage rows belonging to the current store (request.store).
    Scoped in get_queryset() so a store admin cannot see or edit language settings of
    other stores.  save_model() enforces the store FK on creation.

    Languages may not be deleted — only disabled (ML-012). Redirect entries are retained.
    A custom "Add Language" page is available at add-language/ to guide admins through
    the lang_code + routing options without exposing the raw store FK.

    module_key="domains" (ADR-033 D3c, high confidence). has_delete_permission
    stays hardcoded False below regardless of module grant — the ML-012
    "disable, never delete" rule is independent of the permission matrix.
    """

    module_key = "domains"

    def has_delete_permission(self, request, obj=None):
        """Languages are disable-only, never deleted (ML-012)."""
        return False

    def get_queryset(self, request):
        qs = super().get_queryset(request)
        if not hasattr(request, "store") or request.store is None:
            return qs.none()
        return qs.filter(store=request.store)

    def save_model(self, request, obj, form, change):
        if hasattr(request, "store") and request.store:
            obj.store = request.store
        super().save_model(request, obj, form, change)

    def get_urls(self):
        from django.urls import path

        urls = super().get_urls()
        custom = [
            path(
                "add-language/",
                self.admin_site.admin_view(self.add_language_view),
                name="stores_storelanguage_addlanguage",
            ),
        ]
        return custom + urls

    def add_language_view(self, request):
        """
        Simple admin page for adding a new StoreLanguage to the current store.

        Django admin does not natively support modals; this extra-URL pattern is the
        closest Django-native equivalent (ADR-008 §5, TICKET-024).

        On POST, creates the StoreLanguage via get_or_create so duplicate submissions
        are idempotent. Requires request.store to be set (store admin context only).

        ADR-033 D3a: this is a custom admin view, not a ModelAdmin permission
        method, so the structural mixin does not cover it — gated directly
        via check_module_access at "domains"/"full" (adding a language is a
        mutation, same threshold as has_add_permission would use).
        """
        from django.http import HttpResponseForbidden
        from django.shortcuts import redirect
        from django.template.response import TemplateResponse

        store = getattr(request, "store", None)
        if store is None or not check_module_access(request, "domains", "full"):
            return HttpResponseForbidden()

        if request.method == "POST":
            lang_code = request.POST.get("lang_code", "").strip().lower()
            use_path_prefix = request.POST.get("use_path_prefix") == "true"
            is_default = request.POST.get("is_default") == "true"
            if lang_code:
                StoreLanguage.objects.get_or_create(
                    store=store,
                    lang_code=lang_code,
                    defaults={
                        "use_path_prefix": use_path_prefix,
                        "is_default": is_default,
                        "is_enabled": True,
                    },
                )
            return redirect("..")

        return TemplateResponse(
            request,
            "admin/stores/storelanguage/add_language.html",
            {
                "store": store,
                "title": "Add Language",
            },
        )


admin.register(StoreLanguage, site=store_admin_site)(StoreLanguageStoreAdminAdmin)
admin.register(StoreLanguage, site=super_admin_site)(StoreLanguageAdmin)


class ShippingCountryAdmin(admin.ModelAdmin):
    """
    ShippingCountry admin — target-market declarations per (store, language) pair.

    NOT operational shipping rates — those are shipping zones (TICKET-032).
    """

    list_display = ["country_code", "store_language"]
    search_fields = ["country_code"]
    raw_id_fields = ["store_language"]


class ShippingCountryStoreAdminAdmin(ShippingCountryAdmin):
    """
    Store-admin view: only ShippingCountry rows whose store_language belongs to the
    current store (request.store).  The store is two hops away via store_language__store.

    module_key="domains" (ADR-033 D3c, high confidence, grouped with
    StoreDomain/StoreLanguage — target-market declarations live on the same
    settings screen, ADR-008 §3/§5).
    """

    module_key = "domains"

    def get_queryset(self, request):
        qs = super().get_queryset(request)
        if not hasattr(request, "store") or request.store is None:
            return qs.none()
        return qs.filter(store_language__store=request.store)


admin.register(ShippingCountry, site=store_admin_site)(ShippingCountryStoreAdminAdmin)
admin.register(ShippingCountry, site=super_admin_site)(ShippingCountryAdmin)
