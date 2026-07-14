"""
Two Django AdminSite instances for Pradize (ADR-001 §2).

store_admin_site  — per-store surface at /admin/
                    Access gated by StoreEmployee rows (TICKET-002).
                    has_permission checks User.is_active and User.is_store_admin.
                    Store whitelist: a user with ≥1 StoreEmployee rows is restricted
                    to those stores; 0 rows means open access (all stores).

super_admin_site  — cross-org control plane at /superadmin/
                    Access gated by User.is_super_admin.
                    has_permission checks User.is_active and User.is_super_admin.

Both authenticate against the same User table (core.User).
"""

from django.contrib.admin import AdminSite, ModelAdmin


class _StoreScopedRegistrationMixin:
    """
    ADR-031 Addendum 3 (D2): every ModelAdmin registered on either admin site
    automatically gets core.admin.StoreScopedFormFieldsMixin mixed in, so any
    FK/M2M field targeting a StoreOwnedModel always resolves to a safe
    default queryset (store-scoped on the store site, cross-store —
    optionally narrowed to a bound instance's own store — on the super site)
    even when the concrete ModelAdmin never explicitly scopes that field.

    This is enforcement-by-construction, matching the "stop relying on
    per-site memory" philosophy already used for StoreOwnedInlineMixin
    (core/admin.py) and the raw-id-widget sweep (core/admin_widgets.py) —
    a per-admin opt-in mixin is exactly the kind of thing a future admin
    forgets (this family has bitten repeatedly per ADR-031's addenda), so
    registration itself is the one place that can't be skipped.

    Deferred (function-local) import of core.admin: core/admin.py imports
    store_admin_site/super_admin_site from THIS module at its own module
    level, so this module cannot import core.admin at module level without
    a circular import. By the time register() is actually called (whether
    from an app's admin.py during Django's admin autodiscovery, or from
    core/admin.py's own bottom-of-file registrations), core.admin's classes
    are already defined even if the module is still mid-execution, so the
    late import always resolves.
    """

    def register(self, model_or_iterable, admin_class=None, **options):
        from core.admin import StoreScopedFormFieldsMixin

        base_class = admin_class or ModelAdmin
        wrapped_class = _mix_in(base_class, StoreScopedFormFieldsMixin)
        super().register(model_or_iterable, admin_class=wrapped_class, **options)


def _mix_in(base_class, mixin_class):
    """
    Return base_class unchanged if it already inherits mixin_class; otherwise
    a freshly-built subclass with mixin_class inserted ahead of it in the
    MRO. Shared by every registration-time auto-wrap in this module
    (ADR-031 Addendum 3 D2, ADR-033 D3b) — the __name__/__module__ are copied
    from base_class so Django admin's app-list grouping and any code that
    inspects admin_class.__name__ see no difference.
    """
    if issubclass(base_class, mixin_class):
        return base_class
    return type(
        base_class.__name__,
        (mixin_class, base_class),
        {"__module__": base_class.__module__},
    )


class StoreAdminSite(_StoreScopedRegistrationMixin, AdminSite):
    site_header = "WebsiteEcom Store Admin"
    site_title = "WebsiteEcom"
    index_title = "Store Management"

    def register(self, model_or_iterable, admin_class=None, **options):
        """
        ADR-033 D3b: every ModelAdmin registered on the STORE site (and only
        the store site — the super site is never matrix-gated, D3b/D7) is
        additionally auto-wrapped with stores.permissions.
        StoreModulePermissionMixin, the same registration-time-wrapping
        precedent as _StoreScopedRegistrationMixin above (TICKET-052).

        Deferred import: stores/permissions.py does not import webecom.admin,
        so there is no circular-import hazard here (unlike core.admin, which
        imports store_admin_site/super_admin_site from this module) — the
        import is still function-local for consistency with the sibling wrap
        and to keep webecom/admin.py import-light at module load time.

        Wrapping happens here, BEFORE calling super().register() (which
        applies the StoreScopedFormFieldsMixin wrap) — the two mixins target
        disjoint method sets (permissions vs. formfield scoping) so wrap
        order between them does not matter.
        """
        from stores.permissions import StoreModulePermissionMixin

        base_class = admin_class or ModelAdmin
        wrapped_class = _mix_in(base_class, StoreModulePermissionMixin)
        super().register(model_or_iterable, admin_class=wrapped_class, **options)

    def has_permission(self, request):
        """
        Grant access to active users who are either store admins or super admins.

        Super admins bypass the store whitelist entirely — they are not store-scoped.

        Store whitelist (applies to is_store_admin users only, TICKET-002):
        - 0 StoreEmployee rows for this user → open default: access all stores.
        - ≥1 StoreEmployee rows (active OR inactive) → restricted: access requires
          an ACTIVE row for the current store.  Deactivating a row must never
          widen access — the presence of ANY row (even inactive) locks the user
          into restricted mode.  The current store is read from request.store,
          which is set by HostResolutionMiddleware for admin paths (LocaleMiddleware
          excludes /admin/ from locale resolution, so the middleware-chain result
          from HostResolutionMiddleware stands).
          If request.store is absent or None, access is denied as a safe default.

        Actual per-module access within a store is gated further by
        StoreEmployee.permissions_json (TICKET-047).
        """
        if not (
            request.user.is_active
            and (request.user.is_store_admin or request.user.is_super_admin)
        ):
            return False

        # Super admins are not store-scoped — bypass the whitelist.
        if request.user.is_super_admin:
            return True

        # Store whitelist for is_store_admin users.
        from stores.models import StoreEmployee

        # All rows — active or not — determine whether the user is in restricted mode.
        # Even a deactivated row means the user is store-scoped: they must have an
        # ACTIVE row for the current store to gain entry.  Deactivation must never
        # widen access to all stores (the open-default path).
        all_memberships = StoreEmployee.objects.filter(user=request.user)
        if all_memberships.exists():
            # Restricted mode: at least one row exists (active or not).
            # Access requires an ACTIVE row for the current store.
            if getattr(request, "store", None) is None:
                return False
            return all_memberships.filter(store=request.store, is_active=True).exists()

        # Zero rows: open default — access all stores.
        return True


class SuperAdminSite(_StoreScopedRegistrationMixin, AdminSite):
    site_header = "WebsiteEcom Platform Admin"
    site_title = "WebsiteEcom"
    index_title = "Platform Management"

    def has_permission(self, request):
        """
        Grant access only to active super-admin users.
        """
        return request.user.is_active and request.user.is_super_admin


store_admin_site = StoreAdminSite(name="store_admin")
super_admin_site = SuperAdminSite(name="super_admin")
