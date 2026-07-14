# ADR-010: Abandoned-checkout campaign — detection, email sequence, resume token, suppression, attribution

**Status:** ACCEPTED (2026-07-05)

**Ticket:** TICKET-021 (depends on TICKET-022 email infrastructure — already built).
**Extends:** ADR-007 §10 (abandoned checkout uses the CampaignSession machinery), ADR-009 §4
(shared `Campaign` root, dedicated email-step model), ADR-002 (unified `DiscountCode`),
ADR-001 §4 (StoreOwnedModel).
**Binding constraints:** design-pattern-ideas §IV/§XIV (never a single enum on Order; funnel
state machines), §VII/§XV-2 (stable IDs, never positions), §VIII/§XV-3 (state never inferred
from absence), §XIII/§XV-5 (validation at persistence boundaries), §XV-4 (single resolution
function), §XV-6 (idempotent jobs, not idempotent steps), §XV-1 (no invisible failures).
**Acceptance criteria:** AC-070 – AC-075, AC-182.

**Decisions already fixed upstream (not re-opened here):** unique single-use `DiscountCode`
per recipient via `CampaignIssuedCode` (ADR-009 §3); HMAC token via `django.core.signing`,
30-day expiry = cart lifetime (DECIDED UF-E), stateless; `send_delay_hours` integer per email
step, required, default 1, lives in the email wizard (DECIDED AF-C4, 2026-07-04);
CampaignSession-based state machine (ADR-007 §10); dedup key = cart session × campaign step;
suppression on purchase completion, cross-session by email address (AC-182); all
customer-visible translation via AiJob CLI runner, never direct API (§15 binding rule).

---

## Context

TICKET-021 delivers abandonment detection, a timed email sequence with an admin wizard
(Type / Style / Copy / Send-after), tokenized resume-cart links, suppression when the order
completes, idempotent coupon issuance, and recovery-revenue attribution.

The campaigns app (T027) already ships the shared pieces: `Campaign` with
`campaign_type='abandoned_checkout'`, `CampaignSession` (explicit state machine, terminal
states never overwritten), `CampaignIssuedCode` (issuance idempotency), and
`evaluate_eligibility` (the single precedence/eligibility function). What is missing and
decided here: where abandonment detection state lives (Q1), where the per-email wizard fields
live (Q2), the exact resume-token format (Q3), the Celery beat scan design (Q4), and the
suppression mechanism (Q5), plus the migration delta and mandated tests.

A structural fact drives several decisions below: **at abandonment time there is usually no
Order row.** The shopper enters an email at the checkout address step — that writes
`Cart.customer_email` (cart/models.py, TICKET-010-CART) — and leaves before payment.
`CampaignSession` currently requires a non-null `order` FK. ADR-007 §1 always specified the
session table as `(order FK nullable, cart FK nullable, customer_email, …)`; the implemented
model narrowed this because T027 only needed post-purchase funnels. T021 restores the
cart-anchored half.

---

## Q1 — Abandonment detection model

**DECIDED: no new `AbandonedCart` model and no new enum on Order. Detection state =
`Cart.status` (coarse lifecycle, already exists) + `CampaignSession` extended with a nullable
`cart` FK (campaign journey state) + a per-step send record (Q4). Option "extend existing
models", not Option "dedicated AbandonedCart table".**

### Options considered
- **A** — dedicated `AbandonedCart(StoreOwnedModel)` holding cart FK, state, timestamps.
- **B** — status flag/enum on Order.
- **C** — `Cart.status` (existing `ABANDONED` choice) for the coarse lifecycle +
  `CampaignSession` (with new nullable `cart` FK) for the per-campaign state machine.

### Chosen option: C

### Why
- **B is forbidden outright.** ADR-007 §10 / §IV: "never a single enum on Order". Also
  factually impossible: no Order exists for most abandonments (see Context).
