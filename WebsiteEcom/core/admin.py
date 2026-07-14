"""
Registers core models with both admin sites.

Two separate ModelAdmin classes:
- UserAdmin (super_admin_site only) — full access including role flags.
- ReadOnlyProfileAdmin (store_admin_site) — scope to own profile only;
  role flags are read-only to prevent privilege escalation.

Security note (Safety Audit HIGH finding):
  is_super_admin and is_store_admin must never be editable by store admins
  (a store admin with core.change_user could escalate to super-admin).
  ReadOnlyProfileAdmin enforces this at the field level and scopes the queryset
  to the requesting user only (no cross-tenant user enumeration).
"""

from django.contrib.auth.admin import UserAdmin as DjangoUserAdmin
from django.core.exceptions import ValidationError

from core.formsets import StoreSafeInlineFormSet
from core.models import StoreOwnedModel, User
from stores.modules import MODULE_EXEMPT
from webecom.admin import store_admin_site, super_admin_site


class StoreUniqueValidationFormMixin:
    """
    ADR-032: store-excluded admin forms must still validate store-inclusive
    unique constraints. Two primitives, both required (two independent kill
    switches — see the ADR):
      1. inject instance.store BEFORE BaseModelForm._post_clean() runs, so
         Model._perform_unique_checks / UniqueConstraint.validate stop
         None-skipping the whole check;
      2. discard "store" from _get_validation_exclusions(), so the check is
         assembled at all (checked tuple-wise in Model._get_unique_checks and
         field-wise in UniqueConstraint.validate).

    _pradize_validation_store is set on a per-request form class (ModelAdmin
    .get_form / inlineformset_factory build a fresh class per call, so a class
    attribute cannot leak across requests). None on the super site and for
    inline forms — inline injection comes from the parent instance via
    StoreSafeInlineFormSet (core/formsets.py) instead.
    """

    _pradize_validation_store = None

    def _post_clean(self):
        if (
            self._pradize_validation_store is not None
            and self.instance.store_id is None
        ):
            self.instance.store = self._pradize_validation_store
        super()._post_clean()

    def _get_validation_exclusions(self):
        exclude = super()._get_validation_exclusions()
        if "store" not in self.fields and self.instance.store_id is not None:
            exclude.discard("store")
        return exclude


