# Release Notes — T023: Store-Admin Site + Whitelist Access Model

**Release label:** T023-store-admin-whitelist
**Date:** 2026-07-04
**Scope:** webecom/admin.py, stores/admin.py, catalog/admin.py, orders/admin.py

---

## Summary

This release hardens the per-store admin access model (ADR-001 §2/§3).
It fixes a security bug where a deactivated StoreEmployee row could silently
widen access to all stores, and adds complementary admin hardening for the
StoreEmployee editor and module-permission hooks.

---

## Changes

### webecom/admin.py — StoreAdminSite.has_permission (two-phase is_active)

Bug fixed: the previous implementation filtered StoreEmployee rows by is_active
upfront. A user with only inactive rows got zero results and fell through to the
open-default path, gaining access to all stores.

New behavior:
- Phase 1: fetch all rows for the user regardless of is_active. If any row
  exists, the user is in restricted mode.
- Phase 2: within restricted mode, access requires an ACTIVE row for the current
  store (request.store). No active row = denied.
- Zero rows still means open-default (access all stores).
- Super-admins bypass the whitelist entirely.
- request.store absent or None → deny as the safe default.

### stores/admin.py — StoreEmployeeStoreAdminAdmin hardening

Three changes applied:
1. exclude = ("store",) — the store FK is removed from the add/change form.
   A store admin cannot use the form to assign an employee to a different store.
2. save_model pins obj.store = request.store on creation (not on update), so the
   store FK is always correct regardless of what data reaches the view.
3. has_add_permission, has_change_permission, has_delete_permission now gate on
   _check_employee_access(request, "full"). Without this, any store admin with
   admin site access could manage employees regardless of their module permissions.

### catalog/admin.py, orders/admin.py — has_module_permission rename

The method was previously named has_module_perms (non-standard). Django calls
has_module_permission (Django docs: ModelAdmin.has_module_permission). Renamed
in ProductAdmin, CollectionAdmin, and OrderAdmin.

### All three _check_module_access / _check_employee_access functions

except Exception → except StoreEmployee.DoesNotExist. Bare exception handling
masked unexpected errors. The narrowed form lets real bugs surface.

---

## Tests added (21 new, 291 total)

- webecom/tests/test_store_admin_access.py: 11 tests (whitelist logic)
- stores/tests/test_store_employee_admin.py: 12 tests (StoreEmployeeStoreAdminAdmin)
- catalog/tests/test_admin_hooks.py: 3 tests (CollectionAdmin.has_module_permission)
- orders/tests/test_admin_hooks.py: 3 tests (OrderAdmin.has_module_permission)
- stores/tests/test_admin.py: has_module_permission coverage added

## Bug regression (BUG_TESTS.csv)

T023-inactive-employee — PROVEN:
- Revert to naive filter(is_active=True): both regression tests FAIL (has_permission
  returns True for a user with only inactive rows).
- Restore two-phase fix: both tests PASS.

---

## Deployment checklist

1. No new migrations to run for this feature.
   (Three pre-existing unapplied migrations from other tickets exist:
   stores.0009, catalog.0005, permalinks.0003 — do NOT apply these as part
   of this release.)
2. No new environment variables or settings.
3. No static file changes.
4. Deploy is a standard Django code push. No downtime required.
5. After deploy, verify: a deactivated employee cannot access any store's admin.
6. After deploy, verify: a new employee created via store-admin has store
   automatically set to the current store (not selectable via form).

---

## Deferred to TICKET-047

- _check_employee_access("limited") path — permissions UI not yet built.
- has_view_permission/has_module_permission not gated on StoreEmployeeStoreAdminAdmin.
- Super-admin save_model IntegrityError when no store context (tracked).
