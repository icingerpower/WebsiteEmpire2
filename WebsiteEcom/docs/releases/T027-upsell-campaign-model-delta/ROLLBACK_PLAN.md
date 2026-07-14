# Rollback Plan — TICKET-027: Up-sell Campaign Model Delta (ADR-009)

---

## Is rollback safe?

Yes. Migration 0002 is entirely additive. No columns were dropped or renamed.
All new tables (CampaignIssuedCode, CampaignStepTranslation) and new columns
(Campaign.owner_scope, Campaign.entry_step, CampaignSession.campaign_type) can be
removed without affecting pre-T027 rows.

The UniqueConstraint on (CampaignSession.order, CampaignSession.campaign_type)
is the only constraint that could block rollback — but this constraint was added AFTER
the backfill, and the backfill is non-destructive.

---

## Rollback steps

### Step 1 — Revert code

```
git revert <T027 commits> --no-edit
```

Or check out the last commit before T027 began if a clean revert is not possible.
The key pre-T027 commit is the state before the 0002 migration and the campaigns
app changes landed.

### Step 2 — Reverse the migration

```
python3 manage.py migrate campaigns 0001
```

Django will run the reverse operations for 0002:
- Drop UniqueConstraint on (CampaignSession.order, CampaignSession.campaign_type)
- RunPython reverse: noop_reverse (intentional — clearing campaign_type is harmless)
- Drop CampaignStepTranslation table
- Drop CampaignIssuedCode table and its UniqueConstraint
- Drop CampaignSession.campaign_type field
- Drop Campaign.entry_step field
- Drop Campaign.owner_scope field

All pre-T027 Campaign, CampaignStep, CampaignSession data is preserved.

### Step 3 — Verify

```
python3 manage.py migrate --check
python3 manage.py test campaigns
```

Expected: 31 tests passing (baseline test_campaigns.py only; T027 test files will
not exist on the reverted branch).

---

## What is NOT recoverable after rollback

- Any CampaignIssuedCode rows written during the T027 window (attribution data only —
  the underlying DiscountCode rows in the discounts app remain intact and redeemable).
- Any CampaignStepTranslation rows (translated content is lost; re-running the AiJob
  CLI would regenerate them after re-deploy).
- campaign_type values on CampaignSession rows (the column is dropped; the canonical
  value remains on the Campaign row).

None of these losses affect order processing, payment, or customer-facing state,
since T028 (the accept/charge handler) has not shipped.

---

## Risk rating

LOW. T027 is pure model/admin code. No payment code, no public endpoints, no
customer-facing storefront. A rollback before T028 ships causes zero customer impact.
