# SECURITY CHECKLIST — Post-purchase one-click upsell (capture-window model)

> Safety Agent review of ADR-007 §2/§6 (capture-window payment model), ADR-006 (payment routing),
> Area 14 acceptance criteria (AC-140 – AC-144), tickets T027/T028.
> Scope: combined-capture semantics, races, access control, crash recovery, refunds.
> Date: 2026-07-03. Status of ADR-007: ACCEPTED, implementation gated on this review (ADR-007 §6, T028 HARD GATE).

## VERDICT: **APPROVED**

> **Resolved 2026-07-03 — ADR-007 amendments applied, T028 implemented, all 9 amendment
> conditions satisfied.** ADR-007 §2 was amended to the two-charge baseline and its §6
> gate records "Safety Agent review RESOLVED 2026-07-03 — all 9 amendments applied."
> The BLOCKED rationale below is retained unchanged as the audit record of what the
> original design was missing; every required mitigation is now implemented and the
> implementation checklist at the bottom is fully checked off.

### Original BLOCKED rationale (historical, superseded by the resolution above)

The capture-window model is sound in principle (authorize at checkout, single capture at
funnel end, watchdog backstop), but **the flow as specified — "Accept → one combined capture
(original + upsell)" — is not implementable on Stripe**, because a card capture cannot exceed
the authorized amount as a baseline capability. This is a design-level flaw (Risk 1), not a
missing guard, and two further controls are absent from the spec entirely (Risks 5 and 7's
multi-charge refund model). ADR-007 §2 and T028 must be revised per the mitigations below;
after that revision this review converts to APPROVED. The `CampaignSession` data model
(T027) is safe to implement now — the block applies to the payment flow (T028) only, which
matches the existing gating in T027/T028.

---

## Risk-by-risk assessment

### 1. Over-capture — combined capture exceeds the authorized amount — **BLOCKER**

**Finding.**
- **Stripe:** `PaymentIntent` capture accepts `amount_to_capture` **≤ the authorized amount**
  only. Capturing original + upsell against an auth for the original amount is rejected.
  Stripe's two escape hatches are *not* baseline capabilities:
  - **Overcapture** (`payment_method_options.card.request_overcapture: if_available`) —
    eligibility is per-merchant / per-network (Visa/MC, limited MCCs); the actual headroom
    is only known per-PaymentIntent (`overcapture.maximum_amount_capturable`). Cannot be
    the day-1 design.
  - **Incremental authorization** (`request_incremental_authorization: if_available`, then
    `increment_authorization`) — per-card availability, signaled by
    `incremental_authorization_supported` on the charge. Usable as an optimization, never
    as the only path.
- **PayPal (Orders v2):** an authorization may be captured up to **115% of the authorized
  amount, capped at authorized + 75 USD** — small upsells fit, larger ones do not; also
  the 3-day honor window applies (irrelevant at a 10-min window, but relevant to the
  watchdog if it lags). To be confirmed and recorded in the connector flag work of T019.

**Required mitigation (redesign of ADR-007 §2, choose the two-charge model):**
1. **Two-charge model (mandatory baseline):** at checkout, authorize the original amount
   **and save the payment method for off-session reuse** (Stripe:
   `setup_future_usage: off_session`; PayPal: vaulted payment token / `PAY_UPON_INVOICE`-style
   reference transaction as available). On accept:
   capture the original auth at the original amount, and charge the accepted upsell steps
   as a **separate off-session payment** (new PaymentIntent, `off_session: true`,
   `confirm: true`) with its own idempotency key. Decline/expiry: capture original only.
   This also makes AC-141 ("charged immediately using the stored payment token")
   consistent with the ADR — today AC-141 and ADR-007 §2 contradict each other
   (per-step immediate charge vs one combined capture at funnel end); the spec must pick
   one; the two-charge model resolves it.
2. **Optional optimizations, behind connector capability flags:** use incremental
   authorization (Stripe) or the 115%/+$75 headroom (PayPal) to fold the upsell into a
   single capture *when the processor confirms availability for this specific auth*;
   fall back to the two-charge model otherwise. Connector interface gains
   `supports_incremental_auth` / `max_capture_amount(auth)` alongside
   `supports_delayed_capture`.
