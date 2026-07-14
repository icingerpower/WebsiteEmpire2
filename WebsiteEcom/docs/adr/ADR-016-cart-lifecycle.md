# ADR-016 — Post-purchase cart session_key rotation

**Status:** Accepted  
**Date:** 2026-07-06  
**Deciders:** Architect

---

## Context

After a successful checkout, `cart.status` is set to `CONVERTED`. However, the
`unique_together(store, session_key)` constraint on `Cart` prevented
`get_or_create_cart` from creating a fresh `ACTIVE` cart for the same browser
session: the old session key was still occupied by the converted cart.

This caused two problems:

1. `get_or_create_cart(store, session_key)` returned the already-converted cart,
   allowing subsequent `add_item` calls to add items to a completed order's cart.
2. The mini-cart in `storefront/context_processors.py` picked up the converted cart
   and showed stale items to the customer after purchase.

## Decision

On checkout completion, rotate the converted cart's `session_key` to
`"converted-{cart.pk}"`. This frees the original session key for a new `ACTIVE`
cart without any schema change.

### Why rotation over other approaches

| Option | Tradeoff |
|---|---|
| Rotate session_key (chosen) | No schema change, simple, reversible for the session |
| Allow multiple carts per session | Requires schema change; complicates get_or_create_cart query |
| Delete cart items on conversion | Loses audit trail; cascade risk on CartItem FK |
| Soft-delete cart | Adds complexity; same root problem remains |

## Implementation

### `cart/checkout.py`

At **both** CONVERTED save points (zero-total path and normal step 13):

```python
cart.status = CartStatus.CONVERTED
cart.session_key = f'converted-{cart.pk}'
cart.save(update_fields=['status', 'session_key', 'updated_at'])
```

Both saves are inside `@transaction.atomic` / savepoints — the rotation rolls back
atomically if the transaction fails, which leaves the cart `ACTIVE` with its
original session key.

`cart.pk` is guaranteed non-`None` at both sites: the cart was retrieved from the
database by `get_or_create_cart` before `begin_checkout` is called.

### `cart/service.py` — `get_or_create_cart`

Added `status=CartStatus.ACTIVE` filter to the lookup. This is defense-in-depth:

- Converted carts are unreachable by their original session key after rotation.
- Abandoned carts (never rotated) are excluded so a returning user gets a fresh cart
  rather than an old abandoned one.

### `storefront/context_processors.py` — `_get_mini_cart_context`

Added `status=CartStatus.ACTIVE` filter to the cart lookup so the mini-cart
immediately shows empty after a completed purchase.

### `cart/migrations/0005_backfill_converted_cart_session_keys.py`

Data migration: rotates all pre-existing `CONVERTED` rows that were created before
this ADR (session_key does not yet start with `"converted-"`). Reversal is a no-op.

## Sentinel value format

`"converted-{pk}"` where `pk` is the Django auto-increment integer PK.

- Maximum length: `len("converted-") + 19 = 29` chars (19 = max 64-bit int digits).
- `Cart.session_key.max_length = 40` — fits with 11 chars to spare.
- Prefix `"converted-"` is safe: Django session keys are 32-char hex strings and
  never start with a letter sequence followed by a dash.

## Consequences

- A user who completes a purchase and immediately adds another item receives a fresh
  empty cart as expected.
- The mini-cart shows 0 items immediately after checkout.
- CONVERTED carts remain in the database with all their CartItems for auditing and
  potential Phase 2 features (order history, reorder).
- ABANDONED carts are excluded from the active session by the `status=ACTIVE` filter;
  Phase 2 abandoned-cart recovery handles them separately.
- The idempotency key computed in `_make_idempotency_key` uses `cart.session_key` at
  the time `begin_checkout` is called (before rotation), so duplicate-submit detection
  is unaffected.
