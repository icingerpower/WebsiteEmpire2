# Release Checklist — T023: Store-Admin Site + Whitelist Access Model

**Release label:** T023-store-admin-whitelist
**Date:** 2026-07-04
**Release Manager verdict:** READY

---

## Checklist

### Spec approved (by human where critical)
PASS — ADR-001 §2/§3 (09_architecture_decisions.md) documents the two-site
architecture, StoreEmployee row model, and module-gating intent. The two-phase
is_active check (security fix) is a direct consequence of the documented
security invariant: "deactivating a row must never widen access." No new
product requirement was introduced; this is a correct implementation of the
approved architecture.

NOTE — Ticket numbering discrepancy: the pipeline labels this as TICKET-023,
but `specs/ecommerce_engine/10_implementation_tickets.md` assigns TICKET-023 to
"Review system" (Phase 2). The implemented work falls under TICKET-002 scope
("storage + gating of /admin/ module access by permissions_json"). The
discrepancy is logged under KNOWN_RISKS and requires a human to reconcile the
spec ticket index.

### Architecture decisions recorded (ADRs in 09_architecture_decisions.md)
PASS — ADR-001 is the governing decision. The two-phase is_active check is a
security-correctness refinement of the ADR-001 §3 StoreEmployee access model,
not a separate architectural decision; it is fully documented in
webecom/admin.py docstring (lines 25–71). No new ADR is required.

### Implementation complete
PASS — all three layers verified by direct file read:

webecom/admin.py StoreAdminSite.has_permission:
- Phase 1: all_memberships = StoreEmployee.objects.filter(user=request.user)
  (no is_active filter) — any row (active or not) triggers restricted mode.
- Phase 2: within restricted mode, grant only when
  all_memberships.filter(store=request.store, is_active=True).exists().
- Super-admin bypass before whitelist check.
- request.store absent/None → deny as safe default.

stores/admin.py StoreEmployeeStoreAdminAdmin:
- exclude = ("store",) — store field absent from form.
- save_model pins obj.store = request.store on creation (not change).
- has_add/change/delete_permission gated on _check_employee_access(request, "full").

catalog/admin.py:
- has_module_permission (not has_module_perms) on ProductAdmin and CollectionAdmin.
- except StoreEmployee.DoesNotExist (not bare except Exception).

orders/admin.py:
- has_module_permission (not has_module_perms) on OrderAdmin.
- except StoreEmployee.DoesNotExist.

stores/admin.py _check_employee_access:
- except StoreEmployee.DoesNotExist.

### Tests added
PASS — test files and method counts verified:
- webecom/tests/test_store_admin_access.py: 11 methods
- stores/tests/test_store_employee_admin.py: 12 methods (new)
- catalog/tests/test_admin_hooks.py: 3 methods (new)
- orders/tests/test_admin_hooks.py: 3 methods (new)
- stores/tests/test_admin.py: 10 methods (includes has_module_permission coverage)

### Tests passing (run myself)
PASS — command run:
  DJANGO_SETTINGS_MODULE=webecom.settings.development \
    python3 manage.py test stores catalog orders webecom --verbosity=0

Result: Ran 291 tests in 24.370s — OK. Zero failures, zero errors.

### Coverage checked
PASS — all three safety fixes have direct test coverage:
- Two-phase is_active: test_store_admin_inactive_row_does_not_grant_all_store_access,
  test_store_admin_inactive_row_for_target_store_is_denied.
- StoreEmployeeStoreAdminAdmin hardening: 12 tests in test_store_employee_admin.py.
- has_module_permission on catalog/orders/stores: test_admin_hooks.py (3+3+3 tests).

Gaps (non-blocking, deferred to TICKET-047):
- _check_employee_access("limited") path not tested.
- has_view_permission/has_module_permission not gated on StoreEmployeeStoreAdminAdmin.

### Spec Reviewer approved
PASS — Spec Reviewer approved with two test gaps (high: StoreEmployee admin
tests; medium: has_module_permission tests). Both gaps addressed by Test Agent
(21 new tests, 291 total). Spec Reviewer sign-off accepted as conditional PASS,
now fully satisfied.

### Safety Agent approved
PASS — Safety Agent APPROVED WITH CONDITIONS. Three mandatory fixes were
required and have been verified implemented:
1. Two-phase is_active check — DONE (webecom/admin.py lines 62–71).
2. StoreEmployeeStoreAdminAdmin hardening — DONE (exclude, save_model, has_*).
3. has_module_perms → has_module_permission rename — DONE (all three files).
Additional narrowing: except Exception → except StoreEmployee.DoesNotExist — DONE.

### SEO Agent approved
N/A — this feature affects admin surfaces only (/admin/, /superadmin/). No
public or indexable pages are touched.

### Designer approved
N/A — this feature adds no new UI elements; it is access-control logic only.

### Migrations reviewed
PASS — no new migrations required. All model fields (StoreEmployee.is_active,
store FK, permissions_json) existed before this work. The admin changes are
pure Python/metaclass registration.

Pre-existing unapplied migrations (from other tickets, not this feature):
- stores.0009_alter_store_custom_domain_and_more
- catalog.0005_add_product_collection_translations
- permalinks.0003_alter_permalink_slug
These are NOT part of this release and must not be applied as part of it.

### Settings documented
PASS — no new settings introduced. The whitelist behavior is entirely in code
with no configuration knobs. The feature's behavior is documented in the
webecom/admin.py module docstring and in ADR-001 §2/§3.

### Deployment checklist ready
See DEPLOYMENT_CHECKLIST section in RELEASE_NOTES.md.

### Rollback plan ready
See ROLLBACK_PLAN.md.

### Known risks listed
See KNOWN_RISKS.md.

### Human decisions listed
1. Ticket numbering: reconcile whether this work is TICKET-002 (scope match) or
   a separate ticket. The spec must be updated to avoid future confusion.
2. TICKET-047 dependency: _check_employee_access("limited") path and
   StoreEmployeeStoreAdminAdmin has_view/module_permission gating are deferred.
   Human must approve the deferred scope before TICKET-047 is released.
3. Super-admin save_model IntegrityError: super-admin accessing store-admin
   without store context can trigger IntegrityError in save_model for new objects.
   Tracked risk, fix deferred — human must confirm acceptable until TICKET-047.

---

## Verdict

**READY**

All tests pass (291/291). All three Safety Agent mandatory fixes verified in
code. Bug proven in BUG_TESTS (T023-inactive-employee, PROVEN). Spec Reviewer
gaps closed by Test Agent. No new migrations. No new settings. Rollback is a
one-line revert.

One human action required before closing: reconcile the ticket number in the
spec file (TICKET-023 currently maps to "Review system" in the spec; this work
is TICKET-002 scope).
