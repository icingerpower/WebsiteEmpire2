# ADR-029: Automated gift-card campaigns (TICKET-045)

**Status:** ACCEPTED (human, 2026-07-11) — D6 (jurisdictional expiry: 5-year
French statutory floor with configurable validity, enforced by the campaign
form based on the store's markets) and D9 (refund voiding policy: deactivate
unspent code / flag spent code for manual review) sign-off approved as
recommended; D3's v1 scope (fixed-value only) is approved as designed — the
two reserved, non-fixed value modes (`percent_of_order`,
`percent_discount_coupon`) remain individually PENDING (human) as documented
inline in D3 and do not block this ADR or TICKET-045. This ADR also resolves
the 11:#3 remainder's "what auto-issued the 765 uniform $5 cards" mystery (see
D1 note below and `specs/ecommerce_engine/11_uncertainties_to_validate.md`).
**Tickets:** TICKET-045 (single Developer ticket — see "Ticket scope" at the end).
**Related:** ADR-002 §1/§4/§5 (unified `DiscountCode`, atomic counters,
`CampaignReward` idempotency), ADR-007 (payment webhooks / PAID transition),
ADR-010 Q5 (post-PAID hook pattern — nested savepoint, never a signal),
ADR-014/ADR-021 (translation pipeline), ADR-023 (currency: charge currency is the
store's; conversions are display-only), ADR-027 D2 (campaign-shaped model outside
the funnel machinery — the closest structural precedent).
**Spec sources:** 04 AF-110; `screen_inventory_parts/part_D_super_admin_jpg.md`
(super-admin-12 list + edit); `screen_inventory_parts/part_C_admin_013-021.md`
(admin-020 issued-cards list / issue dialog / filter); 02 feature matrix
"Automated gift card campaigns" + "Source of bulk auto-issued cards";
11:#3 remainder; design-pattern-ideas §XIII (idempotent campaign triggers),
§XV-3/-5/-6.

