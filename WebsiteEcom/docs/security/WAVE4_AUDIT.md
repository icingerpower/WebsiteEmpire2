# WAVE 4 Pre-Release Security Audit

**Scope:** the final internal wave — the employee permission matrix (TICKET-054 /
TICKET-047, ADR-033), per-store sender domains (TICKET-040, ADR-034), and the
TimescaleDB detection surface (TICKET-044, ADR-035).

**Auditor:** Safety Agent (defensive review only — no production code changed).
**Date:** 2026-07-12.
**Tests observed:** `stores.tests.test_permission_matrix_drift`,
`test_permissions_resolver`, `test_admin_gating`, `test_employee_invite_flow`,
`test_permission_matrix_http`, `emails.tests.test_sender_domain` — **123 passed**.

**Verdict: APPROVED WITH CONDITIONS.** One medium-severity race gap in the
lockout guard (F-1) and its mis-labelled "concurrency" test should be fixed or
formally accepted before this ships to production on a row-locking backend. All
other areas are sound. Conditions are listed at the end.

---

## 1. Permission matrix (ADR-033) — the core system

### What is correctly built

- **Single resolver** (`stores/permissions.py::check_module_access`) — bypass
  order (inactive→deny, super-admin→allow, no store→deny, no row→deny,
  full_access→allow, `module_key is None`→deny, else `has_module_access`) is
  correct and fail-closed. The `module_key is None` branch (undeclared admin)
  denies for non-full-access employees — matches ADR-033 D3b.
- **Request-cache keying** (`get_store_employee`) uses `request.__dict__`
  membership rather than `hasattr/getattr`. This is the right fix: a real
  `HttpRequest` stores attribute assignments in `__dict__`, so `"_..._cache" in
  request.__dict__` is a genuine presence test, while a `MagicMock` cannot
  auto-vivify a `__dict__` key. Cache is per-request-object; a new request =
  fresh `__dict__` = no cross-request poisoning. **Sound.** One nuance: the
  cache stores the row fetched for `(request.user, request.store)`; nothing
  re-keys it if `request.store`/`request.user` were mutated mid-request. No code
  path does that on `/admin/` (store is set once by `HostResolutionMiddleware`,
  user once by auth), so this is safe in practice — noted only so a future
  middleware that swaps `request.store` mid-request knows to clear the cache.
- **Structural auto-wrap** (`webecom/admin.py::StoreAdminSite.register`) wraps
  **every** store-site ModelAdmin with `StoreModulePermissionMixin` at
  registration. An admin registered directly against `store_admin_site` cannot
  skip it — `.register()` is the only entry point, and `admin.register(..., site=store_admin_site)`
  routes through it. The super site is deliberately never matrix-wrapped
  (matrix is store-level by definition). **Cannot be bypassed by a future admin.**
- **Fail-closed + drift test** (`test_permission_matrix_drift`): every store-site
  ModelAdmin must declare a valid `module_key` or `MODULE_EXEMPT`; a dummy
  unmapped admin makes the test fail. Confirmed passing. Every store-site admin
  audited declares a key: badges=`security_badges`, pixels=`pixels`,
  reviews=`reviews`, currency=`currency_converter`, orders/cart=`orders`,
  chat/engagement/feeds=`apps`, consent/emails/shipping/sender-domains=`settings`,
  discounts=`gift_cards`, campaigns=`(upsell_campaigns, abandoned_campaigns)`,
  domains group=`domains`, catalog=`products`/`pages`, employees=`employees`.
- **`_find_override` deference is safe.** The concern was that
  `StoreModulePermissionMixin` defers *entirely* to any pre-existing
  `has_*_permission` method, which could re-open a matrix bypass. I read every
  overriding body across all 17 store-site admin files. **Every override either
  returns `False` (strictly more restrictive: reviews, consent records,
  campaigns sessions/charges/issued-codes, catalog stock-notification,
  currency-settings, pages static-page, orders/catalog read-only inlines) or
  itself calls `check_module_access` (campaigns Campaign/CampaignStep at
  `"full"`, pages ContactMessage / catalog QuotationRequest at
  `"pages"/"limited"`, StoreDomain/SenderDomain `has_module_permission`).**
  None returns an unconditional `True` and none returns a matrix-independent
  truthy value. `_find_override` also correctly stops at `BaseModelAdmin` so the
  mixin never falls through into Django's stock `Permission`/`Group` machinery
  (which, per ADR-033 D6, would wrongly deny real employees since no `Permission`
  rows exist). **Verified sound.**
