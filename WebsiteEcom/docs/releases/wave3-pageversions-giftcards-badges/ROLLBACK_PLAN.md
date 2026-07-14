# Rollback Plan — Wave 3: Page Versions, Gift-Card Campaigns, Security Badges + Isolation-Hardening Chain

**Date:** 2026-07-11

This release bundles two very different kinds of change, and they must be rolled back
differently:

1. **Three additive product features** (page versions, gift-card campaigns, security badges) —
   each independently rollback-able, standard additive-migration levers, low blast radius.
2. **The isolation-hardening chain** (TICKET-050/051/052/053) — a correctness fix to a
   cross-cutting core mechanism (`core/managers.py`, `core/admin.py`, `core/formsets.py`,
   `core/models.py`) used by every `StoreOwnedModel` in the codebase, not a new feature.
   **Rolling this back is explicitly NOT recommended** — see the dedicated section below before
   considering it.

---

## Part 1 — Page versions (TICKET-042 / ADR-028)

### What was added
- New tables (additive only): `catalog.ProductPageVersion`, `catalog.ProductPageVersionImage`
  (`catalog/migrations/0013_productpageversion_productpageversionimage.py`).
- `catalog/migrations/0014_alter_product_tags.py` — unrelated, folded-in `blank=True` change
  (see Part 4, isolation chain D5) — reversible, validation-only, no column type change.
- New admin: `ProductPageVersionAdmin` + inline image admin.
- Touched existing surfaces: `cart/views.py` (`_validate_page_version_id`),
  `permalinks/registry.py` (redirect creation/rename cascade), `sitemaps/sitemaps.py`
  (content-type exclusion), `storefront/views.py` (canonical/og/JSON-LD branching on
  `page_version`), `analytics/ingest.py`/`aggregate_metrics.py` (also touched by W3-2, see
  Part 4), `permalinks_tags.py` (`_resolve_canonical_target_url`, `hreflang_tags` exemption
  check).
- Soft `page_version_id` int columns added to `Event.properties` (JSON, no schema change),
  `CartItem`, `OrderItem` — never joined for money display, never a FK.

### Rollback levers, in order of safety/speed

**Lever 1 (fastest, no deploy): deactivate all versions.** Set every
`ProductPageVersion.is_active=False` via bulk admin action. Verified effect: every version URL
302-redirects to its primary (per ADR-028's own reversible-deactivation design) — reversible in
the other direction just as fast.

**Lever 2 (full rollback):**
1. `python3 manage.py migrate catalog 0012_fix_stocknotification_index_name_length` — drops
   `ProductPageVersion`/`ProductPageVersionImage`. **Confirm `showmigrations catalog` on the
   actual deployed environment first** — do not skip past 0014 (the `tags` field) without
   checking whether it should be retained (see Part 4 — it is an independent fix, not part of
   this feature, and reverting it un-does the CsvListWidget corruption fix).
2. Revert `storefront/views.py`, `cart/views.py`, `permalinks/registry.py`,
   `sitemaps/sitemaps.py`, `permalinks_tags.py` page-version branches via git. Every branch
   added is additive (an `if page_version is not None:`/`if request.seo_canonical_path...:`
   guard) — the "attribute absent" / non-version code path is byte-identical to pre-ticket
   behavior per the SEO Agent's own verification, so a partial revert that misses one file is
   low-risk (worst case: a version page behaves like a primary page).
3. Existing `SlugRedirect` rows created by version deactivation/deletion are not automatically
   cleaned up — harmless to leave (they 302/301 to the primary, which will 404 once the version
   feature and its Permalink rows are gone unless the primary Permalink itself is untouched,
   which it is).

### Data
No financial state is touched by any rollback lever — page versions never affect price, SKU,
or checkout. `OrderItem.page_version_id`/`CartItem.page_version_id` are soft ints with no FK;
dropping the source tables does not orphan or corrupt any order/cart row.

---

## Part 2 — Gift-card campaigns (TICKET-045 / ADR-029)

### What was added
- New table `discounts.GiftCardCampaign`; new nullable/defaulted fields
  `DiscountCode.refund_flagged`/`refund_flagged_reason`
  (`discounts/migrations/0005_gift_card_campaigns.py`, `0006_campaignreward_campaign_id_pattern_index.py`
  — index only).
- New Celery task path (`_enqueue_gift_card_campaigns` → `issue_campaign_reward_for_order`),
  hooked into both processors' PAID transitions via `transaction.on_commit`.
- Touched existing surfaces: `payments/webhook_views.py` and `payments/paypal_webhook_views.py`
  (the W3-1 REFUNDED-transition whitelist — see Part 4 note: this fix should be kept even if
  the campaign feature itself is rolled back, since it closes a real money-liability bug
  independent of whether campaigns exist) and `discounts/admin.py` (new admin, new fieldset
  help text).

### Rollback levers, in order of safety/speed

**Lever 1 (fastest, no deploy): disable every campaign.** Set every
`GiftCardCampaign.status='archived'` (or the equivalent disable state) via bulk admin action.
Verified effect: `issue_campaign_reward_for_order`'s publish re-check (already inside the row
lock) refuses to issue for a non-published campaign — no code path issues a card for an
archived campaign.