class StoreScopedFormFieldsMixin:
    """
    Shared formfield_for_foreignkey/formfield_for_manytomany default queryset
    scoping (ADR-031 Addendum 3, D2 — "same family as the raw-id fix").

    Every FK/M2M field targeting a StoreOwnedModel formfields via
    `db_field.formfield(**kwargs)`, which — unless a `queryset` kwarg was
    already supplied — resolves the widget's choices from the target
    model's raw `_default_manager` (the raising StoreScopedManager,
    core/managers.py, ADR-001 §4). That crashed silently only because
    `count()`/`exists()` didn't raise before TICKET-051 closed that hole;
    `ModelChoiceIterator.__len__`/`__bool__` (forms/models.py:1455,1458) call
    exactly those two methods, so rendering ANY such widget crashes today.

    This mixin backfills a sane default queryset whenever one wasn't already
    supplied — it never overrides an explicit `kwargs["queryset"]` a
    subclass's own override sets (same `"queryset" not in kwargs` convention
    Django's own ModelAdmin.formfield_for_foreignkey uses), so existing
    per-field overrides (e.g. discounts.admin.GiftCardCampaignAdmin's
    trigger_products scoping) continue to take precedence — this mixin only
    covers fields nobody bothered to scope explicitly, which is exactly what
    was silently broken.

    Store-admin site: `.for_store(request.store)` — the standing convention
    for every store-scoped read. `request.store` is None is a defensive
    edge case (e.g. a super-admin browsing /admin/ without a resolved store);
    `.none()` is the sanctioned safe-empty queryset (mirrors the `qs.none()`
    fallback already used by several ModelAdmin.get_queryset() overrides in
    this codebase, e.g. stores.admin.StoreEmployeeStoreAdminAdmin).

    Super-admin site: `.cross_store_unsafe()`, narrowed to the ALREADY-BOUND
    instance's own store when one exists (TICKET-050's
    GiftCardCampaignAdmin.formfield_for_manytomany precedent: resolve
    `object_id` from `request.resolver_match.kwargs`, which is only reliable
    for a non-inline ModelAdmin resolving to a URL for THIS SAME model — an
    InlineModelAdmin's object_id refers to the PARENT model, not
    `self.model`, so narrowing is skipped for inlines and they fall back to
    the unfiltered cross-store list on both add and change forms, which
    matches how super-admin inlines already behave elsewhere (nothing was
    previously enforcing narrower scoping there either — it just crashed).

    Non-admin ModelForms are untouched by this mixin (ADR-031 Addendum 3:
    "views pass scoped querysets" remains the convention outside admin).
    """

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        self._apply_store_scoped_default_queryset(db_field, request, kwargs)
        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def formfield_for_manytomany(self, db_field, request, **kwargs):
        self._apply_store_scoped_default_queryset(db_field, request, kwargs)
        return super().formfield_for_manytomany(db_field, request, **kwargs)

    def get_form(self, request, obj=None, change=False, **kwargs):
        """
        ADR-032 D2: wrap the per-request form class with
        StoreUniqueValidationFormMixin so store-excluded ModelForms still
        validate store-inclusive unique_together/UniqueConstraints. Reached
        by every ModelAdmin on both sites (this mixin is auto-mixed in by
        webecom.admin._StoreScopedRegistrationMixin.register()) — zero
        per-admin edits.
        """
        form_class = super().get_form(request, obj, change=change, **kwargs)
        return self._wrap_store_unique_validation(request, form_class)

    def get_formset(self, request, obj=None, **kwargs):
        """
        ADR-032 D2: only exists on InlineModelAdmin subclasses — ModelAdmin
        never calls it. Wraps the formset's inline form class the same way
        get_form() wraps the top-level form class; store injection for
        inline forms happens separately via StoreSafePKFormSetMixin
        ._construct_form (core/formsets.py), which stamps the PARENT's store
        onto each inline instance at construction time.
        """
        formset_class = super().get_formset(request, obj, **kwargs)
        formset_class.form = self._wrap_store_unique_validation(
            request, formset_class.form, inline=True
        )
        return formset_class

    def _wrap_store_unique_validation(self, request, form_class, inline=False):
        """
        Returns form_class unchanged when:
        - self.model isn't a StoreOwnedModel (nothing to validate against);
        - "store" is already a real form field (DiscountCodeSuperAdmin
          convention — stock Django validates the tuple natively, D3);
        - form_class is already wrapped (idempotent — get_form/get_formset
          can be called more than once per request by some admin code paths).

        Otherwise builds a fresh subclass (never mutates form_class itself,
        which may be shared/cached) mixing in StoreUniqueValidationFormMixin,
        with _pradize_validation_store set to request.store on the store
        site for non-inline forms only. Inline forms and super-site forms
        get None — the store-site request.store is not the right value to
        inject into an inline instance (its store may differ from the
        request's resolved store in edge cases, and the parent-store
        stamping in StoreSafePKFormSetMixin is the authoritative source for
        inlines), and there is no request.store truth on the super site
        (D3).
        """
        if not issubclass(self.model, StoreOwnedModel):
            return form_class
        if "store" in form_class.base_fields:
            return form_class  # stock Django validates natively (DiscountCodeSuperAdmin pattern)
        if issubclass(form_class, StoreUniqueValidationFormMixin):
            return form_class
        store = None
        if not inline and self.admin_site is not super_admin_site:
            store = getattr(request, "store", None)  # HostResolutionMiddleware
        return type(
            form_class.__name__,
            (StoreUniqueValidationFormMixin, form_class),
            {"_pradize_validation_store": store, "__module__": form_class.__module__},
        )

    def _apply_store_scoped_default_queryset(self, db_field, request, kwargs):
        """
        Sets kwargs["queryset"] to a store-scoped default iff: (a) no
        queryset was already supplied, and (b) the field's target is a
        StoreOwnedModel subclass. No-op otherwise (leaves kwargs untouched
        so Django's/the subclass's own logic decides).
        """
        if "queryset" in kwargs:
            return
        remote_field = getattr(db_field, "remote_field", None)
        target_model = getattr(remote_field, "model", None) if remote_field else None
        if not (isinstance(target_model, type) and issubclass(target_model, StoreOwnedModel)):
            return

        if self.admin_site is super_admin_site:
            kwargs["queryset"] = self._cross_store_default_queryset(request, target_model)
        else:
            store = getattr(request, "store", None)
            kwargs["queryset"] = (
                target_model.objects.for_store(store)
                if store is not None
                else target_model.objects.none()
            )

    def _cross_store_default_queryset(self, request, target_model):
        """
        Super-admin site default: cross_store_unsafe(), narrowed to the
        bound instance's own store when this is a non-inline ModelAdmin
        resolving object_id for `self.model` (see class docstring).
        """
        queryset = target_model.objects.cross_store_unsafe()
        if hasattr(self, "parent_model"):
            # InlineModelAdmin: object_id in the URL names the PARENT
            # instance, not self.model — narrowing would look up the wrong
            # model. Left unfiltered, matching existing super-admin inline
            # behavior elsewhere.
            return queryset
        if not issubclass(self.model, StoreOwnedModel):
            return queryset
        resolver_match = getattr(request, "resolver_match", None)
        obj_id = resolver_match.kwargs.get("object_id") if resolver_match else None
        if not obj_id:
            return queryset
        try:
            obj = self.model.objects.cross_store_unsafe().get(pk=obj_id)
        except (self.model.DoesNotExist, ValueError, TypeError, ValidationError):
            return queryset
        return target_model.objects.for_store(obj.store)


