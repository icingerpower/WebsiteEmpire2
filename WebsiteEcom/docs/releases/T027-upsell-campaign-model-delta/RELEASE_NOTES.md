# Release Notes — TICKET-027: Up-sell Campaign Model Delta (ADR-009)

Release date: 2026-07-04
Scope: campaigns app — model delta, migration, admin, validators, service, AiJob trigger

---

## What changed

### New model fields

**Campaign**
- `owner_scope` (CharField, choices store/platform, default 'store'): precedence discriminator.
  Platform (super-admin) campaigns are `owner_scope='platform'` rows pinned to a store; store-admin
  campaigns shadow them at evaluation time.
- `entry_step` (nullable FK to CampaignStep, SET_NULL): explicit funnel entry point. Never derived
  from position. Required to activate funnel-type campaigns.

**CampaignSession**
- `campaign_type` (CharField, denormalized from Campaign at session creation): enables the DB-level
  UniqueConstraint on (order, campaign_type), enforcing the ADR-007 §1 invariant at the persistence
  boundary. Backfilled from existing sessions in migration 0002.

### New models

**CampaignIssuedCode**: attribution and idempotency link from (campaign_session, step) to a
DiscountCode. Unique per (campaign_session, step) — the accept handler creates DiscountCode +
CampaignIssuedCode in one transaction; a retry returns the already-issued code rather than creating
a duplicate. (The accept transaction itself is TICKET-028 scope.)

**CampaignStepTranslation**: per-language translated offer content (title, description, cta_label)
for a CampaignStep. Status: draft/published. ai_job FK nullable (SET_NULL) — deleting an AiJob row
never deletes a published translation. Unique per (store, step, lang_code). clean() rejects a store
that differs from step.campaign.store.

### Migration

`campaigns/migrations/0002_campaign_model_delta.py` — fully additive:
- AddField × 3, CreateModel × 2, AddConstraint × 2, AlterUniqueTogether × 1
- RunPython backfill of campaign_type before UniqueConstraint addition
- No columns dropped or modified

### Validators (`campaigns/validators.py`)

`validate_offer_config(offer_type, config)`: per-archetype JSON schema enforcement at the
persistence boundary. Covers related_products, buy_x_get_x (no cap field — DECIDED),
storewide_discount (discount_pct or discount_fixed, expires_in_days default 30), one_click_funnel,
order_bump.

`validate_campaign_activation(campaign)`: DFS cycle detection through the CampaignStep branch
graph; rejects cross-campaign branch FKs; requires entry_step for funnel types; rejects
abandoned_checkout with CampaignStep rows.

### Service (`campaigns/service.py`)

`evaluate_eligibility`: single resolution function (§XV-4). Case annotation orders
store-scope (precedence=0) before platform-scope (precedence=1); deterministic tie-break by
created_at. Funnel-type campaigns with NULL entry_step are excluded (B3 fix).

`create_session`: writes the denormalized campaign_type; enforces store consistency guard.

### Admin (`campaigns/admin.py`)

Three read-only surfaces for store-admins facing platform campaigns:
- CampaignAdmin: has_change_permission / has_delete_permission return False for platform rows;
  owner_scope forced to 'store' in save_model; activation gate errors surfaced.
- CampaignStepInline: has_change_permission / has_delete_permission False when parent campaign
  is platform-scoped (guard: obj.owner_scope == PLATFORM, not hasattr — B2 fix).
- CampaignStepAdmin: has_change_permission / has_delete_permission False when
  obj.campaign.owner_scope == 'platform' (B1 fix).
- formfield_for_foreignkey overrides for funnel FKs: store-scoped queryset prevents
  IsolationError 500.
- CampaignStepTranslationInline: visible in CampaignStepAdmin.
- CampaignIssuedCodeAdmin: view-only (audit table).

### AiJob trigger (`campaigns/signals.py`, `campaigns/ai_jobs.py`)

post_save on CampaignStep: when offer_config_json contains any translatable field (title /
description / cta_label), creates one AiJob per configured StoreLanguage that lacks a published
CampaignStepTranslation. Non-terminal dedup guard prevents duplicate pending jobs (B5 fix: no
try/except around create_job — exceptions propagate).

Job type `campaign_step_translation` registered in the aijobs plugin registry at priority 100.
build_prompt / persist_output are NotImplementedError stubs pending the AI job orchestration phase.

---

## Bugs fixed in this ticket (all regression-proven)

| ID | Description |
|---|---|
| T027-B1 | CampaignStepAdmin had no change/delete permission guard for platform steps |
| T027-B2 | CampaignStepInline used hasattr guard (always False) instead of owner_scope check |
| T027-B3 | evaluate_eligibility returned funnel campaigns with NULL entry_step |
| T027-B4 | CampaignStep.clean() did not re-run validate_campaign_activation for active campaigns |
| T027-B5 | create_job exceptions in the signal handler were silently swallowed |
| T027-B6 | create_job with unknown type silently wrote a junk row at priority 0 |

---

## Deferred items (not blockers)

- HTTP-level 403 test for store-admin POST to platform campaign URL (model-level proof exists;
  full Django test-client integration deferred).
- build_prompt / persist_output for campaign_step_translation job type (AI job orchestration
  phase).
- A/B variant canonical sitemap exclusion (T029).
- CampaignSessionToken, accept/decline handler, DiscountCode lifecycle (T028).

---

## Tests

81 tests passing. 0 failures.
- test_campaigns_ticket027.py: 34 methods (ADR-009 required scenarios)
- test_campaigns_t027_regression.py: 16 methods (6 bugs × proven regression tests)
- test_campaigns.py: 31 methods (baseline)
