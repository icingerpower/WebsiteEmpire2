# ADR-020: PayPal buyer-approval completion flow

**Status:** ACCEPTED (human, 2026-07-10) — recommended defaults chosen so implementation can
proceed; human may veto via `specs/ecommerce_engine/11_uncertainties_to_validate.md`
items 20:P1 / 20:P2.

**Extends:** ADR-011 (capture-window payment flow — Q3/Q4 PayPal decisions ratified,
not re-opened), ADR-015 §4/§6 (PI lifecycle, return URL, retry), ADR-007 (two-stage
capture).
**Fixes:** the "functional observation" in
`docs/security/CHECKOUT_PAYPAL_AMOUNT_VALIDATION.md` §2 (PayPal orders stuck PENDING
forever — no authorize call after buyer approval, stub `retrieve_payment_intent_raw_status`,
no-op `void_payment_intent`).
**Binding constraints:** design-pattern-ideas §IX (decision-tree/capability routing, never
processor special-cases at call sites), §XIII (atomic conditional UPDATE), §XIV (funnel
state machines), §XV-1 (invisible failures are the enemy), §XV-3 (state explicit, never
inferred from absence), §XV-4 (single resolution function), §XV-6 (idempotent jobs).

---

## Decision

1. **D1 — Dedicated PayPal return/cancel views.** Two new GET endpoints,
   `/checkout/paypal/return/` and `/checkout/paypal/cancel/`, registered in
   `storefront/urls.py` before the `<path:slug>` catch-all. `checkout_payment_return`
   (Stripe-shaped) is **not** branched — its success/decline tail is extracted into a
   shared helper both views call (behavior-preserving refactor for Stripe).
2. **D2 — Return/cancel URLs ride on the PayPal order payload.**
   `begin_checkout` gains optional `approval_return_url` / `approval_cancel_url`
   parameters (supplied by `checkout_pay_post` via `request.build_absolute_uri`), passed
   through new defaulted kwargs on `PaymentProcessor.create_payment_intent`
   (`return_url=''`, `cancel_url=''`; Stripe ignores them). The PayPal connector puts
   them in `payment_source.paypal.experience_context` and accepts both
   `rel == 'approve'` and `rel == 'payer-action'` when extracting the approval link
   (providing `payment_source` changes the link rel to `payer-action`).
3. **D3 — Authorize on buyer return, webhook as reconciliation fallback.** A single
   resolution function `payments/paypal_service.py::ensure_authorized(order) -> str`
   (returns a Stripe-shaped status) is called synchronously by the return view AND by
   the `CHECKOUT.ORDER.APPROVED` webhook handler (upgraded from log-only). Idempotent
   via `PayPal-Request-Id: authorize:{order_pk}` plus tolerance of
   `ORDER_ALREADY_AUTHORIZED`.
4. **D4 — Full status mapping.** `retrieve_payment_intent_raw_status` implemented for
   PayPal with the exact mapping table below (onto the Stripe-shaped strings consumed by
   `checkout_payment_return`, `checkout_retry`, and `_pi_permits_void`).
   `retrieve_payment_intent_status` (watchdog: `'authorized'`/`'captured'`) is
   implemented in the same call.
5. **D5 — `void_payment_intent` for PayPal.** GET the order, extract the authorization
   id, `POST /v2/payments/authorizations/{auth_id}/void`. No authorization → no-op
   (log at INFO; an unapproved/unauthorized PayPal order holds no funds and expires on
   its own). Never raises.
6. **D6 — Cancel path.** Buyer cancels on PayPal → `/checkout/paypal/cancel/` → flash
   "Your PayPal payment was cancelled…" → 302 `/checkout/`. `CheckoutState` stays at
   PAYMENT (existing decline-retry path in `_cart_or_redirect` re-opens the payment
   section); the PENDING order is left for the existing fast-path void / GC machinery.
7. **D7 — Buyer redirect via connector capability, not processor name.** New class
   attribute `PaymentProcessor.approval_flow: str` — `'sdk'` (Stripe: client SDK
   confirms) vs `'redirect'` (PayPal: 302 to the approval URL carried in
   `client_secret`). `checkout_pay_post` branches on the capability: AJAX → return
   `{'redirect_url': client_secret}` (already handled by `checkout.js` — zero JS
   change); non-AJAX → direct 302 to the approval URL.
