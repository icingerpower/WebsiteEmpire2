"""
The single permission resolver for the store-employee module matrix
(ADR-033 D3a/D3b), replacing the seven copy-pasted per-app
`_check_module_access` helpers (badges, catalog, orders, engagement, pages,
feeds, stores/admin.py) and analytics/admin_views.py's distinct decorator-
style check.

Exports:
    get_store_employee(request)      -- request-cached StoreEmployee lookup.
    check_module_access(request, module_key, level) -- THE resolution function.
    require_module(module_key, level) -- decorator for custom admin views.
    StoreModulePermissionMixin       -- structural ModelAdmin mixin, auto-
                                         applied by webecom.admin.StoreAdminSite
                                         .register() (never opted into by hand).
"""

from functools import wraps

from django.contrib.admin.options import BaseModelAdmin
from django.core.exceptions import PermissionDenied

from stores.modules import MODULE_EXEMPT

_CACHE_ATTR = "_pradize_store_employee_cache"


def get_store_employee(request):
    """
    Return the active StoreEmployee row for (request.user, request.store), or
    None. Cached on the request object: the admin index page calls
    has_module_permission once per registered ModelAdmin (~30 times per
    request), so an uncached lookup here would be an N-queries-per-request
    regression (ADR-033 D3a).

    Cache presence is checked via request.__dict__ directly, NOT
    hasattr()/getattr() — a MagicMock request (the prevailing pattern across
    this codebase's admin permission tests) auto-vivifies ANY attribute
    access, so hasattr(mock, "anything") is always True and getattr() would
    return a fresh, truthy MagicMock instead of the real cached value or
    None. Real Django HttpRequest objects store plain attribute assignments
    in __dict__ exactly the same way, so this is equally correct there.
    """
    if _CACHE_ATTR in request.__dict__:
        return request.__dict__[_CACHE_ATTR]

    employee = None
    user = getattr(request, "user", None)
    store = getattr(request, "store", None)
    if (
        user is not None
        and getattr(user, "is_authenticated", False)
        and store is not None
    ):
        from stores.models import StoreEmployee

        employee = StoreEmployee.objects.filter(
            user=user, store=store, is_active=True
        ).first()

    request.__dict__[_CACHE_ATTR] = employee
    return employee


def check_module_access(request, module_key, level="limited"):
    """
    THE single module-access resolution function (design-pattern-ideas.txt
    §XV-4 — one place, not seven copies).

    module_key may be:
      - a str matching a stores.modules.MODULES key,
      - a tuple[str, ...] -- access is granted if ANY key in the tuple grants
        at `level` (needed by campaigns, whose admins serve both
        upsell_campaigns and abandoned_campaigns),
      - None -- fail-closed: only the bypass steps below (1-4) can grant
        access. Used for admins missing a module_key declaration (ADR-033
        D3b's "undeclared is not silently open" default).

    Resolution order (byte-for-byte the semantics of the seven deleted
    per-app helpers):
      1. Inactive/anonymous user -> deny.
      2. is_super_admin -> allow (the matrix never applies to super-admins).
      3. No request.store -> deny.
      4. No active StoreEmployee row for (user, store) -> deny.
      5. full_access=True on that row -> allow (master override).
      6. module_key is None -> deny.
      7. Otherwise: StoreEmployee.has_module_access(key, level) for the key
         (or ANY key when module_key is a tuple).
    """
    user = getattr(request, "user", None)
    if user is None or not getattr(user, "is_active", False):
        return False
    if getattr(user, "is_super_admin", False):
        return True
    if getattr(request, "store", None) is None:
        return False

    employee = get_store_employee(request)
    if employee is None:
        return False
    if employee.full_access:
        return True
    if module_key is None:
        return False
    if isinstance(module_key, tuple):
        return any(employee.has_module_access(key, level) for key in module_key)
    return employee.has_module_access(module_key, level)


def require_module(module_key, level="limited"):
    """
    View decorator for custom admin views that are not a ModelAdmin
    permission method — analytics' dashboard view, the feeds regenerate/
    rotate-token views, pages' contact inbox, stores' add-language view.
    Denies with a 403 (django.core.exceptions.PermissionDenied).
    """

    def decorator(view_func):
        @wraps(view_func)
        def wrapped(request, *args, **kwargs):
            if not check_module_access(request, module_key, level):
                raise PermissionDenied("You do not have permission to access this page.")
            return view_func(request, *args, **kwargs)

        return wrapped

    return decorator


