# Checkout Batch 2 — Security Audit

Date: 2026-07-10
Scope: country-meta endpoint, address validation (ADR-017), FiredPixel purchase
guard, void_pending_order / GC task, PayPal H1 amount-guard verification,
checkout.js pre-loaded client_secret path.
Auditor: Safety Agent (defensive review — no production code modified).

Files reviewed:
- `storefront/views_checkout.py`, `storefront/address_meta.py`, `storefront/forms_checkout.py`, `storefront/tokens.py`, `storefront/urls.py`
- `storefront/static/storefront/js/checkout.js`
- `storefront/templates/storefront/pages/thank_you.html`, `checkout.html`, `checkout_payment.html`
- `orders/service.py`, `orders/models.py` (FiredPixel), `orders/admin.py`
- `cart/service.py`, `cart/tasks.py`
- `discounts/service.py` (restore paths)
- `payments/paypal_webhook_views.py`, `payments/webhook_views.py`
- `emails/signals.py`, `emails/templates/emails/order_confirmation.html`
- `analytics/static/analytics/beacon.js`, `webecom/settings/base.py`

---

## Findings

### MEDIUM-1 — Purchase-pixel guard is burned on first thank-you render, regardless of payment status

**Location:** `storefront/views_checkout.py::order_thank_you` (FiredPixel
`get_or_create`, lines ~1240–1245); `storefront/tokens.py` (thank-you token has
no max_age by design).

**Attack / failure scenarios:**
1. *Leaked token:* the thank-you token never expires and encodes only the order
   PK. Anyone holding a leaked URL (forwarded email, shared link, synced browser
   history, upstream access log) can GET the page once; the sentinel row
   (`pixel_type='__purchase_guard__'`) is created and `fire_purchase_pixel`
   becomes `False` forever for the real customer's visit. Once T030 pixel
   providers consume this flag, purchase attribution for that order is
   permanently lost. No money moves; impact is analytics/marketing integrity.
2. *Self-burn (more likely in practice):* the guard is created even when the
   order is still `PENDING` (e.g. the `processing` redirect path in
   `checkout_payment_return` sends the shopper to thank-you before the webhook
   lands). If T030 gates actual pixel firing on `payment_status == PAID`, the
   flag is already consumed by the time the order becomes PAID → pixel never
   fires, for every slow-settling order — a systematic attribution gap, not an
   edge case.

**Recommended fix (before T030 lands):**
- Only create the guard row when `order.payment_status == PaymentStatus.PAID`
  (or at minimum `authorized_at` is set), and re-render `fire_purchase_pixel`
  accordingly on later visits.
- Alternatively key the fire decision on `created AND order is PAID`.
- Add a regression test: thank-you GET on a PENDING order must NOT consume the
  guard; a later GET after PAID must fire exactly once.

**Verified safe:** row growth is bounded — `unique_together (order, pixel_type,
event)` with a single sentinel type means at most one row per order, and row
creation requires a validly signed token. No unbounded write path.

---

### MEDIUM-2 — void_pending_order can race a payment that succeeded at the processor but is not yet recorded locally

**Location:** `orders/service.py::void_pending_order`,
`cart/tasks.py::void_stale_pending_orders`,
`payments/webhook_views.py::_handle_payment_intent_succeeded`,
`storefront/views_checkout.py::checkout_pay_post` (fast-path void) and
`checkout_payment_return`.

**Analysis of the guards (as requested):** the DB-side transition is correct —
a single conditional UPDATE `PENDING → CANCELLED WHERE authorized_at IS NULL`
means a concurrent confirmation that has already stamped `authorized_at` or
PAID wins cleanly, and the GC task re-checks under `select_for_update` on the
CheckoutState row. The gap is that **processor-side state is never consulted**:
`authorized_at` is only written by `checkout_payment_return` (browser must come
back) or by webhooks (delivery latency). Between `stripe.confirmPayment()`
succeeding at Stripe and either of those writes, the order still satisfies
`PENDING AND authorized_at IS NULL`.

**Race sequence:**
1. Shopper resumes a >24h-stale checkout (exactly the population the ADR-010
   retry/recovery emails invite back) and confirms the card.
2. GC (or the pay-view fast path in a parallel tab) voids the order: restores
   FIXED_QTY stock, restores coupon `times_used` / gift-card balance, marks
   charges FAILED, deletes the CheckoutState.
3. Best-effort PI cancel fails — the PI already succeeded (Stripe rejects
   cancel of a succeeded intent).
4. `payment_intent.succeeded` arrives. `_handle_payment_intent_succeeded` has
   **no payment_status guard other than "already PAID"** — it transitions
   `CANCELLED → PAID` and accrues volume, but never re-consumes the stock and
   discount counters restored in step 2.

**Result:** a PAID order whose inventory was over-restored (oversell risk) and
whose coupon counter was under-counted; money captured while resources were
released. The window is seconds wide, but the stale-checkout retry flow makes
the preconditions realistic.