- **MODULE_EXEMPT** — sole use is `core.admin.ReadOnlyProfileAdmin` (own-profile,
  self-scoped queryset, add/delete hardcoded False, role flags read-only). The
  justification comment is present. Legitimate.

### F-1 (MEDIUM) — Lockout-guard demote/deactivate race is NOT race-free (self excluded from the lock)

`StoreEmployee._validate_lockout_guard()` (stores/models.py:706) acquires:

```python
StoreEmployee.objects.select_for_update()
    .filter(store_id=self.store_id, full_access=True, is_active=True)
    .exclude(pk=self.pk)           # <-- self is excluded from the LOCK, not just the count
    .exists()
```

`exclude(pk=self.pk)` is correct for the *count* ("is there another qualifying
row besides me?") but wrong for the *lock*. Consider a store whose only two
active full-access employees are A and B, demoted concurrently:

- Txn1 (demote A): `SELECT ... FOR UPDATE ... WHERE full_access AND active AND pk != A` locks **{B}**, sees B qualifying → allowed.
- Txn2 (demote B): `SELECT ... FOR UPDATE ... WHERE full_access AND active AND pk != B` locks **{A}**, sees A qualifying → allowed.

The two transactions lock **disjoint** row sets, so `select_for_update` never
serializes them. Both commit → the store ends with **zero active full-access
employees** — exactly the ≥1→0 transition the guard exists to block, and which
ADR-033 D5b explicitly claims is handled ("two concurrent 'demote the other
admin' requests must not interleave past the check").

- **Contrast with the delete path, which is correct.** `delete_model` /
  `delete_queryset` (stores/admin.py:442, 458) lock
  `StoreEmployee.objects.select_for_update().filter(store_id=...)` — the whole
  store set, no `exclude`, no `full_access` filter — so two concurrent deletes
  contend on a common row set and serialize. Only the demote/deactivate
  (`save()`) path has the gap.
- **Severity: Medium.** Requires two precisely-timed concurrent requests by two
  same-store admins on a backend with real row locking (Postgres prod). Outcome
  is a *recoverable* lockout: ADR-033 D5c makes the super site the recovery path
  (a super-admin re-grants). Not a permanent DoS, not cross-tenant, no data loss.
  But it does defeat a guarantee the ADR asserts, and store admins cannot
  self-recover.
- **Fix (developer):** lock the full qualifying set *including* self, e.g. drop
  the `.exclude(pk=self.pk)` from the `select_for_update()` locking query and
  keep the exclusion only in the `.exists()` count — or lock the whole store
  employee set like the delete path does. Either makes both transactions contend
  on a shared row and serialize.

### F-2 (LOW / test integrity) — the "concurrency" test is sequential and cannot catch F-1

`test_store_employee_admin.py::test_only_one_of_two_concurrent_demotes_succeeds`
runs `t1.start(); t1.join(); t2.start(); t2.join()` — fully sequential
(acknowledged in its own comment, because SQLite cannot interleave writers).
It therefore proves only that the *second* demote is blocked *after* the first
has committed — which the unlocked pre-check (`_is_candidate_lockout_transition`)
already guarantees with no lock at all. **The test would still pass even if
`select_for_update` were removed**, so it does not prove the race-freedom
ADR-033's "Tests required #5" demands, and it would not fail on F-1.

- Not a production vulnerability by itself, but it is why F-1 shipped unnoticed.
- **Fix (test agent):** an After-Bug-style proof of F-1 needs a real
  row-locking backend (Postgres) or a deterministic two-thread barrier; on
  SQLite the guarantee is untestable and should be labelled as such rather than
  presented as a concurrency proof.

### Escalation paths — traced, all closed

- **Self-edit (store site):** `has_change_permission`/`has_delete_permission`
  deny when `obj.user_id == request.user.pk` (super-admins exempt).
  Defense-in-depth at `StoreEmployee._validate_no_self_escalation` rejects a
  save that changes `full_access`/`permissions_json`/`is_active` on the actor's
  own row. Threaded via `_ActingUserFormMixin` per-request subclass — the
  transient `_acting_user` is None on shell/migration/super paths, so those are
  unaffected. **Closed.**
- **Bulk actions:** `delete_queryset` explicitly blocks a selection containing
  the actor's own row (Django calls `has_delete_permission(obj=None)` only once
  for the whole action, so the per-object self-guard would otherwise miss it) —
  correctly handled.
- **Store/user field spoofing:** the store-site form
  (`StoreEmployeeStoreAdminForm.Meta.fields`) omits `store` and `user`; `store`
  is pinned to `request.store` and `user` resolved server-side in `save_model`.
  A store-A admin cannot create an employee for store B or target an arbitrary
  user pk. **Closed.**
- **Super-site "allow-with-warning" path abused by a store user:** the
  `_allow_lockout_transition` escape hatch is set *only* in
  `StoreEmployeeSuperAdminAdmin.save_model`, which is registered on
  `super_admin_site` (gated `is_super_admin`). A store-level user never reaches
  it. **Closed.**
- **`is_super_admin`/`is_store_admin` self-elevation:** `ReadOnlyProfileAdmin`
  keeps both flags in `readonly_fields` unconditionally
  (`get_readonly_fields` re-adds them). **Closed.**

### IDOR (employee rows)

`get_queryset` filters by `request.store` (else `.none()`); the permission grid
and change form are reachable only for the current store's rows. Cross-store
enumeration/edit not possible. **Closed.**

### Observation O-1 (LOW, non-blocking) — editable inlines are fail-closed for everyone

Inlines are attached to a parent admin, not registered via `.register()`, so
they are **not** wrapped by `StoreModulePermissionMixin`. Any inline that does
not override `has_add/change/delete_permission` falls through to Django's stock
`BaseModelAdmin.has_perm`, which returns `False` for every Pradize user
(`is_staff`/`is_superuser` are never set — ADR-033 D6 confirmed against
`core.models.User`). This means editable inlines that rely on stock permissions
(e.g. `orders.OrderFulfillmentInline`, whose docstring says "full access can add
fulfillment rows") may render **read-only or not at all**, even for full-access
employees and super-admins. This is **fail-closed (safe direction) and
pre-existing** — not a security hole — but it is a likely *functional* gap the
developer/test agents should verify: if inline editing is a required feature,
those inlines need explicit matrix-aware permission methods.

---

## 2. Sender domains (ADR-034) — spoofing surface

### Verified sound

- **Global uniqueness** (`SenderDomain.domain unique=True`) — store B cannot
  claim the exact domain store A holds. A *subdomain* of A's domain is a
  distinct string and can be created as a row, but it can only reach VERIFIED by
  publishing the row's unique DKIM key in that subdomain's DNS, which requires
  control of the parent zone — i.e. control of the subdomain. No takeover.
- **Binding to platform+row is real.** VERIFIED requires SPF `include:` match
  **AND** DKIM `p=` match. SPF alone would not bind to a specific store (the
  include string is platform-global), but **DKIM binds**: each `SenderDomain`
  generates its own 2048-bit keypair (`dkim_keys.py`), and `check_dkim` requires
  the published `p=` to contain *this row's* `dkim_public_key`. Only the owner of
  that admin row sees the key to publish, and publishing requires DNS control.
  So verification proves both domain control and binding to this exact row.
  **Sound.**
- **FAILED/PENDING never used as From.** `resolve_from_email` filters
  `status=VERIFIED` only; else falls back to `settings.DEFAULT_FROM_EMAIL`. A
  domain flipping to FAILED mid-send just falls back on the next resolve — no
  crash (matches ADR-034 test #2).
- **`force_verify` bypass is correctly fenced.** Only added as an action on
  `SenderDomainSuperAdmin` (super site, `is_super_admin`-gated), re-checks
  `request.user.is_super_admin` explicitly (defense in depth), and writes a
  `SenderDomainManualVerification` audit row per domain. The store-site
  `SenderDomainAdmin` exposes only `verify_domain` (the real DNS check). The
  audit model is read-only in admin (add/change/delete all False). **Both ADR-034
  properties — super-admin-only + audited — hold, and it is unreachable from the
  store site.**
- **DKIM private key hygiene.** `EncryptedCharField` (Fernet) at rest; never in
  any `fields`/`fieldsets`/`readonly_fields` on either admin (explicit class
  docstring guarantee, verified); not logged anywhere. `dkim_public_key` is
  plain text by design (meant to be published). **Sound.**
- **Transient-error handling.** `run_verification_check` never flips status on a
  DNS ERROR (timeout/network) — only on MATCH/MISMATCH/budget-exhaustion. A
  transient resolver failure cannot read as "verification failed" (§XV-1).
  Correct.
- **Un-verification window.** The weekly `recheck_verified_sender_domains`
  demotes VERIFIED→FAILED when records disappear. See R-1 for the residual
  window.

### F-3 (LOW) — `sender_local_part` has no validator; header-injection is mitigated only downstream

`sender_local_part` (CharField(64), store-editable, default `noreply`) is
interpolated directly into the From address of a VERIFIED domain:
`f"{sender_local_part}@{domain}"` → `formataddr(...)`. It has **no
RegexValidator**, so a store admin could set it to a value containing spaces,
`@`, or control characters. `domain` is normalized in `save()` (lowercase, strip
scheme/port/trailing dot) but **not** stripped of CRLF/whitespace either —
though a domain with such characters could never pass DNS verification, so the
domain half is protected by the VERIFIED gate; `sender_local_part` is **not**
gated by any verification.

- **Why only LOW:** Django's `send_mail` → `sanitize_address` /
  `forbid_multi_line_headers` raises `BadHeaderError` on CR/LF in a header, so
  actual header injection (Bcc smuggling) is blocked at the send layer, and
  `send_transactional_email` catches it and records `status=failed`. The
  residual is a malformed From address (e.g. `foo@bar@domain`) that degrades
  deliverability for that store only — self-inflicted, single-tenant.
- **Hardening (developer):** add a mailbox-charset `RegexValidator` (e.g.
  `^[A-Za-z0-9._%+-]+$`) to `sender_local_part`, and CRLF/whitespace stripping in
  `SenderDomain.save()` for defense in depth.

### F-4 (LOW / test hygiene) — some sender-domain tests perform real DNS I/O

The test run logged live `DNS operation timed out` errors for
`shop.example.com`, despite `dns_verification.py`'s docstring promising "Zero
real network I/O in CI" via patching `_resolve_txt`. The dedicated DNS-check
tests DO patch correctly (`test_sender_domain.py`), so the unpatched calls come
from an adjacent test (likely the admin `verify_domain` action test or a task
test that triggers a real check). No security impact — the ERROR path is safe —
but it makes CI slow and flaky and violates the module's own contract.
- **Fix (test agent):** patch `_resolve_txt` (or set an autouse fixture) in
  every test that can reach `run_verification_check`, so no test touches the
  network.

### R-1 (accepted risk, documented) — post-verification spoofing window

A domain lost after VERIFIED (expired registration / DNS change) stays trusted
until the weekly re-check demotes it — up to ~7 days of spoof-capable From. This
is explicitly flagged PENDING (cadence) in ADR-034 and out of the 2026-07-11
decision. **Accept for v1, but the release note / runbook must state the window
and the cadence should be a documented, tunable setting** so ops can shorten it
(e.g. daily) if a domain is known-sensitive.

---

## 3. Invite flow (ADR-033 D4b) — tokens, takeover, enumeration

- **Set-password token** (`stores/tokens.py`) reuses Django's
  `PasswordResetTokenGenerator` — its hash includes the user's password hash and
  `last_login`, so the token self-invalidates the instant a password is set
  (single-use without a separate table) or the password otherwise changes.
  Expiry via `PASSWORD_RESET_TIMEOUT`. **Replay after acceptance → invalid page.
  Sound.**
- **No overwrite of an existing user's password.** `resolve_invitee` *links*
  existing users (no token, no set-password link); only *new* users (created with
  `set_unusable_password()`) receive the set-password link
  (`send_store_invite_email`, `is_new_user=True`). An existing user with real
  credentials is never sent a link, so an invite **cannot overwrite an existing
  account's password.** Inviting an email the attacker owns to create a fresh
  account is by-design. **Closed.**
- **`accept_invite_view` null-safety & stamp-once.** If `User` exists but the
  `StoreEmployee` row does not, both are reset to None → `valid=False` → 400
  page; no `NoneType` deref on `employee.accepted_at`. `accepted_at` is stamped
  once and never re-stamped (chat/admin.py `subprocessor_terms_accepted_at`
  precedent). **Sound.**
- **Cross-store token reuse.** The token binds only to the `User` (uidb64), not
  to store/employee; the URL carries `employee_pk`, and the view fetches
  `StoreEmployee.get(pk=employee_pk, user=user)`. Reuse would require another
  valid employee_pk for the *same* user, and the token dies once the password is
  set. Because set-password acts on the one shared `User`, this only ever affects
  the invitee's own account. **No cross-tenant exposure.**
- **Enumeration.** Invalid/expired tokens render a generic "no longer valid"
  page (HTTP 400); no user-existence oracle beyond what a global password-reset
  already exposes. Acceptable.
- **Note N-1 (LOW):** `resolve_invitee` flips `is_store_admin=True` on a
  pre-existing `User` who wasn't a store admin, and silently links them to the
  store. The user is notified (the "you now have access" email fires), and the
  `has_permission` whitelist confines them to stores where they hold an active
  row, so blast radius is the single inviting store. By-design per D4b, but worth
  a one-line release note ("inviting an existing account grants it store-admin
  entry to *your* store and emails them").
- **TOCTOU residual (duplicate invite), honest severity: LOW.**
  `find_conflicting_employee` runs in `clean()`; the `(user, store)` unique
  constraint is the real backstop. A precisely-timed concurrent duplicate invite
  could still slip past the pre-check and raise `IntegrityError` (HTTP 500) at
  `save()`. No duplicate row is created (constraint holds), no security impact —
  just an ugly 500 in a rare race. Acceptable to ship; optionally wrap
  `save_model` to convert a late `IntegrityError` into the same friendly form
  error.

---

## 4. TICKET-044 — TimescaleDB detection (small surface)

- **SQL is parameterized.** `_detect_extension` / `_detect_hypertable` /
  `purge_events` all use bound parameters (`%s` / `?`), never string
  interpolation of user data. The only f-string in `purge_events._purge_via_delete`
  interpolates the **placeholder token** (`%s` vs `?` chosen by `conn.vendor`),
  not a value — the value (`cutoff`) is passed as a bound param. No SQL injection.
  Table/function names (`analytics_event`, `create_hypertable`, `drop_chunks`)
  are static literals, not request-derived. **Sound.**
- **Fail-safe.** Every catalog query resolves any exception to `False` (the
  plain-Postgres/DELETE path) and logs loudly — never a silent `except: pass`,
  never "error means yes". The migration uses `use_cache=False` so it never
  trusts a stale cache. `create_hypertable` runs only when the extension is
  actually detected; otherwise a logged no-op. **No injection, no privilege
  surprise, no destructive default.** Low-risk, approved as-is.
- **Note N-2 (INFO):** the per-process detection cache is module-global keyed by
  connection alias. That is fine for a catalog fact, but a test or a long-lived
  process that installs the extension mid-life would see a stale `False` until
  `clear_cache()`. Documented in the module docstring; no action needed.

---

## Conditions for release

1. **F-1 (Medium) — decide before prod on Postgres.** Either fix the
   demote/deactivate lock (lock the qualifying set *including* self; keep the
   exclusion only in the count), or the human formally accepts the recoverable
   ≥1→0 race with the super-admin recovery path as the documented mitigation.
   The delete path is already correct.
2. **F-2 (Low) — reclassify or strengthen the concurrency test.** It is
   sequential and does not prove the lock; it must not be cited as the ADR-033
   "Tests required #5" race proof. Pair with the F-1 fix (After-Bug regression on
   a row-locking backend).
3. **F-3 (Low) — add a `sender_local_part` validator** (mailbox charset) and CRLF
   stripping. Defense in depth; Django currently blocks the injection downstream.
4. **F-4 (Low) — patch DNS in every test that can reach `run_verification_check`**
   so CI does no real network I/O (matches the module's stated contract).
5. **R-1 / N-1 — release note & runbook:** state the post-verification spoofing
   window and make the re-check cadence a documented tunable; note that inviting
   an existing account grants it store-admin entry and emails them.

No blocker was found. The permission enforcement architecture (single resolver,
registration-time structural wrap, fail-closed drift test) is well-built and the
escalation/IDOR/invite-token/DKIM-secret/force-verify surfaces are all closed.
The one substantive issue is the F-1 lock scope; everything else is low-severity
hardening or documentation.

**VERDICT: APPROVED WITH CONDITIONS** (F-1 fix or explicit human acceptance;
F-2/F-3/F-4 hardening; R-1/N-1 release-note items).
