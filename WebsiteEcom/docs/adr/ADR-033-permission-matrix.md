# ADR-033: Employee permission matrix — frozen module vocabulary, structural enforcement, full admin UX (TICKET-047)

**Status:** ACCEPTED (human, 2026-07-11 — full bundle approved: 19-module vocabulary with separate `employees` row, coupons→gift_cards mapping, structural enforcement, lockout guard, no self-edit. All D1c/D4c/D7 PENDING items DECIDED per the defaults recommended in this ADR — see D1c, D4c, D7. Safety Agent gate remains MANDATORY before release — this is a permission system)

**Date:** 2026-07-11

**Spec sources:** `04_admin_flows.md` AF-002 (the authoritative 18-module grid), AF-112 (super-admin cross-store invite), AF-108 ("Store invitation" transactional template); `11_uncertainties_to_validate.md` AF-C1, 18:A2, Part D; ADR-001 §3; TICKET-002 (shipped storage), TICKET-047 (this work).

---

## Decision

Freeze the module vocabulary as a code registry (`stores/modules.py`), keep the shipped
`permissions_json` JSON-matrix storage on `StoreEmployee`, replace the seven copy-pasted
per-app `_check_module_access` helpers with ONE resolver (`stores/permissions.py`), enforce
module gating **structurally at registration** on the store admin site (the TICKET-052
auto-wrap precedent) with an introspective drift test, ship the AF-002 grid editor +
invite/deactivate flows as a custom `StoreEmployee` admin form, and add two guardrails
(no self-escalation; last-full-access lockout prevention) at the persistence boundary.

Sub-decisions D1–D8 below. Implementation splits into **TICKET-054** (enforcement core)
and **TICKET-047** (rewritten: the UX layer, depending on TICKET-054).

---

### D1 — Canonical module vocabulary (frozen)

**D1a. Registry of record.** New `stores/modules.py`:

```python
Module = namedtuple("Module", "key label levels")   # levels ⊆ ("full", "limited", "none")

MODULES = (
    Module("apps",                "Apps",                 ("full", "none")),
    Module("pages",               "CMS",                  ("full", "none")),
    Module("customers",           "Customers",            ("full", "none")),
    Module("dashboard",           "Dashboard",            ("full", "none")),
    Module("domains",             "Domains",              ("full", "none")),
    Module("gift_cards",          "Gift cards",           ("full", "none")),
    Module("inventory",           "Inventory",            ("full", "none")),
    Module("orders",              "Orders",               ("full", "limited", "none")),
    Module("products",            "Products & Collections", ("full", "none")),
    Module("analytics",           "Reports",              ("full", "none")),
    Module("settings",            "Settings",             ("full", "none")),
    Module("themes",              "Themes",               ("full", "none")),
    Module("upsell_campaigns",    "Up-sell campaigns",    ("full", "none")),
    Module("abandoned_campaigns", "Abandoned campaigns",  ("full", "none")),
    Module("pixels",              "Pixels",               ("full", "none")),
    Module("reviews",             "Reviews",              ("full", "none")),
    Module("currency_converter",  "Currency Converter",   ("full", "none")),
    Module("security_badges",     "Security Badge",       ("full", "none")),
    # 19th row — see D1c (DECIDED per D7 item 2, human 2026-07-11: separate row):
    Module("employees",           "Employee Accounts",    ("full", "none")),
)
```

"Invoice Orders", "CSV Templates", "Files", "Zapier" are removed from the engine (AF-002
owner annotations) — they never enter the vocabulary.

**Naming rule (freeze criterion):** a key already shipped in code is canonical; the AF-002
label is the display name only. Hence `pages` (label "CMS" — closes 18:A2's ASSUMPTION as
"keep the key, no rename") and `analytics` (label "Reports"). New modules get snake_case
keys derived from the AF-002 label. Keys are stable IDs, never display values (§XV / §VII of
design-pattern-ideas: never use display values as identity).

**D1b. Drift found in the census** (every key checked in code today: `apps`,
`products`, `collections`, `orders`, `pages`, `analytics`, `security_badges`, `employees`):

