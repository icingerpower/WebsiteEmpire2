# Rollback Plan — T023: Store-Admin Site + Whitelist Access Model

**Release label:** T023-store-admin-whitelist

---

## Rollback trigger

Roll back if any of the following are observed after deploy:
- Store admins with zero StoreEmployee rows lose access (should not happen;
  zero rows = open-default is unchanged).
- Active employees with a valid row for the current store are denied access.
- Employees can no longer see their allowed modules (catalog, orders).
- Any 500 error on /admin/ pages that was not present before deploy.

---

## Rollback procedure

No database migrations were applied for this feature. Rollback is a pure code
revert.

1. Identify the commit SHA of the last good state (before this feature).
2. git revert or git reset to that commit on the deployment branch.
3. Redeploy (restart the Django/Gunicorn process).
4. Verify: admin login works for an active store employee.

Estimated rollback time: under 5 minutes.

---

## No data cleanup required

This release made no schema changes. No migration rollback is needed. No data
was written to the database by these code changes.

---

## What reverts to in the rolled-back state

- has_permission reverts to the naive is_active=True filter (the original bug
  where deactivated users could gain all-store access via the open-default path).
  This is a known security regression — prioritize re-fixing after rollback.
- StoreEmployee admin form reverts to exposing the store FK field (allows
  store-switching by the admin user).
- has_module_perms (incorrect method name) returns instead of has_module_permission.
  Django may silently ignore the incorrectly-named override, reverting to no
  module-level gating for catalog/orders.

If a rollback is triggered, the deactivated-employee security bug is live again.
Deactivate any suspect accounts manually in the database while the fix is
re-prepared.