**Lever 2 (full rollback):**
1. `python3 manage.py migrate discounts 0004_alter_discountcode_times_used` — drops
   `GiftCardCampaign`; the `refund_flagged`/`refund_flagged_reason` columns on `DiscountCode`
   also roll back with this step (they are part of the same migration file). **Caution:** if any
   `DiscountCode` row has `refund_flagged=True` in production, that audit signal is lost on
   rollback — export first if retention is needed (same caution pattern as prior releases'
   consent/lead-signup rollback notes).
2. Revert `discounts/service.py` issuance/idempotency code and the webhook enqueue call sites
   via git. **Do not revert the W3-1 REFUNDED-transition whitelist itself** (see above) — that
   fix protects order-state correctness regardless of whether gift-card campaigns exist; keep
   it even in a full campaign-feature rollback.
3. Already-issued `DiscountCode` rows (the actual gift cards) are **not** cleaned up by this
   rollback — they are ordinary, already-redeemable discount codes in the pre-existing
   `discounts` app and remain valid/redeemable. Decide separately (Product decision, not code)
   whether to deactivate them.

### Data
No refund/void logic is undone by a code-level rollback — cards already voided/flagged for
refund stay in that state. `CampaignReward` rows (the idempotency ledger) are dropped with the
table; this is safe since they are audit/dedup rows, not money state themselves.

---

## Part 3 — Security badges (TICKET-046 / ADR-030)

### What was added
- New app `badges/` (`badges/migrations/0001_initial.py`), `badge_presets.py` registry,
  `SecurityBadgeSlotProvider`.
- Touched existing surface: `cart.html` template gains one line wiring `slot.security_badge`
  into the cart page (the slot itself and its rendering mechanism already existed and were
  already reviewed for product/checkout — this release only adds the cart call site).

### Rollback levers, in order of safety/speed

**Lever 1 (fastest, no deploy): disable every badge.** Set every `SecurityBadge.is_active=False`
via bulk admin action, or remove `"badges"`'s provider registration. Verified effect: the slot
mechanism (per the existing, already-reviewed Pixels-precedent isolation) renders its empty
wrapper cleanly with the provider unregistered or with no active rows.

**Lever 2 (full rollback):**
1. `python3 manage.py migrate badges zero` — drops the `SecurityBadge` table. Purely additive;
   no other app's table is touched.
2. Remove `"badges"` from `INSTALLED_APPS`; revert the `badges/` directory and the one-line
   `cart.html` addition via git.
3. Any store-uploaded custom badge images under `MEDIA_ROOT` are not deleted by this rollback —
   harmless to leave (see `KNOWN_RISKS.md` W3-10 for the pending `nosniff`/extension-allowlist
   hardening on this same upload path, independent of rollback).

### Data
No financial or order state — badges are purely decorative/trust-signal UI.

---

## Part 4 — Isolation-hardening chain (TICKET-050/051/052/053, ADR-031 + 3 addenda, ADR-032)

### DO NOT ROLL THIS BACK INDEPENDENTLY — read this section before touching any of it

This chain is not a feature; it is a correctness fix to the store-isolation guard
(`StoreScopedManager`/`_RaisingQuerySet` in `core/managers.py`) that **every** `StoreOwnedModel`
in the codebase relies on. Rolling any part of it back re-opens a hole that was proven, in
code, to exist before the fix:

- **Rolling back TICKET-051's terminal-method closure re-opens the silent cross-store write.**
  Before this fix, `SomeStoreOwnedModel.objects.filter(...).update(...)` executed the SQL
  directly against **every store's matching rows**, not just the caller's store, with no
  exception raised — a purely silent failure mode (the exact opposite of this codebase's
  stated isolation invariant, ADR-001 §4, "the unscoped query IS the loud failure"). This is
  not a theoretical risk described in a design doc; it was independently re-confirmed in code
  by this release pass (`core/managers.py::_RaisingQuerySet.update()` raises unconditionally
  today) and is proven closed by `BUG_TESTS.csv` row `TERMINAL-METHODS-ISOLATION` (STATUS=PROVEN).
- **Rolling back TICKET-052's validation window breaks `full_clean()`/admin validation on every
  `StoreOwnedModel` with a unique constraint** — this was the tree-red state TICKET-052 was
  written to fix (27/28 test failures, one root cause). Rolling back to before this fix does not
  return to a "known-good" state; it returns to a state where the admin change/add forms 500 on
  any duplicate-value submission across the codebase.
