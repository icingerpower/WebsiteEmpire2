# Security Audit — Consent Management (TICKET-048/049, ADR-025)

**Date:** 2026-07-11 · **Re-verification of fixes:** 2026-07-11 (see "Re-verification" section and Verdict v2 at the bottom)
**Auditor:** Safety Agent
**Scope:** `consent/` app (models, views, state, slot provider, admin, purge task/command),
`pixels/slot_provider.py`, `pixels/service.py::claim_purchase_pixels`,
`storefront/views_checkout.py::order_thank_you`,
`storefront/templatetags/storefront_tags.py` (beacon gating),
`storefront/templates/storefront/partials/consent_banner.html` + `consent_manage_button.html`,
`webecom/urls.py`, `stores/middleware.py`, `webecom/settings/production.py`.

**Evidence:** `DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py test consent`
→ **83 tests, OK**. Additionally ran `storefront.tests.test_consent_beacon`,
`storefront.tests.test_consent_banner`, `pixels.tests.test_consent_mode`,
`pixels.tests.test_slot_provider`, `pixels.tests.test_service` → **60 tests, OK**.

---

## 1. `POST /_consent/` endpoint (`consent/views.py`, `consent/urls.py`, `webecom/urls.py:40`)

| Check | Result |
|---|---|
| CSRF enforced | **PASS.** No `csrf_exempt`; global `CsrfViewMiddleware` applies. Proven by `test_post_without_csrf_token_is_rejected` (uses `enforce_csrf_checks=True`, asserts 403 **and** zero `ConsentRecord` rows). |
| CSRF cookie readable by banner JS | **PASS with condition (F1).** `CSRF_COOKIE_HTTPONLY` is not set anywhere (Django default `False`), so `document.cookie` can read `csrftoken`. **However** the `csrftoken` cookie is only *set* when some template on the page calls `{% csrf_token %}`/`get_token()` — see finding F1. |
| Input validation | **PASS.** `action`/`source` validated against closed enum sets (`ConsentAction`/`ConsentSource`); `analytics`/`marketing`/`am_objected` must be genuine JSON booleans (`isinstance(..., bool)` — rejects `0/1/"true"`); body must be a JSON dict; body capped at 4096 bytes; `accept_all`/`refuse_all` **force** the category values server-side (defense in depth against inconsistent payloads). |
| Rate limiting | **PASS (with deployment dependency F3).** `pages.antispam.rate_limit_exceeded(request, "consent_post")` — 5/hour keyed on (scope, store, REMOTE_ADDR); 429 on excess, proven by `test_rate_limit_exceeded_returns_429`. Runs *after* CSRF middleware, so unauthenticated garbage never reaches the DB. |
| Response splitting / header injection | **PASS.** The only header derived from request data is `Set-Cookie`, whose value is built exclusively from a server-generated `uuid4().hex` and `int(bool)` values (`serialize_cookie_value`). `lang` (≤8 chars) and `user_agent` (≤256 chars) go to the DB only, never into headers or the body. Responses are 204/plain constant strings; `test_no_user_supplied_string_ever_echoed_in_response_body` pins this. |
| Open redirect | **PASS.** No `next`/redirect parameter exists; the view returns 204/4xx/5xx only. The client-side `location.reload()` takes no attacker-controlled target. |
| Store resolution | **PASS.** `request.store is None` → 400; unknown Host never reaches the view (404 in middleware, `test_unresolved_store_never_reaches_the_view`). `/_consent/` is in `SF_LANG_WRITE_EXEMPT_PREFIXES` (stores/middleware.py:142) so it cannot clobber `sf_lang` — locale still resolves for `ConsentRecord.lang`. |
| URL shadowing | **PASS.** Mounted before the storefront catch-all; slug normalization strips leading underscores so a permalink `_consent` cannot exist (webecom/urls.py:34-40). |

## 2. Cookie integrity (`pradize_consent`)

- **Attributes:** `HttpOnly=True`, `SameSite=Lax`, `Path=/`, `Max-Age=180d`,
  `Secure=settings.SESSION_COOKIE_SECURE` (`True` in production — `webecom/settings/production.py:103`;
  asserted both ways by `test_cookie_is_secure_when_session_cookie_secure_is_true` /
  `..._not_secure_by_default_in_test_settings`). No JS ever reads it — HttpOnly is real, not decorative.
