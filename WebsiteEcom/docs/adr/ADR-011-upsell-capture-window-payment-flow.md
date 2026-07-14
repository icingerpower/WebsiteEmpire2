# ADR-011: Up-sell capture-window payment flow (TICKET-028)

**Status:** ACCEPTED (2026-07-05)

**Extends:** ADR-007 (upsell funnel state machine — frozen, 9 safety amendments applied),
ADR-009 (campaign data model, Appendix A "TICKET-028 adds on top"), ADR-006-R (routing /
processor pinning), ADR-002 (discount engine — issuance transaction reused verbatim).
**Binding constraints:** design-pattern-ideas §VII/§XV-2 (stable IDs, never positions),
§VIII/§XV-3 (state never inferred from absence), §XIII (atomic conditional UPDATE, no
check-then-save), §XIV (funnel state machines), §XV-1 (invisible failures are the enemy),
§XV-4 (single resolution function), §XV-6 (idempotent jobs).

**Decisions already frozen upstream (NOT re-opened here):** two-charge baseline;
processor-account pinning on Order; `CampaignSessionToken` purposes + hash-only storage;
`PENDING_CAPTURE` provisional items; `UPDATE … WHERE state='AUTHORIZED' AND affected_rows==1`
race guard; watchdog captures original + voids provisional on expiry; **5** accept attempts then
auto-decline (UPDATED 2026-07-05 — supersedes the original "3" in this block); `paypal_capture_mode` delayed/immediate on ProcessorAccount (DECIDED 2026-07-05).

---

## Context

T028 turns the frozen ADR-007 payment design into an implementable flow. The data layer is
further along than the ticket text assumes:

- `orders.OrderCharge` **already exists** (`orders/models.py`, migration `0004_ordercharge`)
  with `charge_type` ORIGINAL/UPSELL, `status` AUTHORIZED/CAPTURED/FAILED/REFUNDED,
  `processor_payment_intent_id` (indexed), `processor_charge_id`, `amount_cents`,
  `captured_at`, `refunded_amount`.
- `campaigns.CampaignSessionToken` **already exists** (migration `0001_initial`) with
  `purpose` enum, unique `token_hash` (SHA-256), `expires_at`, `used_at`.
- `Order` already carries `processor_account` FK (pinning), `processor_payment_intent_id`,
  `capture_window_expires_at`, `authorized_at`/`captured_at`, and a two-axis status machine.
- Connector interface (`payments/processor.py`) already exposes `create_payment_intent`
  (manual capture), `capture_payment_intent`, `create_off_session_charge`, `refund`,
  `verify_webhook`, `health_check`. Stripe implements all of them; PayPal implements
  authorize/capture but `create_off_session_charge` is a **documented stub** (Vault
  onboarding required).
- `payments/webhook_views.py` establishes the handler pattern: `@transaction.atomic`,
  `cross_store_unsafe()` + `select_for_update()` lookup by intent ID, idempotent
  status-guarded transition, side-effects only on first transition, non-fatal side-effects
  in nested savepoints.
- Celery beat (`webecom/settings/base.py` `CELERY_BEAT_SCHEDULE`) already runs
  `campaigns.tasks.scan_abandoned_checkouts` every 5 minutes (T021) and
  `payments.probe_processor_health` every 5 minutes.

What T028 must therefore add: the checkout-side vault/window wiring, the accept/decline
endpoints + token issuance service, the two-charge orchestration, the watchdog, the webhook
promotion of provisional items, the PayPal delayed-mode configuration — and the small
schema deltas listed in Q6. This ADR fixes the six open placement/design questions.

---

## Q1 — `OrderCharge` model placement

**DECIDED: `orders` app — ratify the existing implementation** (`orders/models.py`,
migration `0004_ordercharge`), with the deltas listed below.

**Options considered:**
- **A — `orders`:** charge rows are per-order, store-scoped financial state.
- **B — `payments`:** a charge is a payment record.

**Why A:**
- **The app boundary in this codebase is scope, not vocabulary.** Every `payments` model is
  platform-global control-plane state (ProcessorAccount, OrganizationRule, DecisionLog — all
  in `EXEMPT_MODELS` of the StoreOwnedModel compliance test, none store-scoped). `OrderCharge`
  is store-scoped per-order money state and must be a `StoreOwnedModel` (it already is).
  Moving it to `payments` would put the first store-scoped model into a deliberately
  platform-global app and weaken the compliance test's signal.
