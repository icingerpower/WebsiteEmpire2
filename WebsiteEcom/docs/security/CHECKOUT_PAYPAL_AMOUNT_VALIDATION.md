# PayPal Amount Validation — Checkout Flow Security Findings

Audit date: 2026-07-06 · Scope: PayPal amount handling in the Pradize checkout flow
(re-identification of the H1 finding lost in context compaction).

Files reviewed: `payments/paypal_connector.py`, `payments/paypal_webhook_views.py`,
`payments/webhook_views.py` (Stripe, for comparison), `payments/processor.py`,
`cart/checkout.py` (`begin_checkout`), `storefront/views_checkout.py`
(`checkout_pay_post`, `checkout_payment_return`), `campaigns/upsell_service.py`,
`campaigns/tasks.py` (capture watchdog), `orders/models.py` (OrderCharge).

---

## PayPal Amount Validation Security Findings

### H1 — PayPal PAYMENT.CAPTURE.COMPLETED handler marks the Order PAID without validating the captured amount or currency

**Severity**: HIGH
**Location**: `payments/paypal_webhook_views.py:301-381` (`_handle_capture_completed`;
PAID transition at lines 343-344, volume accrual at 352-353)

**Description**:
The Stripe webhook handler has an explicit forged-amount defense before the PAID
transition ("H1 step 4", `payments/webhook_views.py:252-269`): the event's
`amount_received` and `currency` must equal `int(order.total * 100)` and
`order.currency`, or the event is logged as `payments.webhook.amount_mismatch`
and ignored.

The PayPal equivalent has **no such check**. `_handle_capture_completed` resolves
the Order (via `custom_id` or the `OrderCharge.processor_charge_id` fallback),
verifies account binding, and then unconditionally:

1. transitions `payment_status` → `PAID`,
2. accrues monthly routing volume using `order.total` (via `_accrue_volume`),
3. fires the server-side purchase analytics event,
4. suppresses abandoned-checkout campaigns.

The PayPal capture resource carries `resource['amount']['value']` and
`resource['amount']['currency_code']` — the data is available in the event but
is never compared against the order.

PayPal explicitly supports **partial captures** (the connector itself exposes
`amount_to_capture` in `capture_payment_intent`, `payments/paypal_connector.py:162-234`,
for upsell item declines). A `PAYMENT.CAPTURE.COMPLETED` event is therefore not
a guarantee that `order.total` was collected.

**Attack / failure scenario**:
- A capture issued for less than the authorized amount (partial capture from the
  PayPal merchant console, a mis-scripted ops action, a future partial-capture
  code path, or a PayPal-side amount adjustment) delivers a legitimately signed
  `PAYMENT.CAPTURE.COMPLETED` for e.g. 0.01 EUR. The order is marked fully PAID,
  fulfillment proceeds for the full order, monthly volume is over-accrued with
  `order.total`, and the revenue shortfall is invisible.
- A currency mismatch (order in EUR, capture completed in USD due to account
  misconfiguration) is likewise accepted silently.
- Cross-account forgery is already blocked by the `_order_bound_to_account`
  check, so the residual attacker model is a PayPal-signed event whose amount
  differs from the order — exactly the class the Stripe handler defends against.

**Recommended fix** (mirror Stripe's H1 step 4, in `_handle_capture_completed`,
after the `_order_bound_to_account` check and the already-PAID idempotency guard,
before the PAID transition):

```python
from decimal import Decimal, InvalidOperation

amount_obj = resource.get('amount') or {}
try:
    captured_cents = int((Decimal(str(amount_obj.get('value', ''))) * 100)
                         .to_integral_value())
except (InvalidOperation, TypeError):
    captured_cents = None
event_currency = (amount_obj.get('currency_code') or '')
expected_cents = int(order.total * 100)

if captured_cents != expected_cents or event_currency.upper() != (order.currency or '').upper():
    logger.warning(
        'payments.webhook.amount_mismatch: PAYMENT.CAPTURE.COMPLETED for order %s '
        'carries amount=%r currency=%r but the order expects %s %s — event ignored.',
        order.pk, amount_obj.get('value'), event_currency,
        expected_cents, order.currency,
    )
    return
```