8. **D8 — Capture window per-processor cap.** New class attribute
   `PaymentProcessor.max_capture_delay: timedelta` — Stripe `timedelta(days=7)`,
   PayPal `timedelta(days=2)` (3-day honor period minus 24 h of watchdog-outage
   headroom). `begin_checkout` step 11 computes
   `capture_window_expires_at = now + min(window, connector.max_capture_delay)` where
   `window` is the funnel's minutes or the fallback `max_capture_delay` itself
   (replacing the hardcoded 7 days). The Campaign validator (1–60 min) already keeps
   funnel windows far below both caps — no per-campaign config change.

   **UPDATE 2026-07-10 (20:P1, DECIDED — human, the refinement was chosen over
   the "keep watchdog-at-expiry" default originally recorded here):**
   `capture_window_expires_at` as computed above now serves ONLY as the
   capture-window watchdog's reconciliation fallback for non-funnel orders — it
   is no longer the primary capture trigger for them. `begin_checkout` (step 11)
   additionally sets `Order.capture_immediately = True` when no funnel campaign
   applies. The payment-confirmation path calls
   `campaigns.tasks.capture_original_charge_now(order)` right after
   authorization is confirmed, for both processors:
   - Stripe: `payments.webhook_views._handle_payment_intent_authorized`
     (`payment_intent.amount_capturable_updated`, async fallback) and
     `storefront.views_checkout._finalize_payment_return`'s shared tail
     (synchronous, buyer-return path).
   - PayPal: `payments.paypal_webhook_views._handle_authorization_created`
     (`PAYMENT.AUTHORIZATION.CREATED`, async fallback — split into an inner
     `@transaction.atomic` DB-write half and an outer capture-trigger half so
     the capture's network call never runs while the order row lock is held)
     and the same `_finalize_payment_return` tail via
     `payments.paypal_service.ensure_authorized`.

   `capture_original_charge_now` reuses `_watchdog_claim_and_capture` verbatim
   (same atomic claim, same `capture:{order_id}` idempotency key) — there is
   exactly one capture code path, not two racing implementations. On failure the
   charge reverts to `AUTHORIZED` exactly as it does for the watchdog's own Pass
   1, so `capture_window_watchdog` remains the money-safe backstop. Funnel
   orders are completely unaffected — `capture_immediately` stays `False` and
   the D8 window computation above governs them exactly as before. See ADR-011's
   own 2026-07-10 addendum for the mirrored note in that ADR.

Out of scope (unchanged from ADR-011): `paypal_capture_mode='immediate'`
(`intent=CAPTURE`) remains unimplemented and inert; Vault off-session charges stay
Phase 2; refund currency TODO (L2) stays with the refund ticket.

---

## Context

`PayPalConnector.create_payment_intent` creates a PayPal Order (`intent=AUTHORIZE`) and
returns the approval URL in `client_secret`. After that, the flow is dead:

- **No redirect wiring.** `checkout_pay_post` (AJAX) returns
  `{'client_secret': <approve_url>, 'return_url': …}`; `checkout.js` feeds it to
  `stripe.elements({clientSecret: …})` — which fails. Non-AJAX renders
  `checkout_payment.html`, also Stripe-only.
- **No return/cancel URL in the order payload** (verified: payload is
  `intent` + `purchase_units` + optional vault `payment_source` only) — PayPal has
  nowhere to send the buyer back to.
- **Nothing calls `/v2/checkout/orders/{id}/authorize`** after approval.
  `checkout_payment_return` reads `?payment_intent=` (Stripe); PayPal returns
  `?token={paypal_order_id}&PayerID=…`.
- `retrieve_payment_intent_raw_status` returns `''` → a returning PayPal buyer always
  lands on the decline branch; `void_payment_intent` is a no-op.
- The webhook handlers (`payments/paypal_webhook_views.py`) are complete for the
  post-authorization lifecycle (AUTHORIZATION.CREATED/VOIDED, CAPTURE.COMPLETED with
  the H1 amount/currency check, DENIED, REVERSED) — but those events only fire once an
  authorization exists. `CHECKOUT.ORDER.APPROVED` is log-only.

Consequence: every PayPal order sits PENDING until the GC voids it. Fails closed (no
money risk — the safety audit's verdict), but PayPal checkout is 100 % broken (§XV-1:
a silent, invisible failure for the merchant).

PayPal Orders API v2 semantics used below: buyer approves → order status `APPROVED` →
merchant `POST /v2/checkout/orders/{id}/authorize` → authorization resource
(`purchase_units[0].payments.authorizations[0]`, status `CREATED`/`PENDING`/`DENIED`) →
later `POST /v2/payments/authorizations/{auth_id}/capture` (already implemented) or
`POST /v2/payments/authorizations/{auth_id}/void` (returns **204 No Content**).
Authorizations are valid 29 days but funds are only guaranteed during the 3-day honor
period. `INSTRUMENT_DECLINED` recovery is officially "send the buyer back through the
approval link".

---

## Options considered

**Return URL handling (D1):**
- **A — dedicated `/checkout/paypal/return/` + `/checkout/paypal/cancel/` views (chosen).**
- B — branch `checkout_payment_return` on presence of `?token=` vs `?payment_intent=`.
  Rejected: mixes two wire protocols in one view, violates the hard "must not change the
  Stripe flow" constraint at its most sensitive point, and PayPal needs a *cancel*
  endpoint anyway (Stripe has no equivalent), so a branch would only cover half the
  problem.
- C — one generic `/checkout/payment/return/<processor>/`. Rejected: YAGNI — future
  redirect processors (ADR-011 lists HiPay/Mollie as TICKET-041) can generalize then;
  today it buys nothing but indirection.

**When to authorize (D2/D3):**
- **A — authorize synchronously in the return view; `CHECKOUT.ORDER.APPROVED` webhook
  as reconciliation fallback (chosen).** Immediate confirmation UX (buyer sees the
  thank-you page and the upsell funnel with a live capture window); the webhook covers
  the buyer who approves then closes the tab.
- B — webhook-only. Rejected: the buyer returns to a page that cannot confirm anything
  (webhook latency is unbounded), the thank-you redirect would race the webhook, and
  the ADR-011 funnel needs `authorized_at`/window set before the thank-you render.
- C — return-view-only. Rejected: tab-close after approval leaves an APPROVED order
  never authorized → stuck PENDING again — exactly the bug class being fixed.

**Void when never authorized (D5):**
- **A — no-op with INFO log (chosen).** No funds are held before authorization; PayPal
  orders expire server-side on their own.
- B — best-effort order-level cancellation. Rejected: Orders v2 has no supported
  "cancel order" call (the PATCH-to-VOIDED hint in `processor.py`'s docstring is not a
  real endpoint); inventing one adds failure modes for zero benefit.

**Buyer redirect mechanism (D7):**
- **A — connector capability `approval_flow` + reuse of the existing
  `{redirect_url}` JSON contract (chosen).** Zero changes to `checkout.js`.
- B — `if processor_type == 'paypal'` in the view. Rejected: §IX/ADR-011 "registry
  capability, never special-cased at call sites".
- C — render a PayPal-Buttons JS SDK integration. Rejected for this ticket: a whole new
  front-end surface; the redirect flow is the minimal correct completion of the
  existing design and keeps NFR-1 degradation identical.

**Capture window vs honor period (D6):**
- **A — per-connector `max_capture_delay` capability, clamp in `begin_checkout`
  (chosen).**
- B — per-processor field on `ProcessorAccount`. Rejected: it is a property of the
  processor's API contract, not merchant configuration — a constant, not a setting.
- C — do nothing (7-day fallback for PayPal too). Rejected: watchdog capture at day 7
  is 4 days past PayPal's honor period — a systematic decline/shortfall generator for
  every non-funnel PayPal order (money-losing invisible failure, §XV-1).

---

## Chosen option — detailed design

### 1. Connector payload and interface deltas (`payments/processor.py`, `paypal_connector.py`)

`PaymentProcessor.create_payment_intent` gains two trailing kwargs, defaulted so every
existing call site and the Stripe connector are untouched:

```python
def create_payment_intent(self, amount, currency, customer_id, metadata,
                          capture_method='manual', setup_future_usage=False,
                          return_url='', cancel_url='') -> PaymentIntentResult: ...
```

PayPal payload changes:

```python
payload['payment_source'] = {'paypal': {
    'experience_context': {
        'return_url': return_url,          # → /checkout/paypal/return/
        'cancel_url': cancel_url,          # → /checkout/paypal/cancel/
        'user_action': 'PAY_NOW',
        'shipping_preference': 'NO_SHIPPING',   # 20:P2 — see uncertainties
    },
    # merged with the existing vault block when setup_future_usage=True
}}
```

**UPDATE 2026-07-10 (20:P2, DECIDED — human, the address-forwarding refinement
was chosen over keeping `NO_SHIPPING` for v1):** `create_payment_intent` gains a
third trailing kwarg, `shipping_address: dict | None = None` (Stripe ignores it,
same untouched-by-default posture as `return_url`/`cancel_url`). When a non-empty
dict is supplied — the checkout-collected address, keys `{name, line1, line2,
city, state, postal_code, country}` — `PayPalConnector.create_payment_intent`
maps it onto `purchase_units[0].shipping` (Orders API v2 shape: `name.full_name`,
`address.address_line_1/2`, `admin_area_2`=city, `admin_area_1`=state,
`postal_code`, `country_code`; blank keys omitted) and flips
`experience_context.shipping_preference` to `'SET_PROVIDED_ADDRESS'`. `None` (the
default) or an all-blank address dict preserves the exact payload shown above —
no `shipping` key, `NO_SHIPPING` preference. `cart.checkout.begin_checkout`
forwards its own `shipping_address` parameter (already collected at the address
step) unchanged into `connector.create_payment_intent(...)`. Carrying the
address onto the PayPal transaction record is what PayPal Seller Protection
eligibility for physical goods ties to.

- Approval-link extraction accepts `rel in ('approve', 'payer-action')` — supplying
  `payment_source` makes PayPal emit `payer-action` instead of `approve`.
- If `return_url` is empty (defensive), fall back to today's payload (no
  `experience_context`) so the connector never sends an empty URL to PayPal.