- **A duplicates `CampaignSession`.** An `AbandonedCart` row would carry exactly the fields
  the session already has (state, timestamps, campaign link, revenue attribution) and would
  break the §XIV unified view — "what campaigns did this customer experience" must be
  answerable from one session table, including exclusion rules ("no upsell coupon if they
  already got an abandonment coupon"). Two state carriers for one journey is the drift/
  invisible-failure class (§XV-1).
- **C reuses what is already specced.** `CartStatus.ABANDONED` exists with the docstring
  "detected by the abandoned-checkout campaign, Phase 2"; `Cart.customer_email` exists "enabling
  abandoned-cart recovery". The Celery beat query (Q4) runs on `Cart` fields that already
  exist: `Cart.objects.for_store(store).filter(status=ACTIVE, expires_at__gt=now,
  updated_at__lte=cutoff).exclude(customer_email='')` — fully `StoreScopedManager`-compliant
  (ADR-001 §4) because the scan iterates per store (Q4).
- Semantics split cleanly: `Cart.status` answers "what is this cart" (active / converted /
  abandoned — one writer each); `CampaignSession` answers "what did the campaign do about it"
  (detected / emailed / clicked / recovered / expired). Neither infers the other.

### CampaignSession delta (migration 0003, see Migration delta)
- `order` becomes **nullable**; new nullable `cart` FK (`cart.Cart`, `on_delete=SET_NULL` —
  purging an expired cart must not delete the campaign audit trail); new `customer_email`
  field (denormalized at session creation — needed for cross-session suppression, AC-182, and
  survives cart deletion); new `abandoned_at` timestamp (the frozen reference time for all
  step delays, Q4).
- `CheckConstraint`: `order IS NOT NULL OR cart IS NOT NULL` — a session is always anchored.
- Conditional `UniqueConstraint (cart, campaign_type) WHERE cart IS NOT NULL` — one
  abandoned-checkout session per cart, the DB-level half of "never re-triggered per session".
  (The existing `(order, campaign_type)` constraint is kept; with `order` nullable, NULLs are
  distinct, so it keeps enforcing exactly the post-purchase invariant.)

### State machine mapping (existing `CampaignSessionState`, no new states)
| Transition | State | Written by |
|---|---|---|
| Beat detects abandonment, session created | `NOT_STARTED` (+ `abandoned_at` set, `Cart.status → ABANDONED`) | beat task (Q4) |
| First (or any) email actually sent | `IMPRESSION` | send task (Q4) |
| Resume link clicked (valid token) | `INTERACTION` (+ `started_at` as click time) | resume view (Q3) |
| Matching order reaches `payment_status=PAID` | `CONVERTED` (= "recovered", AC-074/AC-182; `converted_at` set; `recovery_revenue` per Attribution below) | suppression service (Q5) |
| Cart lifetime (30 d) elapses unconverted | `EXPIRED` | beat sweep (Q4) |

Terminal states are never overwritten (existing invariant, campaigns/service.py enforces).
`DISMISSED` is unused by this campaign type in v1 (reserved for a future unsubscribe path).

**Attribution (AC-075):** `recovery_revenue = order.total` (the **new** order's full total,
per the AC-075 edge case) is written on the `CONVERTED` transition **only when** the session
had reached `INTERACTION` (link clicked) **or** the completed order redeemed a `DiscountCode`
linked to this session via `CampaignIssuedCode`. A recovery with no attributed touch
(AC-074: purchase before any email) converts the session with `recovery_revenue = 0` and no
impression counted. "Sales recovered" / "Revenue recovered" analytics read sessions with
`recovery_revenue > 0`; "Unique Impressions" read sessions that reached `IMPRESSION`.

### Risks
- Making `order` nullable touches T028's post-purchase paths → mitigated: all T028 writes go
  through `create_session(store, order, campaign)` which keeps passing a non-null order; a
  new `create_abandonment_session(store, cart, campaign)` service function is the only writer
  of cart-anchored rows (§XV-4 — one creation path per anchor).
- `SET_NULL` on cart deletion leaves sessions with `cart=NULL, order=NULL` — allowed by the
  CheckConstraint? **No**: the constraint would reject the UPDATE. Therefore the constraint is
  `(order IS NOT NULL) OR (cart IS NOT NULL) OR (customer_email <> '')` — email-anchored
  survival after cart purge. Tests must cover this.

### Rollback strategy
All deltas are additive/relaxing (nullable order, new nullable columns, new constraints).
Reverting migration 0003 restores 0002 exactly; no data transformation is involved beyond
dropping the new columns (abandonment sessions are lost, post-purchase sessions untouched).

---

## Q2 — Where the email wizard fields live

**DECIDED: a dedicated `AbandonedCheckoutEmailStep` model. NOT `offer_config_json` on
`CampaignStep`, NOT fields on `Campaign`. This was already decided in ADR-009 §4 (ACCEPTED by
Human 2026-07-04) — this section binds the concrete schema.**

### Reading of the spec (resolves the "one-shot vs multi-step" question)
`abandoned_checkout` is a **linear timed multi-email sequence** — neither a one-shot email nor
a branch-graph funnel:
- AC-071: "the **first email in the campaign sequence** is enqueued" — a sequence.
- TICKET-021 ASSUMPTION: "for **steps 2..N**, `send_delay_hours` is measured from the
  abandonment timestamp; the wizard warns when a later step's delay is ≤ an earlier step's"
  — N steps exist.
- `validate_campaign_activation` rejecting `CampaignStep` rows does **not** make the campaign
  one-shot; per ADR-009 §4 it exists precisely because the email steps get "**its own step
  model in TICKET-021 scope**", and the validator carries the marker comment "T021 email-step
  check (>= 1 AbandonedCheckoutEmailStep) is deferred".
- Therefore fields do not go on `Campaign` (that would hardcode exactly one email — contradicts
  the sequence) and not on `CampaignStep` (ADR-009 §4: `CampaignStep` models an interactive
  accept/decline branch graph; a timed linear sequence forced into `offer_config_json` bloats
  the schema and pollutes T039's visual funnel editor — the `StepOfferType` enum already
  deliberately excludes `ABANDONED_CHECKOUT`).

### Model (campaigns app)

```
AbandonedCheckoutEmailStep(StoreOwnedModel)
    campaign          FK → Campaign, on_delete=CASCADE, related_name='email_steps',
                      limit_choices_to={'campaign_type': 'abandoned_checkout'}
    position          PositiveIntegerField — display-order hint ONLY; gaps allowed,
                      never renumbered; NEVER used as identity (§VII/§XV-2).
                      Sequence order is defined by send_delay_hours, identity by PK.
    send_delay_hours  PositiveIntegerField, required (no blank), default=1,
                      MinValueValidator(1) — DECIDED AF-C4. The wizard's hours/days
                      dropdown is a FORM-layer affordance: days are converted to
                      hours × 24 before save; storage is always hours (AC-071).
    email_type        CharField(20), choices: warning / reminder / incentive
    email_style       CharField(30) — wizard style key (rendering wrapper choice)
    subject           CharField(255) — Twig-style variables, rendered by emails app
    body_template     TextField — Twig-style variables, rendered by emails app
    coupon_config_json JSONField, default=dict — {} means "no coupon for this step".
                      Non-empty schema: {'value_type': 'percent'|'fixed',
                      'value': number > 0, 'expires_in_days': int >= 1 (default 30)}
    created_at / updated_at
```

Validation at the persistence boundary (§XV-5), in `campaigns/validators.py`:
- `validate_email_step(step)` — subject and body_template non-empty, `send_delay_hours >= 1`,
  coupon schema as above (rejected keys → `ValidationError`, same style as
  `validate_offer_config`). Called from `AbandonedCheckoutEmailStep.clean()` and admin.
- `validate_campaign_activation` gains the deferred branch: an `abandoned_checkout` campaign
  with `is_active=True` requires **≥ 1** `AbandonedCheckoutEmailStep` and every step valid
  (replaces the "deferred" comment; the no-`CampaignStep`-rows rejection stays).
- Non-blocking wizard **warning** (form layer, not model rejection): a step whose
  `send_delay_hours` ≤ an earlier-sequenced step's (per TICKET-021 ASSUMPTION).

### CampaignIssuedCode adaptation
`CampaignIssuedCode.step` is FK→`CampaignStep` (PROTECT). Abandoned-checkout coupons are
issued per **email step**, so migration 0003: make `step` nullable, add nullable
`email_step` FK → `AbandonedCheckoutEmailStep` (PROTECT), `CheckConstraint` exactly-one-of
(`step`, `email_step`), and `UniqueConstraint (campaign_session, email_step) WHERE email_step
IS NOT NULL` — the same ON-CONFLICT idempotency target as ADR-009 §3, now per email step.
Issuance parameters are ADR-009 §3's table verbatim (`usage_limit=1`, `provenance='campaign'`,
`ends_at = now + expires_in_days` default 30, unpredictable `secrets`-generated code,
`stackable=False`), values sourced from `coupon_config_json`.