- **Dependency direction stays one-way.** `orders` already references
  `'payments.ProcessorAccount'` as a lazy string FK; `payments` never imports `orders` at
  module level (only inside webhook handler bodies). `OrderCharge` in `payments` would force
  a hard `payments → orders` model dependency and a cycle risk.
- **Lifecycle coupling:** `OrderCharge` rows CASCADE with their Order; refund caps, admin
  display, and CSV exports all read them alongside OrderItems. ADR-009 Appendix A already
  says "OrderCharge migration in **orders** app; admin already registers it".

**Deltas to the existing model (see Q6 for the migration):**
1. `ChargeStatus` gains **`CAPTURE_IN_PROGRESS`** — the claimed-but-unconfirmed state the
   watchdog reconciles (ADR-007 §6b). Without it, "capture call in flight" would be inferred
   from absence (§XV-3 violation).
2. `capture_claimed_at` (nullable datetime) — set by the atomic claim UPDATE; the watchdog's
   "stuck" detector needs a timestamp for the CAPTURE_IN_PROGRESS state (created_at is the
   row's birth, not the claim).
3. `campaign_step_id` (nullable BigIntegerField, **soft reference — no FK**) on UPSELL rows.
   Attribution "which step produced this charge" without making `orders` depend on
   `campaigns` (the dependency direction is campaigns → orders, established by ADR-010).
4. **Partial unique constraint** `processor_charge_id` WHERE `processor_charge_id <> ''`
   — the ticket spec says unique, but the column is blank until capture; an unconditional
   unique constraint would collide on `''`.
5. **Partial unique constraint** `(order, campaign_step_id)` WHERE
   `campaign_step_id IS NOT NULL` — the DB-level idempotency backstop for "one upsell charge
   per step per order" (belt to the `upsell:{order_id}:{step_id}` processor idempotency key's
   suspenders).
6. Index on `status` — the watchdog scans by status cross-store.

**Risks:** none new — additive columns/constraints on a table with no production data yet.
**Rollback:** drop the added columns/constraints; the 0004 baseline is untouched.

---

## Q2 — `CampaignSessionToken` placement

**DECIDED: `campaigns` app — ratify the existing implementation** (migration `0001_initial`).

**Options considered:**
- **A — `campaigns`:** it is a session artifact (FK CASCADE to CampaignSession).
- **B — `payments`:** it gates a payment operation.