3. **Never authorize original + max-funnel-value at checkout** as a workaround — inflated
   pending holds are a disclosure/chargeback risk and a card-network compliance problem.
4. A failed upsell charge must **never** endanger the original order: capture original
   first (or independently); upsell charge failure ⇒ upsell item removed/marked failed,
   shopper sees a soft failure, original order unaffected (extends AC-141's expired-token
   edge case).

### 2. Double-accept race (concurrent accepts → double capture) — **NEEDS-MITIGATION**

**Finding.** Neither ADR-007 nor T028 specifies a concurrency guard on accept. Two
concurrent POSTs (double-click, two tabs, replayed request) could both add the item and
both call capture/charge.

**Required mitigation:**
- **Atomic state transition as the single arbiter.** Payment-side state on the order
  (e.g. `AUTHORIZED → CAPTURE_IN_PROGRESS → CAPTURED`) advanced with a guarded UPDATE:
  `UPDATE ... SET state='CAPTURE_IN_PROGRESS' WHERE order_id=%s AND state='AUTHORIZED'`
  — proceed only if `rows_affected == 1` (same idiom as ADR-002 §4). The loser gets
  HTTP 409, no processor call.
- Per-step accept recorded via the `CampaignSession` transition under `SELECT FOR UPDATE`
  in the same transaction as the item add.