New capability attributes on `PaymentProcessor` (defaults chosen so unknown future
connectors behave like Stripe):

```python
approval_flow: str = 'sdk'                     # PayPalConnector: 'redirect'
max_capture_delay: timedelta = timedelta(days=7)   # PayPalConnector: timedelta(days=2)
```

`PayPalClient` gains 204-tolerance: `post()` returns `{}` when
`resp.status_code == 204` or the body is empty (the `/void` endpoint returns
204 No Content — today's unconditional `resp.json()` would raise on a *successful*
void).

### 2. `payments/paypal_service.py` — the single resolution function (§XV-4)

```python
def ensure_authorized(order) -> str:
    """Authorize an approved PayPal order. Idempotent. Returns a Stripe-shaped status."""
```

Steps (no DB row lock held across network calls):
1. Guards: pinned account is PayPal + `paypal_capture_mode == 'delayed'`; else return
   `retrieve_payment_intent_raw_status(...)` unchanged.
2. Short-circuit: `order.authorized_at` already set → return `'requires_capture'`
   (idempotent re-entry: page refresh, webhook/return race).
3. `POST /v2/checkout/orders/{id}/authorize` with header
   `PayPal-Request-Id: authorize:{order.pk}` (PayPal's idempotency mechanism —
   concurrent return-view and webhook calls collapse to one authorization).
