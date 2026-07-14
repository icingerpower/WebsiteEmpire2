# Known Risks — T023: Store-Admin Site + Whitelist Access Model

---

## Risk 1 — Ticket numbering discrepancy (LOW operational risk, HIGH documentation risk)

The pipeline labels this work as TICKET-023. In
`specs/ecommerce_engine/10_implementation_tickets.md`, TICKET-023 is assigned to
"Review system" (Phase 2). The implemented work (StoreAdminSite whitelist,
StoreEmployee admin hardening, has_module_permission gating) maps to TICKET-002
scope ("storage + gating of /admin/ module access by permissions_json").

Operational impact: none. The code is correct. The risk is documentation confusion:
future developers looking up TICKET-023 in the spec will find Review system, not
this work.

Required human action: update `specs/ecommerce_engine/10_implementation_tickets.md`
to either (a) add a TICKET-002b or TICKET-002-followup entry for this work, or
(b) clarify that the orchestrator's TICKET-023 label is an internal tracking
number separate from the spec's TICKET-023.

---

## Risk 2 — _check_employee_access("limited") path untested (LOW)

The "limited" level path in stores/admin.py _check_employee_access is not tested.
It can only be exercised when StoreEmployee.has_module_access("employees", "limited")
returns True, which requires the permissions UI from TICKET-047.

Impact: if has_module_access("employees", "limited") has a bug, it would not be
caught by the current suite. The "full" path is tested and covers the production
path for this ticket.

Mitigation: deferred to TICKET-047 (permissions UI). Until then, only "full"
access is used for employee management, which is conservative and secure.

---

## Risk 3 — StoreEmployeeStoreAdminAdmin has_view_permission / has_module_permission not gated (LOW)

has_view_permission and has_module_permission are not overridden on
StoreEmployeeStoreAdminAdmin. Django defaults allow any store-admin user to see
the employee module even if they lack the "employees" module permission.

Impact: a user with no "employees" permission in permissions_json can still see
the employee list. They cannot add, change, or delete (those are gated). Viewing
alone reveals employee email addresses for the current store.

Mitigation: deferred to TICKET-047. Acceptable until then given that:
(a) the user must already be an authenticated, active store-admin;
(b) they can only see employees of their own store;
(c) the store admin surface is not public-facing.

---

## Risk 4 — Super-admin save_model IntegrityError without store context (LOW)

In stores/admin.py StoreEmployeeStoreAdminAdmin.save_model, the store is only
pinned when `not change`. If a super-admin accesses the store-admin site
without a store context (request.store is None) and tries to create a new
StoreEmployee, obj.store will not be set, and the NOT NULL constraint will raise
an IntegrityError.

Impact: super-admins using the store-admin surface (rare; they typically use
/superadmin/) without a valid store context get a 500 error on save.

Mitigation: super-admins should use /superadmin/ for cross-store employee
management. The store-admin surface assumes request.store is always set.
Fix tracked; deferred until TICKET-047 adds proper store-context enforcement.

---

## Risk 5 — First-label subdomain match deprecation (LOW, tracked separately)

Noted in the pipeline as tracked with ADR-008 migration. Not specific to this
release. No action required here.