| Shipped key | AF-002 module | Action |
|---|---|---|
| `apps` | Apps | keep (engagement/, feeds/) |
| `products` | Products & Collections | keep (catalog/) |
| `collections` | Products & Collections | **DRIFT — merge into `products`.** catalog/admin.py Collection admins re-key to `products`; data migration rewrites `permissions_json`: `products = max(products, collections)` by hierarchy rank, then drops `collections`. |
| `orders` | Orders | keep (orders/) |
| `pages` | CMS | keep key, label "CMS" (pages/, catalog/ static-page views) |
| `analytics` | Reports | keep key, label "Reports" (analytics/admin_views.py, catalog/admin.py:634) |
| `security_badges` | Security Badge | keep (badges/) |
| `employees` | *(not in the 18-row grid)* | keep — see D1c |

**Migration story for the one merged key:** single reversible data migration
(forward: merge + drop; reverse: re-add `collections` copied from `products`). No other
key is renamed, so no other data migration exists.

**D1c. The `employees` key — DECIDED (human, 2026-07-11; CRITICAL, carried from AF-002 edge cases).**
AF-002 flags that "Settings — Full access" implicitly granting employee management is a
privilege-escalation hole and recommends a separate permission. **Decided (matches shipped
code, which already gates `stores/admin.py` on `"employees"`):** `employees` is a separate
19th grid row labeled "Employee Accounts"; `settings` does NOT grant it. The grid therefore
renders 19 rows, not 18 — a deliberate, human-visible deviation from the screenshot grid.

**D1d. Validation at the persistence boundary (§XV-5).** `StoreEmployee.clean()` (and a
model-level guard in `save()` for non-form writes) rejects: keys not in `MODULES`, values
not in the module's declared `levels`. Loud failure, not silent key-dropping (§XV-1). A
data migration first strips legacy/unknown keys so existing rows pass.

---

### D2 — Storage: keep the JSON matrix on `StoreEmployee` (no schema pivot)

`permissions_json` on `StoreEmployee` (TICKET-002, shipped) stays. `PERMISSION_LEVELS =
("full", "limited", "none")` and the rank comparison in `has_module_access()`
(stores/models.py:590 — verified correct: rank(full)=0, pass when stored_rank ≤
required_rank) stay as the storage semantics.

Schema delta is limited to invite-flow fields (D5): `StoreEmployee.invited_name`,
`StoreEmployee.invited_phone` (both nullable, informational only — phone 2FA use is
DECIDED out of scope per D7 item 5, human 2026-07-11, AF-002 step 4).

UI restriction vs storage: the grid only *offers* "limited" where the module declares it
(orders only, per AF-002), but storage/validation accept "limited" for any module that
declares it — adding a level to a module later is a registry edit, not a migration.

---

### D3 — Enforcement point: structural, at registration (not per-admin)

**D3a. The single resolver** — new `stores/permissions.py`:

- `get_store_employee(request)` — returns the active `StoreEmployee` for
  `(request.user, request.store)` or `None`; **cached on the request object** (one query
  per request — the admin index calls `has_module_permission` once per registered admin,
  ~30 times, so an uncached resolver is an N-queries-per-request regression).
- `check_module_access(request, module_key, level) -> bool` — the ONE resolution function
  (§XV-4). Order: user inactive → deny; `is_super_admin` → allow; no `request.store` →
  deny; no active employee row → deny; `full_access=True` → allow; else
  `has_module_access(module_key, level)`. This is byte-for-byte the semantics of the seven
  existing per-app copies (badges, catalog, orders, engagement, pages, feeds, stores) —
  all seven are deleted and their call sites migrated.
- `require_module(module_key, level)` — view decorator for custom admin views (the
  analytics/admin_views.py precedent, feeds regenerate action view, pages contact inbox,
  stores add-language view). Denies with 403.

**D3b. Structural mixin, applied by the site itself.** `StoreModulePermissionMixin`
(core/admin.py or stores/) implements, driven by a declared `module_key`:

| ModelAdmin method | Matrix requirement |
|---|---|
| `has_module_permission` | `module_key` at `"limited"` (view threshold — `full` passes by rank) |
| `has_view_permission` | same |
| `has_add_permission` / `has_change_permission` / `has_delete_permission` | `module_key` at `"full"` |