4. Outcome handling:
   - authorization `CREATED` → `'requires_capture'`
   - authorization `PENDING` (buyer under review / eCheck) → `'processing'`
   - authorization `DENIED` → `'requires_payment_method'`
   - HTTP 422 `ORDER_ALREADY_AUTHORIZED` → fall through to
     `retrieve_payment_intent_raw_status` (someone else won; report reality)
   - HTTP 422 `ORDER_NOT_APPROVED` → `'requires_payment_method'` (buyer bailed before
     approving)
   - `INSTRUMENT_DECLINED` → `'requires_payment_method'` (recovery = re-approval, the
     retry view serves the approval link again)
   - any other error → log + `retrieve_payment_intent_raw_status` fallback; `''` on
     total failure (fails closed onto the decline branch — today's behavior).
5. `ensure_authorized` does NOT write `authorized_at` itself: the return view stamps it
   via the shared completion helper (same conditional UPDATE as Stripe), and the
   `PAYMENT.AUTHORIZATION.CREATED` webhook handler already stamps it idempotently —
   two existing writers, both `WHERE authorized_at IS NULL` (§XIII), no third one.

The service raises nothing to callers; all paths return a status string.

### 3. Status mapping — `retrieve_payment_intent_raw_status` for PayPal (D4)

One `GET /v2/checkout/orders/{payment_intent_id}`; inspect most-specific resource
first (captures → authorizations → order status). Exact table:

| PayPal state (precedence order) | Returned Stripe-shaped status | Consumers' behavior |
|---|---|---|
| any capture `COMPLETED` / `REFUNDED` / `PARTIALLY_REFUNDED` | `'succeeded'` | return-view success; void aborted |
| any capture `PENDING` | `'processing'` | return-view success; void aborted |
| capture `DECLINED`/`FAILED` only | fall through to authorization row | — |
| authorization `CREATED` | `'requires_capture'` | return-view success; void permitted (auth voidable) |
| authorization `PENDING` | `'processing'` | success path; void aborted (money may move) |
| authorization `CAPTURED` / `PARTIALLY_CAPTURED` | `'succeeded'` | success; void aborted |
| authorization `DENIED` | `'requires_payment_method'` | decline message; retry available |
| authorization `VOIDED` / `EXPIRED` | `'canceled'` | "payment was cancelled"; void permitted (no-op) |
| order `CREATED` / `SAVED` (no auth) | `'requires_payment_method'` | retry available (re-serve approval link) |
| order `PAYER_ACTION_REQUIRED` | `'requires_action'` | generic decline message; retryable |
| order `APPROVED` (no auth yet) | `'requires_confirmation'` | retryable; void permitted (no funds held) |
| order `VOIDED` | `'canceled'` | — |
| order `COMPLETED` | `'succeeded'` | — |
| unknown status / any exception | `''` | retryable / degrades exactly as today |

Never raises. This table is the contract — the Developer implements it verbatim and
the Test Agent asserts each row.

`retrieve_payment_intent_status` (watchdog mapping) from the same GET:
any capture `COMPLETED` → `'captured'`; otherwise → `'authorized'` (preserving the
stub's safe re-attempt bias; the watchdog's `capture:{order_id}` idempotency key
already guards double-capture).

`retrieve_payment_intent_client_secret` for PayPal: return the `approve`/`payer-action`
link href from the order GET when order status is `CREATED`, `PAYER_ACTION_REQUIRED`,
or `APPROVED`-without-authorization; else `''`. This makes `checkout_retry` work for
PayPal with its existing status gate (`'requires_payment_method'`, `''` → retryable):
the retry template shows a "Continue with PayPal" link when
`processor_type == 'paypal'` and `client_secret` is a URL (presentational branch only,
like `_build_payment_summary`). Best-effort: if PayPal has expired the link, the buyer
lands back on the decline path and re-submits checkout (fast-path void covers the
stale order). (ASSUMPTION 20:A2.)

### 4. `void_payment_intent` for PayPal (D5)

```
GET /v2/checkout/orders/{id} → auth_id = purchase_units[0].payments.authorizations[0].id
if auth_id and authorization status in ('CREATED', 'PENDING'):
    POST /v2/payments/authorizations/{auth_id}/void       # 204 No Content
    log INFO 'paypal.void.authorization_voided'
elif no auth_id:
    log INFO 'paypal.void.no_authorization' — nothing to void; order expires on its own
else:  # CAPTURED / DENIED / VOIDED / EXPIRED
    log INFO — nothing voidable
```

Never raises (interface contract: callers treat it as best-effort). Callers are
unchanged: `_void_processor_intent_best_effort` already wraps it, and
`_pi_permits_void` now gets real PayPal statuses from D4 — `'processing'`/`'succeeded'`
correctly abort the void for in-review or captured PayPal payments.

### 5. Return view, cancel view, shared completion helper (D1/D3/D6-cancel)

`storefront/views_checkout.py` — extract the tail of `checkout_payment_return`
(everything from the PAID back-button guard through the decline redirect) into:

```python
def _finalize_payment_return(request, order, pi_status, fallback_status='') -> HttpResponse
```

— identical statements, same order: PAID → thank-you; CANCELLED → cart with "expired"
message; `pi_status in ('succeeded', 'requires_capture', 'processing')` → conditional
`authorized_at` stamp + CheckoutState PAYMENT→CONFIRMED conditional UPDATE + thank-you;
else `_map_decline_status(fallback_status or pi_status)` → `/checkout/`.
`checkout_payment_return` becomes: resolve order by `?payment_intent=` →
`_finalize_payment_return(request, order, _retrieve_pi_status(order), redirect_status)`.
**Stripe behavior is bit-identical** (tests below guard this).

```
GET /checkout/paypal/return/?token=<paypal_order_id>&PayerID=…
  1. token missing → 302 /checkout/
  2. order = Order.objects.for_store(store).get(processor_payment_intent_id=token)
     — DoesNotExist → 302 /checkout/   (store isolation identical to the Stripe view;
     PayerID is never read — nothing from the query string is trusted for money state)
  3. status = paypal_service.ensure_authorized(order)
  4. return _finalize_payment_return(request, order, status)

GET /checkout/paypal/cancel/?token=<paypal_order_id>
  1. resolve order as above (missing/unknown token → plain 302 /checkout/)
  2. messages.error(request, _('Your PayPal payment was cancelled. You have not been
     charged — you can try again or choose another payment method.'))
  3. 302 /checkout/
```

Cancel leaves everything as-is by design: `CheckoutState.step` stays PAYMENT (the
decline-retry path in `_cart_or_redirect` reopens checkout), the order stays PENDING
with no authorization (no funds held), and the existing fast-path void in
`checkout_pay_post` (or the GC task) reclaims it on the next submit. No new state, no
new cleanup path (§XV-4).

URLs (before the catch-all): `checkout/paypal/return/` name
`checkout-paypal-return`, `checkout/paypal/cancel/` name `checkout-paypal-cancel`.

### 6. `checkout_pay_post` + `begin_checkout` wiring (D2/D7)

- `checkout_pay_post` passes
  `approval_return_url=request.build_absolute_uri(reverse('storefront:checkout-paypal-return'))`
  and `approval_cancel_url=…('storefront:checkout-paypal-cancel')` into `begin_checkout`.
- `begin_checkout(...)` gains the two optional str params (default `''`), forwards them
  to `connector.create_payment_intent(..., return_url=…, cancel_url=…)`, and adds
  `'approval_flow': connector.approval_flow` to its result dict.
- `checkout_pay_post` success tail branches on the capability:
  - `approval_flow == 'redirect'`: AJAX → `JsonResponse({'redirect_url': result['client_secret']})`
    (checkout.js already does `window.location.href = data.redirect_url` — zero JS
    change); non-AJAX → `redirect(result['client_secret'])` (302 to PayPal; the
    approval URL is server-generated by the connector from PayPal's own response, never
    client input — no open-redirect surface).
  - `approval_flow == 'sdk'`: existing Stripe path, byte-for-byte unchanged.

### 7. Webhook fallback (D3) — reuse, don't duplicate

`payments/paypal_webhook_views.py::_dispatch_paypal_event`: replace the
`CHECKOUT.ORDER.APPROVED` log-only branch with `_handle_order_approved(resource,
verified_account)`:

1. Resolve the order: for this event the resource IS the PayPal order object —
   `custom_id` lives at `resource['purchase_units'][0]['custom_id']`, not top-level.
   Extend `_resolve_order_id` to check `purchase_units[0].custom_id` as an additional
   source (additive; existing resources unaffected).
2. Guards (no `select_for_update` held across the network call): order exists,
   `_order_bound_to_account` passes, `payment_status == PENDING`,
   `authorized_at IS NULL`.
3. Call `paypal_service.ensure_authorized(order)` — the same function the return view
   uses. The shared `PayPal-Request-Id: authorize:{order.pk}` makes the
   return-view/webhook race collapse to a single authorization at PayPal;
   `PAYMENT.AUTHORIZATION.CREATED` then stamps `authorized_at` through the existing
   idempotent handler. The handler ignores the returned status (the webhook has no
   buyer to redirect); a `'requires_payment_method'` outcome is just logged —
   `PAYMENT.AUTHORIZATION.VOIDED` / GC handle the corpse as today.

This closes the tab-close gap: approve-then-vanish orders get authorized within webhook
latency, enter the capture window, and are captured by the watchdog (§XV-6 — the
watchdog remains the idempotent finisher; ADR-011 Q5 unchanged).

### 8. Capture-window / upsell eligibility (D8)

The ADR-011 capture-window model applies to PayPal **identically**: ORIGINAL
`OrderCharge` is created AUTHORIZED at `begin_checkout` (already the case),
`capture_window_expires_at` gates the funnel, the watchdog captures at window expiry
via the already-implemented `capture_payment_intent` (auth-id lookup + capture with
`capture:{order_id}` idempotency header). One correction is required:

- `begin_checkout` step 11: `window = funnel.capture_window_minutes` if funnel else
  `connector.max_capture_delay`; then
  `capture_window_expires_at = now + min(window, connector.max_capture_delay)`.
  Stripe: unchanged (7d cap = old fallback). PayPal: fallback drops 7d → 2d, keeping
  watchdog capture inside the 3-day honor period with 24 h of beat-outage headroom.
  Funnel windows (1–60 min) are unaffected by the clamp on both processors — no
  per-campaign or per-account config is added.
- One timing nuance vs Stripe: for Stripe the window effectively starts at PI creation
  and authorization follows within seconds; for PayPal the buyer may dawdle minutes on
  the PayPal page, so a 10-minute funnel window partially elapses before
  `authorized_at`. Accepted for v1 (the funnel eligibility guard in
  `serve_thank_you_step` already checks `capture_window_expires_at` is in the future;
  worst case the funnel window is shorter, never longer — fails safe). Recomputing the
  window at authorization time would move `capture_window_expires_at` after
  `CampaignSession.expires_at` was pinned — rejected as a consistency hazard.
- Funnel eligibility for PayPal remains exactly ADR-011 Q4's predicate (delayed mode +
  vault enabled + vault token persisted); this ADR changes nothing there.