### Risks
- A second step model means a second admin surface — mitigated: inline on the Campaign admin,
  shown only when `campaign_type='abandoned_checkout'` (mirror of the CampaignStep inline
  gating), platform-scope rows read-only for store admins (ADR-009 §2 permission rules apply
  unchanged, including HTTP-level 403 tests).

### Rollback strategy
New table + nullable-FK widening on `CampaignIssuedCode`: dropping the table and the
`email_step` column restores 0002 behavior; existing funnel issued-codes are untouched
(`step` back to non-null is safe — no abandoned rows would exist after revert).

---

## Q3 — Resume-token format

**DECIDED: stateless `django.core.signing` token, salt `'abandoned_checkout_resume'`, 30-day
`max_age`, URL `/checkout/resume/<token>/`.**

- **Mint (at email send time):**
  `signing.dumps({'cart': cart.pk, 'session': campaign_session.pk, 'store': store.pk},
  salt='abandoned_checkout_resume')`.
  No explicit `exp` claim — `signing.dumps` embeds a signed timestamp; expiry is enforced by
  `max_age` at verification (a duplicated claim could drift from the enforced one).
  `session` is in the payload because attribution (AC-075) must credit **the specific campaign
  whose link was clicked** — the token is the attribution carrier. `store` is defense in depth
  against cross-store replay.