- **Processor idempotency keys** on every capture/charge call, derived deterministically:
  `capture:{order_id}` and `upsell:{order_id}:{step_id}` — so even a retried HTTP call
  after a crash cannot double-charge (aligns with AC-064's idempotency requirement).
- Applies identically to the accept endpoint, the decline endpoint (decline triggers
  capture of the original), and the watchdog (Risk 6) — all three race for the same
  transition and only one may win.

### 3. Window-expiry check ordering — **NEEDS-MITIGATION**

**Finding.** ADR-007 §2 covers the UI ("never renders after expiry, eligibility reads
`capture_window_expires_at`") and AC-143's edge case demands server-side rejection of a
late accept — correct requirements, but the *ordering* (check before processor call,
inside the lock) is unstated, and the accept-vs-watchdog race at T≈expiry is unhandled.

**Required mitigation:**
- Evaluate `now < capture_window_expires_at` **server-side, inside the same locked
  transaction** as the Risk-2 state transition (re-read under lock), strictly **before**
  any processor API call. A request-entry check alone is insufficient (TOCTOU with the
  watchdog).
- Use DB time (`NOW()`) or a single clock source — never client time, never the render
  timestamp.
- Accept at T+9:59 racing the watchdog at T+10:00: whichever wins the
  `AUTHORIZED → CAPTURE_IN_PROGRESS` transition proceeds; the loser aborts cleanly.
  An accept that loses to the watchdog returns "window expired"; the upsell item is
  never added (or is rolled back in the same transaction).

### 4. Routing consistency (same processor + merchant-of-record) — **NEEDS-MITIGATION**

**Finding.** ADR-007 consults the DecisionLog only for *eligibility*
(`supports_delayed_capture`). Nothing forbids re-running the routing engine at capture
time, and the two-charge model (Risk 1) creates a second charge that could accidentally
route elsewhere — a cross-processor / cross-merchant-of-record split on one order, which
breaks refunds, reconciliation, and the org's settlement accounting.

**Required mitigation:**
- Persist the **resolved `ProcessorAccount` (and Organization) FK on the order at
  authorization time** (ADR-006 §4 DecisionLog is the audit trail; the order row is the
  operational pointer). Capture, upsell charge, void, watchdog capture, and refund use
  **only** that stored account — the routing engine (T020) is *never* re-invoked for any
  operation on an existing order.
- The upsell charge is not a new routing decision and must not increment
  `ProcessorSplitCounter` or write a new stage-1/2 DecisionLog entry (log it as a
  child entry referencing the original decision, `reason=upsell_charge`).
- Assertion in the payment service: processor account of any follow-up operation ==
  order's stored account, else raise (loud failure, §XV-1).

### 5. Thank-you page access control (guest checkout) — **NEEDS-MITIGATION**

**Finding.** Not addressed anywhere in ADR-007, T027, T028, or Area 14. With guest
checkout there is no session-auth fallback: a guessable `/orders/<order_id>/thank-you/`
URL exposes buyer PII (name, address, items) **and** the accept endpoint would let an
attacker add charges to a stranger's saved payment method by enumerating order ids.
This is a charge-forgery primitive, not just an IDOR.

**Required mitigation:**
- **Signed, short-lived, order-scoped token** required on the thank-you URL and on every
  funnel endpoint (accept/decline): e.g. Django `TimestampSigner`/HMAC over
  `(order_id, purpose)`. Constant-time verification; invalid/expired token ⇒ generic
  404/expired page, no oracle on whether the order exists (same principle as AC-073).
- **Two token purposes, two lifetimes:** `thankyou-view` (read-only confirmation, may
  live as long as the email link needs) and `upsell-act` (accept/decline, lifetime =
  capture window, single-use bound to the CampaignSession). The emailed confirmation
  link carries only the view token — a leaked email link must never be able to charge.
- Accept/decline are **POST-only, CSRF-protected, rate-limited** per order and per IP.
- Order public identifiers must be **non-sequential (UUID/opaque)** as defense in depth;
  the token remains the actual control.
- The token never appears in analytics events, pixels, or `DecisionLog`.

### 6. Server crash between item-add and capture — **NEEDS-MITIGATION**

**Finding.** ADR-007/T028's watchdog covers "still authorized at expiry ⇒ capture
original". It does not cover the intermediate states: upsell item added but capture/charge
never issued (crash after item-add), or state stuck in `CAPTURE_IN_PROGRESS` (crash after
the processor call, response lost).

**Required mitigation:**
- Upsell order items are written as **provisional** (`PENDING_CAPTURE` item state) and
  become fulfillable **only** when their payment succeeds (webhook-confirmed). Fulfillment
  and confirmation email must ignore provisional items — otherwise a crash ships unpaid
  goods.
- Watchdog behavior by state, for any order past `capture_window_expires_at`:
  - `AUTHORIZED` (with or without provisional items) ⇒ win the state transition, **capture
    the original amount only**, void/remove provisional upsell items, session ⇒ `EXPIRED`.
  - `CAPTURE_IN_PROGRESS` older than a stuck-threshold ⇒ **reconcile with the processor
    first** (retrieve the PaymentIntent/authorization status) before acting — the capture
    may have succeeded with the response lost; the idempotency key (Risk 2) makes a
    re-issued capture safe.
- Watchdog runs on a schedule tighter than the auth validity window with alerting on any
  order that stays uncaptured past window + grace (invisible-failure rule, §XV-1) — an
  authorization silently lapsing (Stripe ~7 days, PayPal 3-day honor/29-day validity) is
  lost money.
- Watchdog uses the same atomic transition and idempotency keys as the accept path
  (Risks 2–3), so crash + retry of the watchdog itself is also safe.

### 7. Refund on an upsell-inclusive order — **NEEDS-MITIGATION**

**Finding.** Unspecified. The answer depends directly on Risk 1's resolution:
- Single combined capture (incremental-auth/overcapture path) ⇒ **one charge**, so full
  and partial refunds are single refund calls against that charge (AC-061 works as-is).
- Two-charge model (the mandatory baseline) ⇒ the order has **up to two captures**
  (original + upsell); a naive single refund call against "the" charge breaks.

**Required mitigation:**
- Model **multiple charges per order**: an `OrderCharge` (or equivalent) row per
  capture/charge with `amount_captured`, `amount_refunded`, processor charge id, and kind
  (`original` / `upsell`). T009's order model and T028 must reference it.
- Refund service allocates refunds across charges, **item-aware**: refunding the upsell
  item refunds the upsell charge; order-level partial refunds drain charges in a defined
  order (recommend: upsell charge first, then original) and record the split. Refund cap =
  Σ captured − Σ refunded across all charges (extends AC-061's over-refund rejection).
- All refunds go to the stored `ProcessorAccount` from Risk 4 — never cross-processor.
- Gift-card-paid portions remain governed by the open UF-K decision (AC-U1/AC-U3) — out
  of scope here but the multi-charge model must not preclude it.

---

## Do T027 / T028 adequately describe the required safety controls? **No — amendments required.**

What they already get right:
- T028: watchdog at expiry, window check as the eligibility source, delayed-capture
  eligibility via DecisionLog processor, pre-order exclusion, hard gate on this review.
- T027: `CampaignSession` unique per (order, campaign_type), stable-FK branches, explicit
  states — the data model is safe to build now.
- T019: PayPal delayed-capture confirmation is already a named deliverable.
- AC-143 covers the server-side late-accept rejection; AC-064 covers checkout idempotency.

Gaps to add before T028 starts:
1. **T028 rewrite of the capture step** per Risk 1 (two-charge baseline + capability-flagged
   single-capture optimization). ADR-007 §2 must be amended in the same change.
2. **Resolve the AC-141 ↔ ADR-007 §2 contradiction** (per-step immediate token charge vs
   one combined capture at funnel end) and update AC-141/AC-142 accordingly.
3. T028: **atomic state-transition guard + processor idempotency keys** (Risk 2) — currently
   absent.
4. T028: **window check inside the locked transaction, watchdog/accept race arbitration**
   (Risk 3) — currently only implied by AC-143.
5. T028 (+T009): **stored ProcessorAccount on the order; no re-routing on capture/refund;
   no counter increment for the upsell charge** (Risk 4).
6. **New scope (T028 or a dedicated ticket): tokenized thank-you/funnel access control**
   (Risk 5) — missing from the entire backlog; also add an acceptance criterion
   (suggest AC-145: forged/expired token on accept endpoint ⇒ rejected, no charge, no PII).
7. T028: **provisional upsell items + watchdog reconciliation of in-progress captures**
   (Risk 6).
8. T028 (+T009): **multi-charge order payment model and refund allocation** (Risk 7);
   extend AC-061 coverage to upsell-inclusive orders.
9. T018/T019: record per-connector facts as capability flags during implementation —
   Stripe overcapture/incremental-auth availability, PayPal 115%/+$75 capture headroom and
   3-day honor window — verified against current processor documentation, not assumed.

---

## Implementation checklist (to be checked off before T028 merges)

- [x] ADR-007 §2 amended: two-charge baseline; single combined capture only behind
      per-auth processor capability confirmation.
- [x] Checkout saves the payment method for off-session reuse (Stripe
      `setup_future_usage=off_session`; PayPal vault equivalent) with buyer disclosure.
- [x] Connector interface: `supports_delayed_capture`, `supports_incremental_auth`,
      `max_capture_amount(auth)`; PayPal flags confirmed and documented (T019).
- [x] Capture never exceeds processor-confirmed headroom; no blanket over-authorization
      at checkout.
- [x] Atomic `AUTHORIZED → CAPTURE_IN_PROGRESS` guarded UPDATE (`rows_affected == 1`)
      shared by accept, decline, and watchdog paths.
- [x] Deterministic idempotency keys on every capture/charge/refund processor call.
- [x] Window check (`NOW() < capture_window_expires_at`) inside the locked transaction,
      before any processor call.
- [x] Order stores resolved `ProcessorAccount`/Organization at authorization; all
      follow-up operations assert against it; routing engine never re-invoked; upsell
      charge does not touch `ProcessorSplitCounter`.
- [x] Thank-you + funnel endpoints require signed order-scoped tokens; separate
      view vs act purposes; act token lifetime = capture window, single-use; POST + CSRF
      + rate limit; constant-time verification; opaque order ids; token excluded from
      logs/analytics.
- [x] Upsell items provisional until payment confirmed; fulfillment/emails ignore
      provisional items.
- [x] Watchdog: state-aware (authorized ⇒ capture original + void provisional items;
      in-progress ⇒ reconcile with processor first); schedule + alert before auth lapse.
- [x] `OrderCharge` multi-charge model; item-aware refund allocation; refund cap across
      charges; refunds only via the stored processor account.
- [x] Upsell charge failure degrades softly: original order intact, item removed,
      no error page for the shopper (AC-141 edge case).
- [x] Tests: concurrent double-accept (exactly one charge), late accept vs watchdog race,
      forged/expired/replayed token, crash-recovery reconciliation, refund on
      two-charge order, PayPal >115% upsell falls back correctly.

---

*Reviewed against: `specs/ecommerce_engine/09_architecture_decisions.md` (ADR-006, ADR-007),
`specs/ecommerce_engine/08_acceptance_tests.md` (Area 14, AC-061/064/073),
`specs/ecommerce_engine/10_implementation_tickets.md` (T009, T018, T019, T020, T027, T028).*