This ADR closes the 11:#3 / Part-C CRITICAL mystery **"what auto-issued the
765 × $5 cards"**: the legacy platform's automated gift-card campaign
("5 USD after purchase", 744 issued / 10 used / 1 outstanding on the
super-admin-12 list screen) is the issuer. This feature is its replacement,
with the idempotency guard the legacy platform lacked (the "765×$5 incident
guard" named in TICKET-045).

---

## Decision

An automated gift-card campaign is a **store-scoped configuration row**
(`GiftCardCampaign`, new model in the existing `discounts` app) that, when
**published**, issues one `gift_card_auto` `DiscountCode` per qualifying PAID
order and emails the code to the buyer. Issuance is a Celery task enqueued
`transaction.on_commit` after the PAID transition, made idempotent by
`discounts.CampaignReward` with `campaign_id = f"gift_card_campaign:{campaign.pk}:order:{order.pk}"`,
and serialized per campaign by `SELECT FOR UPDATE` on the campaign row
(frequency-cap and budget-cap checks are otherwise check-then-act — §XIII).

### D1 — Model shape: dedicated `GiftCardCampaign` in `discounts`, NOT a new `campaigns.CampaignType`

**Options considered:**

1. **New `CampaignType.GIFT_CARD` on `campaigns.Campaign`** — maximal reuse of
   the campaign list/precedence machinery.
2. **Dedicated `GiftCardCampaign(StoreOwnedModel)` in `discounts/models.py`** —
   the ADR-027 D2 precedent (`engagement.LeadCaptureCampaign`): a
   campaign-shaped model in the app that owns the domain, using the generic
   `CampaignReward` guard for issuance.
3. New `giftcards` app — a third money-adjacent app boundary.

**Chosen option: 2.** The `campaigns` app machinery is funnel-oriented —
`CampaignStep`, `CampaignSession` state machine, capture windows, thank-you
tokens, `entry_step` activation validation. A gift-card campaign has **no
funnel, no session, no storefront UI, no step graph**: it is a server-side
order-event reactor whose entire output is a `DiscountCode` + one email.
Wedging it into `Campaign` would force every campaign validator/service to
special-case a type that shares nothing but the word "campaign", and would drag
`CampaignIssuedCode` (explicitly NOT a reward ledger — 05 §3 warning) into
scope. `discounts` already owns `DiscountCode`, `GiftCardTransaction`,
`CampaignReward`, and the issuance invariants; the entire write-path of this
feature is discount rows. ADR-027 already established that "campaign-shaped
config + `CampaignReward` idempotency" lives with its domain, not in
`campaigns`. Option 3 fails the "do not over-engineer" rule — three models and
one service module do not justify an app.

**`GiftCardCampaign(StoreOwnedModel)` fields:**

| Field | Notes |
|---|---|
| `store` FK | ADR-001 hard rule. See scoping note below. |
| `name` | required (screen: "Campaign name") |
| `status` | `draft` / `published` / `archived` — explicit state (§XV-3), maps to "Save as draft" / "Publish". `archived` replaces deletion once cards were issued (audit trail; `CampaignReward.discount_code` is PROTECT anyway). |
| `owner_scope` | `platform` / `store`, same vocabulary as `campaigns.Campaign`. The super-admin-12 screens and 00_product_positioning place this feature in super-admin, so the UI creates `platform` rows; the field keeps the door open for store-admin campaigns without migration. |
| `trigger_event` | choices, v1 = `order_paid` only (D2) |
| `trigger_scope` | `all_products` / `manual_products` / `conditions` (screen tabs) |
| `trigger_products` M2M → `catalog.Product` | for `manual_products` |
| `trigger_rules_json` | for `conditions` — same incremental vocabulary posture as `Campaign.targeting_rules_json` (Phase 1 ignores it; tab hidden in v1 UI — D2) |
| `value_mode` | `fixed` (v1); `percent_of_order`, `percent_discount_coupon` reserved (D3) |
| `value` | Decimal; fixed face value in store currency for `fixed` |
| `expiry_mode` + `expiry_days` + `expiry_date` | `relative_days` (default **30** — the screen's "1" is a placeholder per part-D LOW note) / `absolute_date` (D6) |
| `cap_enabled`, `cap_count`, `cap_window_value`, `cap_window_unit` (`minutes`/`hours`/`days`) | per-user frequency cap, exactly the screen's "[n] gift cards per [n] [unit]" (D5) |
| `max_issued_cards` | nullable budget cap, `null` = unlimited (D9 — beyond screens, ASSUMPTION) |
| `email_subject`, `email_body` | delivery override; blank = platform `gift_card_campaign` template (D7) |
| `created_at`, `updated_at` | |

`clean()` delegates to `discounts/validators.py:validate_gift_card_campaign()`
(single validation function for all save paths, §XV-5): publishing requires
`value > 0` for `fixed`, a coherent expiry config, `cap_count/window > 0` when
`cap_enabled`, and non-empty `trigger_products` when `trigger_scope=manual_products`.

**Multi-store scoping (MEDIUM, PENDING):** every row carries a store FK; the
super-admin list shows campaigns across stores with a store column/filter.
"Apply to all sub-stores" is, if ever needed, a **bulk-create action**, not an
org-level schema — recommended and assumed for v1.

**New nullable FK `DiscountCode.gift_card_campaign`** (`SET_NULL`,
`related_name='issued_codes'`): restores ADR-002 §1's original "`campaign` FK
nullable — link to the auto-issuing campaign for `giftcard_auto`" intent that
the shipped model dropped. Required for the list-screen counters (D8) — counting
via `CampaignReward.campaign_id` string prefixes would be un-indexed LIKE
queries. `provenance=CAMPAIGN` stays the coarse audit field; the FK is the
precise one.

### D2 — Trigger vocabulary: `order_paid` only in v1, extensible by choices + service dispatch

**Options considered:** (1) full registry/plugin pattern per trigger;
(2) `TextChoices` + an explicit dispatch map in `discounts/service.py`;
(3) hardcode post-purchase with no vocabulary.

**Chosen option: 2.** The mandated screens show exactly one trigger family:
gift card issued automatically **after purchase**, scoped by product
(all / manual / conditions). One concrete trigger does not justify a registry
(hard rule: no over-engineering); zero vocabulary (option 3) would force a
migration to add the known future triggers. `trigger_event` choices ship with
`order_paid`; `nth_order`, `spend_threshold`, `win_back` are documented reserved
values. Each future trigger declares its **idempotency axis** as the rule for
minting `CampaignReward.campaign_id` (D4): time-based triggers (`win_back`) are
evaluated by a beat task and encode a window bucket
(`…:{campaign.pk}:winback:{email}:{bucket}`); `order_paid` encodes the order PK.
The "Products Based on Conditions" tab is **hidden in v1** (rules vocabulary is
the same PENDING as `Campaign.targeting_rules_json` — 11 Part C); `all_products`
and `manual_products` ship.

### D3 — Value modes: fixed amount only in v1; store currency

**Options considered per the screen's radio group:** "Set value" ($ fixed) /
"Percentage of order total" / "Set % discount off order".

**Chosen option: `fixed` only in v1** — this matches the only observed
production usage (765 uniform $5 cards) and is the only mode with unambiguous
semantics under the stored-value model.

- `percent_of_order` — **PENDING (human):** computable at issuance
  (initial_balance = pct × order total) but pre-/post-shipping/discount base is
  unspecified (AF-110 PENDING). Recommended future semantics: percentage of the
  **items subtotal after coupon, before shipping**, floor-rounded to cents.
- `percent_discount_coupon` ("Set % discount off order") — **PENDING (human,
  CRITICAL in part-D):** this is semantically **not a gift card** — it is a
  percentage coupon delivered through the gift-card campaign channel. If
  approved it must issue a `DiscountCode(discount_type=COUPON,
  value_type=PERCENTAGE)` with `provenance=CAMPAIGN` and **no**
  `GiftCardTransaction` ledger involvement — never a pseudo-stored-value card
  (kept out of v1 types per ADR-002 §1 PENDING). The enum value is reserved so
  the form radio can appear later without migration.

**Currency: the store's currency, always.** ADR-023 fixed that charge/value
currency is the store's and all other currencies are display-only conversions.
At issuance the code copies the store's currency into `DiscountCode.currency`
(never empty for gift cards — a card is a monetary liability in one currency).
No cross-currency redemption in v1.

### D4 — Issuance mechanics: post-PAID Celery task, `CampaignReward` idempotency, campaign-row lock

**Trigger point.** Options: (a) inline in the webhook PAID handlers;
(b) Django signal; (c) Celery task enqueued via `transaction.on_commit` from the
PAID transition sites. **Chosen: (c).** ADR-010 Q5 / Safety M1 already banned
signals and established the "after PAID commit, non-fatal" posture
(`suppress_abandoned_checkout` in `payments/paypal_webhook_views.py` and the
Stripe equivalent). Unlike suppression, issuance sends email and takes locks —
it does not belong inside the webhook transaction at all. Each PAID transition
site (Stripe `payment_intent.succeeded` handler, PayPal
`_handle_capture_completed`, and any future processor's PAID path — a review
checklist item for TICKET-041) adds:

```python
transaction.on_commit(
    lambda: process_gift_card_campaigns.delay(order.pk)
)
```

`on_commit` guarantees the task never observes an uncommitted (or rolled-back)
PAID row. Both handlers already skip duplicate deliveries before reaching the
PAID transition, so the enqueue fires once per first PAID transition; the task
is idempotent anyway.

**Task algorithm** (`discounts/tasks.py::process_gift_card_campaigns(order_id)`,
`max_retries=3`, idempotent — §XV-6):

1. Load order; assert `payment_status == PAID` (re-check, not trust — the task
   may run late). Resolve recipient email (order email, lowercased — the
   `CampaignReward` docstring normalization rule).
2. Abuse gate (D9): skip entirely if the processor-paid amount is zero
   (order fully covered by gift card/coupon).
3. Select published `GiftCardCampaign` rows for the order's store whose
   trigger scope matches at least one order line (`all_products`, or
   `manual_products` ∩ order products ≠ ∅).
4. Per matching campaign, in one `transaction.atomic()`:
   a. `SELECT FOR UPDATE` the campaign row — serializes steps b–e per campaign
      (§XIII: cap checks are check-then-act without this).
   b. Budget cap: if `max_issued_cards` is set and
      `issued_codes.count() >= max_issued_cards`, skip (and surface in admin —
      D8).
   c. Frequency cap: if `cap_enabled`, count `CampaignReward` rows for
      `(store, recipient_email)` with `campaign_id` startswith
      `f"gift_card_campaign:{campaign.pk}:"` and `issued_at` inside the window;
      at/over `cap_count` ⇒ skip.
   d. **Idempotency guard first** (ADR-002 §5 — reward row before code):
      `CampaignReward.objects.for_store(store).create(campaign_id=
      f"gift_card_campaign:{campaign.pk}:order:{order.pk}",
      recipient_email=email, discount_code=code)` — but since the FK is
      non-nullable, the concrete write order inside the atomic block is:
      create the `DiscountCode`, create the `CampaignReward`; an
      `IntegrityError` on the reward's unique
      `(store, campaign_id, recipient_email)` rolls back **both** — identical
      net effect to "guard before code", matching the shipped
      `engagement.service.issue_reward` idiom (catch `IntegrityError` ⇒ already
      issued ⇒ do nothing, no email).
   e. The `DiscountCode`: `discount_type=GIFT_CARD_AUTO`,
      `provenance=CAMPAIGN`, `gift_card_campaign=campaign`,
      `initial_balance=value`, `currency=store currency`, `ends_at` per D6,
      `code` = 15–16 uppercase hex chars from `secrets` (admin-020 observed
      format; store-unique constraint retries on collision),
      `usage_limit=None` (stored-value cards deplete by balance, not uses).
      **No `GiftCardTransaction` row at issuance** — the shipped ledger defines
      balance as `initial_balance − Σ(amount)`; an "initial balance row" (the
      old ADR-002 §2 phrasing) does not exist in the shipped model and must not
      be invented. The ledger stays spend/refund-only.
5. After the atomic block commits: enqueue the delivery email task (D7) via
   `transaction.on_commit`. Email is at-least-once and outside the money
   transaction; a failed email never rolls back an issued card (the admin
   audit view shows the card; `SentEmail` shows the failure — §XV-1).

**Idempotency axis per trigger (the rule):** `campaign_id` encodes the
campaign PK **plus the natural unit of the trigger** — `order_paid` ⇒ the order
(a customer with two qualifying orders legitimately gets two cards, bounded by
the frequency cap); future `win_back` ⇒ customer + window bucket; future
`nth_order` ⇒ customer + n. Retries of the same unit always collide on the
unique constraint and issue nothing.

### D5 — Frequency cap semantics

Per-recipient-email (guest-checkout-safe, consistent with
`per_email_limit`/`DiscountCodeEmailUse` reasoning in ADR-002 §4), counting
**issued rewards** in a sliding window ending now, evaluated under the D4
campaign-row lock. Default `cap_enabled=False` per the screen (unchecked
checkbox), but the admin form help text recommends enabling it
(e.g. 1 card / 30 days) — the cap is the primary self-gifting brake (D9).
ASSUMPTION: window unit vocabulary minutes/hours/days exactly as on the screen.

### D6 — Expiry

`expiry_mode=relative_days` (default 30) or `absolute_date`; resolved to
`DiscountCode.ends_at` at issuance (relative: issuance time + N days;
absolute: end-of-day UTC of the configured date — issuing after the date fails
publish-time validation… and issuance-time re-check skips with a logged
counter, never issues an already-expired card).

**DECIDED (human, 2026-07-11):** French law effectively lets a customer claim
stored value for **5 years** (prescription commerciale); a 30-day expiry on a
monetary instrument would be unenforceable in France and other jurisdictions
have their own floors. Resolution: a **5-year floor for FR-market stores**,
with **configurable validity otherwise** — the campaign form determines
whether the floor applies from the store's markets (the same
`StoreLanguage`/`ShippingCountry` data the platform already uses elsewhere for
market/targeting determinations) and rejects publishing/issuing a
shorter-than-5-year expiry for a store whose markets include France. Stores
whose markets do not include France keep the configurable
`relative_days`/`absolute_date` expiry as drawn (default ~30 days, matching the
legacy platform's admin-020 behavior). Recorded in
`specs/ecommerce_engine/11_uncertainties_to_validate.md` (11:#3 remainder,
now DECIDED).

### D7 — Delivery: email only in v1

**Options considered:** email; thank-you-page display; both.
**Chosen: email only.** The thank-you page belongs to the upsell funnel
(ADR-011 capture window) and the card may be issued minutes after the page was
closed (webhook timing) — a thank-you surface would be unreliable and collides
with funnel offers (§XIV: don't fire two campaigns on one surface). The screens
only show email delivery.

- Sent via `emails.service.send_transactional_email(template_id=
  'gift_card_campaign', …)` from a dedicated Celery task; audit via the
  existing `SentEmail` row.
- **Template precedence (AF-110 MEDIUM, recommended and adopted):** campaign
  `email_subject`/`email_body`, when non-blank, override the store/platform
  `gift_card_campaign` template; blank falls through to the normal
  `EmailTemplate`-then-file chain. One precedence rule, resolved in one place
  (§XV-4) — inside the send task, not in the admin form.
- **Token pills:** the editor's `[DISCOUNT]` and `[GENERATED GIFT CARD CODE]`
  are stored as-is and mapped at send time to context `{{ gift_value }}` /
  `{{ gift_code }}` — the same variable names as the manual-issue dialog
  (admin-020-02), one vocabulary for both flows. Context also gets `store`,
  `expires_at`.
- **Translation:** customer-visible content ⇒ the established AiJob CLI
  pipeline (ADR-014/ADR-021 — never direct API). v1 sends in the store default
  language (ASSUMPTION, matching the shipped abandoned-checkout email posture);
  a `GiftCardCampaignTranslation` table mirroring `CampaignStepTranslation`
  (subject/body per language, AiJob-populated, order-language selection at send)
  is specified as the follow-up increment and is **not** in the TICKET-045
  critical path.

### D8 — List counters ("Issued / Used / Outstanding")

Computed via the D1 `DiscountCode.gift_card_campaign` FK (indexed), displayed on
the super-admin list exactly as on super-admin-12:

- **Issued** = count of linked codes.
- **Used** = linked codes with at least one `GiftCardTransaction` spend row
  (`transactions.filter(amount__gt=0).exists()` — partial redemption counts as
  used).
- **Outstanding** = linked codes that are `is_active`, unexpired
  (`ends_at` null or future), and `current_balance > 0` — i.e. live monetary
  liability. This adopts the AF-110 PENDING recommendation
  (MEDIUM — confirm with human; the screen's 744/10/1 is consistent with most
  of the 744 having expired by screenshot time).

Counters are computed queries, never denormalized counters to maintain (no new
race surface; list pages are low-traffic admin).

### D9 — Abuse, refunds, budget

- **Self-gifting loop** (card pays for the order that triggers the next card):
  v1 rule — an order whose processor-charged amount is **zero** (fully covered
  by gift card) never triggers issuance (D4 step 2). Orders partially paid by
  gift card still trigger (the buyer injected new money). ASSUMPTION,
  recommended default; the frequency cap is the second brake. Threshold-based
  triggers (future `spend_threshold`) must compute their threshold on the
  processor-charged amount, not the pre-gift-card total — recorded here as a
  binding constraint for that future trigger.
- **Refund after issuance — DECIDED (human, 2026-07-11):** on the full
  refund of the triggering order (`REFUNDED` transition in
  `payments/webhook_views.py::_handle_charge_refunded` / PayPal equivalent),
  look up the order's issued campaign cards (via `CampaignReward.campaign_id`
  suffix match on the order — or, cheaper, `GiftCardTransaction`-free codes
  found through the reward row): if the card is **unspent**, set
  `is_active=False` with an audit note; if partially/fully spent, do **not**
  claw back — flag the order in the refund admin view for manual review.
  Voiding runs in the same nested-savepoint non-fatal posture as other
  post-transition hooks. Partial refunds: no automatic voiding (v1).
- **Budget cap:** `max_issued_cards` per campaign (D1), enforced under the
  campaign-row lock. Monetary budget (`max_issued_cards × value` for fixed
  mode) is displayed as help text; a separate money-denominated budget field is
  deferred until non-fixed value modes exist.
- **Liability visibility:** the Outstanding counter (D8) is the per-campaign
  liability dashboard (§XV-1 — issued-but-unredeemed value must be visible, not
  discoverable).

### D10 — Admin surfaces

Super-admin section (permission-gated to super-admin; store admins see nothing
in v1 — `owner_scope` keeps the store-admin variant open):

- **List** (`/superadmin/gift-card-campaigns/`, spec URL `admin/settings/gift-cards`):
  name, status dot (published/draft/archived), Issued / Used / Outstanding
  counters, store column, filters (status, store), bulk archive. Matches
  super-admin-12.
- **Form** (create/edit): name; value radio group (only "Set value" enabled in
  v1, the two PENDING modes rendered disabled with a "pending decision" note so
  the screen layout is preserved); product-trigger tabs (Conditions tab hidden
  — D2); expiry radio group; frequency cap block; delivery subject + body
  editor with the two token pills and "(Paste default message)" inserting the
  platform template body; footer "Back" / "Save as draft" / "Publish". Matches
  super-admin-12-02.
- **Issued-cards audit:** the existing admin-020 issued-gift-cards list gains a
  "Campaign" filter (the new FK) alongside the existing type filter; each
  campaign list row links to it pre-filtered. No new list view is built.

---

## Why (summary)

The design reuses every already-hardened primitive — `DiscountCode` +
`gift_card_auto` type, the append-only ledger, `CampaignReward` idempotency
(the exact §XIII prescription for this exact feature), the post-PAID-commit
hook posture, `send_transactional_email`, the ADR-027 campaign-shaped-model
precedent — and adds exactly three schema elements (one model, one translation
follow-up model, one nullable FK). The only genuinely new mechanics are the
campaign-row lock serializing cap checks and the trigger-idempotency-axis rule,
both of which are direct applications of §XIII/§XV-6.

## Risks

- **Money issuance bug = real liability.** Mitigated by: idempotency guard +
  unique constraint, row lock, budget cap, computed (not stored) counters, and
  the mandatory Safety gate below.
- **Frequency-cap prefix query** (`campaign_id` startswith) needs the existing
  `(store, campaign_id)` index to serve a prefix LIKE; on Postgres with a
  non-C collation this may require a `varchar_pattern_ops` index — Developer
  must verify the query plan and add the index in the same migration if needed.
- **Email at-least-once:** a Celery retry after a successful send double-sends
  the (idempotent, harmless) email; accepted — the alternative (email inside
  the money transaction) is worse.
- **FR expiry legality** (D6 PENDING) — flagged, not resolved here.
- **Future processors forgetting the enqueue** — the PAID-transition checklist
  item added to TICKET-041's acceptance criteria is the guard; a shared
  `payments/post_paid.py` helper consolidating suppression + analytics +
  gift-card enqueue is the recommended refactor for the Developer if the third
  call site makes duplication obvious.

## Rollback strategy

Set all campaigns to `draft` (instant stop — the task only selects
`published`), or remove the enqueue lines from the two webhook handlers.
Schema rollback: the new model + FK are additive; reverse migration drops them
(`CampaignReward` rows keep the audit trail — PROTECT means issued codes
survive, correctly, as ordinary `gift_card_auto` codes with
`provenance=CAMPAIGN`). Already-issued cards remain redeemable by design —
rollback never confiscates customer value.

## Tests required

- **Idempotency (the 765×$5 guard):** task run twice / concurrently for the
  same order ⇒ exactly one code, one reward row, one email enqueued.
- Duplicate PAID webhook delivery ⇒ single enqueue path exercised end-to-end.
- Frequency cap: at cap ⇒ skip; window expiry re-allows; concurrent orders for
  the same email under the row lock never over-issue.
- Budget cap boundary (`max_issued_cards` reached mid-batch of matching
  campaigns).
- Trigger scoping: `all_products`; `manual_products` intersecting and
  non-intersecting orders; draft/archived campaigns never fire; store isolation
  (multi-tenant: campaign of store A never fires for store B's order).
- Fully-gift-card-paid order ⇒ no issuance (self-gifting brake).
- Expiry: relative and absolute `ends_at` computed correctly; expired absolute
  date skips issuance.
- Currency: issued code carries the store currency, never empty.
- Delivery: token mapping `[DISCOUNT]`/`[GENERATED GIFT CARD CODE]` →
  rendered values; campaign override vs `gift_card_campaign` template
  fallback; failed email leaves the issued card intact + `SentEmail` FAILED row.
- Counters: Issued/Used/Outstanding definitions incl. partial redemption and
  expiry transitions.
- Refund voiding (once policy approved): unspent ⇒ deactivated; spent ⇒
  untouched + flagged.
- Redemption regression: issued `gift_card_auto` codes pass AC-041/AC-042/
  AC-043 (full redemption zero-charge, partial remainder, one-card-per-order).
- Permission: super-admin only; store admin gets 403/404 on all D10 views.

## Safety gate (mandatory — money-adjacent)

Safety Agent review before release must cover: webhook-driven issuance (forged
PAID → forged card — relies on existing signature verification, verify no new
unauthenticated path), code entropy (`secrets`, ≥ 60 bits), email content
injection via campaign body (Django template rendering of admin-authored
strings — sandboxing posture consistent with `EmailTemplate`), enumeration of
issued codes, and the refund/void flow.

## Ticket scope (TICKET-045, one Developer ticket)

In: D1 model + FK + migration, validators, D4 task + two webhook enqueues,
D5/D6/D9 gates, D7 email (store default language), D8 counters, D10 admin,
tests above. Out (follow-ups): `GiftCardCampaignTranslation` + AiJob wiring,
non-fixed value modes (PENDING), conditions rule builder, win-back/Nth-order
triggers, store-admin-scoped campaigns, FR expiry floor setting.

## Addendum (2026-07-11, Developer) — DISCOUNTS-INLINE-ISOLATION fix, and a wider M2M-to-StoreOwnedModel read-path trap

Fixed a follow-up bug left open when D10's admin was built: `GiftCardTransactionInline`
and `CampaignRewardInline` (`discounts/admin.py`), shared by both `DiscountCodeAdmin`
(store_admin_site) and `DiscountCodeSuperAdmin` (super_admin_site), had no
`get_queryset()` override. `TabularInline.get_queryset()` fell back to the raising
`StoreScopedManager` default manager (`core/managers.py`) for `GiftCardTransaction` /
`CampaignReward` (both `StoreOwnedModel`), so opening the `DiscountCode` add/change
form on **either** admin site crashed with `IsolationError` while Django built the
inline formset — the changelist-level version of this had already been fixed via
`cross_store_unsafe()`, but the inlines were explicitly left as a separate ticket.
Fix: both inlines now override `get_queryset()` to return
`<Model>.objects.cross_store_unsafe()`, matching the established convention for
inlines shared across both admin sites (`orders.admin.OrderItemInline` /
`OrderFulfillmentInline`, `catalog.admin.CollectionProductInline`) — safe because
Django's `InlineModelAdmin` re-filters the rendered formset by the parent object's
FK after `get_queryset()` runs, so rows are always scoped to the one
already-authorized parent `DiscountCode`. Also added `has_add_permission()`
returning `False` on both inlines, consistent with each model's own docstring
(append-only / service-written, never created manually through the admin).
Regression tests: `discounts/tests/test_discount_admin_inline_isolation.py`
(BUG_TESTS.csv `DISCOUNTS-INLINE-ISOLATION`, PROVEN).

**A wider trap surfaced during this fix, left OUT of scope here:**
`DiscountCode.product_conditions` / `.collection_conditions` — plain M2M fields
to `catalog.Product` / `catalog.Collection`, both `StoreOwnedModel` — crash
**every** `DiscountCode` change view (store or super admin), independent of the
fix above and independent of gift cards entirely. The crash path is
`django.forms.models.model_to_dict()` → `field.value_from_object()` →
`getattr(obj, attname).all()`, invoked while Django builds
`ModelForm(instance=obj)` for the GET change view — i.e. *before* inline
formsets are even reached. Reproduced independently against
`GiftCardCampaign.trigger_products` on `super_admin_site` too (unrelated to this
ticket's inlines), confirming it is not `DiscountCode`-specific. This is the
same M2M-to-StoreOwnedModel class of trap as `GiftCardCampaignAdmin.save_related`
(this ADR, D-10 section) and `engagement/service.py:resolve_excluded_paths` for
`LeadCaptureCampaign` — but on the **read** path (`model_to_dict`, populating
the change form's initial data) rather than the **write** path (`save_m2m`,
which those two workarounds cover). The existing `save_related` workaround does
**not** protect the read path; any `StoreOwnedModel` with a directly-editable
M2M-to-`StoreOwnedModel` field is currently broken on its change view. Not
fixed here — it is not one of the two named inlines in this ticket, it affects
at least two apps (`discounts`, and by the same pattern likely `campaigns` /
`engagement`), and closing it requires an architecture decision on where a
`model_to_dict`-level (not just `save_m2m`-level) M2M workaround lives across
every affected `ModelAdmin`/`ModelForm`. Routed to the orchestrator / Architect
Agent as a follow-up ticket — until fixed, it blocks the `DiscountCode` and
`GiftCardCampaign` change views outright, with or without the inline fix above.

## Addendum (2026-07-11, wave-3 spec review) — refund flagging mechanism, and a known audit-trail gap

**D9's wording said "flag the order in the refund admin view for manual
review."** The shipped implementation instead flags the **`DiscountCode`**
(the gift card itself), not "the order": `discounts.service.
void_or_flag_campaign_gift_cards_for_refund()` sets
`DiscountCode.refund_flagged=True` + `refund_flagged_reason` on the
spent/partially-spent gift-card code, via a plain `.update()`
(`discounts/service.py`, called from the Stripe/PayPal refund webhook
handlers). There is no `Order`-level flag field. This is recorded here as
**functionally equivalent-or-better** than D9's literal wording, not a
deviation to fix:
- The reviewer surface D9 actually cared about ("flag ... for manual review")
  is satisfied — `refund_flagged` / `refund_flagged_reason` are both list
  columns and list filters on **both** `DiscountCodeAdmin` (store admin) and
  `DiscountCodeSuperAdmin` (super-admin) — see `discounts/admin.py`. A
  reviewer finds every flagged card directly, without having to cross-reference
  an order-level flag back to which gift card was implicated.
- Flagging the code (rather than the order) is the more natural home for this
  data: the thing under manual review is "should this card's remaining
  balance be honored," which is a property of the card, not of the
  already-refunded order. The order itself is fully identified in
  `refund_flagged_reason`'s free text (`"Order {pk} was refunded after this
  gift card was partially or fully spent..."`), so no traceability is lost.
- No schema or UI feature described anywhere else in this ADR or in
  `05_database_schema.md` §3 depended on the flag living on `Order` instead —
  D9's own admin note (§D10, "Issued-cards audit") already anticipated
  reviewing this from the gift-card list, not an order list.

This ADR's own field list in `05_database_schema.md` §3 has been updated
(wave-3 spec review pass) to show `refund_flagged` / `refund_flagged_reason`
on the `DiscountCode` row and the new `gift_card_campaign` FK.

**Known gap, recorded but not blocking:** the **unspent-code void path** in
the same function (`else` branch, `DiscountCode.objects.filter(pk=code.pk,
is_active=True).update(is_active=False)`) only writes a `logger.info(...)`
line — it does **not** persist any audit note on the `DiscountCode` row
itself (unlike the spent/flagged branch, which writes a permanent
`refund_flagged_reason`). Once a voided card's log line rotates out of log
retention, there is no way to tell from the database alone *why* a given
gift-card code is inactive (voided-for-refund vs. deactivated for some other
reason). This is a real but narrow audit-trail gap: the money-safety property
D9 exists to guarantee (never claw back a spent balance) is unaffected — this
gap only touches unspent, already-worthless codes. **Optional follow-up**
(non-blocking, not required before release): add a `deactivation_reason`
free-text field (or reuse `refund_flagged_reason` for this branch too,
renaming it to something branch-neutral) so voided-for-refund codes carry the
same permanent audit trail spent/flagged codes already get.