- **Unsigned — assessed, acceptable:** a shopper forging their own cookie can only grant/refuse
  *for their own browser*. The server never trusts the cookie for identity, storage, or
  cross-user state: `ConsentRecord` rows are only created via the POST (fresh server-side UUID),
  and the cookie's `consent_id` is never looked up to authorize anything. Forging
  `a=1,m=1` is exactly equivalent to clicking "Accept all" — self-harm only. No cross-user,
  no cross-store, no server-trust consequence. Matches ADR-025 D2's documented reasoning.
- **Fail-closed parser fuzz (`consent/state.py`):** fully anchored regex with bounded
  quantifiers (no ReDoS); wrong version → UNDECIDED (`int()` cannot throw — regex limits to 1-4
  digits); truncated / wrong separators / out-of-range flags / 100 000-char garbage → UNDECIDED
  without raising (`test_oversized_garbage_value_is_undecided_without_raising` et al.).
  `allows()` on an unknown category returns `False` and logs loudly — a typo'd category in a
  caller can never grant a tracker. **Genuinely fail-closed.**
- **Multi-tenant scoping:** the cookie is set host-only (no `Domain` attribute) and
  `StoreDomain.host` is globally unique (stores/models.py:617), so with one custom domain per
  store there is no cross-store leakage; `test_get_consent_never_mixes_stores_via_shared_test_client_cookies`
  pins the server side. **Residual hardening note F2** for sibling-subdomain deployments.

## 3. `ConsentRecord` proof integrity

- Written server-side, one INSERT per decision, `store=request.store`, before the cookie
  (insert-before-cookie proven: simulated DB failure → 500 + **no** `Set-Cookie`,
  `test_consent_record_write_failure_returns_500_with_no_cookie`).
- **No PII:** no IP field (schema-pinned invariant test `test_no_ip_field_exists_on_consent_record`,
  mirroring the analytics `Event` no-IP test); `user_agent` truncated to 256 at write time;
  `lang` truncated to 8.
- **Tenant isolation:** `for_store` scoping in the view and both admin querysets
  (`ConsentRecord.objects.none()` when no store on the request); store A's rows invisible to
  store B (`test_store_a_record_invisible_from_store_b_queryset`).
- **Admin exposure:** read-only audit table — `has_add/change/delete_permission` all `False`.
- **13-month purge** (`consent/services.py`): `created_at < now - 395d` → `.delete()` — rows are
  actually deleted, shared by the Celery beat task and the management command.
  `cross_store_unsafe()` use is deliberate and documented (platform-wide retention sweep).
- **Flooding:** bounded to 5 rows/hour/IP/store by the rate limit; a distributed attacker can
  still insert at that rate per IP (same accepted posture as every other public form —
  `pages/antispam.py` module docstring: "best-effort spam control, not a security boundary").
  Rows are tiny, indexed on (store, created_at), and purged at 13 months. Accepted.

## 4. Gating correctness as a security property (fail-closed invariant)

- **Error paths traced:** `get_consent` cannot raise (no cookie / regex miss / version mismatch
  all return `UNDECIDED_STATE`); `UNDECIDED_STATE.allows()` → `False` for both consent
  categories. All three cross-app call sites carry independent ImportError shims that never
  import from `consent` in the except branch:
  - `pixels/slot_provider.py::_resolve_consent` → necessary-only stand-in (pixels dark);
  - `storefront/views_checkout.py::_resolve_consent_allowed_categories` → `frozenset()` (zero claims);
  - `storefront_tags.py::_resolve_consent` / `_beacon_requires_consent` → exempt-default beacon.
  All pinned by `consent/tests/test_fail_closed_shim.py`. If `ConsentSlotProvider.render()`
  itself fails, `PixelsSlotProvider` resolves consent independently — a broken banner never
  opens the pixel gate.
- **Claim burning via crafted cookie:** a cookie claiming marketing consent causes
  `claim_purchase_pixels` to consume that shopper's own order's claims and render the pixels in
  that shopper's own browser — the identical outcome of legitimately clicking "Accept all".
  Self-harm only, confirmed. Burning *someone else's* claims requires their signed thank-you
  token — a pre-existing capability boundary (AC-110 leaked-link posture, ADR-022 D6) that this
  feature does not widen; pre-025, claims were burned on first render regardless of consent.