Orders "Limited access" = view-only therefore falls out of the generic mapping (the
TICKET-002 default, DECIDED human 2026-07-11 per 11 Part D) — orders/admin.py needs no
special case.

`StoreAdminSite` gets a registration wrapper exactly like TICKET-052's
`_StoreScopedRegistrationMixin.register()` (webecom/admin.py:46 — the proven
"enforcement-by-construction" place a future admin cannot skip): every ModelAdmin
registered on **the store site only** is auto-wrapped with the mixin. The super site is
NEVER matrix-gated — the matrix is store-level by definition (D7).

`module_key` is a class attribute on each store-site ModelAdmin: a `str`, a
`tuple[str, ...]` (access = pass if ANY listed module grants — needed for the campaigns
app, whose admins serve both `upsell_campaigns` and `abandoned_campaigns`), or the
explicit sentinel `MODULE_EXEMPT` (requires a justification comment; sole shipped case:
core's read-only own-profile `User` admin).

**Fail-closed default:** a store-site admin with NO declared `module_key` is treated as
inaccessible for non-`full_access` employees (deny), while `full_access` employees and
super-admins are unaffected. Undeclared ≠ silently open (§XV-1) — but the loud signal is
the drift test, not a registration-time crash (a crash would brick currently-ungated
admins for everyone, including full-access owners, before the audit completes).

**D3c. What breaks / must migrate (the module-gating audit).** Census at design time —
store-site admin files WITH ad-hoc gating (helper deleted, checks become declarations):
badges, catalog, orders, engagement, pages, feeds, stores. Files registered on the store
site with NO gating today, and their proposed `module_key` (ASSUMPTION-marked where the
AF-002 grid has no obvious row):

| App / admins | module_key | Confidence |
|---|---|---|
| campaigns (Campaign, CampaignStep, CampaignSession, OrderCharge, CampaignIssuedCode) | `("upsell_campaigns", "abandoned_campaigns")` | ASSUMPTION |
| cart (Cart) | `orders` | ASSUMPTION |
| chat (ChatSession, StoreChatSettings) | `apps` | ASSUMPTION |
| consent (ConsentSettings, ConsentRecord) | `settings` | ASSUMPTION |
| currency (StoreCurrencySetting) | `currency_converter` | high |
| discounts (DiscountCode, CampaignReward) | `gift_cards` | ASSUMPTION — ADR-002 unified coupons+gift cards into DiscountCode; AF-002 has a "Gift cards" row but no "Coupons" row. If a human wants coupons gated separately, that is a vocabulary change (new module), not a code change. |
| emails (EmailTemplate, SentEmail) | `settings` | ASSUMPTION |
| pixels (Pixel) | `pixels` | high |
| reviews (Review, ReviewRequest) | `reviews` | high |
| shipping (ShippingRate) | `settings` | ASSUMPTION (grid has no shipping row; store-level shipping config sits under Settings in the reference platform) |
| stores (StoreDomain, StoreLanguage, ShippingCountry) | `domains` | high |
| core (User read-only profile) | `MODULE_EXEMPT` | high |

Reserved keys with no admin surface yet: `inventory` (per-variant quantities live inside
the product editor → `products` for now; a standalone inventory screen takes the key when
it ships), `themes` (store theme selection, TICKET-029 area), `customers` (customers app
when store-site registered), `dashboard` (gates admin-index dashboard *widgets* via
`require_module`, never the index page itself — an employee must always reach the menu).

**D3d. The introspective drift test** (precedent: ADR-031/ADR-032 drift tests). A test
iterates `store_admin_site._registry`: every ModelAdmin must expose a `module_key` that is
either `MODULE_EXEMPT` or entirely within `MODULES` keys. Unmapped admin = test failure —
this is the mechanism that makes the vocabulary self-enforcing when the next app is added.
A second assertion: every non-reserved key in `MODULES` is claimed by ≥1 admin or listed
in an explicit `VIEW_ONLY_MODULES` allowlist (`dashboard`, `analytics` — gated via
`require_module` on custom views, no ModelAdmin), so dead vocabulary is also loud.

---

### D4 — The matrix UX (the AF-002 screen)

No dedicated screenshot exists for this screen (checked
`/home/cedric/Dropbox/Applications/WebsiteEmpire2/spec-ecom/` — no employee/permission/team
captures); AF-002's written spec is the authority.

**D4a. Access mode + grid.** Custom `ModelForm` on the store-site `StoreEmployee` admin:

- **Access mode**: segmented two-option radio ("Full Access" / "Limited Access") bound to
  `full_access`.
- **Grid**: rendered from `MODULES` — one row per module: label + a radio group of exactly
  that module's `levels` (so only Orders shows three options). Implemented as dynamically
  generated form fields `perm__<key>` (one `ChoiceField` per module); `clean()` composes
  them back into `permissions_json`. No per-module DB columns, no JS framework — an admin
  template override (`admin/stores/storeemployee/change_form.html`) + small vanilla JS
  that hides the grid when Full Access is selected (AF-002 step 5 allows hidden or greyed;
  we hide). Toggling Full Access does NOT wipe `permissions_json` — switching back
  restores the previous grid (least-surprise; the master flag simply bypasses at read
  time, which is already the shipped `has_module_access` semantics).
- **List view** (AF-002 step 2): name, email, active status dot, `last_login_at`,
  `invited_at`; row click opens the same panel pre-filled; on/off toggle (`is_active`)
  independent of deletion; "Delete Account" hard-deletes after Django's standard confirm
  page (AF-002 edge case — hard delete is spec'd).

**D4b. Invite flow** (AF-002 steps 3–9). The store-site form replaces the raw `user`
FK widget with Full name / Email / Phone fields:

- Email matches an existing `User` → link it (no duplicate account).
- No match → create `User(email=…, is_store_admin=True)` with an unusable password and
  send the **"Store invitation"** transactional email (AF-108 template family) through
  `emails.service.send_transactional_email` — the single email entry point — containing a
  set-password link built on Django's `PasswordResetTokenGenerator`.
- `invited_at` exists (shipped); `accepted_at` is stamped when the invitee completes
  set-password. Stamp-once audit semantics follow the chat/admin.py precedent
  (`subprocessor_terms_accepted_at` — stamped on acceptance, never cleared afterwards).
- Full name / phone are stored on `StoreEmployee` (`invited_name`, `invited_phone`), NOT
  on `User`: a store admin must not gain a write surface onto a shared `User` row that
  other stores' memberships also read (same isolation instinct as ADR-001 §4).

**N-1 (accepted note, WAVE 4 Safety Agent audit, docs/security/WAVE4_AUDIT.md):**
`resolve_invitee`'s existing-user link path flips `is_store_admin=True` on a
pre-existing `User` who wasn't previously a store admin, and silently links them to the
inviting store — by design per D4b above. The user is notified (the "Store invitation"
email still fires for the linking case), and `has_permission`'s per-store membership
check confines the new access to the single store that invited them (no other store's
data is exposed). **Accepted for v1;** the release note must state plainly: "inviting an
existing account grants it store-admin entry to *your* store and emails them."

**D4c. Multi-store employees + super-admin (AF-112, decision ADR-001 §3 / 11:#8).**
One `StoreEmployee` row per (user, store) assignment, each with its own matrix —
**AF-C1 DECIDED (human, 2026-07-11): per-store matrix** (AF-112's own recommendation; the
row structure was built for this, and "org-wide" degenerates to copying one matrix to every
row, so the default forecloses nothing). The super-admin site
keeps the cross-store `StoreEmployee` admin (store FK exposed, all rows visible) and gains
the same grid form; "assign employee to N stores in one panel" (AF-112 step 4) is a UX
nicety deferred — creating N rows is the v1 flow.

---

### D5 — Guardrails

**D5a. No self-escalation.** On the store site, an employee can never edit or delete their
own `StoreEmployee` row: `has_change_permission`/`has_delete_permission` return False when
`obj.user_id == request.user.pk` (super-admins exempt — they bypass the matrix anyway and
the super site is the recovery path). Defense in depth at the persistence boundary
(§XV-5): `clean()` rejects a save that changes `full_access`/`permissions_json`/`is_active`
on the actor's own row when the actor is not a super-admin (the actor is threaded in via
the admin form, so non-admin code paths — shell, migrations — are unaffected).

**D5b. Lockout prevention.** A store must never transition from ≥1 to 0 *active
full-access* employees through store-site actions: demote (`full_access` True→False),
deactivate, and delete are blocked on the last such row. Enforced in model validation +
`ModelAdmin.delete_model`/`delete_queryset` (bulk-action path included). The count check
runs inside the write transaction with `select_for_update()` on the store's employee rows
— two concurrent "demote the other admin" requests must not interleave past the check
(§XIII check-then-act lesson). Stores that already have zero full-access employees (the
legitimate "0 rows = open access" whitelist case in `StoreAdminSite.has_permission`) are
unaffected — only the ≥1→0 transition is blocked. The **super site allows** these
operations with a `messages.warning` (a super-admin can always re-grant; hard-blocking the
control plane creates unfixable states).

**D5c. Super-admin bypass semantics.** `is_super_admin` passes every module check
(resolver step 2) and the super site is never matrix-gated. `full_access=True` bypasses
the grid but NOT store scoping, NOT `is_active` on the row, and NOT the guardrails above.

---

### D6 — Composition with Django auth (traced, Django 6.0.4)

The matrix fully replaces Django's model-permission machinery on both admin sites;
`django.contrib.auth` `Permission`/`Group` rows and `is_staff`/`is_superuser` play **no
role**. Exactly what is overridden and what the stock behavior would have been:

- `AdminSite.has_permission` — stock: `request.user.is_active and request.user.is_staff`
  (django/contrib/admin/sites.py). Overridden (shipped, webecom/admin.py): store site =
  `is_active AND (is_store_admin OR is_super_admin)` + the store whitelist (≥1
  `StoreEmployee` row — active or not — locks the user to stores with an ACTIVE row;
  deactivation never widens access); super site = `is_active AND is_super_admin`. So
  `is_super_admin` alone DOES grant entry to both sites today; what it does not grant is
  Django's stock admin (`is_staff` is never set by our flows) — that is the earlier
  "is_super_admin alone doesn't grant admin access" finding, and it stays true for any
  stock `AdminSite` that might be mounted.
- `ModelAdmin.has_module_permission` — stock: `request.user.has_module_perms(app_label)`
  (django/contrib/admin/options.py); consulted per-admin by `AdminSite._build_app_dict`
  to build the index/menu. Overridden by the D3 mixin → matrix check, so the menu
  self-filters to permitted modules with no extra template work.
- `ModelAdmin.has_view/add/change/delete_permission` — stock: `user.has_perm("app.codename")`
  (options.py; view accepts view-or-change). Overridden by the mixin → matrix check. The
  mixin does NOT call `super()` into the stock implementations, so no `Permission` rows
  are ever consulted and `ModelBackend`'s `is_superuser`-grants-everything rule cannot
  leak access to users lacking the platform flags. Inline admins, admin actions, and
  autocomplete endpoints all route through these same five methods (options.py), so the
  mixin covers them without extra hooks.

`request.store` on `/admin/` comes from `HostResolutionMiddleware` (LocaleMiddleware
excludes `/admin/`) — shipped behavior, unchanged.

---

### D7 — Items DECIDED (human, 2026-07-11; all recommended defaults approved)

1. **AF-C1** — per-store vs org-wide matrix. DECIDED: **per-store** (D4c).
2. **Employees row** (AF-002 edge case, CRITICAL) — separate 19th grid row vs folded into
   Settings. DECIDED: **separate row** (D1c).
3. **Orders "Limited access" semantics** (11 Part D, CRITICAL) — DECIDED: stays
   **view-only** (TICKET-002 decision: no refunds, no status changes, no CSV export).
4. **Coupons module placement** — DECIDED: unified DiscountCode admin gated under
   `gift_cards` (D3c). LOW.
5. **Phone field purpose** — DECIDED: informational only; 2FA out of scope (AF-002 step 4). LOW.
6. Mapping ASSUMPTIONS in the D3c table (campaigns tuple, cart→orders, chat→apps,
   consent/emails/shipping→settings) — DECIDED (approved as-is). LOW — each remains a
   one-line `module_key` edit if a future need arises.

---

## Context

- **Shipped storage (TICKET-002):** `StoreEmployee` with `full_access` master flag,
  `permissions_json` (free-form keys today — nothing validates them), `invited_at`,
  `accepted_at`, `last_login_at`, `is_active`, unique `(user, store)`;
  `has_module_access()` with hierarchy full > limited > none (stores/models.py:590,
  semantics verified correct).
- **The copy-paste reality:** seven near-identical `_check_module_access` helpers
  (badges, catalog, orders, engagement, pages, feeds, stores/admin.py — feeds' variant
  even hardcodes `"apps"`), plus a distinct decorator-style check in
  analytics/admin_views.py. Eight keys in production use; `collections` conflicts with
  the AF-002 grid; 12 more store-site admin files have NO module gating at all (D3c
  table). This is exactly the "per-admin opt-in a future admin forgets" failure family
  that ADR-031/ADR-032 killed with registration-time wrapping — permission gating is the
  third member.
- **AF-002** specifies the grid exactly: 18 modules, Full/No for all except Orders
  (Full/Limited/No), Full-Access mode hiding the grid, invite email, active-dot list,
  hard delete. No screenshot exists; the written flow is authoritative.
- §XV-1 (invisible failures), §XV-4 (single resolution function), §XV-5 (persistence-
  boundary validation), §XIII (check-then-act races) are the binding lessons applied.

## Options considered

1. **Vocabulary**: (a) rename shipped keys to match AF-002 labels (`pages`→`cms`,
   `analytics`→`reports`); (b) **keep shipped keys, AF-002 labels as display names,
   merge only the true conflict (`collections`)** — chosen; (c) free-form keys forever
   (status quo).
2. **Storage**: (a) **JSON matrix on StoreEmployee (status quo)** — chosen;
   (b) `StoreEmployeeModulePermission` row per (employee, module).
3. **Enforcement**: (a) keep per-admin helpers, just deduplicate; (b) middleware
   URL-prefix gating; (c) **registration-time mixin + single resolver + drift test** —
   chosen.
4. **Unmapped-admin behavior**: (a) registration-time exception; (b) silently open;
   (c) **fail-closed for limited employees + drift-test failure** — chosen.
5. **Grid widget**: (a) raw `JSONField` textarea (status quo); (b) 18-row inline formset
   over permission rows (forces option 2b storage); (c) **dynamic ChoiceFields composed
   into `permissions_json`** — chosen.

## Chosen option

D1–D8 as above: option 1b + 2a + 3c + 4c + 5c.

## Why

- **Keys ≠ labels** avoids two pointless data migrations (`pages`, `analytics`) while
  still freezing a 1:1 grid mapping; the only migration is the one real conflict
  (`collections`), and it is mechanical (max-rank merge).
- **JSON matrix wins on all three weighed axes**: admin-form ergonomics (one form, no
  18-row formset), query cost (ONE row fetch per request, request-cached — vs 18-row
  prefetch multiplied by ~30 `has_module_permission` calls on the index page), and the
  drift-test story (key validation against `MODULES` at clean() + the registry test is
  strictly simpler than FK integrity + vocabulary-change data migrations). Cross-employee
  queries on permission values are an admin-filter rarity, not a hot path.
- **Registration-time wrapping is the only enforcement point that cannot be forgotten** —
  proven twice in this codebase (ADR-031 Addendum 3 D2, ADR-032 D2). Per-admin helpers
  already drifted (12 ungated files); a convention would drift again.
- **Fail-closed + drift test** makes the unmapped state deny-by-default (safe) and
  test-visible (loud) without bricking full-access owners during the audit rollout —
  a registration-time crash would take down modules for everyone before keys are assigned.
- **Guardrails at the persistence boundary** rather than only in admin methods: the admin
  is not the only writer (shell, future API), and §XV-5 says system-error validation
  belongs where the write happens.

## Risks

- **Fail-closed rollout narrows access**: limited employees relying on today's UNGATED
  admins (pixels, reviews, currency…) lose them until their matrix grants the new keys.
  Accepted: `permissions_json` is empty-by-default today, so gated modules were already
  invisible to limited employees; the newly gated ones were arguably open by mistake.
  Release note must tell store owners to review employee grids once.
- **`module_key` tuple semantics (campaigns)** grant with EITHER campaign module —
  slightly broader than a per-campaign-type split. Accepted until campaigns get per-type
  proxy admins.
- **Guardrail race residual**: `select_for_update` serializes same-store employee writes;
  deadlock risk is negligible (tiny row sets, single-store scope).
- **Own-row read-only** means a lone full-access owner cannot edit their own name/phone on
  the store site — the super site or a second admin is the path. Accepted (tiny surface,
  closes the escalation hole completely).
- **Menu correctness depends on every admin declaring the right key** — a WRONG (not
  missing) key is invisible to the drift test. Mitigated by per-module permission tests
  (below) and Spec Reviewer's screen-by-screen pass.

## Rollback strategy

- All enforcement lands behind the registration wrapper on `StoreAdminSite` — reverting
  the TICKET-054 commit restores per-admin behavior wholesale; the seven deleted helpers
  come back with the revert (single-commit discipline for the swap).
- The `collections`→`products` data migration is reversible (reverse re-adds
  `collections` mirroring `products`).
- UX ticket (TICKET-047) is additive UI on top of shipped storage — reverting it returns
  to the raw-JSONField admin without touching enforcement.
- No storage schema is destroyed at any step (`invited_name`/`invited_phone` are additive
  nullable columns).

## Tests required

1. **Resolver unit tests** (`stores/permissions.py`): every branch — inactive user, super
   admin, missing `request.store`, no row, inactive row, `full_access`, each
   level×requirement pair; request-level caching (second call = 0 queries).
2. **Drift test (D3d)**: every store-site ModelAdmin declares a valid `module_key` or
   `MODULE_EXEMPT`; every non-reserved module key is claimed or allowlisted. Must FAIL on
   an unmapped registration (prove by registering a dummy admin in the test).
3. **Structural gating tests**: for a representative admin per mapping row (incl. one
   tuple-keyed campaigns admin and the exempt profile admin): none/limited/full/
   full_access/super-admin × view/add/change/delete/module visibility (index menu).
4. **Vocabulary validation**: clean() rejects unknown key, illegal level, "limited" on a
   module not declaring it; migration test for the `collections` merge (max-rank).
5. **Guardrails**: self-edit denied (change + delete + bulk action), self-escalation
   rejected at clean(); last-full-access demote/deactivate/delete blocked on store site,
   allowed-with-warning on super site; concurrency test for the ≥1→0 race
   (`select_for_update` proven — After-Bug-style: test fails without the lock);
   zero-full-access store unaffected.
6. **Invite flow**: existing-user link, new-user creation (unusable password,
   `is_store_admin=True`), invitation email sent via `send_transactional_email`,
   `accepted_at` stamped on set-password and never re-stamped; store-B admin cannot see
   or invite into store A (existing isolation tests keep passing).
7. **Grid form round-trip**: permissions_json → form initial → POST → permissions_json
   identity; Full-Access toggle preserves the stored grid; Orders shows 3 options, others
   2; 19th row present (until D1c is decided otherwise).
8. **Query-count**: admin index page for a limited employee performs exactly one
   `StoreEmployee` query.
9. **Regression**: all existing per-app permission tests (badges, analytics, stores,
   orders, catalog…) stay green after their helpers migrate to the shared resolver.

## Tickets

- **TICKET-054 — Permission enforcement core** (Phase 3, P2, depends on TICKET-002/052):
  D1 vocabulary + validation + `collections` merge migration, D3 resolver + decorator +
  mixin + registration wrap + full `module_key` audit (D3c table), D5 guardrails, D6
  composition unchanged, tests 1–5, 8–9.
- **TICKET-047 — rewritten as the UX ticket** (depends on TICKET-054): D4 grid form +
  widget + template, invite/deactivate/delete flows + AF-108 email, super-admin
  cross-store management, tests 6–7. Designer pass after; **Safety Agent gate mandatory
  before release** (permission system: escalation, lockout, invite tokens, IDOR on
  employee rows).
