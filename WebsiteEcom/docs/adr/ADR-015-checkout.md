# ADR-015: Checkout flow — checkout state, order creation, payment lifecycle, URLs

**Status:** ACCEPTED (human, 2026-07-10; proposed 2026-07-06). Implements spec
`specs/ecommerce_engine/16_checkout.md` (CK/CO/SA/SM/PY/TH/OF/ML/SEO/PA/EC-xxx).

**Extends (frozen — not re-opened here):**
- ADR-006-R — payment routing (`route_payment`/`preview_route`, method families, pinning).
- ADR-007/ADR-011 — capture-window model: PI created `capture_method='manual'`,
  authorized at Elements confirm, captured by the watchdog (`campaigns/tasks.py`).
- ADR-008 — locale (`resolve_locale`, `/checkout/` never language-prefixed).
- ADR-012 — storefront template system (D5 single-page checkout, locked `pages/checkout.html`,
  slot registry, D10 thank-you page at `/orders/<public_order_id>/thank-you/`).
- The accepted `cart/checkout.py::begin_checkout` transaction (OF-001) — this ADR *extends*
  its signature and totals math; it does not redesign its transaction shape.

**Binding constraints:** design-pattern-ideas §III (single URL resolver), §X (FiredPixel
insert-before-fire), §XIII (atomic conditional UPDATE), §XIV (funnel/checkout state machines),
§XV-1 (no invisible failures), §XV-3 (state explicit, never inferred from absence),
§XV-4 (single resolution function), §XV-5 (validation at persistence boundaries).

**Working assumptions ratified from `11_uncertainties_to_validate.md` (all four are
`ASSUMPTION` pending human approval — each is isolated so a reversal is a bounded change):**
- **U16-2 (ASSUMPTION):** failed-payment recovery = **retry view on the existing Order**
  (Order stays `PENDING`; the cart stays CONVERTED). No cart re-activation.
- **U16-3 (ASSUMPTION):** checkout language = **session-persisted browsing language**;
  `/checkout/` is never language-prefixed; fallback = domain root language.
- **U16-4 (ASSUMPTION):** login during checkout is **non-interrupting** (low-key CTA,
  never forced; link hidden until Phase-5 account pages exist).