### Implementation map (single Developer ticket — suggested id TICKET-042 / T031-PP)

| File | Change |
|---|---|
| `payments/processor.py` | `approval_flow`, `max_capture_delay` class attrs; `return_url`/`cancel_url` kwargs on `create_payment_intent` (defaulted) |
| `payments/paypal_client.py` | `post()` tolerates 204/empty body |
| `payments/paypal_connector.py` | experience_context payload + `payer-action` link rel; `approval_flow='redirect'`; `max_capture_delay=timedelta(days=2)`; implement `retrieve_payment_intent_raw_status` (table above), `retrieve_payment_intent_status`, `retrieve_payment_intent_client_secret`, `void_payment_intent` |
| `payments/paypal_service.py` (new) | `ensure_authorized(order)` |
| `payments/paypal_webhook_views.py` | `_handle_order_approved`; `_resolve_order_id` reads `purchase_units[0].custom_id` |
| `cart/checkout.py` | `approval_return_url`/`approval_cancel_url` params; window clamp; `approval_flow` in result |
| `storefront/views_checkout.py` | extract `_finalize_payment_return`; `checkout_paypal_return`, `checkout_paypal_cancel`; capability branch in `checkout_pay_post` |
| `storefront/urls.py` | two new paths before the catch-all |
| `storefront/templates/.../checkout_retry.html` | "Continue with PayPal" branch when processor_type is paypal and client_secret is a URL |
| `specs/ecommerce_engine/11_uncertainties_to_validate.md` | items 20:P1, 20:P2, 20:A1–A3 (appended alongside this ADR) |