- **PAID gate retained:** `claim_purchase_pixels` re-checks `payment_status != PAID` → empty set
  before any claim, independent of consent (pixels/service.py:57).

## 5. Banner inline JS (`consent_banner.html`)

- **Interpolated server values:** `{% url 'consent:consent_post' %}` (static reverse),
  `{{ policy_url }}` (server-resolved, store-scoped active `Permalink` slug — cross-store or
  missing permalink yields `None` and the link is omitted; autoescaped inside `blocktrans`),
  and `analytics_pre/marketing_pre/am_allowed_pre` (server-built `"1"/"0"` literals). No store
  name, no shopper input, nothing attacker-controlled reaches the script or markup.
  Translation strings come from the platform-owned `.po` catalog (platform-code trust boundary,
  not tenant input).
- **Reload-on-grant loop:** `location.reload()` fires only on HTTP 204 *and*
  `newlyGranted` — post-reload the cookie is decided, `show_banner` is False, the accept button
  no longer exists, so no automatic loop is possible. With cookies disabled client-side, each
  reload requires a fresh human click — degraded UX, no loop, still fail-closed.
- **CSRF token handling:** cookie read + `X-CSRFToken` header, same pattern as the chat widget.
  See F1 for the cookie-presence gap.

## 6. Beacon exemption invariants (ADR-025 D4)

The five named invariants are all asserted in tests:
1. No IP/durable identifier on `Event` — `analytics/tests/test_no_ip_invariant.py`.
2. Exempt-by-default keeps running while undecided/refused —
   `test_undecided_no_cookie_still_renders_the_beacon`, `test_refused_all_categories_still_renders_the_beacon`.
3. **Objection genuinely stops the render** — server-side no-op at
   `storefront_tags.py:235-236` (`consent.am_objected` → empty string), proven by
   `test_am_objected_true_suppresses_the_beacon`.
4. `beacon_requires_consent` override demotes to the analytics category — three tests including
   the important `test_override_enabled_marketing_only_still_suppresses_the_beacon`.
5. Panel discloses the exempt row with its own toggle (template rows present; refuse-all
   preserves `am` state per D4 — asserted structurally, no JS harness exists, KNOWN_RISKS 13).

## 7. Launch check (`ConsentSlotProvider.validate_settings`)

- Active `Pixel` + `is_enabled=False` + `non_eu_acknowledged=False` ⇒ blocking error — present
  and tested (`consent/tests/test_slot_provider.py`).
- **Bypass boundary, stated explicitly:** the only UI path to `is_enabled=False` is the store
  admin form, which requires the acknowledgment checkbox and permanently stamps
  `non_eu_acknowledged_at/by` (never cleared, `save_model`). A direct ORM/SQL write of
  `non_eu_acknowledged=True` bypasses the form — that requires operator/DB access or a
  code path outside this feature, which is inside the platform trust boundary, not a shopper- or
  store-employee-reachable surface. Acceptable; the audit stamp fields make a forged
  acknowledgment without `_at/_by` visibly anomalous.

---

## Findings

### F1 — MEDIUM (functional availability of consent capture; fail-closed direction)
**STATUS: FIXED — verified 2026-07-11** (see Re-verification section). Original finding kept below for the record.

The banner JS depends on the `csrftoken` cookie existing, but **no storefront view or base
template guarantees it is set**. There is no `ensure_csrf_cookie`/`get_token()` anywhere in
production code; the cookie is only set on pages where some partial renders `{% csrf_token %}`
(currency picker — only when the store has 2+ currencies; product/contact/checkout forms).
On a single-currency store, a fresh visitor landing on e.g. the home page may have no
`csrftoken` cookie → `POST /_consent/` → 403 → the error message shows and **neither grants nor
refusals can be persisted from that page** (banner re-appears everywhere; pixels stay dark;
refusal nag). This is invisible to the current test suite (view tests don't enforce CSRF on the
happy path, and there is no JS harness).
**Impact:** compliance-adjacent availability defect, not a vulnerability — the failure direction
is fail-closed (nothing tracks). **Fix suggestion (for the Developer, not applied here):** call
`django.middleware.csrf.get_token(request)` inside `ConsentSlotProvider.render()` (or decorate
storefront page views with `@ensure_csrf_cookie`) so the cookie is guaranteed exactly where the
banner renders. Add a regression test that renders a form-free page with a fresh client and then
POSTs with `enforce_csrf_checks=True` using only what that response provided.

