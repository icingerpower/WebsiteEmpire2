# ADR-006-R: Payment routing engine — revised implementation schema (T017R / T020R)

> Owned by Architect Agent. Supersedes the *implementation* of TICKET-017/TICKET-020
> (payments/models.py `RoutingRule`, payments/routing.py). Does NOT change the decisions
> in ADR-006 — it makes the implementation actually conform to them after the Spec
> Reviewer's Phase-1 rejection (flat rule list + "first account by pk" violate ADR-006 §1–§3, §IX).
>
> Status: PROPOSED — 2026-07-03. Developer implements directly from this document.

## ADR-006-R: Decision-tree routing rebuild

**Decision:** Drop `payments.RoutingRule` entirely (no compatibility shim). Rebuild the control
plane as: `OrganizationRule` (two-stage decision tree with explicit `fallback_rule` chain),
`OrgPool` + `OrgPoolMember` (split targets), `ProcessorSplitCounter` (running counters, org-level
and method-level), `PaymentMethod` + `ProcessorOption` (method-family strategy layer), a revised
`DecisionLog` (`evaluated_rules_json`, `over_cap`, `parent_decision`), and the missing fields on
`Organization` and `ProcessorAccount`.

**Context:** Current code is a flat priority list where `_pick_account()` returns the first active
account by pk. §IX names this exact trap. Missing: geography (AC-130), running-counter splits
(AC-131/AC-133), health fall-through (AC-132), hide-when-no-route (AC-134), monthly caps/`over_cap`
(AF-C6), broad-shadows-narrow lint, fallback chain.

**Options considered:**
1. Patch `RoutingRule` incrementally (add columns, keep the flat loop) — rejected: the evaluation
   *shape* is wrong, not just the columns; patching preserves the flat-list structure §IX forbids.
2. Pure-JSON tree (`conditions_json` + `outcome_json` with org/pool IDs inside JSON) — rejected for
   outcome targets: a deleted Organization would leave a dangling ID in JSON and fail invisibly at
   checkout time (§XV-1). Conditions stay JSON (open vocabulary); outcome *targets* become FKs.
3. **Chosen:** hybrid — FK outcome targets with `on_delete=PROTECT` (loud failure at delete time),
   JSON conditions, explicit self-referential fallback chain, two counter scopes.

**Why:** Conforms to ADR-006 §1–§6 and §IX verbatim; every AC-130…134 behavior has a dedicated
structure; deletes fail loudly instead of routing failing silently.

**Risks:** (a) counter-row lock contention at high order volume — bounded: one `SELECT FOR UPDATE`
on ≤ pool-size rows per order; (b) month-boundary races on volume/counters — handled by
`(period)`-keyed counter rows and atomic get-or-create; (c) layer composition (org split ×
method strategy) still PENDING Part E — this design keeps the two counters independent so either
composition rule can be adopted without schema change.

