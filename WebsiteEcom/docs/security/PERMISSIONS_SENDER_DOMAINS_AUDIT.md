# Security Audit — Employee Permission Matrix & Per-Store Sender Domains

Scope: joint mandatory safety gate for TICKET-054/047 (ADR-033, permission matrix — the
authz system) and TICKET-040 (ADR-034, per-store sender domains — email spoofing surface).
Reviewed on branch `main`, working tree as-is. No production code changed by this audit.

Evidence run: `DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py test stores emails`
→ **314 tests, OK** (24.1s, 0 failures, 0 errors).

Files reviewed: `stores/modules.py`, `stores/permissions.py`, `stores/models.py`,
`stores/admin.py`, `stores/invite.py`, `stores/tokens.py`, `stores/views.py`,
`webecom/admin.py`, `core/admin.py` (ReadOnlyProfileAdmin), `stores/migrations/0013…`,
the drift test, resolver/http/invite tests, `emails/models.py`,
`emails/dns_verification.py`, `emails/sender_domain_service.py`, `emails/sender.py`,
`emails/dkim_keys.py`, `emails/tasks.py`, `emails/admin.py`, `payments/fields.py`
(EncryptedCharField).

---

## VERDICT

| Area | Verdict |
|------|---------|
| Permission matrix (ADR-033) | **CLEAR-WITH-NOTES** — one HIGH item requires a human product/security decision before GA (F1: invite grant-ceiling). |
| Sender domains (ADR-034) | **CLEAR-WITH-NOTES** — no blockers; two LOW hardening notes. |
| **Overall** | **CLEAR-WITH-NOTES** — releasable once F1 is explicitly accepted (or fixed) by a human. Everything else is fail-closed and well-tested. |

---

## PERMISSION MATRIX

### F1 (HIGH) — Privilege escalation by proxy through the invite/grant flow: no grant-ceiling

**This is the headline finding.** A store admin holding `employees = full` can create (invite)
a new `StoreEmployee` and assign it **any** module level, including `full_access = True` and
modules the inviter does not themselves hold.

Path (`stores/admin.py`):
- `PermissionGridFormMixin.__init__` exposes every module as a free `ChoiceField`, and
  `full_access` is a plain `Meta.fields` boolean on both `StoreEmployeeStoreAdminForm` and
  `StoreEmployeeSuperAdminForm`.
- `StoreEmployeeStoreAdminAdmin.save_model` resolves/creates the invitee and persists the
  grid verbatim. There is **no** check that the granted levels are a subset of the acting
  user's own levels, and no cap on `full_access`.
- `StoreEmployee._validate_no_self_escalation` (models.py) only guards the actor's **own**
  row (`self.user_id == acting_user.pk`). Creation is explicitly a no-op (`self.pk is None`).

Consequence: the D5a "no self-escalation" guardrail — a headline feature of ADR-033 — is
fully sidesteppable. An admin with `employees=full` but, say, `orders=none` can mint a new
`full_access=True` employee (a puppet account they control, or a colleague), then act with
full rights. Least privilege collapses for anyone holding `employees=full`; that grant is
effectively "keys to the kingdom."

Verified against spec: ADR-033 D5 defines only (a) no self-edit and (b) last-full-access
lockout. It does **not** state a grant-ceiling, and `employees` ships as an ordinary
full/none module. So the code matches the approved ADR — this is a **design gap**, not a
code defect against spec. But it is a genuine authz escalation vector and industry norm
(e.g. Shopify staff permissions) is "you can only grant permissions you hold."

Recommendation (human decision required before GA):
- **Option A (recommended):** enforce a grant-ceiling — in the store-site form/`save_model`,
  reject granting any module level above the acting user's own level, and reject granting
  `full_access` unless the actor has `full_access`. Add it as a fourth `StoreEmployee`
  validator (`_validate_grant_ceiling`) threaded off `_acting_user`, mirroring
  `_validate_no_self_escalation`, so shell/migration paths stay exempt.