def _find_override(admin_instance, method_name):
    """
    Walk admin_instance's MRO, starting just after StoreModulePermissionMixin,
    for the first class whose OWN __dict__ defines `method_name`.

    Returns that plain function, or None if the walk reaches
    django.contrib.admin.options.BaseModelAdmin (the stock definition every
    ModelAdmin ultimately inherits) without finding one first -- i.e. no
    genuine app-specific override exists.

    Why this exists: StoreModulePermissionMixin is injected AHEAD of the
    concrete ModelAdmin in the MRO (webecom.admin.StoreAdminSite.register(),
    mirroring the StoreScopedFormFieldsMixin precedent), so the mixin's own
    method would otherwise shadow any pre-existing business-rule override on
    the concrete class outright (StoreLanguageStoreAdminAdmin's hard "never
    delete" rule; StoreDomainStoreAdminAdmin's extra "no resolved store"
    check; QuotationRequestAdmin's "limited access may still open the
    read-only change form"; StockNotificationAdmin's permanently-False
    add/change). This helper lets the mixin DEFER ENTIRELY to such an
    override when one exists (the override is expected to call
    check_module_access itself with whatever level it needs), while
    guaranteeing the mixin never falls through into Django's own stock,
    Permission/Group-based implementation if no override exists (ADR-033 D6:
    those rows are never populated by our flows — is_staff/is_superuser are
    never set by any Pradize flow — and must never be consulted; consulting
    them would silently DENY real employees whose access the matrix grants,
    since ModelBackend.has_perm() is empty for everyone but Django-superusers).
    """
    mro = type(admin_instance).__mro__
    start = mro.index(StoreModulePermissionMixin) + 1
    for klass in mro[start:]:
        if method_name in klass.__dict__:
            if klass is BaseModelAdmin:
                return None
            return klass.__dict__[method_name]
    return None


class StoreModulePermissionMixin:
    """
    Structural module-permission gate (ADR-033 D3b), auto-mixed into every
    ModelAdmin registered on store_admin_site by
    webecom.admin.StoreAdminSite.register() -- never opted into by hand, so
    a future admin cannot forget it (the TICKET-052/ADR-031/ADR-032
    registration-time-wrapping precedent).

    Concrete ModelAdmins declare a `module_key` class attribute:
      - a str or tuple[str, ...] (see check_module_access's docstring), or
      - stores.modules.MODULE_EXEMPT (bypass entirely -- requires a
        justification comment at the declaration site; the drift test
        (stores/tests) still requires the attribute to be present).
    Leaving `module_key` at its None default fails closed for non-full_access
    employees; full_access employees and super-admins are unaffected
    (check_module_access's own bypass order runs first regardless).

    Generic mapping (ADR-033 D3b's table):
      - has_module_permission / has_view_permission -> module at "limited"
        (the view threshold; "full" passes by rank).
      - has_add_permission / has_change_permission / has_delete_permission
        -> module at "full".
    "Orders limited access = view-only" falls out of this mapping with NO
    special case: orders/admin.py just declares module_key="orders".

    A concrete ModelAdmin that ALREADY defines one of these five methods
    (found via _find_override) is left to run AS-IS instead of the generic
    mapping above — this is how the handful of pre-existing bespoke
    overrides keep their exact behavior after being ported off the deleted
    per-app `_check_module_access` helpers. Either way, Django's stock
    Permission/Group-based checks are NEVER consulted (D6) — this mixin does
    not call super() into them.

    Deliberately NO `module_key = None` class attribute here (unlike the
    method defaults above, which use the _find_override indirection):
    ordinary attribute lookup along the MRO is NOT cooperative the way a
    method calling super() is — if this mixin declared its own `module_key`
    class attribute, it would sit ahead of the concrete ModelAdmin in the
    MRO (same position as StoreScopedFormFieldsMixin, TICKET-052 precedent)
    and would therefore ALWAYS win over ProductAdmin's own `module_key =
    "products"`, silently discarding every concrete declaration in this
    file's sweep. `getattr(self, "module_key", None)` below reads whatever
    the concrete class (or MODULE_EXEMPT) actually set, falling back to None
    only when NOTHING in the MRO defines it at all (ADR-033 D3b's fail-
    closed default for an undeclared admin).
    """

    def has_module_permission(self, request):
        override = _find_override(self, "has_module_permission")
        if override is not None:
            return override(self, request)
        module_key = getattr(self, "module_key", None)
        if module_key == MODULE_EXEMPT:
            return True
        return check_module_access(request, module_key, "limited")

    def has_view_permission(self, request, obj=None):
        override = _find_override(self, "has_view_permission")
        if override is not None:
            return override(self, request, obj)
        module_key = getattr(self, "module_key", None)
        if module_key == MODULE_EXEMPT:
            return True
        return check_module_access(request, module_key, "limited")

    def has_add_permission(self, request):
        override = _find_override(self, "has_add_permission")
        if override is not None:
            return override(self, request)
        module_key = getattr(self, "module_key", None)
        if module_key == MODULE_EXEMPT:
            return True
        return check_module_access(request, module_key, "full")

    def has_change_permission(self, request, obj=None):
        override = _find_override(self, "has_change_permission")
        if override is not None:
            return override(self, request, obj)
        module_key = getattr(self, "module_key", None)
        if module_key == MODULE_EXEMPT:
            return True
        return check_module_access(request, module_key, "full")

    def has_delete_permission(self, request, obj=None):
        override = _find_override(self, "has_delete_permission")
        if override is not None:
            return override(self, request, obj)
        module_key = getattr(self, "module_key", None)
        if module_key == MODULE_EXEMPT:
            return True
        return check_module_access(request, module_key, "full")