No schema changes. No new settings. No JS changes.

---

## Why

- **Return-view authorize + webhook fallback** is the standard PayPal Orders v2 pattern
  and the only combination that gives both immediate confirmation UX (thank-you page +
  live upsell window) and tab-close resilience; the shared `ensure_authorized` +
  PayPal-Request-Id makes the two paths race-free without locks across network calls
  (§XIII, §XV-4, §XV-6).
- **Dedicated views** honor the hard constraint that the Stripe flow is untouched; the
  extracted `_finalize_payment_return` keeps success/decline semantics single-sourced
  instead of copy-pasted (§XV-4).
- **Capability attributes** (`approval_flow`, `max_capture_delay`) keep processor
  differences in the registry, not at call sites (§IX; same pattern as
  `supports_delayed_capture`).
- **The mapping table fails closed**: every unknown/error path returns `''`, which every
  consumer already treats as "retryable, degrade gracefully" — the current (broken)
  behavior is the worst case, never a false `'succeeded'`.
- **The honor-period clamp** prevents a silent revenue leak (watchdog captures at day 7
  against a 3-day guarantee) — exactly the invisible-failure class §XV-1 forbids.

## Risks

- **`ensure_authorized` makes an outbound API call inside the webhook request.**
  Bounded by the client's 15 s timeout; the endpoint already round-trips PayPal for
  signature verification, so latency profile is unchanged in kind. PayPal retries
  failed webhook deliveries — an authorize timeout self-heals on redelivery.