### F2 — LOW (hardening; conditional on deployment topology)
**STATUS: RECORDED — verified 2026-07-11.** ADR-025 now carries a dated addendum ("binding provisioning constraint for any future shared-subdomain deployment", `docs/adr/ADR-025-consent-management.md:162-173`) mandating the `__Host-pradize_consent` rename before any shared-subdomain provisioning ships. Accepted as the agreed disposition.

The cookie is unsigned, not store-bound, and not `__Host-`-prefixed. With one custom domain per
store (current model — `StoreDomain.host` globally unique) this is fine. **If** stores are ever
provisioned as sibling subdomains of a shared registrable domain (e.g. `store1.pradize.com` /
`store2.pradize.com`), any script running on one subdomain can plant a
`Domain=pradize.com; pradize_consent=v1:...:a=1,m=1,am=0` cookie that the browser sends to the
sibling stores, where `get_consent` will parse it as valid consent → pixels fire for shoppers
who never consented (cookie tossing; compliance impact, no data exposure).
**Hardening suggestion:** rename to `__Host-pradize_consent` in production (its current
attributes — `Secure`, `Path=/`, no `Domain` — already satisfy the prefix rules), which makes
parent-domain injection impossible by browser enforcement. Record the constraint either way in
any future shared-subdomain deployment decision.

### F3 — LOW (deployment dependency, already documented in `pages/antispam.py`)
**STATUS: RECORDED — verified 2026-07-11.** Appended to the deploy section of `docs/releases/pixels-currency-chat/RELEASE_CHECKLIST.md` (item 7, dated 2026-07-11, citing this finding): verify the edge proxy restores the real client IP into `REMOTE_ADDR` before `/_consent/` traffic. Accepted as the agreed disposition.

Rate limiting keys on `REMOTE_ADDR` only. Behind a reverse proxy that does not restore the real
client IP, *all* shoppers of a store share one 5/hour bucket → after five decisions per hour
store-wide, every shopper gets 429, the banner cannot be dismissed, and pixels stay dark
(platform-wide fail-closed DoS of consent capture). Conversely, if the proxy passes
client-controlled headers without stripping, the limit is not weakened (X-Forwarded-For is
deliberately never parsed). **Action:** the release checklist must include verifying real-IP
restoration at the edge (same item the antispam module already demands).

### F4 — LOW (proof-trail semantics)
**STATUS: FIXED — verified 2026-07-11** (see Re-verification section). Original finding kept below for the record.

`accept_all`/`refuse_all` force the category booleans server-side, but `withdraw` (and `custom`)
accept whatever the client sends — `action="withdraw", analytics=true` writes a proof row that
records a *grant* labeled "withdraw". No privilege consequence (the client can grant itself
anyway), but it muddies the demonstrability chain the record exists for. **Suggestion:** force
`analytics=marketing=False` on `withdraw`, or reject inconsistent combinations with 400.

### F5 — INFO (launch-checklist severity semantics)
`validate_settings` returns the non-blocking "no policy page" **warning in the same list** as
the blocking error, `"(warning) "`-prefixed (documented ASSUMPTION — no severity mechanism
exists platform-wide). If the launch-checklist consumer treats every entry as blocking, a store
with consent enabled but no policy page cannot launch — *stricter* than ADR-025 D6 intends, so
not a safety risk, but confirm the consumer's behavior matches expectations before release.

### F6 — INFO
`purge_consent_records()` does `count()` then `delete()` — the logged count can drift from the
actual deletion under concurrent inserts (cosmetic; `delete()` itself returns the real count).

---

## Human verification checklist (live environment)

1. **F1:** On a deployed single-currency store, open the home page in a fresh private-browsing
   session, click "Accept all", and confirm the network tab shows 204 (not 403) and the
   `pradize_consent` cookie is set. Repeat with "Refuse all".
2. **F3:** Confirm the edge proxy restores the real client IP into `REMOTE_ADDR` (existing
   antispam checklist item — now consent-critical).