- **Verify (resume view):**
  `signing.loads(token, salt='abandoned_checkout_resume', max_age=timedelta(days=30))`.
  `BadSignature` and `SignatureExpired` are caught **together** and render one identical page:
  *"This link has expired. Please visit our store to start a new order."* (AC-073 — same
  message for tamper and expiry, no oracle on whether the token ever existed). The same page
  is shown when the payload's cart no longer exists, is expired, or belongs to another store
  than the request's — one indistinguishable failure mode.
- **On success:** load the cart; re-price every item from the current catalog (AC-072:
  current price, never the snapshot); out-of-stock item → visible error blocking checkout,
  never silent removal; re-validate and re-apply the cart's coupon only if still valid;
  transition the session `IMPRESSION → INTERACTION` (idempotent — repeat clicks and
  terminal-state sessions leave state unchanged; a click on a `CONVERTED` session redirects
  to the storefront). GET renders the checkout; no mutation of order/payment state happens
  on GET.
- Tokens never appear in analytics events, pixels, or logs (ADR-007 §5 rule extends here).
- 30-day token = 30-day cart lifetime (UF-E). Since `max_age` counts from **send** while
  `cart.expires_at` counts from cart creation, the cart check in the view is the effective
  earlier bound — by design, never a resurrection of an expired cart.