class StoreOwnedInlineMixin(StoreScopedFormFieldsMixin):
    """
    Shared base for every InlineModelAdmin whose model is a StoreOwnedModel
    (ADR-031 addendum, TICKET-051).

    Replaces the copy-pasted pair every such inline previously hand-wrote
    (this family has bitten five times before this ticket — §XV-1: stop
    relying on per-site memory):

        def get_queryset(self, request):
            return <Model>.objects.cross_store_unsafe()

    Safe because Django's InlineModelAdmin always further filters the
    formset's queryset by the parent object's FK after get_queryset() runs
    (BaseInlineFormSet.__init__) — the rows are already scoped to one
    specific, already-authorized parent instance, whichever admin site
    reached it.

    `formset = StoreSafeInlineFormSet` closes INLINE-FORMSET-PK-ISOLATION
    (core/formsets.py) for every inline that uses this mixin — without it,
    POSTing a form with >=1 existing row 500s regardless of the get_queryset
    fix above (BaseModelFormSet.add_fields() builds the hidden pk field from
    the raw, unscoped default manager, not from get_queryset()'s result).

    Inlines that need extra chaining (e.g. `.select_related(...)`) should
    override get_queryset() and call `super().get_queryset(request)` rather
    than re-typing `.cross_store_unsafe()`. Inlines with genuinely different
    scoping needs (e.g. cart.CartItemInline, which prefers `.for_store()`
    when `request.store` is available) may override get_queryset() entirely
    — they still get the formset PK fix for free by inheriting from this
    mixin.
    """

    formset = StoreSafeInlineFormSet

    def get_queryset(self, request):
        return self.model.objects.cross_store_unsafe()


class UserAdmin(DjangoUserAdmin):
    """
    Full user admin for /superadmin/ only.
    Exposes role flags as editable fields.
    """

    fieldsets = DjangoUserAdmin.fieldsets + (
        (
            "Pradize roles",
            {"fields": ("is_store_admin", "is_super_admin")},
        ),
    )
    list_display = DjangoUserAdmin.list_display + ("is_store_admin", "is_super_admin")


class ReadOnlyProfileAdmin(DjangoUserAdmin):
    """
    Restricted user admin for /admin/ (store-admin site).

    Security constraints:
    - Queryset scoped to the requesting user only — no cross-tenant enumeration.
    - is_super_admin and is_store_admin are always readonly — no escalation path.
    - Password, groups, permissions, and staff flags hidden (super-admin domain).
    - Add permission denied (creating users belongs to super-admin).
    """

    # Only expose safe profile fields for self-service editing
    fieldsets = (
        (None, {"fields": ("username", "email", "first_name", "last_name")}),
        (
            "Pradize roles (read-only)",
            {"fields": ("is_store_admin", "is_super_admin")},
        ),
    )
    readonly_fields = ("is_store_admin", "is_super_admin")
    list_display = ("username", "email", "first_name", "last_name", "is_store_admin")

    # ADR-033 D3b: the sole shipped MODULE_EXEMPT case. Justification: this
    # admin already scopes to the requesting user's own row (get_queryset
    # below) and already hardcodes has_add/has_delete_permission to False;
    # there is no separate "profile" row in the 19-module vocabulary, and
    # gating a user's ability to view/edit their OWN profile behind a module
    # they might not hold would lock them out of the one screen that lets
    # them see who they are logged in as.
    module_key = MODULE_EXEMPT

    def get_queryset(self, request):
        """Scope to the requesting user's own account only."""
        return User.objects.filter(pk=request.user.pk)

    def has_add_permission(self, request):
        return False

    def has_delete_permission(self, request, obj=None):
        return False

    def get_readonly_fields(self, request, obj=None):
        """Ensure role flags are always readonly, regardless of DjangoUserAdmin defaults."""
        base = super().get_readonly_fields(request, obj)
        return tuple(set(base) | {"is_super_admin", "is_store_admin"})


store_admin_site.register(User, ReadOnlyProfileAdmin)
super_admin_site.register(User, UserAdmin)