- **Rolling back TICKET-053 (ADR-032) reopens the raw-`IntegrityError`-500 gap** on every
  store-site admin form that follows the "store excluded from the form, set in `save_model`"
  convention — this is described in the ADR as "repo-wide," not scoped to one model.
- **Rolling back TICKET-050 (ADR-031) breaks `model_to_dict`/`save_m2m` on every M2M field
  between two `StoreOwnedModel`s** — this reopens the exact admin change-view crash
  (`DiscountCode`/`GiftCardCampaign`) the chain started as a hotfix to fix.

**If a production incident is traced to this chain, the correct response is a forward fix, not
a rollback** — file it the same way TICKET-051/052/053 were filed (each one surfaced from the
previous fix's own test suite going red, and was fixed forward within the same release cycle
rather than reverted). If a genuine emergency full-application rollback of this entire release
is required (e.g. an unrelated catastrophic issue), the isolation chain must be rolled back
**together with**, never independently of, the three product features above, and the resulting
build must re-run the full suite before being considered a safe rollback target — a partial
revert (e.g. keeping TICKET-053 but reverting TICKET-052) is a "torn" configuration that was
never tested and is very likely to reintroduce a red suite (TICKET-052's own history shows
this exact torn state produces 27/28 failures).

### What was added / changed, for completeness
- `core/managers.py` — `_RaisingQuerySet` terminal-method closure, `_MODEL_VALIDATION_WINDOW`
  contextvar, `StoreScopedManager.none()`.
- `core/m2m_guard.py` (new) — `m2m_changed` pre_add cross-store guard.
- `core/formsets.py` (new) — `StoreSafePKFormSetMixin`, `StoreSafeInlineFormSet`,
  `StoreSafeModelFormSet`.
- `core/models.py` — `StoreOwnedModel.validate_unique`/`validate_constraints` window wrappers.
- `core/admin.py` — `StoreScopedFormFieldsMixin`, `StoreUniqueValidationFormMixin`,
  `StoreOwnedInlineMixin`.
- `core/widgets.py` — `CsvListFormField` (D5, folded in; independent of the isolation
  mechanism itself but shipped in the same ticket).
- 21 existing inlines migrated onto the new formset base classes (per TICKET-051's
  description) — `discounts/admin.py`, `campaigns/admin.py`, `catalog/admin.py`,
  `currency/admin.py`, and others.
- `catalog/migrations/0014_alter_product_tags.py` — `Product.tags` `blank=True` (D5).
  `Organization.coverage_areas_json` carries the equivalent change in `stores/migrations/`.

### Migrations
All isolation-chain migrations are validation-only (`blank=True`, index additions) — no table
was dropped or column-type-narrowed. A rollback of the chain's *code* does not strictly require
a migration rollback; reverting `blank=True` is optional and low-risk either direction.

### Post-rollback smoke check (if a full-release rollback is ever executed)
```bash
DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py test
```
Must return to green before any rolled-back state is considered deployable — per the reasoning
above, a torn isolation-chain state is expected to fail loudly, not silently, which is by
design; do not proceed on a red suite.

---

## Rollback procedure summary (all parts)

1. **Prefer Lever 1** (per-feature disable) for page versions, gift cards, and badges if the
   problem is isolated to one of the three product features.
2. **Never** attempt a standalone rollback of the isolation chain (Part 4) outside of a full
   wave-3 rollback, and never a partial rollback within the chain itself.
3. If a full wave-3 rollback is required: revert all four areas together (Parts 1–4), migrate
   each app back to its pre-wave-3 state in dependency order (`catalog` → `discounts` →
   `badges`; the isolation-chain code changes have no migration to reverse beyond `blank=True`),
   then re-run the full suite before considering the rollback complete.
4. Keep the W3-1 REFUND-transition whitelist (`payments/webhook_views.py`,
   `payments/paypal_webhook_views.py`) even under a gift-card-campaign-only rollback — it is an
   independent money-correctness fix, not part of the campaign feature itself.

## What is NOT rolled back by this plan
- Gift cards already issued through the campaign mechanism — ordinary, already-redeemable
  `DiscountCode` rows in the pre-existing `discounts` app; a Product decision, not a code
  concern, governs whether to deactivate them separately.
- Any `refund_flagged` audit signal already recorded before rollback (export first if needed).
- Store-uploaded custom badge images already on disk.
- The parent repository's `.gitignore` bare `tests` pattern issue (see `KNOWN_RISKS.md`) — this
  is unrelated to any rollback lever above and must be fixed independently, by a human, before
  WebsiteEcom is ever `git init`'d as its own repository.