### Why stateless (vs a `CampaignSessionToken` row)
`CampaignSessionToken` exists for tokens that gate **charging a vaulted payment method**
(single-use, hash-stored). A resume link gates only viewing/pre-filling one's own cart, must
be multi-click (a shopper may open it twice), and needs zero server-side revocation beyond
the session/cart state it already checks — signing gives expiry + tamper-proofing for free
with no table growth per email sent. Consistent with ADR-009 §3's secret-sensitivity
gradient.

### Risks
- `SECRET_KEY` rotation invalidates outstanding links (shoppers see the expired page) —
  accepted; key rotation is rare and the failure mode is the designed one.

### Rollback strategy
Pure view + service code; no schema. Removing the URL pattern kills all links safely.

---

## Q4 — Celery beat task design

**DECIDED: ONE global beat entry, `campaigns.tasks.scan_abandoned_checkouts`, every
5 minutes, iterating store by store; per-cart work is delegated to a per-send worker task
guarded by an explicit send-record state machine.**

### Why one global task (vs one per store)
Per-store beat entries require dynamic schedule mutation on every store/campaign
activation — schedule state that can silently drift from DB state (§XV-1). One global entry
is static; tenancy is respected because the task derives the store list **from the active
campaigns** and then uses `for_store(store)` querysets exclusively — no
`cross_store_unsafe` scans of carts.

### New model — the per-step send record (dedup key: cart session × campaign step, DECIDED)

```
AbandonedCheckoutEmailSend(StoreOwnedModel)
    session      FK → CampaignSession, on_delete=CASCADE, related_name='email_sends'
    email_step   FK → AbandonedCheckoutEmailStep, on_delete=PROTECT
    status       CharField, choices: SCHEDULED / SENT / CANCELLED / FAILED
                 (explicit states, §XV-3 — a row is created before enqueueing,
                 so "no row" only ever means "not yet due")
    scheduled_at / sent_at (nullable) / cancelled_at (nullable) / created_at
    UniqueConstraint (session, email_step)   ← THE dedup key (AC-071 "no re-trigger";
                                               survives repeat scans AND worker retries)
```

### Scan algorithm (each run — idempotent as a whole, §XV-6: every step checks "already done?")
1. `Campaign.objects.cross_store_unsafe().filter(campaign_type='abandoned_checkout',
   is_active=True)` → group by store. (The one sanctioned unscoped read; it touches no
   customer data, only campaign definitions.)
2. Per store: resolve **the** campaign via `campaigns.service.evaluate_eligibility` semantics
   (store-owned shadows platform; deterministic `(precedence, created_at)` order; **take the
   first**). Multiple active store-owned abandoned-checkout campaigns: only the first fires —
   the `(cart, campaign_type)` unique constraint makes >1 impossible anyway; the admin shows
   a warning when a second one is activated. Never inline this precedence in the task —
   extend/reuse the single resolution function (§XV-4).
3. **Detection** (first step, delay `d1` = min `send_delay_hours`):
   `Cart.objects.for_store(store).filter(status=ACTIVE, expires_at__gt=now,
   updated_at__lte=now - d1h).exclude(customer_email='')` and no existing session for
   `(cart, 'abandoned_checkout')`. For each: in one transaction —
   `create_abandonment_session(store, cart, campaign)` (writes `cart`, `customer_email`,
   `campaign_type`, `abandoned_at = cart.updated_at`, state `NOT_STARTED`) +
   `Cart.status → ABANDONED`. `IntegrityError` on the unique constraint = another beat run
   won; skip. No email captured ⇒ never detected (AC-071 edge).
4. **Step due-ness** (all steps, including the first): for every non-terminal
   abandoned-checkout session of the store and every step with
   `session.abandoned_at + send_delay_hours <= now`:
   `get_or_create AbandonedCheckoutEmailSend(session, step, defaults=status=SCHEDULED)`;
   **only if created**, enqueue `send_abandoned_checkout_email(send.pk)`. All delays are
   measured from `abandoned_at` (= last cart activity frozen at detection) — uniform for
   step 1 and steps 2..N (TICKET-021 ASSUMPTION, made precise here).