- **Return view is a bearer-style endpoint** (anyone with the high-entropy PayPal order
  id reaches the order's thank-you redirect). Identical posture to the existing Stripe
  return view (`payment_intent` id as bearer); no new PII surface. Flag for the Safety
  Agent's pass, not a blocker.
- **`experience_context` link-rel change (`payer-action`)** — if PayPal's response shape
  differs from ASSUMPTION 20:A1 in sandbox, only the connector's link extraction needs
  adjusting; the flow design is unaffected.
- **Approve-link reuse in `checkout_retry`** may 404 on PayPal's side after link expiry
  — degrades to the decline path + resubmit (already handled).
- **Window shortened for non-funnel PayPal orders (7d → 2d)** — earlier capture is the
  point; no consumer depends on the 7-day figure for PayPal (it never worked).
- **Refactor risk in `checkout_payment_return`** — mitigated by the regression tests
  below; the extraction moves statements verbatim.

## Rollback strategy

- No migrations, no settings — rollback is a pure code revert.
- Feature-level: reverting only the two URL registrations + the `approval_flow` branch
  in `checkout_pay_post` returns PayPal to today's fail-closed dead-end (orders PENDING
  → GC void) without touching Stripe; the connector/status/void implementations are
  strictly additive and safe to leave deployed.
