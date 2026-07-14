# ADR-009: Up-sell campaign data model — funnel graph, precedence, issued codes, abandoned-checkout sharing

**Status:** ACCEPTED (Human, 2026-07-04) — resolves the three PENDING items carried by ADR-007
(§Consequences) that were DECIDED in `specs/ecommerce_engine/11_uncertainties_to_validate.md`
Part D on 2026-07-04: store-vs-platform precedence, buy-X-get-X cap, next-order discount constraints.

**Extends:** ADR-007 (upsell funnel state machine — frozen), ADR-002 (unified DiscountCode),
ADR-001 §4 (StoreOwnedModel).
**Binding constraints:** design-pattern-ideas §VII/§XV-2 (never position as identity),
§VIII/§XV-3 (state never inferred from absence), §XIII/§XV-5 (validation at persistence
boundaries, atomic coupon decrement), §XIV (funnel state machines), §XV-4 (single resolution
function), §XV-6 (idempotent jobs).

---

## Decision

1. **Campaign root + CampaignStep graph is ratified** (Option A). `Campaign` stays the funnel
   root; `CampaignStep` rows form the branch graph via stable FKs `accept_next_step` /
   `decline_next_step`. Add an explicit **`Campaign.entry_step`** FK — the entry point is never
   derived from `position`.
2. **Precedence via an `owner_scope` discriminator on Campaign** (modified Option A). Platform
   (super-admin) campaigns are rows with `owner_scope='platform'` and a **non-null** `store` FK,
   created from `/superadmin/`, read-only in the store admin. Precedence is resolved in the single
   eligibility function: store-owned campaign shadows a platform campaign of the same
   `campaign_type`. No nullable store, no cross-row FK.
3. **Issued next-order codes are real `DiscountCode` rows** (Option A) plus a thin
   **`CampaignIssuedCode`** link table in the campaigns app for attribution and idempotency.
   Codes are single-use (`usage_limit=1`), expiry configurable per step
   (`expires_in_days`, default **30**).
4. **Abandoned checkout shares the `Campaign` model** (`campaign_type='abandoned_checkout'`,
   already in `CampaignType`) and the `CampaignSession` state machine, but **not** `CampaignStep`:
   its timed email sequence gets its own step model in TICKET-021 scope.
5. **Buy-X-get-X free**: no free-value cap (DECIDED 2026-07-04). The free item is always 100%
   free; the archetype's config schema has no cap field.
6. `CampaignSession` gains a denormalized **`campaign_type`** column with a DB unique constraint
   on `(order, campaign_type)` — enforcing ADR-007 §1 verbatim at the persistence boundary.

---

## Question 1 — Campaign vs CampaignNode relationship

### Context
Multi-step funnel (UF-J, DECIDED): after accept/decline on offer 1, a second offer can be shown.
Each step needs its own accept/decline branch. TICKET-039 later builds a visual editor over this
graph; TICKET-028 uses per-step idempotency keys (`upsell:{order_id}:{step_id}`).

### Options considered
- **Option A** — `Campaign` root + `CampaignStep` node rows; branching via `accept_next_step` /
  `decline_next_step` FKs on the step; `Campaign.entry_step` FK marks the entry.
- **Option B** — `Campaign` IS the node; multi-step expressed as `next_campaign_accept` /
  `next_campaign_decline` FKs on Campaign itself.

### Chosen option: **A** (root + step graph), with an explicit `entry_step` FK added.