3. Confirm in production that the cookie arrives with `Secure` and `HttpOnly` flags
   (browser devtools → Application → Cookies).
4. Legal sign-off on the D4 beacon-exemption reading remains OPEN (tracked in
   `specs/ecommerce_engine/11_uncertainties_to_validate.md`) — external to this audit.

---

## Verdict (v1, superseded — see Verdict v2 below)

**CLEAR-WITH-NOTES** (= APPROVED WITH CONDITIONS)

The implementation is defensively sound: CSRF enforced with proof, strict whitelist input
validation, no reflected output, fail-closed cookie parsing verified against fuzz inputs,
HttpOnly/Secure/SameSite correct, tenant isolation tested, no PII in proof rows, purge real,
claims gated at the claim site exactly as ADR-025 D0 requires, and every rollback shim fails
closed. All 143 relevant tests pass.

**Conditions before release:** verify/fix F1 (CSRF cookie presence on form-free pages — it can
silently make consent un-grantable on some stores, which defeats the feature's purpose even
though it fails in the safe direction), and carry F3 onto the release checklist. F2 must be
recorded as a binding constraint on any future shared-subdomain store provisioning.

---

## Re-verification — 2026-07-11

**Evidence:** `DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py test consent
storefront.tests.test_consent_banner storefront.tests.test_consent_beacon` → **111 tests, OK**
(includes the new F1 and F4 regression tests). Coordinator reports full suite 2754 OK.

### F1 — VERIFIED FIXED
`ConsentSlotProvider.render()` (`consent/slot_provider.py:134-135`) now calls
`django.middleware.csrf.get_token(request)` whenever `show_banner` is True, so
`CsrfViewMiddleware.process_response` sets the `csrftoken` cookie on every page where the
banner renders — including form-free pages. Proven by
`consent/tests/test_csrf_cookie_availability.py`, three full-middleware-stack tests
(`Client`, `enforce_csrf_checks=True`):
- undecided shopper's form-free home page response sets `csrftoken`;
- end-to-end 204-not-403 proof: GET a form-free page, then POST `/_consent/` using **only**
  what that response provided;
- negative case: a decided shopper's form-free page does not force the cookie (pins the
  `show_banner` scoping — but see F7 below, which adjudicates that scoping).

### F4 — VERIFIED FIXED
`consent/views.py:104-107`: `WITHDRAW` is now grouped with `REFUSE_ALL` — both force
`analytics, marketing = False, False` server-side regardless of the payload. A crafted
`action="withdraw", analytics=true` payload can no longer write a grant labeled "withdraw"
into the proof trail. Proven by `test_withdraw_action_forces_categories_false_server_side`
(`consent/tests/test_views.py:140`).

### F7 — LOW (withdrawal availability edge on form-free pages; fail-closed direction)
**STATUS: APPLIED — 2026-07-11.** `consent/views.py::consent_post` now calls
`django.middleware.csrf.get_token(request)` before returning the 204, so every
consent decision (not just banner renders, per F1) refreshes `csrftoken`'s
364-day clock alongside the consent cookie's. Settings-pinning regression test
added (`consent/tests/test_csrf_cookie_refresh.py`):
`settings.CSRF_COOKIE_AGE > CONSENT_COOKIE_MAX_AGE` and
`settings.CSRF_USE_SESSIONS is False`, plus a full-middleware-stack behavioral
test proving the 204 response itself carries a refreshed `csrftoken`
Set-Cookie. Evidence: `DJANGO_SETTINGS_MODULE=webecom.settings.test
python3 manage.py test consent` → **92 tests, OK**; full suite → **2757 tests,
OK**. Original finding kept below for the record.
Adjudication of the developer-flagged assumption: *"is `get_token()` scoped to
`show_banner=True` enough, given a DECIDED shopper reopening the manage panel on a form-free
page may lack the `csrftoken` cookie?"*

**Factual correction first:** the premise "csrftoken is session-scoped" is wrong for this
codebase. Neither `CSRF_COOKIE_AGE` nor `CSRF_USE_SESSIONS` is overridden anywhere in
`webecom/settings/`, so Django defaults apply: `csrftoken` is a **persistent 364-day cookie**
(31 449 600 s), not session-scoped. The common case therefore holds by construction:

- The **only** way to obtain a `pradize_consent` cookie is a successful `POST /_consent/`,
  which requires a valid `csrftoken` at that moment.
- Every banner render (any undecided page view) calls `get_token()`, which re-sends the
  cookie with a **fresh 364-day clock**. So at first-decision time: `csrftoken` expires at
  T+364d, consent at T+180d — the CSRF cookie strictly outlives the consent cookie by ~184
  days, and when it would expire the shopper has long since reverted to undecided (banner
  back → cookie refreshed). A returning decided shopper "months later" *does* have a valid
  `csrftoken` under normal cookie behavior.

**But the invariant breaks on repeated manage-panel re-saves — a real, constructible gap:**
the consent POST response refreshes the *consent* cookie's 180-day clock but does **not**
refresh `csrftoken` (nothing calls `get_token()` on that request, and a decided shopper's
pages never do on a form-free store). Concrete sequence: first visit day 0 (`csrftoken`
expires day 364); decide day 0 (consent expires day 180); re-save preferences via the manage
panel on day 179 (consent now expires day 359 — `csrftoken` still day 364); re-save again day
358 (consent now expires day 538) → **between day 364 and day 538 the shopper is decided but
has no `csrftoken`**, and their withdraw/update POST 403s with only the generic "We couldn't
save your choices" message. Retrying does not help. That is a CNIL "withdrawal as easy as
granting" failure, even though it is rare (requires a form-free store, a late-cycle re-save,
then an update attempt after day 364) and fail-closed (nothing tracks that shouldn't; the
shopper eventually re-prompts when consent expires).

**Recommended fix (surgical, preserves the show_banner scoping and the F1 negative test):**
call `get_token(request)` inside `consent_post` (`consent/views.py`) before returning 204.
Every consent decision response then re-sends `csrftoken` with a fresh 364-day clock alongside
the consent cookie, restoring the invariant *"csrftoken set at decision time with 364d > 180d"*
for **all** decision cycles, including manage-panel re-saves. No per-page `Set-Cookie` for
decided shoppers, no caching-surface change, no test inversion. Residual risk shrinks to
abnormal browser cookie eviction — acceptable.
Alternative (heavier): drop the `show_banner` scoping and call `get_token()` whenever the
feature is enabled — also correct, but adds `Set-Cookie`/`Vary: Cookie` to every storefront
response for decided shoppers and requires inverting
`test_decided_shopper_home_page_does_not_force_a_csrftoken_cookie`.
Add either way: a settings-pinning regression test asserting
`CSRF_COOKIE_AGE > CONSENT_COOKIE_MAX_AGE` and `CSRF_USE_SESSIONS is False`, so a future
settings change cannot silently reopen this gap.

**Non-blocking:** given the multi-condition rarity, the fail-closed direction, and the
self-healing outcome (consent expiry re-prompts), this does not block release — but the
one-line fix is cheap enough that it should ship in the next patch.

### F2 / F3 — dispositions verified
F2: ADR-025 addendum present (`docs/adr/ADR-025-consent-management.md:162-173`, `__Host-`
rename mandated before any shared-subdomain provisioning). F3: release-checklist deploy item
present (`docs/releases/pixels-currency-chat/RELEASE_CHECKLIST.md:380-387`).

### Still open (unchanged)
- F5 (INFO): confirm the launch-checklist consumer's treatment of the `"(warning) "` entry.
- Human live-environment checklist items 2-4 from v1 (real-IP restoration, prod cookie flags,
  legal sign-off on the D4 beacon-exemption reading).

---

## Verdict v2 — 2026-07-11

**CLEAR-WITH-NOTES** (= APPROVED WITH CONDITIONS — none blocking)

Both release conditions from Verdict v1 are discharged: F1 is fixed with a full-middleware
204-not-403 proof, F4 is fixed with a regression test, F2 is recorded as a binding ADR
constraint, and F3 is on the release checklist. 111 consent-area tests pass; full suite
reported green (2754).

Notes (non-blocking): fix **F7** in the next patch (one line — `get_token(request)` in
`consent_post` — plus a settings-pinning test), confirm F5's checklist-consumer semantics,
and complete the v1 human live-environment checklist. No security vulnerability is open;
every identified failure mode fails in the safe (non-tracking) direction.