- **U16-5 (ASSUMPTION):** billing address **defaults to a copy of the shipping address**
  with an "use a different billing address" override checkbox (small superset of spec
  SA-007's copy-only v1; the checkbox is collapsible and off by default).

---

## 1. Context

The storefront (ADR-012 Phases 1–2) renders home/collection/product/cart/search pages.
`begin_checkout()` (cart app) already implements the full atomic order-creation transaction:
inventory locking, discount consumption, routing, PaymentIntent authorization, OrderCharge,
funnel session, cart conversion. What does **not** exist is everything between the cart page
and that service call: the checkout page itself, its step state, its URLs and forms, the
shipping-cost math, the zero-total path, the failed-payment retry path, and the thank-you page.

Known implementation gaps this ADR closes (spec §13):
- **16:G1** — `begin_checkout()` takes no shipping-rate parameter and omits shipping from
  `Order.total` (the `Order.shipping_amount` column already exists and is silently left at 0).
- **16:G2** — no zero-total path: a fully-discounted order still creates a PaymentIntent
  (Stripe rejects amount=0 manual-capture intents → checkout would hard-fail).
- **16:G3** — no `bump_checkout` slot declared/rendered (PA-004).

---

## 2. Decision — checkout state: a DB model (`CheckoutState`), not Django session data

**Decision:** new store-owned model `cart.CheckoutState`, one-to-one with `Cart`, holding the
explicit step enum and all collected-so-far checkout inputs. Django session data is used for
nothing except what it already holds (the session key that identifies the cart, and the
browsing language per U16-3).

**Options considered:**
1. **Django session dict** (`request.session['checkout'] = {...}`) — zero migrations, simplest.
2. **DB model keyed by the cart** — one migration, queryable, cross-device.

**Chosen: (2), DB model.**

**Why:**
- **Abandoned-checkout resume (DECIDED, ADR-010) is cross-device.** The resume link is
  emailed; the shopper may open it on another browser/device where the Django session — and
  option 1's data — does not exist. CO-007 explicitly requires the resume link to restore
  contact + address + shipping method. Only a DB record bound to the cart (which the resume
  token already resolves) can do that.
- **Abandonment analytics and campaign targeting** need "which step did they stop at" —
  §XV-3: that state must be explicit and queryable, not buried in an opaque session blob.
- **The failed-payment retry view (U16-2)** needs a durable pointer from the checkout
  attempt to the created Order after the cart is CONVERTED.
- **§XIV:** checkout is a funnel; funnels get explicit state machines. "No CheckoutState row"
  means "checkout never started" — a real state, not an inference.
- Cost is one small table whose rows die with their cart (CASCADE + the existing 30-day
  cart expiry, DECIDED UF-E). No new cleanup mechanism is needed.

### 2.1 Model definition

```python
# cart/models.py
class CheckoutStep(models.TextChoices):
    CONTACT   = 'contact',   'Contact'
    ADDRESS   = 'address',   'Shipping address'
    SHIPPING  = 'shipping',  'Shipping method'
    PAYMENT   = 'payment',   'Payment'
    CONFIRMED = 'confirmed', 'Confirmed'

class CheckoutState(StoreOwnedModel):
    """
    Server-side checkout progress for one cart (spec 16 CO-007, CK-002).

    Invariants:
    - One row per cart (OneToOne). Created lazily on the first checkout POST
      (never on GET — a bot crawling /checkout/ must not create rows).
    - step is the FURTHEST step reached; earlier sections stay editable (CK-002).
      Editing the address clears shipping_rate and regresses step to ADDRESS.
    - order is set by begin_checkout() success and never cleared — it is the
      anchor for the failed-payment retry view (U16-2) after the cart converts.
    - shipping_rate is advisory (like Cart.discount_code): begin_checkout
      re-resolves and re-prices it server-side inside the transaction (OF-002).
      SET_NULL: a deleted/disabled rate re-opens the shipping step, never 500s.
    - initiate_event_fired: EVT_INITIATE_CHECKOUT once per checkout session
      (CK-003) — insert-before-fire semantics, same idea as FiredPixel (§X).
    """
    cart = models.OneToOneField('cart.Cart', on_delete=models.CASCADE,
                                related_name='checkout_state')
    step = models.CharField(max_length=20, choices=CheckoutStep.choices,
                            default=CheckoutStep.CONTACT)
    email = models.EmailField(blank=True, default='')
    phone = models.CharField(max_length=50, blank=True, default='')
    newsletter_opt_in = models.BooleanField(default=False)
    shipping_address = models.JSONField(default=dict)   # same shape as Order snapshot
    billing_address = models.JSONField(default=dict)    # only when override checked (U16-5)
    shipping_rate = models.ForeignKey('shipping.ShippingRate', null=True, blank=True,
                                      on_delete=models.SET_NULL, related_name='+')
    order = models.ForeignKey('orders.Order', null=True, blank=True,
                              on_delete=models.SET_NULL, related_name='+')
    checkout_language = models.CharField(max_length=10, blank=True, default='')
    initiate_event_fired = models.BooleanField(default=False)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)
```

**State machine (CK-002 gating):** a section is editable/submittable only when all previous
sections are valid; the views enforce `step >= required_step` on each POST endpoint and
redirect to `/checkout/` otherwise (no error page — the single page simply shows the earliest
incomplete section open). Address edits clear `shipping_rate` (SM-001). Email capture on the
contact POST also writes `Cart.customer_email` (CO-002 — arms abandoned-checkout recovery
before any order exists) — the existing beacon path is kept; the form POST is the no-JS path.

---

## 3. Decision — when the Order is created: at Pay-now submit (ratify PY-010) + garbage collection for created-but-unpaid Orders

**Decision:** the Order is created **at Pay-now POST**, inside the single
`begin_checkout()` transaction, *before* the client-side payment confirmation — exactly as
the accepted implementation and spec PY-010/OF-001 already fix. This ADR does not re-open
that decision; it adds the missing garbage-collection story.

**Options considered (for completeness — PY-010 is marked DECIDED in the spec):**
1. On first `/checkout/` load — rejected: phantom Orders for every window-shopper; order
   numbers, inventory decrements and coupon consumption for shoppers who never intended to pay.
2. On shipping-method selection — rejected: still creates unpaid orders for the large
   fraction who abandon at the payment step; buys nothing over (3) since `CheckoutState`
   already persists everything collected before payment.
3. **On Pay-now submit (chosen, already DECIDED):** the order exists during the payment
   processing window (SDK confirm, 3DS, webhook lag) — so there is never a "money moved but
   no order" gap — while shoppers who never press Pay create no order at all. The
   idempotency fingerprint (OF-004) is only meaningful at this point.

**Created-but-unpaid Orders (the cost of choosing 3, and the answer to U16-1):**
a declined/abandoned payment leaves an Order `payment_status=PENDING` holding FIXED_QTY
inventory and consumed discount counters. Two mechanisms reclaim them:

- **GC task:** `orders/tasks.py::void_stale_pending_orders` (new Celery beat entry, hourly).
  Scope: `cross_store_unsafe()` orders with `payment_status=PENDING AND authorized_at IS NULL
  AND created_at < now() − CHECKOUT_PENDING_ORDER_TTL`. Per order, one transaction:
  1. atomic claim `UPDATE … SET payment_status='cancelled' WHERE payment_status='pending'`
     (`rows_affected==1` or skip — §XIII; a concurrent late confirmation wins);
  2. restore FIXED_QTY inventory (`F('quantity') + qty` per OrderItem whose variant is still
     FIXED_QTY);
  3. `discounts.service.restore_discount_on_failed_payment(order)` (already exists — reused,
     §XV-4);
  4. best-effort `cancel`/void of the unconfirmed intent at the processor (outside the DB
     transaction; failure logged, not raised — the auth expires by itself);
  5. mark the ORIGINAL OrderCharge `FAILED`; `logger.info` with a greppable tag
     (`checkout.gc.voided_pending`).
  `CHECKOUT_PENDING_ORDER_TTL` = **24 h** Django setting. `[ASSUMPTION — long enough for
  abandoned-checkout recovery emails (ADR-010 sends within hours) to land on the retry view,
  short enough that FIXED_QTY stock is not held hostage for the 7-day auth window]`
- **Fast path on re-submit (U16-1):** when a shopper edits the cart after a decline, the new
  fingerprint yields a new idempotency key, so a second order is legitimate — but the first
  would double-hold stock. Before starting the new transaction, the Pay-now view checks
  `checkout_state.order`: if it points to a `PENDING`, never-authorized order, it calls the
  same `void_pending_order(order)` service function (steps 1–5 above) first. One function,
  two callers (§XV-4).

**Why authorized_at IS NULL is the guard:** an authorized-but-uncaptured order belongs to the
capture-window watchdog (ADR-011 Q5), never to this GC. The two reapers partition the space
by an explicit column, not by timing heuristics (§XV-3).

---

## 4. Decision — payment intent lifecycle and failed-payment retry

Ratified from ADR-006-R/ADR-011 (unchanged): PI created in `begin_checkout` with
`capture_method='manual'`; authorized by the Stripe Elements/PayPal client confirm;
captured by the capture-window watchdog (campaign window, else 7-day fallback). Checkout
never captures.

**Decision — retry model (U16-2 ASSUMPTION): retry view on the existing Order, reusing the
existing PaymentIntent for same-family retries.**

- **Inline retry (shopper still on `/checkout/`, PY-013):** a declined Stripe confirm leaves
  the PI in `requires_payment_method`; the storefront re-confirms **the same PI** with a new
  card via the same `client_secret`. Zero server round-trip, zero DB writes. This is Stripe's
  designed retry path; amount, manual-capture flag and `setup_future_usage` are preserved.
- **Return retry (resume link / bookmark): `GET /checkout/retry/<signed_order_token>/`**
  (token = `django.core.signing.dumps({'order_id': …}, salt='checkout-retry')`; the
  abandoned-checkout email builder targets this URL when the cart is already CONVERTED with a
  PENDING order). The view: 404 on bad signature or non-matching store; if the order is
  already authorized/paid → redirect to its thank-you page; if voided by GC → friendly
  "this checkout expired" + link to `/cart/` (fresh cart); else render the payment section
  only (order summary + payment element) against the existing PI:
  `retrieve_payment_intent_status(...)` — `requires_payment_method`/`requires_confirmation`
  ⇒ reuse the PI and existing `client_secret`; any unexpected status ⇒ create a **new** PI on
  the **same pinned processor account** with the same amount/flags, update
  `Order.processor_payment_intent_id` and the ORIGINAL OrderCharge row **in place** (one
  ORIGINAL row per order stays invariant; the old intent is cancelled best-effort).
- **Method-family switch on retry** (card → PayPal after a decline): requires re-running
  `route_payment` for the new family. This is legitimate *before* authorization (ADR-007 §4
  pinning protects post-authorization upsell charges, not never-authorized intents) and
  writes a new DecisionLog row — but it is **deferred to a follow-up ticket** and marked
  **PENDING APPROVAL** (§9). v1 retry keeps the family chosen at Pay-now.

**Never re-created on retry:** the Order, its items, its discount consumption, its inventory
decrement, its idempotency key. Retry touches only processor-intent state.

---

## 5. Decision — `begin_checkout()` extension: shipping, zero-total, duplicate signalling (16:G1, 16:G2)

**Revised signature:**

```python
begin_checkout(
    cart, email: str, shipping_address: dict,
    shipping_rate_id: int,                # NEW — mandatory (SM-006/OF-002)
    method_family: str = 'card',          # NEW — from the payment form; feeds preview/route
    phone: str = '',                      # NEW — Order.phone (CO-003)
    billing_address: dict | None = None,  # NEW — None ⇒ copy shipping (U16-5)
    checkout_language: str = '',          # NEW — Order.checkout_language (U16-8)
    utm_data: dict | None = None,
) -> dict
```

**Step insertions into the existing transaction (numbering per OF-001):**

- **Step 2b — shipping resolution (after subtotal):**
  `rates = shipping.service.resolve_shipping_rates(store, country, cart_weight)`;
  the submitted `shipping_rate_id` must be the pk of one of the returned rates — the client
  is never trusted for price or eligibility (SM-006). Miss (rate disabled, zone changed,
  address edited) ⇒ `CheckoutError("The shipping method changed. Please review and try
  again.")`. `shipping_amount = matched_rate.price`, re-read server-side.
  `ShippingRateException` / weight-band re-pricing rides on whatever
  `resolve_shipping_rates` implements — checkout calls the single resolution function and
  adds no shipping logic of its own (§XV-4; weight filtering is that service's reserved
  Phase-2 note, not checkout scope).
- **Step 3b — free-shipping coupon (OF-006):** if the applied coupon's
  `discount_type == FREE_SHIPPING` ⇒ `shipping_amount = 0`; subtotal untouched.
- **Totals (OF-002):**
  `order.total = max(subtotal − coupon, 0) − gift_card + shipping_amount`, with
  `order.shipping_amount` (existing column, finally written), `order.shipping_method_name`
  (new snapshot column — historical display never joins to the rate table, ADR-002 snapshot
  rule) and `order.shipping_rate` (new FK, SET_NULL, analytics only). Gift-card application
  (step 7) caps at `remainder_after_coupon + shipping_amount` so a large gift card can cover
  shipping too (UF-009); the PI amount is `order.total` after all of this — never a stale
  advisory value.
- **Step 5c — zero-total guard (16:G2), placed after gift-card application:**
  when `order.total == 0`:
  - **skip** `preview_route`, `route_payment`, connector customer creation, and PI creation
    entirely (no processor is involved in a zero-money order; Stripe would reject the
    amount-0 manual-capture intent anyway);
  - `order.payment_status = PAID`, `captured_at = placed_at = now()`;
  - OrderCharge row: `charge_type=ZERO_TOTAL` (**new `ChargeType` choice**),
    `status=CAPTURED`, `amount_cents=0`, `captured_at=now()` — the charge history stays
    complete and explicit (§XV-3: "no charge row" must keep meaning "we don't know");
  - **no** capture window, **no** funnel session, **no** vault (nothing capturable — ADR-011
    eligibility predicates all fail naturally, but we skip constructing them);
  - return `{'order_id': …, 'payment_intent_id': '', 'client_secret': '',
    'processor_type': 'none', 'upsell_token': None}` — the view sees
    `processor_type == 'none'` and redirects straight to the thank-you page (PY-022:
    payment section had already collapsed to "No payment needed" at render, because the
    checkout view recomputes the projected total server-side on every summary render, CK-021).
- **Duplicate signalling (OF-004 refinement):** the two "already submitted" `CheckoutError`
  raises become `DuplicateCheckoutError(existing_order)` (subclass of `CheckoutError`, carries
  the order). The Pay-now view redirects to that order's thank-you page when the duplicate
  belongs to the current session (EC-007 UX), instead of showing an error string (§7).
- **`preview_route` pre-flight** now uses the true routed amount
  (`max(subtotal − coupon, 0) + shipping`) and the submitted `method_family` instead of the
  hardcoded `'card'`.
- **Order writes (new columns):** `phone`, `checkout_language`,
  `billing_address = billing_address or shipping_address copy` (U16-5),
  `placed_at = now()` (currently never set at checkout — the spec's "customer confirmed"
  timestamp is Pay-now), `customer_name` from the address `name`.

**Schema migration (`orders`, one migration — all additive):**

| Change | Definition |
|---|---|
| `Order.checkout_language` | CharField(10), blank, default `''` (U16-8 — emails render in it) |
| `Order.shipping_method_name` | CharField(100), blank, default `''` — display snapshot (TH-002) |
| `Order.shipping_rate` | FK `shipping.ShippingRate`, null, SET_NULL, related_name `'orders'` |
| `ChargeType.ZERO_TOTAL` | new choice `'zero_total'` (choices-only AlterField, no DB shape change) |

`cart` migration: `CheckoutState` (§2.1). No other app changes shape.

---

## 6. Decision — URL structure, POST steps, CSRF

**Decision: multi-endpoint POST with Post/Redirect/Get onto one page** — the single-page
progressive layout (CK-001, ADR-012 D5 frozen) served at `GET /checkout/`, with one POST
endpoint per section. **Not** a single-URL AJAX API.

**Options considered:**
1. Single-URL AJAX (`POST /checkout/api/` with a step discriminator) — rejected: violates
   NFR-1 (core flow must work without JavaScript); makes CSRF/error rendering bespoke;
   invisible to standard Django form tooling.
2. **Per-section plain-form POST + redirect back to `/checkout/` (chosen):** every section is
   a plain `<form method="post">`; every POST persists to `CheckoutState` and 302s to
   `/checkout/`, which re-renders with the next section open and the summary recomputed
   server-side (CK-021 — totals never computed in JS). JS is progressive enhancement only
   (inline section swap without full reload), *except* the payment element (below).

**URL map (all in `storefront/urls.py`, registered before the `<path:slug>` catch-all;
views in a new `storefront/views_checkout.py`):**

| Method + path | Behavior |
|---|---|
| `GET /checkout/` | Render the page. No/empty/CONVERTED cart ⇒ 302 `/cart/` (EC-001). Never creates rows. |
| `POST /checkout/contact/` | Validate email (+phone per store setting `checkout_phone_mode`, CO-003), get-or-create `CheckoutState`, write email/phone/opt-in + `Cart.customer_email`, fire `EVT_INITIATE_CHECKOUT` once (`initiate_event_fired` guard), step→ADDRESS, redirect. |
| `POST /checkout/address/` | Validate SA-001…SA-005 (country-adaptive labels/validators from a static table, ML-003), write `shipping_address` (+ optional billing override, U16-5), clear `shipping_rate`, step→SHIPPING, redirect. |
| `POST /checkout/shipping/` | Validate the submitted rate pk against `resolve_shipping_rates` for the stored address (advisory), write `shipping_rate`, step→PAYMENT, redirect. **Does NOT call `begin_checkout`** — deviation from the task sketch, justified below. |
| `POST /checkout/pay/` | The finalization POST (PY-010): void a stale prior PENDING order if the fingerprint changed (§3), call `begin_checkout(...)`. Zero-total ⇒ redirect thank-you. Duplicate ⇒ redirect existing thank-you (§7). Else render the confirm page/section carrying `client_secret` + `processor_type` for the SDK confirm (`return_url = /checkout/payment/return/…`). |
| `GET /checkout/payment/return/` | 3DS/redirect return (PY-012) and post-confirm verify: server-side `retrieve_payment_intent_status`; success/processing ⇒ 302 thank-you; failure ⇒ 302 `/checkout/` with the mapped inline decline message (PY-013), order retained (OF-007). |
| `GET /checkout/retry/<token>/` | Failed-payment retry view (§4). |
| `GET /orders/<public_order_id>/thank-you/` | Thank-you (TH-001). **Deviation from the task sketch's `/checkout/confirmed/<order_ref>/`** — spec TH-001 and ADR-012 D5/D10 already fix `/orders/<public_order_id>/thank-you/`; those are binding. `public_order_id` = `signing.dumps({'order_id': …}, salt='order-thankyou')` — unguessable, store-checked, 404 on tamper (mirrors the DECIDED HMAC resume-token pattern). |

- **Why the Order is NOT created at the shipping POST (task sketch):** PY-010/OF-001 are
  DECIDED — creating the order before Pay-now would mint orders (inventory decremented,
  coupons consumed) for every shopper who abandons at the payment section, precisely the
  phantom-order failure §3 option 2 rejects. The shipping POST persists the advisory
  `shipping_rate` on `CheckoutState`; `begin_checkout` re-validates it (OF-002).
- **CSRF:** Django CSRF middleware on **every** POST (PA-002); no exemptions anywhere in
  checkout (the upsell endpoints' `@csrf_exempt` bearer-token pattern is ADR-011 scope and
  does not extend here). The 3DS return is a GET carrying only the PI reference — no CSRF
  surface; the handler does a server-side status retrieve and never trusts query params for
  money state.
- **NFR-1 boundary (explicit):** contact, address, shipping and the summary work with zero
  JS. The **payment element requires processor-SDK JS** — hosted fields are what keeps card
  data off Pradize servers (SAQ-A, PY-003); a "server-side confirm fallback" accepting raw
  PANs would destroy that posture and is rejected. No-JS/blocked-SDK shoppers get the EC-010
  message ("Payment cannot be loaded…"), never a silently dead Pay button. Zero-total orders
  (§5) complete with zero JS end-to-end.
- **Language (U16-3):** `stores.LocaleMiddleware` gains one line — persist
  `request.locale.language.lang_code` into `request.session['sf_lang']` on every storefront
  page view. Checkout/cart/thank-you views resolve their language as
  `session['sf_lang'] or domain-root-language` — one helper function
  `resolve_checkout_language(request)` (§XV-4), also the value snapshotted into
  `CheckoutState.checkout_language` → `Order.checkout_language`.
- **SEO:** `/checkout/*` and the thank-you page emit `noindex` via the base-template
  `seo_head` block flag and are Disallowed in robots.txt (SEO-001/002 — already the 07 §
  mechanism; no new emission point).

---

## 7. Decision — idempotency and double-submit prevention

**Decision: keep the existing OF-004 mechanism unchanged** — `Order.idempotency_key =
SHA-256(session_key | sorted (variant_id, qty))`, globally unique, pre-check + IntegrityError
savepoint recovery. **No new constraint.**

**Options considered:**
1. `(cart_id, checkout_session_id)` unique constraint on Order — rejected: strictly weaker
   than the existing key (a cart edited between submits *should* be submittable again — the
   fingerprint captures that; a cart-id constraint would block it) and redundant where they
   overlap. Two idempotency mechanisms = two sources of truth that drift.
2. `select_for_update()` on the Cart row at Pay-now — rejected as the primary mechanism:
   serializes the request race but provides no protection across the cart's death
   (post-CONVERTED back-button re-POST), which the key handles. Not needed as a supplement:
   the unique-key INSERT race is already handled by the savepoint recovery.
3. **Existing key, plus UX refinement (chosen).**

**Refinements this ADR adds:**
- `DuplicateCheckoutError(existing_order)` (§5) — the Pay-now view catches it and, when
  `existing_order.idempotency_key` was derived from the **current session** (recompute and
  compare — never trust an order pk from the client), 302s to that order's thank-you page
  (EC-007). A cross-session duplicate (theoretically impossible — the key embeds the session)
  falls back to the generic error.
- Client-side double-click guard on the Pay button (PY-014) — cosmetic; the key is the guard.
- **Session-rotation trap (documented for the Developer):** the key embeds `session_key`, so
  anything that cycles the session mid-checkout (e.g. a future login flow, U16-4) changes the
  key and re-opens double-submit for the same cart. When Phase-5 accounts land, login during
  checkout MUST re-key the cart and `CheckoutState` to the new session key in the same
  transaction. Recorded as a hard note in the login ticket's dependencies; a test asserts the
  invariant `cart.session_key == request.session.session_key` after the (future) login flow.

---

## 8. Implementation plan (tickets for the Developer agent)

> Sequencing: P1 → P2 → P3 → P4 are strictly ordered; P5 (tests) overlaps each phase
> (Test Agent per pipeline). Safety Agent gate after P2 (payments, public forms) — mandatory.
> All templates live in the locked engine template set (ADR-012 D3): themes cannot override
> any checkout partial.

### Phase 1 — Data layer (complexity: M)

| Task | Files | Size |
|---|---|---|
| `CheckoutState` model + migration (§2.1) | `cart/models.py`, `cart/migrations/00XX_checkoutstate.py` | S |
| Orders migration: `checkout_language`, `shipping_method_name`, `shipping_rate` FK, `ChargeType.ZERO_TOTAL` (§5) | `orders/models.py`, `orders/migrations/00XX_checkout_fields.py` | S |
| `begin_checkout()` extension: new signature, shipping resolution + totals, free-shipping coupon, zero-total branch, `DuplicateCheckoutError`, `method_family` pre-flight, new Order writes (§5) | `cart/checkout.py` | M |
| `void_pending_order(order)` service + `void_stale_pending_orders` beat task + `CHECKOUT_PENDING_ORDER_TTL` setting + beat schedule entry (§3) | `orders/tasks.py` (new), `orders/service.py` or inline, `webecom/settings/base.py` | M |

### Phase 2 — Views, forms, URL routing (complexity: L)

| Task | Files | Size |
|---|---|---|
| Checkout views: `checkout_view`, contact/address/shipping/pay POSTs, `payment_return`, `checkout_retry` (§4, §6) with step gating (§2) | `storefront/views_checkout.py` (new) | L |
| URL registration before the catch-all | `storefront/urls.py` | S |
| Forms: `ContactForm` (email validator, phone per `checkout_phone_mode` store setting — default `optional`, setting UI may land later), `AddressForm` (SA-001…SA-005: country whitelist from active zones SA-002, static ISO-3166-2 subdivision table SA-004, per-country postal patterns SA-005, country-adaptive labels ML-003), `ShippingForm` (rate radio validated against `resolve_shipping_rates`) | `storefront/forms_checkout.py` (new) + static address-format table module | M |
| Session-language persistence in `LocaleMiddleware` + `resolve_checkout_language()` helper (U16-3) | `stores/middleware.py` (or current locale middleware module) | S |
| Stripe Elements wiring: deferred-mode mount, confirm on Pay response, 3DS `return_url`, decline-code → translated message map (PY-013); PayPal button path per connector `processor_type` | `storefront/static/storefront/checkout.js` (new), `payments/` message map module | M |
| Thank-you/retry signed-token helpers (salts `order-thankyou`, `checkout-retry`) | `storefront/tokens.py` (new, or `orders/tokens.py`) | S |

### Phase 3 — Templates (complexity: M)

| Task | Files | Size |
|---|---|---|
| `pages/checkout.html` rework: distraction-free header (CK-006), progressive sections with collapse/edit lines (CK-002), step progress indicator, return-to-cart link (CK-004), no-JS-first markup, DOM-contract testids (`checkout-submit`, `coupon-input`, `gift-card-input`, …) | `storefront/templates/storefront/pages/checkout.html` + `partials/checkout_{contact,address,shipping,payment,summary}.html` | L |
| Order summary partial: line items, pre-order dates, coupon/gift-card lines, shipping line, **"All taxes are included in the displayed prices"** on the summary AND per price partial reuse (CK-005 — the existing `partials/price.html` already carries the note; the summary adds the translated line), currency-charge notice (ML-004) | `partials/checkout_summary.html` | M |
| Slots: declare `bump_checkout` in the registry docstring/declared list (16:G3) + `{% render_slot "bump_checkout" %}` between summary items and payment, `{% render_slot "security_badge" %}` under the Pay button (CK-010); suppress `overlay`/`social_proof`/chat on checkout (PA-004) | `storefront/slots.py` (docstring), `pages/checkout.html` | S |
| Coupon/gift-card entry on the summary wired to the existing `apply_discount_code` cart service (PY-020/021 advisory apply; removal chip) | template + small POST endpoint in `views_checkout.py` | M |

### Phase 4 — Thank-you page (complexity: M)

| Task | Files | Size |
|---|---|---|
| `thankyou_view`: signed-token access (TH-001), confirmation content TH-002/003 (ACTIVE items only — `PENDING_CAPTURE` excluded), payment summary, `noindex` | `storefront/views_checkout.py`, `pages/thankyou.html` | M |
| `{% render_slot "thankyou_funnel" %}` embedding (provider is ADR-011/T029 scope — the slot + context contract only here), storewide next-order offer block (TH-006) | `pages/thankyou.html` | S |
| Purchase-event idempotency: `FiredPixel` insert-before-fire on first render post-confirmation (TH-007, §X) — wire the existing table; pixels themselves are the T030 provider | `views_checkout.py` | S |
| Guest account-creation CTA (TH-005) — **placeholder partial, hidden** until Phase-5 accounts exist (CO-005 gating) | `pages/thankyou.html` | S |

### Phase 5 — Tests (Test Agent; acceptance seeds spec §12)

Happy paths and money math:
1. Guest full flow (contact → address → shipping → pay) with coupon + gift card + shipping:
   one Order, `total = subtotal − coupon − gift_card + shipping`, `shipping_amount` and
   `shipping_method_name` set, one AUTHORIZED ORIGINAL OrderCharge, cart CONVERTED,
   `checkout_language`/`placed_at`/`billing_address` populated (seeds 2).
2. Free-shipping coupon zeroes shipping only (OF-006).
3. Zero-total order: no PI, no routing call (assert connector mock untouched),
   `ChargeType.ZERO_TOTAL` CAPTURED row, `payment_status=PAID`, thank-you redirect,
   no funnel session (seed 12 / PY-022).
4. Shipping-rate tamper: submitted rate pk not in server resolution ⇒ `CheckoutError`,
   full rollback (inventory + coupon restored) (OF-002).

Idempotency and state machine:
5. Double POST of Pay-now ⇒ exactly one Order; second request 302s to the same thank-you
   (seeds 3, EC-007).
6. Concurrent last-unit checkout ⇒ exactly one succeeds (seed 4 — regression, existing).
7. Step gating: shipping POST with step=CONTACT ⇒ redirect, no writes; address edit clears
   `shipping_rate` and regresses step.
8. `EVT_INITIATE_CHECKOUT` fires once per `CheckoutState` across reloads.

Retry and GC:
9. Declined confirm → `GET /checkout/retry/<token>` reuses the same PI when
   `requires_payment_method`; success on second attempt; no duplicate order (seed 13).
10. Retry token: tampered signature ⇒ 404; other store's order ⇒ 404; already-paid ⇒
    thank-you redirect; GC-voided ⇒ expired page.
11. GC: PENDING order older than TTL ⇒ CANCELLED, inventory restored, coupon `times_used`
    restored, gift-card balance restored, charge FAILED; authorized order untouched;
    concurrent-confirmation race loses to the conditional UPDATE (§3).
12. Fingerprint change after decline: prior PENDING order voided by the fast path before the
    new order is created; stock held exactly once.

Locale, SEO, isolation:
13. `pradize.com/fr/` browsing then `/checkout/` ⇒ French checkout; `Order.checkout_language='fr'`;
    fresh session ⇒ domain root language (seed 10, U16-3).
14. `noindex` on `/checkout/` and thank-you; thank-you without valid token ⇒ 404, zero PII
    (seeds 9, 11).
15. Thank-you reload fires purchase pixels exactly once (`FiredPixel` assertion, seed 8).
16. Store isolation: store B rate/coupon/cart never usable on store A checkout; retry and
    thank-you tokens store-checked (seed 14).
17. No-JS flow: Django test client completes contact→address→shipping and reaches the payment
    section (and completes a zero-total order end-to-end) with no JS execution.

---

## 9. Open questions (PENDING APPROVAL — none block this ADR)

| # | Item | Recommendation |
|---|---|---|
| Q-A (carries U16-2/3/4/5) | The four working assumptions in the header. | Approve as ratified; each reversal is bounded (retry view ↔ cart re-activation touches only §4; language, login UX, billing toggle are view/template-local). |
| Q-B (U16-6) | Free-shipping threshold messaging (SM-004) needs `min_order_amount` on `ShippingRate`. | **Descoped from this ADR** — checkout ships without threshold messaging; adding the field later is one additive migration + one summary line. Recommend adding the field (spec's own recommendation). |
| Q-C (U16-7) | Shipping-zone "service description" is customer-facing but has no per-language variant. | v1: render it only when the checkout language equals the store default language; otherwise omit (never show wrong-language text). Proper fix = translation-catalog key or per-language column — needs the multilingual-content owner. |
| Q-D | Method-family switch on the retry view (§4) — re-route pre-authorization. | Recommend allowing it in a follow-up ticket (new DecisionLog row, update pinning + ORIGINAL charge in place). v1 retry is same-family only. |
| Q-E | `CHECKOUT_PENDING_ORDER_TTL` default 24 h (§3). | ASSUMPTION — confirm against abandoned-checkout campaign send timings (ADR-010) before release. |
| Q-F | `checkout_phone_mode` store-setting admin UI (CO-003). | Code reads the setting with default `optional`; the admin field ships with the settings-requirements area, not this ADR. |
| Q-G | SA-003 Google Maps autocomplete. | Explicitly out of this ADR's phases (integration ticket once the super-admin credential screen exists); the plain form is the contract. |

## Risks

- **PENDING orders holding FIXED_QTY stock for up to the TTL** — inherent to
  decrement-at-order-creation (frozen OF-001). Bounded by the 24 h GC + the fast-path void;
  visible via the `checkout.gc.voided_pending` log counter (§XV-1).
- **GC vs late webhook race** — a `payment_intent.succeeded` arriving after GC cancelled the
  order. Mitigated: GC claims via conditional UPDATE; the webhook handler's status-guarded
  transition must treat `CANCELLED` as terminal and alert (error-log, greppable) instead of
  resurrecting the order — an authorized-but-cancelled intent is refunded/voided manually.
  Test 11 covers the DB race; the webhook-side guard is a one-line assertion in the existing
  handler.
- **Session-embedded idempotency key vs future login** (§7) — recorded as a hard dependency
  note on the Phase-5 accounts ticket.
- **Payment-element JS dependency** — NFR-1 boundary documented (§6); EC-010 message ensures
  the failure is visible, never a dead button.
- **Advisory `shipping_rate` drift** (rate disabled between selection and Pay) — by design;
  the server re-resolution converts it into a clean, translated `CheckoutError` (OF-002).

## Rollback strategy

- All schema changes are additive (one new table, four nullable/defaulted columns, one
  choices-only alter): reverse migrations drop them without touching pre-existing data.
- The checkout URLs are additions to `storefront/urls.py` before the catch-all; removing them
  restores today's surface (cart page + resume link) with zero effect on other apps.
- `begin_checkout` changes are parameter additions + new branches; the zero-total branch and
  shipping steps are guarded by their inputs — reverting the callers reverts the behavior.
- GC task is an independent beat entry; unregistering it stops all voiding (orders then only
  expire at the processor's auth window, the pre-ADR behavior).
- Feature-level: the storefront can keep `/checkout/` unlinked (cart CTA behind a store
  launch-readiness flag) until the Release Manager gate passes.

## Tests required

See Phase 5 (§8) — seeds 1–14 of spec §12 mapped to tests 1–17, plus the GC/retry/locale
suites. Safety Agent review is mandatory after Phase 2 (public forms, payment views, signed
tokens) per the standard pipeline.

---

## Addendum (2026-07-10, Architect) — sf_lang unconditional-write bug and the write-exemption invariant

**Bug (pre-existing, found during ADR-021 implementation).** §6 ("Language (U16-3)")
specified that `LocaleMiddleware` persist `request.locale.language.lang_code` into
`session['sf_lang']` "on every storefront page view", and that checkout-family views
resolve their language as `session['sf_lang'] or domain-root-language` via
`resolve_checkout_language()`. As implemented, the write ran unconditionally on **every**
resolved request — including requests to the fixed, un-prefixed routes themselves
(`/checkout/`, `/cart/`, `/search/`, `/orders/…/thank-you/`). Because those routes are
registered before the `<path:slug>` catch-all, `resolve_locale` always resolves them to
the domain's ROOT language; the middleware therefore overwrote the buyer's real
browsing-language value with the root language **in the same request/response cycle**,
before any checkout view could read it. Net effect: the "session sf_lang first" priority
in `resolve_checkout_language` was a permanent no-op — `Order.checkout_language` could
never differ from the root language, silently defeating this ADR's U16-3 design.

**Fix.** `stores/middleware.py::LocaleMiddleware` gained
`SF_LANG_WRITE_EXEMPT_PREFIXES`: paths matching these prefixes are exempt from the
session **write only** — locale resolution, translation activation, `request.locale`,
and `Content-Language` behavior are unchanged. Current entries:

- `/checkout/`, `/cart/`, `/search/`, `/orders/` — the §6 URL map's fixed routes
  (the original fix).
- `/campaigns/upsell/` — found during architecture review: the thank-you page's upsell
  widget POSTs to the bare `/campaigns/upsell/accept|decline/` endpoints and then
  `window.location.reload()`s the same thank-you URL, so the un-exempted POST clobbered
  `sf_lang` with the root language and the reload re-rendered the thank-you page (and
  any chained upsell step) in the wrong language. Regression test SF-LANG-UPSELL
  (`campaigns/tests/test_sf_lang_upsell_regression.py`, PROVEN in
  `BUG_TESTS/BUG_TESTS.csv`: fails without the entry, passes with it).
- `/products/` — the T029 notify-me/quotation form POST endpoints, found by the drift
  test below. Caveat recorded in the middleware comment: this entry must only ever
  cover fixed action endpoints; if a future ADR puts genuine product *browsing* pages
  under a literal `/products/` prefix, it must be narrowed.

**Invariant (binding on all future URL registrations):** *fixed, un-prefixed storefront
routes must never write `sf_lang`.* Only language-prefixed browsing requests (the
permalink catch-all) and the explicit browsing entry points (home; crawler-facing
sitemap/robots endpoints, which carry no shopper session continuity) may establish or
change it. Enforcement is automated:
`stores/tests/test_sf_lang_write_exemption_drift.py` walks the resolved root URLconf
and fails if any fixed leaf path is neither covered by `SF_LANG_WRITE_EXEMPT_PREFIXES`
nor explicitly listed in its `_ALLOWED_BROWSING_ENTRY_POINTS` allow-list — so every new
fixed route mounted before the storefront catch-all forces a deliberate categorization
decision rather than silently inheriting the write.

**Why a static list + drift test, not derivation from urlpatterns:** the correct
exemption set is not syntactically determinable — home (`/`) legitimately *should*
write `sf_lang` (landing on the bare root is a real browsing-language signal), while
checkout/cart/search/orders/upsell/product-form routes are funnel-continuation actions
that must preserve prior state. The drift test provides the completeness guarantee
derivation would have offered, without encoding a wrong heuristic.

Tests: 2280 project-wide green at sign-off, including ADR-021 T5 (browse `/fr/` → bare
`/checkout/` renders French) and both SF-LANG-UPSELL regression tests.