### Why
- **Session semantics break under B.** ADR-007 §1 mandates one `CampaignSession` per
  `(order, campaign_type)` — the unified "what did this customer experience" view (§XIV). Under B,
  a 3-step funnel is 3 Campaign rows; either the session multiplies (3 sessions for one funnel
  journey, destroying the unified view and the exclusion rule "campaign B does not fire if A
  fired"), or the session must be keyed to the *head* campaign — which reinvents Option A's root,
  poorly.
- **Shared config lives once under A.** `is_active`, `targeting_rules_json`, name, and the
  capture-window duration are funnel-level facts. Under B they would be duplicated across chained
  rows and could drift (e.g. step 2 active while its head is inactive — an unreachable-but-live
  row, an invisible-failure class §XV-1).
- **Already implemented and referenced.** `campaigns/models.py` ships Option A; ADR-007 §8 froze
  it; T028's idempotency keys and T039's visual editor both assume the step graph. Option B would
  be a rewrite of an accepted, safety-reviewed design with no benefit.
- **Admin/permission surface is cleaner:** one Campaign row to activate, target, and permission —
  steps are an inline.

**Delta required — explicit entry step.** The current model has no `entry_step`; picking the entry
as `MIN(position)` would be positional identity (§XV-2 verbatim: "campaign step order — always use
stable opaque IDs"). Add:

```python
class Campaign(StoreOwnedModel):
    ...
    entry_step = models.ForeignKey(
        "CampaignStep", null=True, blank=True,
        on_delete=models.SET_NULL, related_name="+",
        help_text="First step shown at funnel entry. Required to activate the campaign.",
    )
```

Activation gate (persistence-boundary validation, §XV-5): a Campaign cannot be saved with
`is_active=True` unless `entry_step` is set, belongs to this campaign, and every step's branch
FKs point inside the same campaign. Enforced in the admin form AND in a model-level `clean()` /
service check — deleting the entry step (SET_NULL) must flip the campaign to a visible
"not launchable" condition, never silently pick another step.

`CampaignStep.position` remains a display-order hint only (gaps allowed, never renumbered) — this
is what lets T039 add/remove/reorder steps without positional renumbering.

### Risks
- Graph integrity: a branch FK could point to a step of another campaign (both are just
  `ForeignKey("self")`). Mitigated by the activation gate validation + a test.
- Cycles in the branch graph would loop a shopper forever. The activation validator must walk the
  graph from `entry_step` and reject cycles (bounded: steps per campaign is small).

### Rollback strategy
`entry_step` is nullable and additive — dropping the column reverts to the current schema with no
data loss. The activation gate is service/form logic, revertable independently.

### Tests required
- Activating a campaign without `entry_step` fails; with a foreign-campaign entry step fails.
- Branch FK to a step of another campaign is rejected by the validator.
- Cycle detection: A→accept→B→accept→A is rejected.
- Deleting the entry step deactivation behavior (campaign flagged not launchable, not silently
  re-pointed).

---

## Question 2 — Store-admin vs super-admin precedence

### Context
DECIDED 2026-07-04 (11 Part D): store-admin campaign wins over super-admin campaign on the same
product; store-admin can *view* but not modify the super-admin campaign. Products are
store-scoped, so a platform campaign necessarily applies to a specific store's catalog.
`StoreOwnedModel` (ADR-001 §4) makes `store` non-nullable and `StoreScopedManager` raises on
unscoped queries (§XV-1 tenant-bleed guard).

### Options considered
- **Option A** — runtime query ordered by a `scope` discriminator; take first per type.
- **Option B** — store campaign carries a `platform_campaign` FK to the row it overrides.
- **Option C** — no discriminator; platform campaigns have `store=None`; fall back at query time.

### Chosen option: **A**, implemented as `Campaign.owner_scope` + dedupe inside
`campaigns.service.evaluate_eligibility` (the single resolution function, §XV-4).

### Why
- **Option C is structurally impossible here.** `store=None` requires making
  `StoreOwnedModel.store` nullable, which breaks the ADR-001 §4 invariant, the
  `StoreScopedManager` raise-on-unscoped guard, and the model-introspection compliance test.
  It is also semantically wrong: a platform campaign's `targeting_rules_json` and step
  `offer_config_json` reference store-scoped product/collection IDs — the row *must* be pinned
  to one store.
- **Option B couples rows that have independent lifecycles.** The FK must be maintained when
  either side is created/retargeted/deleted (which product overlaps which campaign is a function
  of *targeting rules*, not a static link); a super-admin retargeting their campaign would have to
  rewrite store-owned rows to fix stale references. Precedence is a *read-time* policy — encoding
  it as write-time state creates drift, the classic invisible failure (§XV-1).
- **Option A is one field + ~10 lines in the function every caller already uses.** No schema
  coupling, precedence policy changeable without migration.

**Model delta:**

```python
class CampaignOwnerScope(models.TextChoices):
    STORE = "store", "Store admin"
    PLATFORM = "platform", "Platform (super-admin)"

class Campaign(StoreOwnedModel):
    ...
    owner_scope = models.CharField(
        max_length=10, choices=CampaignOwnerScope.choices,
        default=CampaignOwnerScope.STORE,
    )
    # index: (store, campaign_type, is_active) already exists and still leads the query.
```

**Precedence query pattern** (inside `evaluate_eligibility`, never inlined in views/templates):

```python
eligible = (
    Campaign.objects.for_store(order.store)
    .filter(campaign_type=campaign_type, is_active=True)
    .annotate(precedence=Case(
        When(owner_scope=CampaignOwnerScope.STORE, then=Value(0)),
        default=Value(1), output_field=IntegerField(),
    ))
    .order_by("precedence", "created_at")   # deterministic tie-break
)
# after targeting-rules filtering, take the first campaign per campaign_type:
# a store-owned eligible campaign shadows every platform campaign of the same type.
```

Shadowing is evaluated **after** targeting rules: a store campaign only shadows a platform
campaign when both are actually eligible for this order (this is what "on the same product"
means once targeting is rule-based, not a static product FK). String ordering is never relied on
('platform' < 'store' alphabetically — the `Case` annotation makes intent explicit and survives
future scope values).

**Permission enforcement (both surfaces):**
- Store admin (`/admin/`): queryset includes platform rows (store-admin can *view*);
  `has_change_permission` / `has_delete_permission` return `False` when
  `obj.owner_scope == PLATFORM`; `owner_scope` is not an editable field — store-created rows are
  forced to `STORE` in `save_model`. Same rule on `CampaignStep` inlines (a store admin must not
  edit a platform campaign's steps).
- Super admin (`/superadmin/`): full CRUD on both scopes; rows created there default to
  `PLATFORM` (explicit, not inferred).
- Permission tests are mandatory (global definition of done): a store-admin POST attempting to
  mutate a platform campaign or its steps must 403, not just hide the button.

### Risks
- Dedupe-per-type happens in Python after rule filtering — fine at these cardinalities (a handful
  of active campaigns per store), but every caller MUST go through `evaluate_eligibility`;
  any second code path reintroduces the precedence bug class (§XV-4).
- A future "platform campaign across all stores" feature would need row fan-out per store (or a
  template/instantiation layer) — acceptable; explicitly out of scope for this ADR.

### Rollback strategy
`owner_scope` is additive with default `'store'`; dropping the column and the annotation reverts
cleanly. Existing rows are unaffected (all current rows are store-created).

### Tests required
- Store + platform campaign of the same type both eligible → store campaign returned, exactly one
  session created, `campaign_type` unique constraint respected.
- Only platform campaign eligible → platform campaign returned (fallback works).
- Store admin cannot change/delete a platform campaign or its steps (HTTP-level permission test).
- Store admin sees the platform campaign in the changelist (read-only view requirement).
- `owner_scope` cannot be spoofed via store-admin form POST.

---

## Question 3 — Generated next-order discount code storage

### Context
The `storewide_discount` archetype issues a code for the customer's *next* order when the shopper
accepts. DECIDED 2026-07-04: single-use, configurable expiry, default 30 days. ADR-002 already
ships the unified `DiscountCode` with atomic conditional-UPDATE redemption (§XIII), provenance
audit, and `CampaignReward` (idempotency for *external* campaigns keyed on an opaque
`campaign_id` string + recipient email).

### Options considered
- **Option A** — generate a `DiscountCode` row (`discount_type='coupon'`) on accept.
- **Option B** — separate `CampaignIssuedCode` table holding the code string itself.

### Chosen option: **A + thin link table** — the code is a real `DiscountCode`; a new
`CampaignIssuedCode` model in the **campaigns** app links session → step → code for attribution
and idempotency. The code *string* lives only in `DiscountCode`.

### Why
- **Redemption must have exactly one path** (§XV-4). A code string stored outside `DiscountCode`
  would need a parallel validation/decrement path at checkout, duplicating the atomic
  conditional-UPDATE machinery ADR-002 §4 got right — reintroducing the check-then-save race
  (§XIII). With Option A, checkout code entry needs zero changes: `usage_limit=1` +
  `ends_at` already enforce single-use and expiry.
- **Attribution still needs a link.** `DiscountCode` has (correctly) no FK to campaigns — the
  discounts app must not depend on the campaigns app. Recovery-revenue attribution
  ("this next-order purchase came from campaign X, step Y") and issuance idempotency need
  campaign-side linkage, hence the thin table on the campaigns side (dependency direction:
  campaigns → discounts, which already holds conceptually via `provenance='campaign'`).
- **Why not reuse `CampaignReward`:** its unique key is `(store, campaign_id, recipient_email)` —
  correct for external one-shot rewards, wrong here: a returning customer who accepts the same
  campaign on a *second order* must receive a second code. The correct idempotency unit is
  `(campaign_session, step)` — retry-safe within one accept flow, non-blocking across orders.

**New model (campaigns app):**

```python
class CampaignIssuedCode(StoreOwnedModel):
    """Attribution + idempotency link: which session/step issued which DiscountCode.

    unique (campaign_session, step) is the ON-CONFLICT target: the accept handler
    creates DiscountCode + CampaignIssuedCode in one transaction; a retry hits
    IntegrityError and returns the already-issued code (§XV-6 idempotent jobs).
    """
    campaign_session = models.ForeignKey(CampaignSession, on_delete=models.CASCADE,
                                         related_name="issued_codes")
    step = models.ForeignKey(CampaignStep, on_delete=models.PROTECT, related_name="issued_codes")
    discount_code = models.ForeignKey("discounts.DiscountCode", on_delete=models.PROTECT,
                                      related_name="+")
    issued_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        constraints = [models.UniqueConstraint(fields=["campaign_session", "step"],
                                               name="uniq_issued_code_per_session_step")]
```

**Issuance parameters** (written by the campaigns service on accept, atomically with the
session-state transition):

| DiscountCode field | Value |
|---|---|
| `discount_type` | `coupon` |
| `provenance` | `campaign` (feeds the ADR-002 "765×$5 audit") |
| `value_type` / `value` | from the step's `offer_config_json` (`discount_pct` or fixed) |
| `usage_limit` | `1` (DECIDED: single-use) |
| `usage_limit_per_customer` / `per_email_limit` | `1` |
| `ends_at` | `now + offer_config_json["expires_in_days"]` (default **30**) |
| `stackable` | `False` |
| `code` | generated, unpredictable (e.g. `secrets` — never sequential), unique per store |

`expires_in_days` is part of the `storewide_discount` config schema and validated at the
persistence boundary (see Question 1's activation gate — per-archetype `offer_config_json`
schemas are validated in the admin form and in a single `validate_offer_config(offer_type,
config)` function; the DECIDED buy-X-get-X schema likewise contains **no cap field**).

The code is delivered on the thank-you page and in the confirmation email. It is a discount
secret, not an auth token — storing it plaintext in `DiscountCode` is consistent with all other
coupons (contrast: `CampaignSessionToken` stores hashes because those tokens gate *charging a
vaulted payment method*).

### Risks
- Accept-handler crash between DiscountCode INSERT and CampaignIssuedCode INSERT would orphan a
  code → both INSERTs and the session transition run in one DB transaction (T028's accept flow is
  already a single locked transaction per ADR-007 §3).
- Code-collision on generation → retry loop on the `(store, code)` unique constraint.

### Rollback strategy
`CampaignIssuedCode` is a new table; dropping it loses attribution but never breaks redemption
(codes remain valid `DiscountCode` rows). PROTECT FKs prevent deleting a code out from under its
audit row.

### Tests required
- Accept twice (retry) on the same session/step → exactly one DiscountCode exists, same code
  returned (IntegrityError path).
- Same customer, second order, same campaign → a second code IS issued.
- Issued code: redeemable once, rejected the second time (via existing DiscountService path);
  rejected after `ends_at`; default expiry = 30 days when `expires_in_days` absent.
- `provenance='campaign'` set (audit query returns the row).

---

## Question 4 — Abandoned checkout: shared Campaign model?

### Context
TICKET-021 (abandoned checkout) has per-email `send_delay_hours`, a 3-step email wizard, resume
links, suppression on completion. ADR-007 §10 already decided the *session* side: same
`CampaignSession` machinery, per-step state transitions, never a single enum on Order.

### Options considered
- Share `Campaign` with `campaign_type='abandoned_checkout'`.
- Separate `AbandonedCheckoutCampaign` model.

### Chosen option: **shared `Campaign` root, separate step model** — `campaign_type='abandoned_checkout'`
(already present in `CampaignType`), with a dedicated `AbandonedCheckoutEmailStep` model
(TICKET-021 scope) instead of `CampaignStep` rows.

### Why
- **Sharing the root is what §XIV exists for:** one table answers "what campaigns did this
  customer experience", exclusion rules ("no upsell if they already got an abandonment coupon")
  read one session table, the admin shows one campaign list, `owner_scope` precedence and
  targeting rules apply uniformly for free.
- **But the step shapes are disjoint.** `CampaignStep` models an interactive accept/decline
  *branch graph*; abandonment emails are a *linear timed sequence* (delay, template, subject,
  optional coupon) with no branches — the "branch" is the customer completing the order, which is
  suppression, not a decline path. Forcing `send_delay_hours`, template refs, and subject lines
  into `offer_config_json` would bloat the JSON schema and give T039's visual funnel editor step
  rows it must specifically exclude. The existing code already anticipates this split:
  `StepOfferType` deliberately excludes `ABANDONED_CHECKOUT` ("drives the campaign-level email
  sequence scheduler, not individual step offers").
- Not over-engineering: one small extra model in T021, versus permanent JSON-schema contortion in
  the shared step table.

Sketch (final design belongs to TICKET-021, listed here only to fix the boundary):
`AbandonedCheckoutEmailStep(StoreOwnedModel)`: `campaign` FK (limit_choices_to
abandoned_checkout), `position`-as-hint + stable PK sequencing, `send_delay_hours`,
`email_template` ref, coupon-issue config (reusing this ADR's Question-3 issuance path with
`CampaignIssuedCode` for internal idempotency, or `CampaignReward` if issued per-email outside a
session — T021 to decide with the AF-C4 wizard placement).

### Risks
- Validation must prevent an `abandoned_checkout` Campaign from having `CampaignStep` rows and
  vice versa (`entry_step` requirement from Question 1 applies only to funnel-type campaigns;
  the activation gate branches on `campaign_type`).

### Rollback strategy
None needed for T027 — this question only fixes the boundary; the email-step model ships with
TICKET-021.

### Tests required (T021)
- `abandoned_checkout` campaign with `CampaignStep` rows rejected; funnel campaign with email
  steps rejected.
- Activation gate for `abandoned_checkout` requires ≥1 email step, not `entry_step`.

---

## Appendix A — Implementation plan

### TICKET-027 delivers (models + migrations + admin)

Already in place from the first data-layer pass (`campaigns/migrations/0001_initial.py`):
`Campaign`, `CampaignStep` (stable-FK branches), `CampaignSession`, `CampaignSessionToken`,
admin registrations for both surfaces, eligibility stub in `campaigns/service.py`.

Remaining T027 scope (this ADR's deltas):

1. **Migration `0002`** (single migration, all additive):
   - `Campaign.owner_scope` (`CharField`, choices store/platform, default `'store'`).
   - `Campaign.entry_step` (nullable self-app FK, SET_NULL).
   - `CampaignSession.campaign_type` (denormalized `CharField`, backfilled from
     `campaign.campaign_type` in a data migration step) + `UniqueConstraint(order, campaign_type)`
     replacing reliance on `(order, campaign)` alone — enforces ADR-007 §1 at the DB layer.
     Keep `(order, campaign)` uniqueness too (both hold).
   - New `CampaignIssuedCode` table (Question 3) with
     `UniqueConstraint(campaign_session, step)`.
2. **Validation layer** (persistence boundary, §XV-5):
   - `validate_offer_config(offer_type, config)` — one function, per-archetype schemas:
     `related_products` (product_ids), `buy_x_get_x` (buy_quantity, get_quantity, product_id —
     **no cap field**, DECIDED), `storewide_discount` (`discount_pct` or fixed value,
     `expires_in_days` default 30), `one_click_funnel`, `order_bump`.
   - Activation gate: `is_active=True` requires — funnel types: `entry_step` set, all branch FKs
     intra-campaign, no cycles, all step configs valid; `abandoned_checkout`: no `CampaignStep`
     rows (email steps arrive in T021).
3. **Admin** (both surfaces):
   - Store admin: platform rows visible read-only (`has_change_permission`/`has_delete_permission`
     False on `owner_scope='platform'`, including step inlines); `owner_scope` forced to
     `'store'` on save; activation-gate errors surfaced in the form.
   - Super admin: `owner_scope` defaults to `'platform'`; full CRUD both scopes.
   - `CampaignIssuedCode`: read-only in both surfaces (audit table).
4. **Service:** precedence dedupe in `evaluate_eligibility` (Question 2 query pattern) — still the
   only entry point; session creation writes the denormalized `campaign_type`.
5. **Tests:** all "Tests required" lists in Questions 1–3, plus the StoreOwnedModel compliance
   introspection for `CampaignIssuedCode`, plus AC-140/AC-144 coverage (single-offer funnel
   configuration via admin).
6. **CampaignStepTranslation model + AiJob trigger** (approved addition, 2026-07-04 — see
   Appendix B): the `CampaignStepTranslation` table, the `post_save` signal on `CampaignStep`
   that creates `campaign_step_translation` AiJobs, and the job-type registration in the aijobs
   plugin registry (`default_priority=100`).

Explicitly NOT in T027: any payment call, token issuance, thank-you rendering, Celery, or the
actual DiscountCode-writing accept handler (the issuance *function* signature and the
`CampaignIssuedCode` table land in T027; the transaction that calls it is T028).
**CampaignStepTranslation is INCLUDED in T027** (approved 2026-07-04, Appendix B) — it is not a
deferred item; only the job type's `build_prompt` / `persist_output` implementations are deferred
to the AI job orchestration phase.

### TICKET-028 adds on top (payment flow + Celery)

1. Checkout authorization path: authorize-only + `setup_future_usage=off_session` /
   PayPal vault; persist `Order.processor_account` pinning; `capture_window_expires_at` from the
   campaign (default 10 min).
2. `OrderCharge` migration in **orders** app (per T028 spec; admin already registers it).
3. Token issuance service for `CampaignSessionToken` (`thankyou-view` ~72 h, `upsell-act` =
   window, single-use, hash-only storage) + confirmation-email inclusion.
4. POST-only accept/decline endpoints: CSRF, rate limit (3/order then auto-decline), opaque order
   IDs, atomic `UPDATE … WHERE state … AND affected_rows == 1` race guard, DB-time window check
   inside the locked transaction, idempotency keys `capture:{order_id}` /
   `upsell:{order_id}:{step_id}`.
5. Two-charge baseline: capture original, off-session upsell charge, soft-fail degradation,
   `PENDING_CAPTURE` provisional items promoted by webhook.
6. **Accept-transaction hook for storewide_discount steps:** create `DiscountCode` +
   `CampaignIssuedCode` inside the same transaction as the session transition (Question 3);
   surface the code on the thank-you page and confirmation email; write `recovery_revenue`
   attribution when a linked code's order completes.
7. Celery: capture-window watchdog (expiry capture + `PENDING_CAPTURE` voiding + stuck
   `CAPTURE_IN_PROGRESS` reconciliation, schedule tighter than auth validity, alert on
   window+grace breaches).
8. Tests: AC-141, AC-142, AC-143, AC-145; race tests (accept vs watchdog); soft-fail path;
   token single-use and expiry; code-issuance idempotency under retry.

Multi-step *visual* builder remains TICKET-039 (pure admin UI over this graph).

---

## Appendix B — CampaignStepTranslation — approved addition (2026-07-04)

**Status:** APPROVED (Human, 2026-07-04) as part of TICKET-027 scope. This model was not in the
original Appendix A plan; it was implemented in T027 and ratified by the human on 2026-07-04.

**Binding rule — §15 (translation rule: CLI, not API):** all customer-visible content — including
campaign offer content rendered on the thank-you page and in sent emails — is translated via the
**AiJob CLI runner**, never via direct API calls. Campaign step offers are customer-visible, so
their translated content follows the same AiJob pipeline as `catalog.ProductTranslation`.

### Model summary (`campaigns/models.py` — `CampaignStepTranslation(StoreOwnedModel)`)

Per-language translated customer-visible content of a `CampaignStep`. The source step keeps its
default-language content in `offer_config_json` (keys `title`, `description`, `cta_label`); this
model stores their translated counterparts — translated and source content are never mixed in one
row (design-pattern-ideas §I).

| Field | Definition |
|---|---|
| `step` | FK → `CampaignStep`, `on_delete=CASCADE`, `related_name="translations"` |
| `store` | non-null FK (StoreOwnedModel compliance, ADR-001 §4) |
| `lang_code` | `CharField(10)` — ISO 639-1 code, e.g. `fr`, `pt-br` |
| `title` | `CharField(255, blank=True)` — translated offer title |
| `description` | `TextField(blank=True)` — translated offer description |
| `cta_label` | `CharField(100, blank=True)` — translated call-to-action button label |
| `status` | `draft` / `published`, default `draft` — only `published` content is live; absence of a record is not a state (§XV-3) |
| `ai_job` | FK → `aijobs.AiJob`, nullable, `on_delete=SET_NULL` — the AiJob that produced this translation; deleting the job never deletes the translation |
| `created_at` / `updated_at` | auto timestamps |

**Constraints:** `unique_together (store, step, lang_code)` — one translation record per step per
language; `clean()` rejects a `store` that differs from `step.campaign.store` (persistence-boundary
validation, §XV-5).

### AiJob signal trigger (`campaigns/signals.py`)

`post_save` on `CampaignStep`: when `offer_config_json` contains any of `title` / `description` /
`cta_label`, create one AiJob per configured `StoreLanguage` of the step's store that does not yet
have a `published` `CampaignStepTranslation` for this step. Properties:

- Jobs are created through `aijobs.service.create_job()` — the only AiJob creation path.
- Payload: `{step_id, offer_type, offer_config, lang_code}`; `requires_human_review=False`
  (auto-publish path); `created_by='trigger'`.
- Idempotent: re-saving a step creates no duplicate jobs for languages that already have a
  `published` translation (§XV-6).
- No jobs are created when the store has no `StoreLanguage` rows or the step has no translatable
  content.

### Job type registration (`campaigns/ai_jobs.py`)

Job type **`campaign_step_translation`**, registered in the aijobs plugin registry (ADR-003 §7)
with **`default_priority=100`** — translations are the highest-priority job type per ADR-003 §1.
`build_prompt` / `persist_output` are stubs deferred to the AI job orchestration phase; the
registration makes the scheduler recognise the type and keeps existing AiJob rows valid.

### T027 scope

`CampaignStepTranslation` is **INCLUDED in T027** (model + migration + signal trigger + job-type
registration). It is NOT a deferred item — only the job type's prompt/persist implementations
land with the AI job orchestration phase. See Appendix A item 6.
