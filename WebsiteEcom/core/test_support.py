"""
Shared test-only helpers (not imported by any production code).

registered_admin() exists because of ADR-033 D3b / TICKET-052: every
ModelAdmin registered on either admin site gets auto-wrapped at
registration time (core.admin.StoreScopedFormFieldsMixin on both sites;
stores.permissions.StoreModulePermissionMixin on the store site only —
see webecom/admin.py's register() overrides). A bare `SomeAdmin(Model,
some_site)` instantiation in a test bypasses ALL of that wrapping and
therefore does not exercise the same has_module_permission/has_view_permission/
has_add_permission/has_change_permission/has_delete_permission behavior a
real request goes through. Tests that assert on permission-gating behavior
must fetch the REGISTERED instance instead.
"""


def registered_admin(model, site):
    """Return the actual wrapped ModelAdmin instance Django uses for `model` on `site`."""
    return site._registry[model]