- **Option B (accept as-is):** formally document that `employees=full` is a
  team-administration super-grant equivalent to `full_access` for escalation purposes, and
  surface that in the grid UI help text so it is never handed out casually.

Either way this must be an explicit, recorded human decision — not left implicit.

### F2 (PASS) — Enforcement completeness / fail-closed / drift test

- Auto-wrap is real and structural: `StoreAdminSite.register()` (webecom/admin.py) mixes
  `StoreModulePermissionMixin` into **every** store-site ModelAdmin via `_mix_in`, ahead of
  the concrete class in the MRO. No opt-in, so a future admin cannot forget it.
- Only the two custom sites are mounted (`webecom/urls.py`: `admin/` → store_admin_site,
  `superadmin/` → super_admin_site). Django's default `admin.site` is not routed, so no
  ungated ModelAdmin is reachable.
- Fail-closed confirmed: `check_module_access` denies for a `None` module_key unless the
  actor is super-admin or `full_access` (resolver steps 1–6). Test
  `test_module_key_none_always_denies_non_bypass_paths` proves it.
- Drift test (`test_permission_matrix_drift.py`) is real and exhaustive: asserts every
  registered store-site admin declares a valid `module_key`/tuple/`MODULE_EXEMPT`, asserts
  every non-reserved/non-view-only key is claimed, and **proves it fails** on an unmapped
  dummy (`test_unmapped_admin_fails_the_check`). The lone `MODULE_EXEMPT` (core.admin
  `ReadOnlyProfileAdmin`) is justified at the declaration site and is self-scoped +
  add/delete-hardcoded-False. `has_module_permission` overrides (StoreDomain/EmailTemplate/
  SenderDomain "no resolved store") correctly `AND` the matrix grant on top — they no longer
  grant visibility unconditionally.
- Super site is intentionally never matrix-gated (D3b/D7); gated instead by
  `SuperAdminSite.has_permission` = `is_active AND is_super_admin`. Correct.

### F3 (PASS) — Lockout guard, race-tested and cross-path

- Demote/deactivate blocked in `_validate_lockout_guard` under `select_for_update()`, held
  inside `save()`'s `transaction.atomic()` so the row lock survives to the write.
- Delete blocked in `delete_model`/`delete_queryset`, each taking its own
  `select_for_update()` on the store's rows before `would_be_last_active_full_access()`.
- Cross-path race (demote row X in one txn while deleting row Y in another, X/Y being the
  last two): the demote path locks qualifying rows excluding self; the delete path locks all
  store rows — the sets overlap, so the two transactions serialize on a locking backend;
  whichever commits second re-reads zero remaining and is rejected. Correct.
- Documented SQLite no-op in tests is honestly stated; production backend supports row
  locking. `_is_candidate_lockout_transition` keeps normal traffic off the lock.
- Super site deliberately allows the transition with `messages.warning`
  (`_allow_lockout_transition`) — recovery-path design, acceptable.

### F4 (PASS) — Invite tokens

- Signing/expiry/single-use reuse Django's `PasswordResetTokenGenerator` (tokens.py): the
  token hash includes the user's password hash + `last_login`, so it self-invalidates the
  instant a real password is set (single-use), and honours `PASSWORD_RESET_TIMEOUT`.
- `accept_invite_view` binds the employee row to the user (`get(pk=employee_pk, user=user)`)
  and validates the token against that user — **no IDOR**: you cannot set a password for an
  arbitrary user (uidb64 = user pk; token bound to that user; employee_pk must belong to the
  user). Tests: `test_wrong_token_rejected`, `test_employee_pk_not_belonging_to_user_rejected`,
  `test_reused_token_is_rejected_after_password_set`, `test_malformed_uidb64_rejected`.
- `accepted_at` stamp-once confirmed (view + `send_store_invite_email` both guard
  `if … is None`); `test_accepted_at_never_re_stamped` proves it.
- Set-password link only issued to genuinely new users (unusable password). Existing users
  are linked without a token and stamped immediately — no password-reset surface for an
  account that can already log in.