**Rollback strategy:** No production data exists — rollback = revert the migration set (models are
new tables plus added nullable/defaulted columns). Rule edits themselves are live-on-save
(DECIDED 11:#4); rollback of a bad rule = edit/delete it, DecisionLog is the audit trail.

**Tests required:** AC-130, AC-131, AC-132, AC-133, AC-134 as integration tests; unit tests listed
per ticket at the end of this document.

---

## 1. Revised data model

All models below are platform-global `[G]` (no `store` FK — add them to `EXEMPT_MODELS` in
`core/tests/test_store_owned_model_compliance.py`). Money condition bounds are integer cents;
monthly volumes are `DecimalField` in the platform accounting currency (see §1.1 note).

### 1.1 Organization — additions only (existing `name`, `is_default`, `created_at` stay)

| Field | Type | Notes |
|---|---|---|
| `legal_name` | `CharField(255, blank=True, default="")` | Legal entity name. `name` remains the internal admin label. |
| `display_name` | `CharField(255, blank=True, default="")` | Buyer-facing name (receipts, emails). |
| `registration_country` | `CharField(2, blank=True, default="")` | ISO 3166-1 alpha-2. |
| `settlement_currencies` | `JSONField(default=list)` | List of ISO 4217 codes, e.g. `["EUR","USD"]`. |
| `coverage_areas_json` | `JSONField(default=list)` | List of area tokens (see §1.8 vocabulary): `["EU","ROW"]` or country codes. Informational for the super-admin UI; routing reads rule conditions, not this field. |
| `statement_descriptor` | `CharField(22, blank=True, default="")` | Card-statement text (processor max is 22 chars). |
| `monthly_threshold` | `DecimalField(12,2, null=True, blank=True)` | Monthly volume cap. `NULL` = no cap. **Constraint: the default org must have `NULL`** (it is the uncapped catch-all — AF-C6). |
| `current_month_volume` | `DecimalField(12,2, default=0)` | Running volume for `volume_month`. Incremented with an `F()` expression at authorization confirmation (webhook), never read-modify-write. |
| `volume_month` | `DateField(null=True)` | First day of the month `current_month_volume` covers. On increment, if month changed: atomic conditional `UPDATE … SET volume=amount, volume_month=new WHERE volume_month=old`. |
| `status` | `CharField(10, choices: active/draft, default="draft")` | Draft orgs are excluded from routing entirely. |

> **ASSUMPTION (threshold accounting PENDING, Part E):** `current_month_volume` accumulates order
> `grand_total` converted to a single platform accounting currency using `Currency.exchange_rate`
> at authorization time. What counts (auth vs capture, refund netting) and the month boundary
> timezone remain PENDING — the fields above are compatible with any resolution.

### 1.2 ProcessorAccount — additions only (existing fields stay; `is_active` remains the manual block)

| Field | Type | Notes |
|---|---|---|
| `method_family` | `CharField(20, choices: card/paypal/crypto, default="card")` | Which checkout method family this account can process. Stripe accounts = `card`; PayPal = `paypal` (PayPal-as-card-capability per AC-132 is expressed by creating a second PayPal account row with `method_family="card"`). |
| `supported_countries` | `JSONField(default=list)` | ISO country codes this account may charge buyers from. Empty list = all countries. |
| `settlement_currency` | `CharField(3, blank=True, default="")` | ISO 4217. |
| `priority_within_family` | `SmallIntegerField(default=100)` | Tie-breaker ordering inside one org + family (lower first). |
| `may_be_primary` | `BooleanField(default=True)` | False = never selected first in a backup chain. |
| `backup_only` | `BooleanField(default=False)` | True = selected only when every non-backup candidate is ineligible. Lint: `backup_only=True` requires `may_be_primary=False` (auto-forced in `clean()`). |
| `health_status` | `CharField(10, choices: GREEN/YELLOW/RED/UNKNOWN, default="UNKNOWN")` | State machine, §1.2.1. |
| `health_source` | `CharField(10, choices: probe/manual, default="probe")` | `manual` freezes the status against probe overwrites (health source PENDING Part E — this field supports both answers). |
| `health_checked_at` | `DateTimeField(null=True)` | Last probe/manual change. |
| `health_detail` | `CharField(255, blank=True, default="")` | Last probe error, for the super-admin screen (§XV-1: failures visible). |

#### 1.2.1 Health-status machine

```
UNKNOWN → GREEN | YELLOW | RED        (first probe or manual set)
GREEN   → YELLOW (1 probe failure) → RED (2 consecutive failures) 
YELLOW  → GREEN (1 probe success)
RED     → GREEN (1 probe success)     (recovery is immediate — AC-134 "reappears on next load")
any     → RED/GREEN/YELLOW (manual, sets health_source="manual")
manual  → probe (super-admin releases the manual pin)
```

- `is_healthy()` ≡ `is_active AND status == "active-org" AND health_status != RED`. GREEN, YELLOW
  and UNKNOWN are all routable (an unprobed account must not block checkout); YELLOW sorts after
  GREEN at equal `priority_within_family`.
- **The checkout path never calls a processor API.** "Health check at evaluation time" (ADR-006 §3)
  means *reading* `health_status` during evaluation. A Celery beat probe job calls each connector's
  `health_probe()` every 5 minutes and applies the machine above, skipping `health_source="manual"`
  rows. If `health_checked_at` is older than 30 min, the probe job alerts (§XV-1) but routing still
  treats the stored status as truth.

### 1.3 OrganizationRule — new (replaces `RoutingRule`, which is DROPPED)

| Field | Type | Notes |
|---|---|---|
| `stage` | `CharField(20, choices: stage1_geography / stage2_allocation)` | |
| `name` | `CharField(255)` | Super-admin label ("EU → Org-FR"). |
| `conditions_json` | `JSONField(default=dict)` | Stage-1 only, validated on save (§1.8 vocabulary). Stage-2 rules have `{}` — they are bound to their pool, not to order attributes. |
| `outcome_organization` | `FK(Organization, null=True, blank=True, on_delete=PROTECT, related_name="stage1_rules")` | Stage-1 outcome, XOR with pool. |
| `outcome_org_pool` | `FK(OrgPool, null=True, blank=True, on_delete=PROTECT, related_name="stage1_rules")` | Stage-1 outcome, XOR with org. |
| `allocation_type` | `CharField(20, blank=True, choices: percent_split / threshold_switch)` | Stage-2 only. |
| `org_pool` | `FK(OrgPool, null=True, blank=True, on_delete=CASCADE, related_name="allocation_rules")` | Stage-2 only: the pool this allocation rule governs. |
| `fallback_rule` | `FK("self", null=True, blank=True, on_delete=PROTECT, related_name="fallback_sources")` | Explicit next node when this rule's outcome is ineligible (§IX). NULL = fall through to default org. |
| `priority_within_stage` | `SmallIntegerField(default=100)` | Lower = evaluated first. Tie-break: `pk` ascending (defined order — §IX "same priority is undefined" trap). |
| `is_active` | `BooleanField(default=True)` | |
| `created_at` / `updated_at` | auto | |

DB `CheckConstraint`s (loud, not lint-only):
- stage1 ⇒ exactly one of `outcome_organization` / `outcome_org_pool` set; `allocation_type=""`; `org_pool` NULL.
- stage2 ⇒ `org_pool` NOT NULL; `allocation_type` set; both outcome FKs NULL.

`clean()` (blocking validation): fallback-chain cycle detection (walk `fallback_rule` from self,
visited-set, reject on revisit); stage-2 `fallback_rule` must also be stage-2 or NULL; conditions
vocabulary validation (§1.8). Non-blocking shadow lint: §3.

### 1.4 OrgPool + OrgPoolMember — new

`OrgPool`: `name CharField(255)`, `is_active BooleanField(default=True)`, timestamps.

`OrgPoolMember` (explicit through table — split targets are configuration and live here, NOT on
counters):

| Field | Type | Notes |
|---|---|---|
| `pool` | `FK(OrgPool, on_delete=CASCADE, related_name="members")` | |
| `organization` | `FK(Organization, on_delete=PROTECT)` | |
| `target_percent` | `DecimalField(5,2, default=0)` | Used by `percent_split`. |
| `position` | `SmallIntegerField(default=0)` | Ordering for `threshold_switch` (first-under-cap wins). |
| unique | `(pool, organization)` | |

Blocking save lint on the pool's members (when the pool has an active `percent_split` rule):
Σ `target_percent` = 100.00.

### 1.5 ProcessorSplitCounter — new (runtime state ONLY; two scopes)

One table, two mutually exclusive scopes (ADR-006 §2 + §6 — `alternate_evenly` uses the same
counter mechanism):

| Field | Type | Notes |
|---|---|---|
| `org_pool` | `FK(OrgPool, null=True, blank=True, on_delete=CASCADE)` | Scope A: org-level percent split. |
| `organization` | `FK(Organization, null=True, blank=True, on_delete=CASCADE)` | Member counted in scope A. |
| `payment_method` | `FK(PaymentMethod, null=True, blank=True, on_delete=CASCADE)` | Scope B: method-level `alternate_evenly`. |
| `processor_account` | `FK(ProcessorAccount, null=True, blank=True, on_delete=CASCADE)` | Member counted in scope B. |
| `period` | `CharField(7)` | `"YYYY-MM"` — counters reset by starting a new period row, never by UPDATE-to-zero (persists across restarts, AC-131). |
| `count` | `PositiveIntegerField(default=0)` | Incremented under `SELECT … FOR UPDATE` in the routing transaction. |

Constraints: `CheckConstraint` (scope A pair XOR scope B pair fully set); unique
`(org_pool, organization, period)` and `(payment_method, processor_account, period)` (two partial
unique constraints). Rows are `get_or_create`d lazily at first routing of a period.
`target_ratio` from the ADR-006 sketch is **deliberately omitted**: targets are config
(`OrgPoolMember.target_percent`), counters are state — duplicating targets onto counter rows
would rot on live-on-save edits.

### 1.6 PaymentMethod + ProcessorOption — new

`PaymentMethod`:

| Field | Type | Notes |
|---|---|---|
| `method_family` | `CharField(20, choices: card/paypal/crypto, unique=True)` | Checkout shows families only; processors invisible to buyer. |
| `displayed_label` | `CharField(100)` | |
| `is_enabled` | `BooleanField(default=True)` | |
| `strategy` | `CharField(20, choices: backup_chain / alternate_evenly, default="backup_chain")` | |
| `hide_when_no_route` | `BooleanField(default=True)` | AC-134. |
| `allow_store_local_disable` | `BooleanField(default=False)` | |

`ProcessorOption`: `payment_method FK(CASCADE)`, `processor_account FK(CASCADE)`,
`position SmallIntegerField`, unique `(payment_method, processor_account)`. Save lint (blocking):
`processor_account.method_family == payment_method.method_family`.

### 1.7 DecisionLog — revised (DROP and recreate; current table is empty)

Keep: `order_id` (soft BigInt), `store_id` (soft Int), `decided_at`. Replace/add:

| Field | Type | Notes |
|---|---|---|
| `evaluated_rules_json` | `JSONField(default=list)` | Full audit trail, §2 step 9 format. Rule IDs + verdicts, never buyer PII. |
| `chosen_organization` | `FK(Organization, null=True, on_delete=SET_NULL)` | |
| `chosen_processor_account` | `FK(ProcessorAccount, null=True, on_delete=SET_NULL)` | |
| `matched_rule` | `FK(OrganizationRule, null=True, on_delete=SET_NULL)` | Final stage-1 rule whose branch produced the outcome (after fallback walking; the walk itself is in `evaluated_rules_json`). |
| `reason` | `CharField(40)` | Closed vocabulary: `rule_match` / `fallback_chain` / `fallback_default_org` / `all_orgs_at_cap` / `routing_failure` / `upsell_charge` (child rows, ADR-007 §4). |
| `over_cap` | `BooleanField(default=False)` | True when the outcome was displaced by a monthly cap (including `all_orgs_at_cap`). |
| `buyer_country` | `CharField(2, blank=True, default="")` | Country granularity only — required to debug geography rules; not PII. Full address never logged. |
| `method_family` | `CharField(20, blank=True, default="")` | |
| `parent_decision` | `FK("self", null=True, on_delete=SET_NULL)` | Upsell child entries reference the original decision (ADR-007 §4). |

Indexes: `order_id`, `(reason, decided_at)`, `decided_at`.

### 1.8 Stage-1 `conditions_json` vocabulary (closed, validated on save)

```json
{
  "countries": ["FR", "DE"],        // ISO alpha-2; omit or [] = any
  "areas": ["EU", "ROW", "ALL"],    // named sets resolved server-side from a
                                    // frozen module-level constant (EU_COUNTRIES);
                                    // ROW = not in any other area token used
  "currencies": ["EUR"],            // omit = any
  "min_amount_cents": 0,            // omit = no bound; compared on order total
  "max_amount_cents": 500000        // in the order's currency
}
```

Effective country set = `countries ∪ resolve(areas)`. `"ALL"` (or an entirely empty condition dict)
= catch-all. Unknown keys or tokens ⇒ `ValidationError` at save (§XV-5: validate at the
persistence boundary — a typo must not become a never-matching rule in production).

---

## 2. Routing algorithm — `route_payment(order, method_family)`

Public API (module `payments/routing.py`, rewritten):

```
route_payment(order, method_family) -> ProcessorAccount | None
    Real routing: increments counters, writes exactly one DecisionLog row, pins the result.
preview_route(store, country, currency, total_cents, method_family) -> bool
    Dry run for AC-134 (hide method): identical evaluation, NO counter increments,
    NO DecisionLog write, no pinning.
```

`route_payment` never raises to the caller (checkout must degrade to a clean failure), but unlike
the current code the DecisionLog write is NOT swallowed on the happy path — a decision that cannot
be logged is itself logged at ERROR and alerted (§XV-1).

```
route_payment(order, method_family):
  ctx:
    buyer_country = upper(order.shipping_address["country"])
                    or upper(order.billing_address["country"]) or ""      # step 1
    currency      = order.currency
    total_cents   = int(order.total * 100)
    period        = now().strftime("%Y-%m")
    is_test       = _PHASE1_IS_TEST                                        # unchanged
  evaluated = []                                                           # audit trail

  with transaction.atomic():                                               # one txn for the whole decision
    # ---- STAGE 1: geography ------------------------------------------- (AC-130)
    outcome = None; matched_rule = None
    for rule in stage1 active rules, ORDER BY priority_within_stage, pk:
      if conditions_match(rule.conditions_json, ctx):                      # step 2
        matched_rule = rule
        outcome, evaluated += walk_branch(rule, ctx, evaluated)            # step 3–7
        break
      else:
        evaluated.append({rule: rule.pk, stage: 1, verdict: "no_match"})

    # ---- Exhaustion → default org ------------------------------------- (AF-C6)
    if outcome is None:
      default_org = Organization.objects.get(is_default=True)
      account = select_account(default_org, ctx, commit=True)              # step 8
      reason  = "fallback_default_org" if matched_rule is None else evaluated.exhaustion_reason
      # exhaustion_reason = "all_orgs_at_cap" when every branch failed on caps,
      #                     "fallback_chain" when it failed on health/eligibility
      over_cap = (reason == "all_orgs_at_cap")
      if account is None: reason = "routing_failure"                       # AC-134 server-side reject
    else:
      account, over_cap, reason = outcome

    write DecisionLog(order_id, store_id, evaluated_rules_json=evaluated,
                      chosen_organization, chosen_processor_account=account,
                      matched_rule, reason, over_cap,
                      buyer_country=ctx.buyer_country, method_family)      # step 9 — EVERY call, no sampling
    if account: pin order.processor_account = account,
                    order.organization = account.organization              # ADR-007 §4 — routing never re-runs
  return account
```

**Step 2 — `conditions_match`:** country in effective set (empty set = match; empty
`buyer_country` matches only catch-all rules), currency in set, `min ≤ total_cents ≤ max`.
Pure function, unit-testable without DB.

**Steps 3–7 — `walk_branch(rule, ctx, evaluated)`** (the decision tree — §IX):

```
walk_branch(rule, ctx, evaluated, visited=set()):
  if rule.pk in visited or len(visited) > 20: return None                  # cycle/depth guard (defense
  visited.add(rule.pk)                                                     # in depth behind save-time check)

  candidate_orgs = resolve_outcome(rule, ctx, evaluated)                   # steps 4–6, ordered list
  for org in candidate_orgs:
    if org.status != "active": log-and-continue
    if org_over_cap(org):                                                  # step 5 — monthly threshold
      evaluated.append({org: org.pk, verdict: "over_cap"}); continue       #   sets exhaustion_reason
    account = select_account(org, ctx, commit=True)                        # step 6/7 — health + method layer
    if account:
      evaluated.append({rule: rule.pk, verdict: "match", org: org.pk})
      if rule is via percent_split: increment its counter HERE (same txn)  # step 7b
      return (account, over_cap=False, reason="rule_match" if depth==0 else "fallback_chain")
    evaluated.append({org: org.pk, verdict: "no_eligible_account"})

  if rule.fallback_rule and it .is_active:                                 # explicit chain (§IX)
    evaluated.append({rule: rule.pk, verdict: "fell_through"})
    return walk_branch(rule.fallback_rule, ctx, evaluated, visited)
  return None                                                              # → default org in caller
```

**Step 4 — `resolve_outcome`:**
- `outcome_organization` set ⇒ `[that org]`.
- `outcome_org_pool` set ⇒ apply the pool's active stage-2 rule (lowest `priority_within_stage`):
  - **`percent_split` (AC-131):** `SELECT … FOR UPDATE` all `ProcessorSplitCounter` rows for
    `(pool, period)` (get-or-create missing members' rows first, `ON CONFLICT DO NOTHING` then
    re-select). `total = Σ count`. For each member: `deficit = target_percent/100 − count/total`
    (`total == 0` ⇒ deficit = target_percent). Order candidates by deficit DESC, position ASC.
    The counter of the org actually chosen is incremented **in the same transaction, only after
    `select_account` succeeds** (step 7b) — an org skipped for health/cap must not consume its
    slot, or the split drifts. Upsell charges never increment (ADR-007 §4).
  - **`threshold_switch`:** members ordered by `position`; candidate list is that order;
    `org_over_cap` filtering in the loop does the switching (first org under its cap wins).
- Stage-2 rule missing/inactive for a referenced pool ⇒ treat as no candidates (falls through;
  the save lint warns about pools without allocation rules).

**Step 5 — `org_over_cap(org)`:** `monthly_threshold` NULL ⇒ never over cap. Else compare
`current_month_volume` (after lazy month rollover, §1.1) `>= monthly_threshold`. Volume
*increment* happens at authorization confirmation via `F()` update — routing only reads.

**Step 6 — `select_account(org, ctx)` (method layer, ADR-006 §6):**

```
options = ProcessorOption rows for PaymentMethod(method_family), joined to accounts of org
eligible = [o.account for o in options
            if account.is_active AND health_status != RED                  # AC-132 fall-through
            AND account.is_test_mode == ctx.is_test
            AND (supported_countries empty OR ctx.buyer_country in it)]
if empty: return None
per method.strategy:
  backup_chain:      primaries = [a for a in eligible if not a.backup_only]
                     pool = primaries or eligible                          # backups only when no primary
                     sort by (may_be_primary DESC, health GREEN<YELLOW/UNKNOWN,
                              ProcessorOption.position, priority_within_family, pk)
                     return pool[0]
  alternate_evenly:  lock (payment_method, account, period) counters FOR UPDATE   # AC-133
                     among eligible pick lowest count (tie: option position);     # blocked accounts are
                     increment chosen counter in-txn; return it                   # simply absent → skip, wrap
```

**Step 8 — default org:** same `select_account`; the default org bypasses `org_over_cap` by
construction (`monthly_threshold` NULL enforced). Failure here ⇒ `reason="routing_failure"`,
return `None`; checkout rejects server-side (AC-134 edge case).

**Step 9 — `evaluated_rules_json` entry format (closed vocabulary):**
`{"rule": <pk>|null, "stage": 1|2, "org": <pk>|null, "account": <pk>|null,
"verdict": "no_match"|"match"|"over_cap"|"no_eligible_account"|"unhealthy"|"fell_through"}` —
answers "why did this order go to processor B" without reproduction (§IX). No buyer PII.

**Concurrency summary:** one `transaction.atomic()` per decision; all counter reads/increments
under `SELECT … FOR UPDATE`; org volume via `F()`; period rollover via new-period rows
(counters) and conditional UPDATE (org volume). AC-131's "concurrent orders must not select from
the same snapshot" is satisfied by the row locks.

---

## 3. Save-time lint — "broad shadows narrow" (non-blocking) + blocking checks

Implemented in `OrganizationRule.clean()` + a `pre_save` signal (so API/bulk/AI writes are also
covered — §XII lesson on signal-level enforcement). Live-on-save means lints warn, they do not
version.

**Blocking (`ValidationError`):** stage/outcome shape (mirrors the DB CheckConstraints, §1.3);
fallback cycle (visited-set walk); stage-2 fallback must be stage-2; conditions vocabulary (§1.8);
pool percent sum = 100 when an active `percent_split` rule exists; `ProcessorOption`
family mismatch; default org with non-NULL `monthly_threshold`.

**Non-blocking shadow warnings** (returned to the super-admin UI as warnings; also recomputed and
displayed as a flag on the rule list — Part E's broad-shadows-narrow paradox):

For the saved stage-1 rule R, against every other active stage-1 rule E with
`(priority_within_stage, pk) < R's`:
1. `countries(E) ⊇ countries(R)` after area expansion (catch-all ⊇ everything), AND
2. `currencies(E)` empty or ⊇ `currencies(R)`, AND
3. amount interval of E ⊇ amount interval of R
⇒ warn `"Rule R is unreachable: shadowed by broader rule E (priority p)"`. Symmetrically, when R
itself is the broad one, warn that it shadows each narrower later… earlier-priority rule it covers
when R precedes them. Also warn (not block): stage-1 outcome pool has no active stage-2 rule;
rule targets an org whose every account is RED/blocked (§XV-1 — visible before an order hits it).

---

## 4. Migration strategy

No production data exists — clean drop and recreate, one migration set, no data migration:

1. `payments`: `DeleteModel RoutingRule`; `DeleteModel DecisionLog` + `CreateModel DecisionLog`
   (revised §1.7 — recreate is simpler and safer than eight `AlterField`s on an empty table);
   `CreateModel` `OrganizationRule`, `OrgPool`, `OrgPoolMember`, `ProcessorSplitCounter`,
   `PaymentMethod`, `ProcessorOption`; `AddField` × ProcessorAccount additions (§1.2, all
   defaulted/nullable).
2. `stores`: `AddField` × Organization additions (§1.1, all defaulted/nullable) + CheckConstraint
   (default ⇒ `monthly_threshold IS NULL`).
3. Data seed (in migration or fixture): the three `PaymentMethod` rows (`card`, `paypal`,
   `crypto` disabled) so `ProcessorOption`s have targets on day 1.
4. Delete the now-dead code paths: `_pick_account`, `_rule_matches`, `RoutingRule` admin
   registration, and their tests (replaced per T020R's test list).
5. Zero-rule behavior must keep working: with no `OrganizationRule` rows, every order routes to
   the default org (existing dev environments keep functioning after migrating).

---

## 5. Implementation tickets

### TICKET-017R: Rebuild control-plane models (replaces the model half of TICKET-017)
**Phase:** 1 · **Priority:** P1 · **Depends on:** TICKET-002 · **Complexity:** L
**Scope:**
- Migrations + models exactly as §1 (Organization additions, ProcessorAccount additions incl.
  health fields, OrganizationRule, OrgPool/OrgPoolMember, ProcessorSplitCounter,
  PaymentMethod/ProcessorOption, DecisionLog recreate; §4 steps 1–3, 5).
- All DB constraints and `clean()` validations of §1.3–§1.6, §1.8; save-time lint of §3
  (blocking + shadow warnings surfaced in `/superadmin/` forms and list flag).
- `/superadmin/` admin screens for all new models (rule list shows stage, priority, outcome,
  shadow flag; DecisionLog read-only).
- Update `EXEMPT_MODELS` compliance list.
**Acceptance criteria:** AC-130 (schema part), AC-203; model unit tests: constraint XOR shapes,
cycle rejection, percent-sum lint, shadow lint (broad-before-narrow warns, narrow-before-broad
does not), vocabulary rejection of unknown tokens, default-org cap constraint, health machine
transitions, `__repr__` credential redaction (regression, existing).
**PENDING carried:** health source (both supported via `health_source`), threshold accounting
(§1.1 ASSUMPTION), DecisionLog retention.

### TICKET-020R: Routing evaluator + counters + health probe + DecisionLog (replaces TICKET-020)
**Phase:** 1 · **Priority:** P1 · **Depends on:** TICKET-017R, TICKET-018, TICKET-019 · **Complexity:** L
**Scope:**
- Rewrite `payments/routing.py` per §2: `route_payment`, `preview_route`, `conditions_match`,
  `walk_branch`, `resolve_outcome`, `select_account`, `org_over_cap` — single transaction,
  `SELECT FOR UPDATE` counters, order pinning (ADR-007 §4 fields on Order if not present).
- Volume accrual hook at authorization confirmation (`F()` increment + month rollover).
- Celery beat health-probe job (§1.2.1) using the connector `health_probe()` from T018/T019;
  staleness alert.
- Checkout integration: `preview_route` drives method visibility (`hide_when_no_route`, AC-134)
  and server-side rejection on placement.
**Acceptance criteria:** AC-130, AC-131, AC-132, AC-133, AC-134. Required tests beyond ACs:
counter increments only after successful account selection (split-drift guard, §2 step 7b);
concurrent split routing (two threads, one counter — locks serialize); period rollover starts new
counter rows; fallback-chain walk with cycle guard; `all_orgs_at_cap` ⇒ default org +
`over_cap=True`; exactly one DecisionLog row per call incl. failure paths; `evaluated_rules_json`
contains the full walk; `preview_route` writes nothing; upsell child log (`parent_decision`,
no counter increment); empty `buyer_country` matches only catch-all.
**PENDING carried:** org-split × method-strategy composition rule (Part E CRITICAL — current
composition: stage 1+2 pick org, method strategy picks account within org; two counters stay
independent), health source.