5. **Expiry sweep:** non-terminal sessions whose cart lifetime has elapsed
   (`abandoned_at + 30 d <= now`) → session `EXPIRED`, all `SCHEDULED` sends → `CANCELLED`.

### Worker task — `send_abandoned_checkout_email(send_id)` (per-send, retry-safe)
In one DB transaction:
1. Atomic claim: `UPDATE … SET status=IN-FLIGHT-check WHERE pk=send_id AND
   status='SCHEDULED'` (implemented as `select_for_update` + status check) — losers exit.
   This is what makes Celery redelivery/retry send **nothing twice**.
2. **Race guard (AC-074/AC-182 "even if the job had already started"):** re-check inside the
   lock — session not terminal, `cart.status != CONVERTED`, and no `Order` in this store with
   `payment_status=PAID`, `customer_email = session.customer_email`,
   `created_at >= session.abandoned_at`. Any hit → `status=CANCELLED`, run suppression (Q5),
   send nothing.
3. Coupon (if `step.coupon_config_json`): create `DiscountCode` + `CampaignIssuedCode`
   in this same transaction; retry hits the `(campaign_session, email_step)` unique
   constraint and reuses the already-issued code (ADR-009 §3 idempotency verbatim).
4. Mint the resume token (Q3), render subject/body.
5. Send **via the emails app** (audit trail requirement — every send writes a `SentEmail`
   row): the step's `subject`/`body_template` are store-authored content, not a platform
   `template_id`, so `emails/service.py` gains a thin variant
   `send_campaign_email(subject_template, body_template, recipient, context, store)` reusing
   `_render_subject` / the Django-engine body rendering, `_sanitize_context`, and the
   `SentEmail` audit write. (Extension of the existing service, not a parallel sender —
   single send path preserved.)
6. On confirmed send: `send.status=SENT`, `sent_at=now`, session → `IMPRESSION`.
   On failure after the emails-app retry policy: `status=FAILED` — a dashboardable state,
   never a silent drop (§XV-1).

Email context: cart items at **current** prices, resume URL, issued coupon code (if any),
store branding. The email footer carries the store contact/unsubscribe block (GDPR open item
— see NOT-in-T021).

### Risks
- Beat cadence (5 min) bounds send precision to ±5 min of the configured delay — acceptable
  for hour-granularity delays; documented in the wizard help text.
- Store count growth makes the scan long → the per-store body is independent; if needed the
  global task fans out one sub-task per store without changing any semantics (the send-record
  state machine is the correctness boundary, not the scan).

### Rollback strategy
Remove the beat entry — detection stops; already-`SCHEDULED` sends still drain through the
worker guards. No schema rollback needed to disable the feature (deactivate campaigns).

---

## Q5 — Suppression on order completion

**DECIDED: Option C — explicit pre-cancel on payment confirmation PLUS the at-send race
guard (Q4 step 2). Neither alone satisfies AC-182.**

- **Pre-cancel — explicit service call, not a `post_save` signal.**
  `campaigns.service.suppress_abandoned_checkout(order)` is invoked from the payment
  confirmation path (the webhook handler that transitions `payment_status → PAID`, T018/T028
  code) immediately after the PAID transition commits. A `post_save` signal on Order was
  rejected: `post_save` cannot see the previous `payment_status` (it would fire on every save
  of every PAID order forever, or require fragile old-value caching), and payment transitions
  already flow through exactly one webhook service — an explicit call there is the visible,
  single path (§XV-4). The function:
  1. Finds non-terminal `abandoned_checkout` sessions in `order.store` matching
     **`customer_email = order.customer_email`** (cross-browser-session match, AC-182 edge).
     **Amendment (DECIDED 2026-07-05):** cart-based matching (original part (a)) is deferred
     to v2 — `Order` has no `cart` FK, so the cart is not available in the webhook handler.
     Email-only matching satisfies AC-182 (which specifies cross-session match *by email*).
     The edge case where the same cart converts under a different email is out of v1 scope.
  2. Transitions each to `CONVERTED` (`converted_at=now`); writes `recovery_revenue =
     order.total` iff attributed (Q1 rule: `INTERACTION` reached, or `order.discount_code`
     is linked to this session via `CampaignIssuedCode`).
  3. Cancels every `SCHEDULED` `AbandonedCheckoutEmailSend` of those sessions
     (`status=CANCELLED`, `cancelled_at=now`) — AC-182: "any queued but unsent emails in the
     sequence are cancelled".