- Cross-store: the accept view is unauthenticated and only sets the user's password + stamps
  one specific employee row; it grants no store access by itself (access is the pre-created
  StoreEmployee row, always pinned to `request.store` server-side in `save_model`). A store-A
  admin cannot create an employee for store B (store/user excluded from the form). No
  cross-store escalation.
- CSRF token present in `accept_invite.html`.

### F5 (PASS) — Resolver integrity

- Request cache lives on `request.__dict__[_CACHE_ATTR]` — a per-request object; no cross-
  request sharing, no poisoning surface (nothing external writes it).
- The MagicMock fix is real, not test-only: `get_store_employee` checks
  `_CACHE_ATTR in request.__dict__` rather than `hasattr`/`getattr`, so an auto-vivifying
  MagicMock does not masquerade as a populated cache. Correct for real `HttpRequest` too.
  Tests `test_second_call_reuses_cached_value_magicmock_request` /`…_real_request` /
  `test_none_result_is_also_cached` cover it.
- Anonymous handled at step 1 via `is_active` (AnonymousUser.is_active is False → deny);
  `test_inactive_user_denied` / `test_none_user_denied` confirm.

### F6 (PASS-WITH-NOTE) — collections→products migration

- Reversible (`reverse_merge` re-adds `collections` mirroring `products`; documented as
  best-effort). Unknown-key strip is a documented one-way cleanup.
- **Escalation note:** "most-permissive-wins" means an employee with `collections=full` and
  `products=none` ends up `products=full` — a strict level escalation on the `products`
  module. This is the deliberate, documented D1b tradeoff (losing granted access is worse
  than a slightly broader merged module, since the two UIs are now one screen). Acceptable,
  but before running the migration in production, confirm no store used collections-vs-
  products as a separation-of-duty boundary (they were UI-merged, so this is very unlikely).
  Covered by `test_collections_merge_migration.py`.

### F7 (LOW) — `verify_domain` admin action runs on view-level grant

`SenderDomainAdminBase.verify_domain` (a custom admin action, not the delete action) calls
`mark_pending()` + enqueues a DNS check with no explicit permission re-check. A store admin
with `settings=limited` (view threshold) can reach the changelist and trigger it. Impact is
benign — it only (re)verifies the store's **own** domain and cannot forge verification — but
it is a mutation on a view-level grant. Consider gating the action behind
`check_module_access(request, "settings", "full")`. LOW.

---

## SENDER DOMAINS

### F8 (PASS) — Verification is cryptographically bound; no cross-store hijack

- `run_verification_check` marks VERIFIED only when **both** SPF **and** DKIM match
  (`spf_result == MATCH and dkim_result == MATCH`). DKIM match (`check_dkim`) requires the
  published TXT record to contain **this row's own** `dkim_public_key` (a per-SenderDomain
  2048-bit RSA public key). Publishing that value requires DNS control of the domain, so
  verification proves control and is bound to this row's keypair — store A cannot verify a
  domain by riding store B's records.
- Global uniqueness (`domain unique=True`, normalized in `save()` like StoreDomain) prevents
  two stores holding the same domain; `test_second_store_claiming_same_domain_is_rejected`
  and `test_domain_normalized_before_uniqueness_check` confirm. Transfer race is bounded by
  the weekly `recheck_verified_sender_domains` demotion when records disappear
  (`test_verified_domain_demoted_when_records_disappear`).
- Transient DNS ERROR never flips status (`test_timeout_never_changes_status`) — a resolver
  outage cannot silently un-verify or falsely verify.
- `force_verify` manual override: super-admin-only and audited.
  `SenderDomainSuperAdmin.force_verify_manual` re-checks `request.user.is_super_admin`
  (defense in depth atop the super-site gate) and writes a `SenderDomainManualVerification`
  row every time; the audit model is read-only in admin. Tests
  `test_denied_to_non_super_admin`, `test_allowed_for_super_admin_and_writes_audit_row`,
  `test_audit_log_is_read_only_in_admin`. A store admin cannot reach it. **Note:** the model
  method `SenderDomain.force_verify()` is itself unguarded by design — safe only because the
  admin action is its sole caller; any future caller MUST replicate the super-admin check +
  audit write.