Notes:
- Use `Decimal`, **not** `int(float(v) * 100)` (see M1 — float truncation).
- Today the ORIGINAL charge is always captured in full (both call sites —
  `campaigns/upsell_service.py:469` and `campaigns/tasks.py:861/:959` — pass no
  `amount_to_capture`), so an exact-match check is correct. If partial capture
  is ever introduced for PayPal ORIGINAL charges, compare against the matching
  `OrderCharge.amount_cents` instead (same caveat already documented in the
  Stripe handler's docstring).
- Log the mismatch on the `payments.webhook` security channel like the Stripe
  handler so all binding/amount violations land in one place.

---

### M1 — Float truncation when converting PayPal decimal amounts to cents

**Severity**: MEDIUM
**Location**: `payments/paypal_connector.py:219-220` (`capture_payment_intent`)
and `payments/paypal_connector.py:333-334` (`refund`)

**Description**:
```python
captured_cents = int(float(captured_value) * 100)
```
`float('83.55') * 100 == 8354.999999999999`; `int()` truncates to **8354** —
a systematic 1-cent under-report for a large class of decimal values. The same
pattern is used for `amount_refunded`.

Today `ChargeResult.amount_captured` is not consumed by any validation logic
(verified by grep across `campaigns/`, `payments/`, `orders/`), so the impact
is latent. But:
- the H1 fix above requires an exact cents comparison — implementing it with
  this float pattern would produce spurious mismatches that block legitimate
  PAID transitions (payment DoS on ~half of all amounts);
- `amount_refunded` feeds refund accounting (refund cap = Σcaptured − Σrefunded,
  ADR-007 §7), where 1-cent drift accumulates per refund.

**Attack scenario**: not directly attacker-triggerable; this is a correctness
defect that silently corrupts financial reconciliation and would undermine the
H1 fix.

**Recommended fix**:
```python
from decimal import Decimal
captured_cents = int((Decimal(captured_value) * 100).to_integral_value())
```
Apply the same to `refund()`. The forward conversion `f'{amount / 100:.2f}'`
(lines 120, 206, 326) is safe for realistic amounts but should also move to
`Decimal(amount) / 100` for consistency.

---

### L1 — `create_payment_intent` performs no amount sanity validation

**Severity**: LOW (defense in depth)
**Location**: `payments/paypal_connector.py:82-160`

**Description**: neither the PayPal connector nor the Stripe connector rejects
`amount <= 0`. Upstream, `begin_checkout` guarantees the amount is positive
(`order.total = max(subtotal - coupon, 0) + shipping`, and the zero-total guard
at `cart/checkout.py:487` diverts `total == 0` orders before PI creation), and
upsell amounts come from admin-controlled campaign steps. No exploit path was
found, but a future caller could pass a zero/negative amount and get undefined
processor behavior.

**Recommended fix**: raise/return `success=False` for `amount <= 0` at the top
of `create_payment_intent` and `create_off_session_charge` in the abstract
contract (`payments/processor.py`) or in each connector.

---

### L2 — Partial refund hardcodes `currency_code='USD'`

**Severity**: LOW (fails closed)
**Location**: `payments/paypal_connector.py:327` (already flagged with a TODO)

**Description**: partial refunds for non-USD stores send the wrong currency;
PayPal rejects mismatched-currency refunds, so this fails closed (refund error,
no money movement), but it blocks legitimate partial refunds on non-USD stores.
Resolve the currency from the OrderCharge/Order as the TODO says.

---

## Answers to the audit questions (no-issue confirmations)

1. **Amount fixation race** — NOT vulnerable. The PI amount is computed from
   `order.total` (post-coupon, post-gift-card, server-re-validated shipping
   rate) entirely inside the single `@transaction.atomic` block of
   `begin_checkout` (`cart/checkout.py:581-589`). The cart is marked CONVERTED
   and its `session_key` rotated in the same transaction, so subsequent cart
   edits go to a fresh ACTIVE cart. Re-submitting the converted cart hits the
   idempotency key (`cart_pk` + item fingerprint) and raises
   `DuplicateCheckoutError`. The gift-card stale-balance case is explicitly
   handled (the post-`apply_gift_card` `order.total` is used, not the advisory
   cart value).

2. **PayPal redirect replay** — NOT vulnerable to amount tampering. The amount
   is never embedded in any client-visible URL or token; the approve URL binds
   to a PayPal order whose amount was fixed server-side at creation, with
   `custom_id` = our order PK. `checkout_payment_return` never trusts
   `redirect_status` for the success transition — it retrieves the PI status
   server-side (`storefront/views_checkout.py:806-827`).
   **Functional observation (not a vulnerability, fails closed)**: the PayPal
   connector's `retrieve_payment_intent_raw_status` is a Phase-1 stub returning
   `''`, so a returning PayPal buyer always lands on the decline branch of
   `checkout_payment_return`; and no code path calls PayPal's
   `POST /v2/checkout/orders/{id}/authorize` after buyer approval, so the
   authorization the capture watchdog depends on may never exist. PayPal orders
   can therefore get stuck PENDING — a completeness gap to fix alongside H1,
   not an exploitable weakness.

3. **Crafted `amount` in POST** — NOT vulnerable. `checkout_pay_post` reads no
   amount-bearing field from the request; everything financial comes from
   server-side `CheckoutState` + cart rows, and `shipping_rate_id` is
   re-validated against `resolve_shipping_rates` both in the view
   (`views_checkout.py:587-598`) and again inside `begin_checkout`
   (`cart/checkout.py:266-278`).

4. **Webhook amount validation** — Stripe: YES (`webhook_views.py:252-269`).
   PayPal: **NO** → finding H1 above.

5. **Zero-amount bypass** — NOT vulnerable. The zero-total path requires
   `order.total == Decimal('0')` computed server-side after atomic coupon and
   gift-card application; no request parameter can force a non-zero order into
   it, and a zero-total order never reaches a processor.

---

## Required tests (for the After-Bug Test Agent once H1/M1 are fixed)

- `PAYMENT.CAPTURE.COMPLETED` with `amount.value` ≠ `order.total` → order stays
  PENDING, no volume accrual, no purchase event, `amount_mismatch` warning logged.
- Same event with mismatched `currency_code` → ignored.
- Matching amount/currency → PAID transition proceeds (regression guard).
- Amounts like `'83.55'` round-trip to exactly 8355 cents through the fixed
  Decimal conversion in `capture_payment_intent` and `refund`.
- Duplicate delivery of a valid COMPLETED event remains idempotent after the fix.

## Verdict

**BLOCKED — H1 must be fixed before PayPal checkout goes live.** The PAID
transition is the single money-state gate for fulfillment, analytics, volume
accrual and campaign suppression; it must not fire on an unvalidated amount
when the sibling Stripe handler already enforces exactly this check. M1 must be
fixed together with H1 (the amount check depends on correct cents conversion).
L1/L2 and the question-2 functional observation can follow as normal tickets.