**Why A:**
- The token's identity, lifetime, and cleanup are all functions of the `CampaignSession`
  (CASCADE FK, expiry = capture window read from the session/order, `thankyou-view` scope =
  the session's thank-you rendering). A `payments` placement would need a cross-app FK into
  `campaigns` — inverting the established dependency direction (campaigns → payments via
  connector factory only).
- The token is **access control for a page and two endpoints**, not the payment safety
  mechanism. The actual charge guard is the DB race guard on `OrderCharge` state plus the
  window check inside the locked transaction (ADR-007 §3). Even a hypothetical token bug
  cannot double-charge; placing the token next to the money code would imply otherwise.
- Same StoreOwnedModel argument as Q1: the token is store-scoped; `payments` is
  platform-global.

**Delta:** none to the model. The **issuance/validation service** is new code:
`campaigns/tokens.py` — `issue_token(session, purpose) -> raw`, `consume_token(raw, purpose)`
(constant-time hash compare, single-use claim for `upsell-act` via
`UPDATE … SET used_at=NOW() WHERE token_hash=… AND used_at IS NULL AND expires_at > NOW()`
`AND affected_rows == 1` — the same conditional-UPDATE pattern, §XIII). Raw values never
logged, never in analytics events, never in DecisionLog (ADR-007 §5).

---

## Q3 — `paypal_capture_mode` and PayPal configuration on ProcessorAccount

**DECIDED:** add to `ProcessorAccount` (payments migration, Q6):

```python
paypal_capture_mode = models.CharField(
    max_length=10,
    choices=[("delayed", "Delayed (authorize, then capture — funnel-eligible)"),
             ("immediate", "Immediate capture (skip post-purchase funnel)")],
    default="delayed",
    help_text="PayPal only. Delayed = Orders API intent=AUTHORIZE; immediate = intent=CAPTURE.",
)
paypal_vault_enabled = models.BooleanField(
    default=False,
    help_text=(
        "PayPal only. True once the merchant has completed PayPal Vault onboarding "
        "(reference-transaction / vaulting agreement). Off-session upsell charges — and "
        "therefore post-purchase funnel eligibility for PayPal-routed orders — require this."
    ),
)
```

**No further PayPal fields are added.** `client_id` (public identifier), `api_secret`
(client secret, encrypted), and `webhook_secret` (carries the PayPal Webhook ID per the
connector's documented convention) already cover the Orders API v2 first-party integration.
`paypal_merchant_id` is only needed for partner/multi-party integrations — not added (YAGNI;
adding it later is one additive migration).

**Why `paypal_vault_enabled` ships in the same migration:** `paypal_capture_mode='delayed'`
alone does NOT make an order funnel-eligible. The accept path needs an **off-session upsell
charge**, and `PayPalConnector.create_off_session_charge` is a stub until Vault onboarding is
completed on the merchant side. Without this flag, every PayPal-routed shopper would see the
funnel, accept, and hit a guaranteed soft-fail — a silent 100%-failure funnel (§XV-1). The
flag makes the capability explicit and per-account.

**Semantics:** both fields are meaningful only when `processor_type='paypal'`; they are
ignored (and hidden in the admin form) for other processor types. `clean()` does not hard-fail
on non-PayPal rows — the defaults are inert.

**Connector behavior:**
- `delayed` (default): `create_payment_intent` uses `intent=AUTHORIZE` (current behavior).
- `immediate`: `create_payment_intent` uses `intent=CAPTURE`; the checkout treats the order
  as captured at approval; funnel eligibility is false.
- New capability method on the connector interface:
  `supports_off_session_charge() -> bool` — Stripe returns `True`; PayPal returns
  `self._account.paypal_vault_enabled`. Registry capability, never special-cased at call
  sites (same pattern as `supports_delayed_capture`).

---

## Q4 — PayPal accept flow (delayed mode)

**DECIDED:** mirror the Stripe two-charge baseline using PayPal Vault
(reference-transaction) tokens; the connector interface is unchanged.

**Funnel eligibility for a PayPal-routed order** (evaluated against the **pinned**
`Order.processor_account`, never re-routed):
`processor_type == 'paypal'` AND `paypal_capture_mode == 'delayed'` AND
`paypal_vault_enabled` AND a vault token was actually stored for this order
(`Order.processor_payment_method_id != ''`). Any miss ⇒ no funnel, standard thank-you page,
watchdog/decline path captures normally. (For Stripe the same predicate is
`supports_delayed_capture` AND `processor_payment_method_id != ''`.)

**Checkout (order creation, delayed mode, funnel-eligible):**
1. Create PayPal order `intent=AUTHORIZE` with
   `payment_source.paypal.attributes.vault = {"store_in_vault": "ON_SUCCESS",
   "usage_type": "MERCHANT", "customer_type": "CONSUMER"}` — the vault opt-in **must be
   requested at order creation**; it cannot be added at charge time (first structural
   difference from Stripe, where `setup_future_usage=off_session` also rides on the initial
   PaymentIntent).
2. After buyer approval + authorization: read the vault token
   (`payment_source.paypal.attributes.vault.id`) and PayPal customer id from the order
   detail GET (fallback: `VAULT.PAYMENT-TOKEN.CREATED` webhook). Persist to
   `Order.processor_payment_method_id` / `Order.processor_customer_id` (new fields, Q6).
   Create the ORIGINAL `OrderCharge` row (status=AUTHORIZED) and set
   `capture_window_expires_at`.
3. If the vault token is not yet available when the thank-you page loads, the funnel is
   simply not shown (eligibility predicate above) — never blocked, never errored.

**Accept (inside the locked transaction described in §"Flow architecture" below):**
1. **Capture the original authorization** — `capture_payment_intent(order_id_paypal)`:
   GET the order, extract the authorization id, POST
   `/v2/payments/authorizations/{auth_id}/capture` with header
   `PayPal-Request-Id: capture:{order_id}` (PayPal's idempotency mechanism is this header,
   not a body param — second difference from Stripe).
2. **Charge the upsell** — `create_off_session_charge(payment_method_id=vault_id, …)`
   implemented as a **new PayPal order with `intent=CAPTURE`** and
   `payment_source.paypal.vault_id={vault_id}` — a merchant-initiated transaction, no buyer
   approval redirect. Header `PayPal-Request-Id: upsell:{order_id}:{step_id}`.
3. **Pending outcomes (third difference):** Stripe off-session confirm returns a terminal
   `succeeded`/error synchronously; PayPal capture can return `PENDING` (e.g. eCheck). A
   PENDING result leaves the UPSELL `OrderCharge` in CAPTURE_IN_PROGRESS and the item in
   PENDING_CAPTURE; promotion happens on the `PAYMENT.CAPTURE.COMPLETED` webhook, voiding on
   `PAYMENT.CAPTURE.DENIED`. Stripe path may promote synchronously; the webhook remains the
   authoritative, idempotent finisher for both processors.
4. **Failure class mapping:** PayPal `INSTRUMENT_DECLINED` / vault-token errors map to the
   same soft-fail degradation as a Stripe off-session decline: original order intact, item →
   FAILED/removed, shopper sees the soft-failure message (AC-141 edge case). No 3DS/SCA
   retry loop exists off-session on either processor — a decline is terminal for that step.

**What is intentionally NOT special-cased:** call sites use only the
`PaymentProcessor` interface + the two capability methods. The routing engine is never
re-invoked; `ProcessorSplitCounter` is never incremented by upsell charges; the upsell
DecisionLog child row (`reason='upsell_charge'`, `parent_decision` FK) is written exactly as
for Stripe (ADR-007 §4).

**Risk:** PayPal Vault onboarding timelines are merchant-dependent. Mitigated by
`paypal_vault_enabled=False` default — Stripe-routed orders are unaffected, PayPal orders
degrade to "no funnel" (already accepted by T028's PENDING note: "Stripe-routed orders are
not blocked").

---

## Q5 — Watchdog task design

**DECIDED:**

- **Placement:** `campaigns/tasks.py` → `capture_window_watchdog()` (Celery task, name
  `campaigns.tasks.capture_window_watchdog`). The watchdog orchestrates campaigns
  (sessions), orders (charges, items), and payments (connector calls); `campaigns` already
  imports both (`campaigns → orders/discounts` per ADR-010; `campaigns → payments.factory`
  is new but consistent — `payments` stays leaf-like). The shared accept/decline/watchdog
  transaction core lives in **`campaigns/upsell_service.py`** (single resolution function
  for the flow, §XV-4) so the watchdog and the endpoints cannot drift.
- **Schedule:** `crontab(minute='*/2')`, registered as a new entry
  `"upsell-capture-watchdog"` in the existing `CELERY_BEAT_SCHEDULE` dict
  (`webecom/settings/base.py`) — same mechanism as T021's `scan-abandoned-checkouts`
  (`*/5`), a **separate entry**, never merged into T021's task: independent failure domains,
  independent cadences. Both tasks are idempotent, so overlapping beat runs are safe.
  Why 2 minutes: the window default is 10 minutes and capture delay directly delays funds
  and fulfillment start; a 5-minute cadence adds up to 50% window-length latency. 2 minutes
  bounds worst-case capture lag at ~2 minutes while staying trivially cheap (one indexed
  scan). This satisfies "tighter than the auth validity window" (days for both processors)
  by 3 orders of magnitude.
- **Window duration — configurable per campaign:** new field
  `Campaign.capture_window_minutes` (PositiveSmallIntegerField, **default 10**, validators
  1–60; meaningful for funnel-type campaigns only, validated in
  `validate_campaign_activation`). The checkout resolves the funnel campaign
  (via `campaigns.service`, the existing single resolution function) at authorization and
  sets `Order.capture_window_expires_at = authorized_at + minutes`. The per-step
  `offer_config_json` example key `capture_window_minutes` is **removed from the documented
  schema** — the window is a funnel-level fact (one window per order, ADR-007 §2), not a
  step fact; a per-step value could silently extend the charge window mid-funnel.
- **Scan scope:** cross-store via `cross_store_unsafe()` (established background/webhook
  pattern), driven by the new `OrderCharge.status` index.

**Watchdog passes (each an idempotent step, §XV-6):**

1. **Expired-window capture (ADR-007 §6a):** ORIGINAL charges with `status='AUTHORIZED'`
   whose order's `capture_window_expires_at < NOW()` (DB time). For each: atomic claim
   `UPDATE … SET status='CAPTURE_IN_PROGRESS', capture_claimed_at=NOW() WHERE
   status='AUTHORIZED'` — `affected_rows==1` or skip (an accept/decline won the race);
   capture via the pinned account with idempotency key `capture:{order_id}`; on success →
   CAPTURED + `captured_at`; void all `PENDING_CAPTURE` items (→ VOIDED); transition the
   session `→ EXPIRED` (only from non-terminal states, same conditional-UPDATE guard as
   `suppress_abandoned_checkout`).
2. **Stuck-capture reconciliation (ADR-007 §6b):** charges with
   `status='CAPTURE_IN_PROGRESS'` and `capture_claimed_at < NOW() − 10 min`. **Reconcile
   first**: retrieve the PaymentIntent / authorization from the processor; if it was
   actually captured (response lost), record CAPTURED and proceed with
   promotion/finalization; only if genuinely uncaptured, re-issue the capture (the
   idempotency key makes this safe).
3. **Session finalization:** non-terminal sessions past `expires_at` whose original charge
   is already CAPTURED (shopper accepted step 1 then closed the tab mid-funnel): transition
   `→ CONVERTED` if ≥1 UPSELL charge CAPTURED, else `→ EXPIRED`; void any `PENDING_CAPTURE`
   items without a successful charge.
4. **Alerting (§XV-1):** any order whose window expired more than **window + 15 minutes**
   ago with the original still uncaptured ⇒ `logger.error` with a stable, greppable tag
   (`upsell.watchdog.uncaptured_breach`) + a counter surfaced in the admin (dashboardable
   status, never a silent lapse). An authorization must never be left to expire — that is
   the money-losing invisible failure ADR-007 §2 names.

---

## Flow architecture (binds Q1–Q5 together — implementation map)

New modules, matching existing app responsibilities:

- `campaigns/tokens.py` — token issuance/consumption (Q2).
- `campaigns/upsell_service.py` — the single transaction core:
  `accept_step(session, step, raw_token)`, `decline_step(session, step, raw_token)`,
  `expire_order(order)` (called by watchdog). All three run the same locked sequence:
  `SELECT … FOR UPDATE` on the ORIGINAL OrderCharge row → DB-time window check
  (`NOW() < capture_window_expires_at`) → race-guard conditional UPDATE → processor calls
  **after** state is claimed → session transition + `data_json` append. The
  storewide-discount hook (DiscountCode + CampaignIssuedCode inside the same transaction,
  ADR-009 §3) is invoked here on accept.
- `campaigns/views.py` (or `storefront` equivalent once T029 lands) — POST-only
  accept/decline endpoints: **`@csrf_exempt`** (bearer token in body; no cookie session
  — ratified T028 safety audit), opaque order IDs, `upsell-act` token consumed via
  `campaigns/tokens.py`, **rate limit = `CampaignSession.accept_attempts`** (new field,
  Q6) incremented atomically with `F('accept_attempts') + 1` inside the locked transaction;
  **limit = 5 attempts — when all 5 are exhausted (accept_attempts reaches 5), the next
  call auto-declines the session (DISMISSED) and returns HTTP 403** (human decision T028
  safety audit, supersedes the original "exceeds 3" draft).
  **Invalid-token response code: HTTP 422** (ratified T028 safety audit).
  DB-backed, not cache-backed: the counter must survive process restarts and be visible in admin.
- `payments/webhook_views.py` — extend the existing handler set: on
  `payment_intent.succeeded` for an **upsell** intent (matched via
  `OrderCharge.processor_payment_intent_id`, indexed), mark the UPSELL charge CAPTURED and
  promote its `PENDING_CAPTURE` item → ACTIVE (local import of the campaigns service inside
  the handler body — the established pattern from `suppress_abandoned_checkout`). PayPal
  webhook equivalents: `PAYMENT.CAPTURE.COMPLETED` / `PAYMENT.CAPTURE.DENIED`.
  Fulfillment triggers and confirmation emails filter `OrderItem.status='active'`
  (provisional invisibility, ADR-007 §6).
- `cart/checkout.py` — set `setup_future_usage`/vault opt-in, persist
  `processor_customer_id` / `processor_payment_method_id`, create the ORIGINAL OrderCharge
  row, set `capture_window_expires_at`, issue the `thankyou-view` token (embedded in the
  confirmation email URL) — closing the Phase-1 `customer_id=''` TODO at
  `cart/checkout.py:396`.

---

## Q6 — Migration delta (all additive — no column drops, no renames, no data rewrites)

**`orders` — one migration (`0008_upsell_capture_flow`):**

| Change | Definition |
|---|---|
| `OrderItem.status` **new field** | CharField(20), choices `active` / `pending_capture` / `voided` / `failed`, **default `'active'`** (existing rows untouched) |
| `Order.processor_customer_id` **new field** | CharField(255), blank, default `''` — Stripe `cus_…` / PayPal payer id |
| `Order.processor_payment_method_id` **new field** | CharField(255), blank, default `''` — Stripe `pm_…` / PayPal vault token id. Identifier, not a secret (unusable without merchant credentials); excluded from admin list displays and logs anyway |
| `OrderCharge.capture_claimed_at` **new field** | DateTimeField, null — set by the CAPTURE_IN_PROGRESS claim |
| `OrderCharge.campaign_step_id` **new field** | BigIntegerField, null — soft reference (no FK) to `campaigns.CampaignStep.pk` |
| `ChargeStatus` **new choice** | `CAPTURE_IN_PROGRESS` (choices-only AlterField; no DB shape change) |
| `OrderCharge` **new constraint** | partial unique `processor_charge_id` WHERE `processor_charge_id <> ''` |
| `OrderCharge` **new constraint** | partial unique `(order, campaign_step_id)` WHERE `campaign_step_id IS NOT NULL` |
| `OrderCharge` **new index** | on `status` (watchdog scan) |

**`payments` — one migration (`0006_paypal_capture_mode`):**

| Change | Definition |
|---|---|
| `ProcessorAccount.paypal_capture_mode` **new field** | CharField(10), choices `delayed`/`immediate`, **default `'delayed'`** |
| `ProcessorAccount.paypal_vault_enabled` **new field** | BooleanField, **default `False`** |

**`campaigns` — one migration (`0005_capture_window_fields`):**

| Change | Definition |
|---|---|
| `Campaign.capture_window_minutes` **new field** | PositiveSmallIntegerField, **default 10**, validators 1–60 |
| `CampaignSession.accept_attempts` **new field** | PositiveSmallIntegerField, default 0 — rate-limit counter (5 then auto-decline) |

**No changes** to `discounts` (issuance reuses ADR-002/ADR-009 machinery unchanged),
`CampaignSessionToken`, `CampaignIssuedCode`, `DecisionLog`, `ProcessorSplitCounter`.
New tables: **none** — both tables named by the ticket already exist (Q1/Q2).

**Rollback strategy:** every field is nullable/defaulted and additive — reverse migrations
drop them without data loss to pre-T028 features. Feature-level rollback: deactivate all
funnel-type campaigns (no sessions created ⇒ checkout falls back to plain
authorize-and-capture); the watchdog remains deployed and drains any in-flight windows.

---

## Tests required (concrete scenarios — Test Agent to implement)

Race and state machine:
1. Accept vs watchdog race: two concurrent claims on the same ORIGINAL charge — exactly one
   wins the conditional UPDATE; the loser makes **zero** processor calls.
2. Decline vs accept race: first writer wins; session ends DISMISSED xor progresses — never
   both; exactly one original capture call issued.
3. Accept after window expiry (tab left open, AC-143 edge): DB-time check inside the locked
   transaction rejects with "window expired"; no charge attempted; original later captured
   by watchdog.
4. Watchdog expired-window pass: AUTHORIZED original past window → captured at original
   amount; all PENDING_CAPTURE items → VOIDED; session → EXPIRED.
5. Watchdog stuck CAPTURE_IN_PROGRESS, capture actually succeeded at processor (response
   lost): reconciliation records CAPTURED, does **not** re-charge, promotes/finalizes.
6. Watchdog stuck CAPTURE_IN_PROGRESS, genuinely uncaptured: re-issued capture with the same
   `capture:{order_id}` idempotency key succeeds; single charge at processor.
7. Session finalization: original captured, shopper vanished mid-funnel past window —
   ≥1 captured upsell ⇒ CONVERTED; zero ⇒ EXPIRED; unpaid PENDING_CAPTURE items voided.

Two-charge money paths:
8. Happy path (AC-141): accept step 1 → original captured at original amount + separate
   off-session upsell charge; two OrderCharge rows (ORIGINAL + UPSELL with
   `campaign_step_id`); next step shown; second accept creates a third charge.
9. Soft-fail (AC-141 edge): upsell off-session charge declined → original order intact and
   captured, upsell item → FAILED/removed, shopper sees soft failure (no error page),
   session records the failure in `data_json`.
10. Decline path (AC-142): decline → original captured only, decline-branch offer shown;
    unconfigured decline branch ⇒ standard thank-you page.
11. Upsell charge idempotency: retry of the accept transaction (worker crash between claim
    and response) issues the processor call with the same `upsell:{order_id}:{step_id}` key;
    the `(order, campaign_step_id)` partial unique constraint prevents a duplicate UPSELL row.
12. Provisional invisibility: PENDING_CAPTURE items excluded from fulfillment triggers and
    the confirmation email; promoted to ACTIVE only on charge-captured webhook; webhook
    delivered twice ⇒ single promotion (idempotent).
13. Upsell charge does NOT increment `ProcessorSplitCounter`; child DecisionLog row written
    with `reason='upsell_charge'` and `parent_decision` set; routing engine not re-invoked
    (assert against the pinned `Order.processor_account` even after routing rules change).

Tokens and access control (AC-145):
14. GET thank-you without valid `thankyou-view` token ⇒ 403 (or home redirect), zero PII.
15. POST accept without `upsell-act` token ⇒ 403; with the `thankyou-view` token ⇒ 403
    (purpose mismatch — a leaked email link can never charge).
16. `upsell-act` single-use: second POST with the same token ⇒ 403, no charge; concurrent
    double-POST of the same token ⇒ exactly one consumption (conditional-UPDATE claim).
17. Rate limit: 5 failed accept attempts ⇒ auto-decline (original captured, session
    DISMISSED); 6th attempt ⇒ 403 with no processor call; `accept_attempts` survives across
    requests (DB-backed).
18. Token hygiene: raw token absent from logs, analytics events, and DecisionLog rows
    (grep-level assertion on captured log output); only the SHA-256 hash is persisted.

PayPal specifics:
19. `paypal_capture_mode='immediate'` account: checkout creates `intent=CAPTURE`; order is
    funnel-ineligible; no session, no tokens, no window set.
20. `delayed` + `paypal_vault_enabled=False`: funnel-ineligible (no soft-fail storm);
    `delayed` + vault enabled + vault id persisted: eligible; PayPal PENDING capture result
    keeps the item PENDING_CAPTURE until `PAYMENT.CAPTURE.COMPLETED` promotes it, and
    `PAYMENT.CAPTURE.DENIED` voids it.
21. Eligibility consults the **pinned** account's capabilities via the connector registry
    (Stripe eligible / crypto-style `supports_delayed_capture=False` ineligible); pre-orders
    excluded (FM-C3).

Configuration and alerting:
22. `capture_window_minutes` respected per campaign (e.g. 5 vs 30); activation validation
    rejects 0 and 61; `Order.capture_window_expires_at = authorized_at + minutes`.
23. Watchdog alert: order uncaptured past window + 15 min emits the
    `upsell.watchdog.uncaptured_breach` error exactly once per order per breach state.
24. Storewide-discount accept hook: DiscountCode + CampaignIssuedCode created atomically with
    the session transition (AC-144 issuance side); crash-retry yields exactly one code
    (existing ADR-009 Q3 tests extended to the real accept transaction).

---

## Explicitly NOT in T028

- **Multi-step funnel execution** — deferred to T029. Accept and decline are terminal in
  T028: the session ends at the first step. The `accept_next_step` / `decline_next_step`
  graph fields and the branching navigation logic are T029 scope (human decision T028
  safety audit).
- **Visual multi-step funnel builder** — TICKET-039 (pure admin UI over the existing graph).
- **Single combined capture optimization** (original + upsell in one call under
  overcapture/incremental-auth capability, ADR-007 §2) — capability-flagged future work;
  the two-charge baseline is the only path shipped.
- **Full multi-charge refund handling** — `_handle_charge_refunded` remains a stub;
  `refunded_amount` accounting, PARTIALLY_REFUNDED/REFUNDED transitions across charges, and
  the refund admin UX are a follow-up ticket (T028 only guarantees the OrderCharge rows those
  refunds will target). The hardcoded `'USD'` in `PayPalConnector.refund` partial refunds is
  flagged for that ticket.
- **PayPal Vault onboarding itself** — merchant-side process; T028 ships the
  `paypal_vault_enabled` gate and the vault-aware connector code paths, dark until enabled.
- **Additional processors** (HiPay, Mollie, BTCPay, BitPay) — TICKET-041; crypto connectors
  auto-excluded via `supports_delayed_capture=False`.
- **Targeting-rules vocabulary** in `evaluate_eligibility` — remains the Phase-2 v1
  pass-through (ADR-007 §9 incremental plan).
- **Thank-you page theme design** — T029 owns the template; T028 delivers the funnel slot's
  server-side contract (context + endpoints + tokens).
- **Order-bump and before-checkout campaign UIs** — only the post-purchase capture-window
  flow is in scope.
- **Pre-order funnel support** — excluded by design (full charge at placement, FM-C3).
- **`thankyou-view` token issuance** — deferred to T029 when the confirmation email is built (human decision 2026-07-05).
- **UPSELL webhook promotion** — deferred to T029; the watchdog handles finalization in T028. `payment_intent.succeeded` webhook updating `Order.processor_payment_method_id` with the vaulted pm_xxx is T029 scope (human decision 2026-07-05).
- **Provisional `OrderItem` creation on upsell accept** — deferred to T029 when `CampaignStep.offer_product_variant` FK is defined. T028 creates the UPSELL `OrderCharge` row but skips the `OrderItem` snapshot (ASSUMPTION documented in `upsell_service.py` Step 13 comment).

---

## Addendum 2026-07-06 — Upsell token availability on the thank-you page (DECIDED)

**Decision:** The `upsell-act` token for the thank-you widget is issued **at thank-you
render time** by calling `_serve_step(session, session.current_step, order)` from
`order_thank_you` (Option B). The token issuance in `begin_checkout` step 12b is
**removed** (session creation stays); `_serve_step` becomes the single issuance point.
The render also performs the missing activation transition: conditional
`UPDATE state IN (NOT_STARTED, IMPRESSION) → INTERACTION, started_at=NOW()` in the same
transaction as the token INSERT (idempotent on refresh; every transition written, §XV-3).

**Why not the alternatives:** storing the raw token in `CheckoutState` (Option A)
violates the Q2 invariant "raw tokens are NEVER persisted"; relaying it through the
Django session cookie (Option C) contradicts the ratified no-cookie bearer-token
pattern and fails on the four cookie-less thank-you entry paths (zero-total redirect,
duplicate-checkout redirect, retry-already-paid redirect, future confirmation-email
`thankyou-view` link).

**Why re-issuing is safe:** `issue_token` does not supersede prior tokens — several
live single-use tokens per session may coexist, all expiring at
`capture_window_expires_at`. Per Q2, the token is *access control, not the payment
safety mechanism*: double-charge is prevented by the locked-transaction charge claim,
the `INTERACTION` state guard, and `accept_attempts` — token count is irrelevant.
Page refresh issues a new token but does not invalidate the one already embedded in an
open tab; whichever is POSTed first is consumed, and the session state machine handles
the rest. Reload-after-action re-renders the widget for the *new* `current_step`
(or hides it on terminal state), so multi-step funnels work through plain page reloads.

**Guards before serving (all fail-soft — widget absent, thank-you page always renders,
same degrade-gracefully posture as `begin_checkout` 12b):** `payment_status == PAID`;
`capture_window_expires_at` in the future; a store-scoped `CampaignSession` for the
order with `campaign_type == ONE_CLICK_FUNNEL`, non-terminal state, and
`current_step IS NOT NULL`.

**Supersedes** the "Flow architecture" line stating the raw token is returned by
`cart/checkout.py` for embedding; `begin_checkout`'s result contract drops
`upsell_token`. ADR-015 §6 is unaffected except that `order_thank_you` gains the
upsell context block.

---

## Addendum 2026-07-10 — 20:P1 immediate capture for non-funnel orders (DECIDED)

**Cross-reference:** ADR-020 D8 records the full decision and implementation map;
this addendum only notes the effect on THIS ADR's Q5 watchdog design, since Q5 was
written assuming the watchdog's expired-window sweep (Pass 1) was the only path
that ever transitions an ORIGINAL charge from `AUTHORIZED` to `CAPTURED`.

**Decision:** for orders with no funnel campaign at checkout
(`Order.capture_immediately=True`, set in `begin_checkout` step 11), Pass 1's
claim/capture logic (`_watchdog_claim_and_capture`) is now also invoked
synchronously from the payment-confirmation path — immediately on authorization
— via the new `campaigns.tasks.capture_original_charge_now(order)`, rather than
waiting for Pass 1's next scheduled sweep of `capture_window_expires_at`.

**Q5 watchdog design is otherwise unchanged:** Pass 1 still runs every 2 minutes
and still scans every `ORIGINAL`/`AUTHORIZED` charge past
`capture_window_expires_at` — for a non-funnel order this is now normally a
no-op (the charge is already `CAPTURED` by the confirmation-path call), but it
remains the reconciliation fallback when the inline call fails, is skipped due
to a crash, or never runs (e.g. a webhook delivery failure with no return-view
visit). Passes 2–6 (stuck-capture reconciliation, session finalization, breach
alerting, ambiguous-outcome/Phase-C recovery) are untouched — they operate on
charge/session state, not on which code path performed the capture.

**Funnel orders are unaffected:** `capture_immediately` stays `False` for them,
so `capture_original_charge_now` is never invoked; the upsell window and Pass 1's
role as the SOLE capture trigger for funnel orders are exactly as this ADR
originally specified.