**Recommended fixes:**
1. In `void_pending_order`, cancel the PI **first** (or retrieve its raw
   status) and only perform the local void when the processor cancel succeeded
   or the PI is already in a canceled/failed state. If the PI is
   `succeeded`/`requires_capture`/`processing`, skip the void and let the
   webhook path finish.
2. In `_handle_payment_intent_succeeded` (and the PayPal capture handler,
   which skips non-PENDING via the PAID check only), detect
   `payment_status == CANCELLED` explicitly: either re-decrement stock/discount
   atomically before the PAID transition, or refuse the silent transition and
   raise a reconciliation alert (`payments.webhook.paid_after_void`).
3. Test: simulate void-then-succeeded-webhook ordering and assert inventory and
   coupon counters end consistent.

---

### LOW-1 — GC staleness clock uses `updated_at` that is not touched by the payment-step writes

**Location:** `cart/tasks.py::void_stale_pending_orders` (filter
`updated_at__lt=cutoff`); `storefront/views_checkout.py::checkout_pay_post`
(`CheckoutState.objects.filter(pk=...).update(order_id=...)`) and
`checkout_payment_return` (`.update(step=CONFIRMED)`).

QuerySet `.update()` bypasses `auto_now`, so `CheckoutState.updated_at` still
reflects the last form-step `save()` (shipping/address), not order/PI creation.
A shopper who idles ~24h at the shipping step and then pays creates an order
whose CheckoutState is *already* GC-eligible — the effective TTL for the order
can be minutes, widening the MEDIUM-2 window.

**Fix:** include `updated_at=timezone.now()` in the two `.update()` calls, or
additionally filter the GC query on `order__created_at__lt=cutoff`.

---

### LOW-2 — checkout_payment_return stamps `authorized_at` and redirects to thank-you without checking payment_status

**Location:** `storefront/views_checkout.py::checkout_payment_return`
(the `authorized_at` conditional UPDATE filters only `pk` +
`authorized_at__isnull`).

After a void (MEDIUM-2 sequence, or a legitimately GC-voided order whose PI
cancel failed), a returning shopper with `pi_status='succeeded'` gets
`authorized_at` stamped on a CANCELLED order and sees a thank-you page for a
voided order. Converges to PAID only via the (currently unguarded) succeeded
webhook. Add `payment_status=PaymentStatus.PENDING` to the UPDATE filter and
branch to a "payment received, order under review" path when the order is not
PENDING/PAID. (CheckoutState deletion itself is harmless here — the
step-CONFIRMED update simply matches 0 rows; the view never dereferences the
state. Confirmed no crash path.)

---

### LOW-3 — No rate limiting on public checkout endpoints

**Location:** `storefront/urls.py` (`/checkout/address/country-meta/`,
`/orders/<token>/thank-you/`, `/checkout/retry/<token>/`); no `RATELIMIT`/
throttle configuration anywhere in `webecom/settings/`.

- `country_meta_view` is cheap (in-memory dict lookup; largest payload is IT
  with 107 provinces — a few KB) and carries `Cache-Control: public,
  max-age=86400`, so a CDN absorbs repeat traffic. Residual DoS surface is the
  generic "any Django view" one.
- Token endpoints: brute-forcing signed tokens is computationally infeasible
  (SECRET_KEY HMAC), but each guess costs a signature check + 404. 
Deploy-level throttling (nginx `limit_req` or django-ratelimit) is the right
control; record it in DEPLOYMENT_HARDENING.

---

### INFO-1 — country-meta endpoint: injection / leakage review (clean)

`country` param is stripped/uppercased and used **only as a dict key**
(`COUNTRY_ADDRESS_META.get(...)`); the response echoes `meta.country` from the
static dataclass, never the raw input — no reflected content. `JsonResponse`
sets `application/json`. The view never touches `request.store`; nothing
store-specific is exposed. One correctness note: `Vary: Accept-Language` is
only sufficient if the active language is derived from the URL/path or the
header; `stores.middleware.LocaleMiddleware` and the `sf_lang` session key
suggest cookie/session influence — a shared cache could then serve
wrong-language labels (no PII; cosmetic). Confirm the language source or drop
`public` caching.

### INFO-2 — Address validation review (clean)

- **Stored XSS:** all address values pass through bound Django form fields with
  length caps (line1/line2 255, city/state 100, postal 20, names 100);
  `to_address_dict()` reads `cleaned_data` only after `is_valid()`, and the
  only writer of `CheckoutState.shipping_address` is `checkout_address_post`.
  Every render path found is autoescaped: `thank_you.html:94-103`,
  `checkout.html:91`, `emails/templates/emails/order_confirmation.html:40-47`
  (context built in `emails/signals.py::_build_order_context` as plain strings),
  and `orders/admin.py` exposes `shipping_address` as a readonly field
  (escaped by the admin). No `|safe` / `format_html` / `mark_safe` on address
  data anywhere. Values are stored unsanitized, so any *future* raw-HTML
  rendering would become stored XSS — add an escaping regression test to keep
  this invariant.