### F9 (PASS) — `resolve_from_email` fallback matrix

Traced every branch of `emails/sender.py`:
- Kill switch `SENDER_DOMAINS_ENABLED=False` → platform default (instant revert).
- No SenderDomain row → default. UNVERIFIED/PENDING/FAILED → default (query filters
  `status=VERIFIED`). Only a VERIFIED row for **this exact store** yields the custom From.
- Cross-store isolation enforced by the `store=store` filter;
  `test_verified_domain_for_a_different_store_does_not_leak` confirms an unverified/other-
  store domain is never used as From. All five fallback tests pass.

### F10 (PASS) — DKIM private key at rest

- `dkim_private_key` is `EncryptedCharField` (Fernet, `payments/fields.py`) — encrypted at
  rest, `FERNET_KEY` separate from `SECRET_KEY`, custom `__repr__` keeps plaintext out of
  logs/shell repr. Test `test_private_key_encrypted_at_rest`.
- Never rendered in admin: absent from every `fields`/`fieldsets`/`readonly_fields` on both
  sites; `test_private_key_never_in_store_admin_*` / `…_super_admin_fieldsets` assert it.
  `dkim_public_key` is intentionally public (DNS-published). Keypair generated once on
  create, not regenerated on update (`test_keypair_not_regenerated_on_update`).

### F11 (LOW) — DNS lookup: SSRF-adjacent surface is minimal but domain input is unsanitized

- The domain string reaches `dns.resolver.Resolver().resolve(hostname, "TXT")`
  (`dns_verification.py`). This is a **DNS TXT query**, not an HTTP/socket connection to the
  domain — the resolver contacts configured DNS servers, not the target host — so classic
  SSRF (reaching internal HTTP services) does not apply. A crafted value like an internal
  name only produces a TXT lookup with no connection to it.
- Timeout-bounded: `resolver.timeout` and `resolver.lifetime` both set from
  `SENDER_DOMAIN_DNS_TIMEOUT_SECONDS` (default 5s), so a black-holed resolver cannot hang a
  Celery worker (`test_check_spf_timeout_is_error`). Beat tasks isolate per-domain failures.
- LOW note: `domain` is normalized (scheme/port stripped, lowercased, `unique`) but not
  validated as a public FQDN. No security impact today given it is only a TXT query, but if
  the resolver logic is ever reused for a connection-making check, add FQDN validation /
  internal-name rejection first.

### F12 (OUT-OF-SCOPE OBSERVATION) — EmailTemplate body rendered as a Django template

Not in this audit's two named areas, but adjacent and worth flagging to the orchestrator:
`EmailTemplate.subject`/`body_html` are documented as rendered as Django template strings and
are editable by a store admin with `settings=full`. Server-side template rendering of admin-
supplied strings is a known SSTI-adjacent surface. Confirm the render path uses a restricted/
sandboxed context (no `{% load %}` of dangerous tags, no object traversal to secrets). Route
to a dedicated review of TICKET-022 if not already covered.

---

## HUMAN ACTION CHECKLIST (before GA)

1. **[REQUIRED] Decide F1:** enforce an invite grant-ceiling (recommended) or formally accept
   `employees=full` as a super-grant and document it in the grid UI. Record the decision.
2. **[Recommended] F7:** gate the `verify_domain` action behind `settings/full`.
3. **[Recommended] F11:** if the DNS resolver is ever extended to make connections, add FQDN /
   internal-name validation.
4. **[Verify] F6:** confirm no production store treated collections-vs-products as a
   separation-of-duty boundary before running migration 0013.
5. **[Route] F12:** ensure EmailTemplate server-side rendering is sandboxed (separate review).

## Regression coverage note

If F1 is fixed, a proven regression test is required (After-Bug Test Agent): a limited admin
(`employees=full`, `orders=none`, `full_access=False`) attempting to invite a `full_access=True`
or `orders=full` employee must be rejected — test must fail without the fix and pass with it.