- The webhook `CHECKOUT.ORDER.APPROVED` handler can be independently reverted to
  log-only; already-authorized orders continue through the existing
  AUTHORIZATION/CAPTURE handlers.

## Tests required

Stripe non-regression (guards the `_finalize_payment_return` extraction — must pass
byte-identically before/after):
1. Stripe return with `succeeded` / `requires_capture` / `processing` → `authorized_at`
   stamped once, CheckoutState → CONFIRMED, thank-you redirect.
2. Stripe return decline (`requires_payment_method`) → decline message, `/checkout/`,
   order retained; PAID back-button → thank-you; CANCELLED → cart + expired message.
3. `checkout_pay_post` with a Stripe account: AJAX response still
   `{client_secret, return_url}`; non-AJAX still renders `checkout_payment.html`.

PayPal return flow:
4. Return with valid token, connector authorize → `CREATED`: order `authorized_at`
   stamped, CheckoutState CONFIRMED, thank-you redirect; ORIGINAL charge untouched
   (AUTHORIZED).
5. Return twice (refresh): second call short-circuits on `authorized_at`, no second
   authorize POST (assert single client call), same thank-you redirect.
6. Return with authorize → `DENIED` / 422 `ORDER_NOT_APPROVED`: decline message,
   `/checkout/`, CheckoutState stays PAYMENT, order PENDING.
7. Return with authorize → `PENDING` auth: treated as `processing` → thank-you;
   PAID only via later `PAYMENT.CAPTURE.COMPLETED`.
8. Return with unknown/missing token, or token for another store's order → 302
   `/checkout/`, no processor call, no PII.
9. Race: return view and `CHECKOUT.ORDER.APPROVED` webhook concurrently → exactly one
   authorize call reaches the (mocked) client per PayPal-Request-Id; `authorized_at`
   stamped once.

Cancel path:
10. Cancel URL → message + `/checkout/`; CheckoutState still PAYMENT; re-entering
    checkout works (decline-retry path); next `checkout_pay_post` fast-path voids the
    prior PENDING order.

Status mapping (one test per table row, mocked order GET):
11. Each PayPal state row returns exactly the mapped string; malformed/missing
    `payments` key and connector exception → `''` (never raises).
12. `_pi_permits_void` with PayPal auth `PENDING` → void aborted; with order `CREATED`
    → void proceeds.
13. `retrieve_payment_intent_status`: capture COMPLETED → `'captured'`, else
    `'authorized'`.

Void:
14. `void_payment_intent` with auth `CREATED` → void POST issued; 204 body handled
    (no JSON error); with no authorization → no POST, no raise; with `CAPTURED` auth →
    no POST.

Webhook fallback:
15. `CHECKOUT.ORDER.APPROVED` with `purchase_units[0].custom_id` resolves the order;
    PENDING + unauthorized → authorize attempted; already-authorized / non-PENDING /
    account-mismatch → no processor call (bound-account warning for mismatch).

Pay view routing & window:
16. PayPal account: AJAX pay response is `{redirect_url: <approve_url>}`; non-AJAX is a
    302 to the approve URL; connector receives non-empty return_url/cancel_url in the
    order payload (assert `experience_context` present, `payer-action` rel accepted).
17. `begin_checkout` window clamp: PayPal non-funnel → `now + 2 days`; Stripe
    non-funnel → `now + 7 days` (regression); funnel 10 min → unchanged on both.

Retry:
18. PayPal order status `CREATED`: `checkout_retry` renders the PayPal continue link
    (approve URL); Stripe retry rendering unchanged.