- **At-send race guard** (Q4 worker step 2) covers the window where payment confirmation and
  the send job interleave (AC-074 dangerous edge: "the email must not be sent even if the job
  had already started processing") — the send task re-checks PAID-order existence inside its
  locked transaction before rendering anything.
- Suppression is a session transition to a terminal state; terminal states are never
  overwritten, so double invocation (webhook retry) is naturally idempotent.

### Risks
- Email-address matching suppresses sessions of a *different* cart by the same person — this
  is the specified behavior (AC-182), not a bug; the session still records `CONVERTED`.
- A future second PAID-transition entry point (e.g. manual admin "mark paid") must call the
  same function — noted as an implementation-ticket checklist item; the test suite pins the
  service function, not the webhook, as the contract.

### Rollback strategy
Pure service code; removing the call reverts to race-guard-only behavior (weaker but safe —
no email is ever sent to a completed order thanks to Q4 step 2).

---

## Migration delta — `campaigns/migrations/0003_abandoned_checkout.py` (single, additive)

1. `CampaignSession`: `order` → nullable; add `cart` FK (nullable, `SET_NULL`),
   `customer_email` (`EmailField, blank, default ''`, backfilled from
   `order.customer_email` for existing rows in a RunPython step), `abandoned_at`
   (nullable `DateTimeField`).
2. `CampaignSession` constraints: `CheckConstraint` (`order` not null OR `cart` not null OR
   `customer_email <> ''`); conditional `UniqueConstraint (cart, campaign_type) WHERE cart IS
   NOT NULL`; index `(store, campaign_type, state)` for the beat scan.
3. New table `AbandonedCheckoutEmailStep` (Q2 schema).
4. New table `AbandonedCheckoutEmailSend` (Q4 schema) with
   `UniqueConstraint (session, email_step)`.
5. `CampaignIssuedCode`: `step` → nullable; add `email_step` FK (nullable, PROTECT);
   `CheckConstraint` exactly-one-of(`step`, `email_step`);
   `UniqueConstraint (campaign_session, email_step) WHERE email_step IS NOT NULL`.
6. No `orders` or `cart` app migration — `Cart.status/customer_email/expires_at` and
   `Order.payment_status/customer_email` already carry everything needed.

Code deltas (no schema): `validate_email_step` + activation-gate branch in
`campaigns/validators.py`; `create_abandonment_session`, `suppress_abandoned_checkout`,
attribution helper in `campaigns/service.py`; `campaigns/tasks.py` (scan + send tasks);
resume view + URL; `send_campaign_email` variant in `emails/service.py`; admin inline for
email steps (store/platform permission rules per ADR-009 §2); beat schedule entry.

---

## Tests required

**Detection & beat idempotency (AC-071):**
1. Cart with email, inactive for > `send_delay_hours` → session `NOT_STARTED` created,
   `Cart.status=ABANDONED`, send row `SCHEDULED`, exactly one email enqueued.
2. Cart without email, same age → nothing created (AC-071 dangerous edge).
3. Beat runs 3× → still exactly one session and one send row per (cart, step).
4. Worker task delivered twice / retried → exactly one `SentEmail`, send row `SENT` once.
5. Step 2 fires at `abandoned_at + delay2`, not relative to email 1's send time.
6. Two active abandoned-checkout campaigns (store + platform) → store campaign's session
   created, platform's never (precedence via the single resolution function).

**Wizard/model validation (AC-071):**
7. Email step without `send_delay_hours` (or 0) rejected; days-unit form input stored as
   hours × 24; activation of an `abandoned_checkout` campaign with 0 email steps rejected;
   with `CampaignStep` rows rejected (existing test extended); later-step delay ≤ earlier
   step produces the wizard warning.

**Resume token (AC-072, AC-073):**
8. Round trip: click → checkout pre-loaded with same items/quantities/variants; still-valid
   coupon re-applied; session → `INTERACTION`.
9. Price changed since abandonment → current price shown (AC-072 edge).
10. Out-of-stock item → visible error, item not silently removed, checkout blocked.
11. Token at T+31 d → exact expired message, no cart restored; tampered token (bit-flip and
    payload-increment) → byte-identical response to the expired case (no oracle); token
    minted for store A used on store B's domain → same response.
12. Repeat click on a `CONVERTED` session → redirect to storefront, state unchanged.

**Suppression (AC-074, AC-182):**
13. Purchase completes before first send → send row `CANCELLED`, session `CONVERTED`,
    zero `SentEmail`, impressions counter 0, later beat runs enqueue nothing.
14. Race: PAID transition committed after the send task claimed the row but before send —
    guard cancels inside the task (simulate by completing the order between claim and guard).
15. Cross-session: abandonment in browser A, purchase with same email in browser B →
    session `CONVERTED`, queued sends cancelled (AC-182 edge).
16. Webhook retry (suppression called twice) → single terminal transition, no error.

**Coupon issuance:**
17. Send-task retry on a coupon step → exactly one `DiscountCode`, same code reused
    (`(campaign_session, email_step)` IntegrityError path).
18. Issued code: single-use (second redemption rejected via existing DiscountService),
    expires per `expires_in_days` (default 30), `provenance='campaign'`.

**Attribution (AC-075):**
19. Click + purchase of the resumed cart → `recovery_revenue = order.total`, attributed to
    the clicked campaign's session only.
20. Click, then a *different* order (other items/total, same email) → new order's full total
    attributed (AC-075 edge).
21. Recovery without click or coupon (AC-074 path) → `CONVERTED`, `recovery_revenue = 0`.

**Cart lifetime (AC-070):**
22. Session past `abandoned_at + 30 d` → `EXPIRED` by the sweep, scheduled sends cancelled.

**Tenancy & permissions (global definition of done):**
23. `StoreOwnedModel` compliance introspection for both new models; store-admin POST mutating
    a platform campaign's email steps → 403; beat never touches carts of stores without an
    active campaign (assert per-store query scoping).

---

## Explicitly NOT in T021 (deferred)

- **Multi-language email steps.** v1 is single-language. When multilingual emails land
  (with T024's pipeline), an `AbandonedCheckoutEmailStepTranslation` mirroring
  `CampaignStepTranslation` is the designated shape, produced by the AiJob CLI runner —
  never direct API (§15 binding rule). The step model's fields are already
  translation-record-friendly (plain subject/body columns, no mixed-language JSON).
- **Reuse of the `CampaignStep` branch graph / T039 visual editor** for email sequences —
  permanently out per ADR-009 §4, not just deferred.
- **GDPR policy for emailing non-purchasers** (open item, 11 Part B / tickets table): the
  implementation ships the mechanism; the *policy* (consent basis, unsubscribe management,
  PII erasure of `customer_email` on sessions) needs human sign-off before any production
  store activates the campaign type. Flag stays in `11_uncertainties_to_validate.md`.
- **Analytics dashboard columns** ("Sales recovered", "Unique Impressions" rendering) —
  T013 reads the session/send rows defined here; no analytics UI in T021.
- **`DISMISSED`/unsubscribe flow** for abandoned-checkout sessions (reserved state, no v1
  behavior).
- **Safety review** of the resume-token endpoint and the non-purchaser email path is a
  pipeline gate after implementation (ticket blocker "resume-token security review"), not
  part of this ADR.