- **`_nfd_lower`:** total function on any Python `str` (NFD normalize → ascii
  ignore-encode → lower/strip); malformed byte sequences are rejected earlier
  by Django's request decoding. No crash or bypass; select-mode subdivisions
  always normalize to a canonical code from the static tuple.
- **ReDoS:** all postal patterns reviewed — simple character classes and
  bounded quantifiers, no nested/overlapping quantifiers; inputs capped at 20
  chars and matched with `re.fullmatch` after normalization. Not exploitable.

### INFO-3 — PayPal H1 fix verification (confirmed correct)

`payments/paypal_webhook_views.py::_handle_capture_completed`:
- Guard ordering verified: order load under `select_for_update` → H1 account
  binding (`_order_bound_to_account`, line ~332) → already-PAID idempotency
  skip (line ~337) → amount/currency guard (lines ~349–374) → PAID transition
  (line ~376). Correct placement: a forged/partial amount can never reach the
  PAID transition, and duplicate deliveries exit before the guard.
- `int(order.total * 100)` is exact: `order.total` is a `DecimalField`
  (max_digits=12, dp=2) loaded fresh from the DB inside the transaction, so the
  arithmetic is pure Decimal (`Decimal('83.55') * 100 == Decimal('8355.00')`).
  The event side parses via `int(Decimal(str(value)) * 100)`. No float
  contamination on either side.
- Partial-capture bypass: `_handle_capture_completed` is the **only** PAID
  transition in the PayPal webhook module; a partial capture mismatches and is
  ignored (logged as `payments.webhook.amount_mismatch`). Multiple sequential
  partial captures are each compared against the full total individually and
  never accumulate — no bypass. Ops note: a legitimate partial capture leaves
  the order PENDING forever; acceptable, already surfaced via warning logs.
- Stripe symmetry: `_handle_payment_intent_succeeded` applies the same
  amount/currency guard before any financial mutation (verified).

### INFO-4 — checkout.js pre-loaded client_secret path (acceptable, two notes)

- The client_secret is *designed* to be client-side; exposure in
  `data-client-secret` on `checkout_payment.html` (a POST response — no cache
  middleware configured, not a GET-cacheable page) is acceptable.
- Not logged: grep confirms no Python log statement includes a client_secret
  (only PI ids are logged, e.g. `checkout_retry`); the Stripe connector logs
  errors without the secret.
- Not beaconed: `analytics/static/analytics/beacon.js` sends
  `location.pathname`, `document.referrer` and the five `utm_*` params only —
  it never captures the raw query string, so Stripe's
  `?payment_intent_client_secret=...` return-redirect param is not ingested
  into analytics.
- Residual notes: (a) that return-redirect query string *will* appear in web
  server / proxy access logs — standard Stripe pattern; add a log-scrubbing or
  acceptance note to DEPLOYMENT_HARDENING. (b) `SecurityMiddleware` is active
  with no `SECURE_REFERRER_POLICY` override, so Django's default
  `Referrer-Policy: same-origin` prevents thank-you tokens and return-URL
  secrets from leaking via outbound-link Referer headers — keep it that way.

### INFO-5 — Discount double-restore (verified safe)

`restore_discount_on_failed_payment` is internally non-idempotent for gift
cards (reversal rows would double-restore — the docstring says so), but every
caller is status-gated and mutually exclusive:
- `void_pending_order` runs it at most once (conditional-UPDATE winner;
  repeated voids return `False` before touching discounts).
- Stripe `payment_intent.canceled` / `payment_intent.payment_failed` and PayPal
  `AUTHORIZATION.VOIDED` / `CAPTURE.DENIED` all skip unless
  `payment_status == PENDING`; after a void the order is CANCELLED, so the
  webhook triggered by the best-effort PI cancel is a no-op.
The coupon decrement additionally floors at 0 (`times_used__gt=0`). Keep the
"callers must status-gate" invariant documented and covered by a test — it is
the only thing standing between the gift-card ledger and double-restores.

---

## Human checklist (live verification)

- [ ] Confirm CDN/proxy in front of production strips or accepts
      `payment_intent_client_secret` in access logs (INFO-4a).
- [ ] Confirm the active-language source for storefront requests (path vs
      session cookie) and adjust country-meta cache headers if session-based
      (INFO-1).
- [ ] Add nginx `limit_req` (or equivalent) for `/checkout/` and token URLs
      before public launch (LOW-3).

## Verdict

**CLEAR-WITH-NOTES**

No exploitable path to money movement, cross-store data access, injection, or
PII leakage was found in the audited batch. Two MEDIUM findings must be
ticketed before their dependent features go live:
- MEDIUM-1 must be fixed **before T030 pixel providers ship** (the guard flag
  is consumed by nothing today, so it does not block this release).
- MEDIUM-2 (+ LOW-1, LOW-2 which widen it) should be scheduled with the
  after-bug-test agent for a proven regression test around the
  void-vs-succeeded-webhook ordering.
