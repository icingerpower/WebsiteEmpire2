"""
Discount application service (ADR-002).

Apply order: coupon FIRST (percentage/fixed/shipping reduction), gift card SECOND
(covers remainder). Atomic increment: DiscountCode.times_used is updated atomically
via a single conditional UPDATE (ADR-002 §4) — prevents TOCTOU races without a
row-level lock. Gift card balance decrement happens inside the same DB transaction
as the order payment record write — rollback restores balance.

Public API:
  calculate_coupon_discount(discount_code, order_amount) -> Decimal
  apply_coupon(discount_code, order_amount, customer_email) -> Decimal
  apply_gift_card(gift_card, amount_to_spend, order=None) -> Decimal
  restore_discount_on_failed_payment(order) -> None

Raises DiscountError for all user-visible failure modes.

---

ADR-029 (TICKET-045) — automated gift-card campaigns. Added to this module
rather than a new one: the entire write-path is DiscountCode/CampaignReward
rows, the same invariants (atomic counters, idempotency-before-code) this file
already owns.

  campaign_matches_order(campaign, order) -> bool
  resolve_campaign_expiry(campaign, now=None) -> datetime | None
  issue_campaign_reward_for_order(campaign, order, recipient_email) -> DiscountCode | None
  send_gift_card_campaign_reward_email(discount_code, order) -> bool
  void_or_flag_campaign_gift_cards_for_refund(order) -> None
"""

import logging
import secrets
from datetime import datetime, time, timedelta, timezone as dt_timezone
from decimal import Decimal

from django.db import IntegrityError, transaction
from django.db.models import F, Q
from django.utils import timezone

from .models import (
    CampaignReward,
    DiscountCode,
    DiscountCodeEmailUse,
    DiscountType,
    GiftCardCampaign,
    GiftCardCampaignCapWindowUnit,
    GiftCardCampaignExpiryMode,
    GiftCardCampaignStatus,
    GiftCardCampaignTriggerScope,
    GiftCardTransaction,
    ProvenanceType,
    ValueType,
)

logger = logging.getLogger('discounts.service')


class DiscountError(Exception):
    """Raised when a discount cannot be applied (invalid code, expired, limit reached, etc.)."""


def _validate_coupon(
    discount_code: DiscountCode,
    order_amount: Decimal,
    customer_email: str,
    gift_card_already_applied: bool = False,
):
    """
    Advisory validation of a coupon before the conditional UPDATE (ADR-002 §4).
    Raises DiscountError if the coupon is clearly invalid.

    This check is intentionally non-locking: it runs on the coupon object as
    supplied by the caller (may be slightly stale). The usage_limit race is
    handled by the subsequent _increment_times_used conditional UPDATE, which is
    the authoritative gating point. All checks here (date window, minimum order,
    stackable, per-email limit) are advisory fast-fail guards.

    Parameters
    ----------
    discount_code:
        The DiscountCode to validate (no lock required).
    order_amount:
        Current cart total (before discount).
    customer_email:
        Customer email for per-email limit check; empty string skips the check.
    gift_card_already_applied:
        Pass True if the cart already has a gift card applied. When True and the
        coupon has stackable=False, raises DiscountError (stackable opt-out).
    """
    if not discount_code.is_valid_now():
        raise DiscountError("This coupon code is no longer valid.")
    if order_amount < discount_code.minimum_order_amount:
        raise DiscountError(
            f"Minimum order amount of {discount_code.minimum_order_amount} required."
        )
    if gift_card_already_applied and not discount_code.stackable:
        raise DiscountError(
            "This coupon cannot be combined with other discounts."
        )
    if customer_email:
        email_use = DiscountCodeEmailUse.objects.filter(
            discount_code=discount_code,
            email=customer_email.lower(),
        ).first()
        if email_use and email_use.use_count >= discount_code.per_email_limit:
            raise DiscountError(
                "You have already used this coupon the maximum number of times."
            )


def _increment_times_used(coupon_pk: int) -> bool:
    """
    Atomically increment times_used only if under the per-use cap (ADR-002 §4).
    Returns True if the increment succeeded, False if the coupon is at max uses.

    A usage_limit of None means unlimited — those coupons always succeed.

    Uses a single conditional UPDATE so that concurrent checkouts cannot both
    increment past the limit: only one UPDATE matches the WHERE clause when
    times_used reaches usage_limit; the losing request gets 0 rows updated and
    must surface a "coupon at capacity" error to the customer.
    """
    updated = DiscountCode.objects.cross_store_unsafe().filter(
        pk=coupon_pk,
    ).filter(
        Q(usage_limit__isnull=True) | Q(times_used__lt=F('usage_limit'))
    ).update(times_used=F('times_used') + 1)

    return bool(updated)


def calculate_coupon_discount(discount_code: DiscountCode, order_amount: Decimal) -> Decimal:
    """
    Returns the discount amount for a coupon without applying it.

    free_product and free_shipping value types return Decimal('0') — their discounts
    are handled separately during order assembly (e.g. zeroing a line item price or
    the shipping line). The caller is responsible for acting on the value_type.
    """
    if discount_code.value_type == 'percentage':
        return (order_amount * discount_code.value / Decimal('100')).quantize(
            Decimal('0.01')
        )
    if discount_code.value_type == 'fixed_amount':
        return min(discount_code.value, order_amount)
    # free_product / free_shipping: handled outside this function
    return Decimal('0')


@transaction.atomic
def apply_coupon(
    discount_code: DiscountCode,
    order_amount: Decimal,
    customer_email: str,
    gift_card_already_applied: bool = False,
) -> Decimal:
    """
    Validates and atomically applies a coupon. Returns the discount amount.

    Steps:
    1. Advisory validation: check active, date window, minimum order, stackable,
       and per-email limit. Uses the coupon object as supplied (no re-read needed).
    2. Calculate the discount amount.
    3. Atomically increment times_used via a single conditional UPDATE (ADR-002 §4).
       If 0 rows were updated the coupon reached max_uses between the advisory check
       and the UPDATE — raise DiscountError to surface a "coupon at capacity" message.
    4. Atomically increment the per-email DiscountCodeEmailUse counter.

    Parameters
    ----------
    gift_card_already_applied:
        Pass True if the cart already has a gift card. When the coupon has
        stackable=False this causes a DiscountError — the caller must present the
        choice to the customer (remove the gift card or remove the coupon).

    Raises DiscountError if the coupon is invalid for any reason.
    """
    _validate_coupon(discount_code, order_amount, customer_email, gift_card_already_applied)

    discount_amount = calculate_coupon_discount(discount_code, order_amount)

    # Conditional UPDATE: authoritative usage_limit gate (ADR-002 §4).
    # No select_for_update needed — the WHERE clause prevents double-increment.
    if not _increment_times_used(discount_code.pk):
        raise DiscountError("This coupon has reached its maximum usage.")

    if customer_email:
        obj, _created = DiscountCodeEmailUse.objects.get_or_create(
            discount_code=discount_code,
            email=customer_email.lower(),
            defaults={'use_count': 0},
        )
        DiscountCodeEmailUse.objects.filter(pk=obj.pk).update(
            use_count=F('use_count') + 1
        )

    return discount_amount


@transaction.atomic
def restore_discount_on_failed_payment(order) -> None:
    """
    Restore coupon times_used and gift card balance after a payment failure or
    a released authorization (B2 — ADR-002 §4).

    Must be called inside an already-atomic context (the webhook handler wraps
    itself in @transaction.atomic so this savepoint nests correctly).

    Steps:
    1. If order.discount_code is a coupon: decrement times_used by 1 (floored at 0).
       Delete any DiscountCodeEmailUse row for (code, order.customer_email).
    2. Find any positive GiftCardTransaction rows written at checkout for this order
       and write negative reversal entries to restore the gift card balance.
       (Using transaction-based lookup rather than order.gift_card alone so that
       orders created before B3 — where the gift card lived in discount_code — are
       also handled correctly.)

    Idempotency: calling this twice is safe. The coupon decrement uses
    `times_used__gt=0` so it floors at 0 rather than going negative. The
    reversal rows are keyed by the positive transaction amount; if a reversal
    already exists (same amount, negative) the balance will be double-restored —
    the caller (webhook handler) must ensure this is only called once per order
    failure by checking payment_status before proceeding.
    """
    # --- 1. Coupon counter restore ---
    if order.discount_code_id:
        # Load the FK only if it's set (avoids a query on None)
        coupon = order.discount_code
        if coupon.discount_type == DiscountType.COUPON:
            # Atomic decrement floored at 0 — never goes negative
            DiscountCode.objects.cross_store_unsafe().filter(
                pk=coupon.pk,
                times_used__gt=0,
            ).update(times_used=F('times_used') - 1)

            # Remove per-email usage record so the customer can use the coupon again
            if order.customer_email:
                DiscountCodeEmailUse.objects.filter(
                    discount_code=coupon,
                    email=order.customer_email.lower(),
                ).delete()

            logger.info(
                'Restored coupon %s times_used for failed order %s',
                coupon.code, order.pk,
            )

    # --- 2. Gift card balance restore ---
    # Query by order rather than order.gift_card so we handle orders that were
    # created before B3 (where gift cards were stored in discount_code).
    spend_txns = list(
        GiftCardTransaction.objects.cross_store_unsafe().filter(
            order=order,
            amount__gt=0,
        )
    )
    for txn in spend_txns:
        GiftCardTransaction.objects.create(
            store=txn.store,
            gift_card=txn.gift_card,
            order=order,
            amount=-txn.amount,
            note=f'Reversed: payment failed (order {order.pk})',
        )
        logger.info(
            'Restored gift card %s balance %s for failed order %s',
            txn.gift_card.code, txn.amount, order.pk,
        )


def restore_stock_on_failed_payment(order) -> None:
    """
    Restore FIXED_QTY variant stock when a payment fails or is voided.

    Called inside the same @transaction.atomic block as the order status update
    so the stock restore and status change are atomic.

    For each line item on the order whose variant still exists and uses FIXED_QTY
    inventory tracking, increments quantity by the ordered amount via an atomic
    F() expression (no select_for_update needed for restoration — an increment
    is always safe; we are never checking against a threshold here).

    Skips items whose product_variant FK was set to NULL (variant deleted) and
    items with non-FIXED_QTY inventory modes (NO_TRACKING, PRESALE, etc.).

    Idempotency note: the caller (webhook handler) must ensure this is called
    at most once per order failure by checking payment_status before proceeding.
    Calling it twice would double-restore the stock.
    """
    from catalog.models import InventoryMode, ProductVariant  # avoid circular import
    from orders.models import OrderItem  # avoid circular import

    items = OrderItem.objects.cross_store_unsafe().filter(order=order)
    for item in items:
        if not item.product_variant_id:
            # Variant was deleted after order was placed; nothing to restore.
            continue

        updated = ProductVariant.objects.cross_store_unsafe().filter(
            pk=item.product_variant_id,
            inventory_mode=InventoryMode.FIXED_QTY,
        ).update(quantity=F('quantity') + item.quantity)

        if updated:
            logger.info(
                'Restored %d stock units for variant %d (order %d)',
                item.quantity, item.product_variant_id, order.pk,
            )


@transaction.atomic
def apply_gift_card(
    gift_card: DiscountCode,
    amount_to_spend: Decimal,
    order=None,
) -> Decimal:
    """
    Applies a gift card to cover up to `amount_to_spend`. Returns the amount actually
    covered (min of balance and amount_to_spend).

    Steps:
    1. Lock the DiscountCode row with select_for_update.
    2. Validate type and is_valid_now().
    3. Check remaining balance > 0.
    4. Create a GiftCardTransaction row for the spend amount.

    The transaction row is the balance decrement — if the surrounding order write
    rolls back, this row rolls back with it and the balance is automatically restored
    (§XIII gift card pattern).

    Raises DiscountError if the gift card is invalid or has no balance.
    """
    locked = DiscountCode.objects.cross_store_unsafe().select_for_update().get(
        pk=gift_card.pk
    )
    if locked.discount_type not in ('gift_card_manual', 'gift_card_auto'):
        raise DiscountError("Not a gift card.")
    if not locked.is_valid_now():
        raise DiscountError("This gift card is no longer valid.")

    balance = locked.current_balance
    if balance <= 0:
        raise DiscountError("This gift card has no remaining balance.")

    actual_spend = min(balance, amount_to_spend)
    GiftCardTransaction.objects.create(
        store=locked.store,
        gift_card=locked,
        order=order,
        amount=actual_spend,
        note='Applied at checkout',
    )
    return actual_spend


# ---------------------------------------------------------------------------
# ADR-029 (TICKET-045) — automated gift-card campaigns
# ---------------------------------------------------------------------------

# The gift-card-campaign namespace inside CampaignReward.campaign_id (ADR-002 §5
# idempotency guard, shared with lead_capture / abandoned_checkout namespaces).
_CAMPAIGN_ID_PREFIX = 'gift_card_campaign'

# Editor token pills (super-admin-12-02 screen) mapped to Django template
# variable syntax at send time — the same {{ gift_value }} / {{ gift_code }}
# context names as the manual-issue dialog (admin-020-02), per D7.
_PILL_DISCOUNT = '[DISCOUNT]'
_PILL_CODE = '[GENERATED GIFT CARD CODE]'


def _campaign_reward_id(campaign_pk, order_pk) -> str:
    """The exact ADR-029 D4 idempotency key: one per (campaign, order)."""
    return f'{_CAMPAIGN_ID_PREFIX}:{campaign_pk}:order:{order_pk}'


def campaign_matches_order(campaign: GiftCardCampaign, order) -> bool:
    """
    ADR-029 D2 trigger scoping: does `order` qualify for `campaign`'s
    trigger_scope?

    - all_products: always matches.
    - manual_products: at least one order line item's product must be in
      campaign.trigger_products. Matched via OrderItem.product_variant.product —
      OrderItem carries no direct Product FK (only snapshots + a nullable
      variant FK), so an item whose variant was later deleted can no longer be
      matched; this is an accepted, documented limitation (the campaign simply
      does not fire for that line).
    - conditions: hidden in the v1 admin form (D2, same PENDING rules
      vocabulary as campaigns.Campaign.targeting_rules_json) — never matches.

    Reads trigger_products via catalog.Product.objects.for_store(...) instead
    of campaign.trigger_products directly: catalog.Product is a StoreOwnedModel,
    so the M2M related manager is built on Product's StoreScopedManager and its
    get_queryset() always returns the isolation-enforcing _RaisingQuerySet
    (core/managers.py) regardless of any relation filter already applied — the
    same limitation documented in engagement/service.py:resolve_excluded_paths.
    """
    if campaign.trigger_scope == GiftCardCampaignTriggerScope.ALL_PRODUCTS:
        return True

    if campaign.trigger_scope == GiftCardCampaignTriggerScope.MANUAL_PRODUCTS:
        from catalog.models import Product

        product_ids = list(
            Product.objects.for_store(campaign.store)
            .filter(gift_card_campaigns=campaign)
            .values_list('pk', flat=True)
        )
        if not product_ids:
            return False
        # ADR-031 addendum audit (TICKET-051): `order.items` is a reverse-FK
        # related manager (OrderItem.order -> Order) — ADR-031 deliberately
        # leaves those raising (only relation-bound M2M managers were
        # relaxed). `.exists()` used to silently bypass that raise entirely;
        # scoped explicitly now that it raises unconditionally.
        from orders.models import OrderItem

        return OrderItem.objects.for_store(order.store).filter(
            order=order, product_variant__product_id__in=product_ids
        ).exists()

    # CONDITIONS — reserved, not evaluated in v1.
    return False


def resolve_campaign_expiry(campaign: GiftCardCampaign, now=None):
    """
    Resolves campaign's expiry config to a concrete DiscountCode.ends_at
    (ADR-029 D6).

    relative_days: now + expiry_days.
    absolute_date: end-of-day UTC of expiry_date (23:59:59), or None if
    expiry_date is unset (should not happen past validate_gift_card_campaign,
    but issuance re-checks rather than trusting publish-time validation alone).
    """
    now = now or timezone.now()
    if campaign.expiry_mode == GiftCardCampaignExpiryMode.ABSOLUTE_DATE:
        if not campaign.expiry_date:
            return None
        return datetime.combine(campaign.expiry_date, time(23, 59, 59), tzinfo=dt_timezone.utc)
    return now + timedelta(days=campaign.expiry_days)


def _cap_window_timedelta(campaign: GiftCardCampaign) -> timedelta:
    unit = campaign.cap_window_unit
    value = campaign.cap_window_value
    if unit == GiftCardCampaignCapWindowUnit.MINUTES:
        return timedelta(minutes=value)
    if unit == GiftCardCampaignCapWindowUnit.HOURS:
        return timedelta(hours=value)
    return timedelta(days=value)


def _generate_unique_gift_card_code(store, max_attempts: int = 5) -> str:
    """
    16 uppercase hex chars from `secrets` (64 bits of entropy — Safety gate
    floor is >= 60 bits) — matches the admin-020 observed code format (15-16
    uppercase hex chars). Retries on the (store, code) unique-constraint
    collision space, which is astronomically unlikely at this entropy but
    checked defensively rather than trusted.
    """
    for _ in range(max_attempts):
        candidate = secrets.token_hex(8).upper()
        if not DiscountCode.objects.for_store(store).filter(code=candidate).exists():
            return candidate
    raise DiscountError(
        f'Could not generate a unique gift card code after {max_attempts} attempts.'
    )


def issue_campaign_reward_for_order(campaign: GiftCardCampaign, order, recipient_email: str):
    """
    ADR-029 D4 steps 4a-4e: issue one gift_card_auto DiscountCode for
    (campaign, order), or return None if the campaign is not eligible to fire
    (cap reached, already issued, or expiry already elapsed).

    The campaign row is locked with SELECT FOR UPDATE for the whole cap-check +
    issue sequence — budget cap and frequency cap are otherwise check-then-act
    (§XIII). The DiscountCode + CampaignReward pair is created in a nested
    savepoint: an IntegrityError on CampaignReward's unique
    (store, campaign_id, recipient_email) rolls back BOTH — net effect
    identical to "guard before code" (ADR-002 §5), matching the
    engagement.service.issue_reward idiom.

    recipient_email must already be normalised (stripped + lowercased) by the
    caller (discounts.tasks.process_gift_card_campaigns).
    """
    with transaction.atomic():
        try:
            locked_campaign = (
                GiftCardCampaign.objects.for_store(order.store)
                .select_for_update()
                .get(pk=campaign.pk)
            )
        except GiftCardCampaign.DoesNotExist:
            return None

        if locked_campaign.status != GiftCardCampaignStatus.PUBLISHED:
            logger.info(
                'gift_card_campaign %s no longer published — skipping order %s',
                locked_campaign.pk, order.pk,
            )
            return None

        if (
            locked_campaign.max_issued_cards is not None
            and locked_campaign.issued_count >= locked_campaign.max_issued_cards
        ):
            logger.info(
                'gift_card_campaign %s at budget cap (%s issued) — skipping order %s',
                locked_campaign.pk, locked_campaign.max_issued_cards, order.pk,
            )
            return None

        if locked_campaign.cap_enabled:
            since = timezone.now() - _cap_window_timedelta(locked_campaign)
            recent_count = CampaignReward.objects.for_store(order.store).filter(
                recipient_email=recipient_email,
                campaign_id__startswith=f'{_CAMPAIGN_ID_PREFIX}:{locked_campaign.pk}:',
                issued_at__gte=since,
            ).count()
            if recent_count >= locked_campaign.cap_count:
                logger.info(
                    'gift_card_campaign %s frequency cap reached for %s — skipping order %s',
                    locked_campaign.pk, recipient_email, order.pk,
                )
                return None

        ends_at = resolve_campaign_expiry(locked_campaign)
        if (
            locked_campaign.expiry_mode == GiftCardCampaignExpiryMode.ABSOLUTE_DATE
            and ends_at is not None
            and ends_at <= timezone.now()
        ):
            # Issuance-time re-check (D6): never issue an already-expired card,
            # even if publish-time validation should have prevented reaching here.
            logger.warning(
                'gift_card_campaign %s expiry_date already elapsed — skipping order %s',
                locked_campaign.pk, order.pk,
            )
            return None

        # Issuance-time re-check (D6, wave-3 spec review): publish-time
        # validation only ever looked at the store's markets AT THE MOMENT OF
        # PUBLISHING. A store can add the FR market afterwards (enable an
        # 'fr' StoreLanguage or a FR ShippingCountry) without ever touching
        # this campaign again, and the campaign would otherwise keep issuing
        # short-expiry cards forever. Re-evaluate against the store's CURRENT
        # markets on every issuance and skip (never silently issue a
        # non-compliant card) — mirrors the absolute-date re-check above.
        from .validators import gift_card_campaign_fr_floor_violation

        if gift_card_campaign_fr_floor_violation(locked_campaign):
            logger.warning(
                'gift_card_campaign %s violates the FR 5-year statutory floor '
                '(ADR-029 D6) for its store\'s current markets — skipping '
                'issuance for order %s',
                locked_campaign.pk, order.pk,
            )
            return None

        code_str = _generate_unique_gift_card_code(order.store)

        try:
            with transaction.atomic():
                discount_code = DiscountCode.objects.create(
                    store=order.store,
                    code=code_str,
                    discount_type=DiscountType.GIFT_CARD_AUTO,
                    value_type=ValueType.FIXED_AMOUNT,
                    value=Decimal('0'),
                    initial_balance=locked_campaign.value,
                    is_active=True,
                    provenance=ProvenanceType.CAMPAIGN,
                    gift_card_campaign=locked_campaign,
                    currency=order.store.default_currency,
                    ends_at=ends_at,
                    usage_limit=None,
                )
                CampaignReward.objects.create(
                    store=order.store,
                    campaign_id=_campaign_reward_id(locked_campaign.pk, order.pk),
                    recipient_email=recipient_email,
                    discount_code=discount_code,
                )
        except IntegrityError:
            logger.info(
                'gift_card_campaign %s already issued for order %s (idempotent no-op)',
                locked_campaign.pk, order.pk,
            )
            return None

        # D4 step 5: enqueue delivery only after this exact commit — never
        # inside the try/except above (an email must not fire for a rolled-back
        # issuance) and never outside the campaign-row lock (must observe the
        # committed DiscountCode).
        transaction.on_commit(
            lambda code_pk=discount_code.pk, order_pk=order.pk: _enqueue_gift_card_campaign_email(
                code_pk, order_pk
            )
        )
        return discount_code


def _enqueue_gift_card_campaign_email(discount_code_pk, order_pk) -> None:
    """Thin indirection so discounts/tasks.py can be imported lazily (avoids a
    service<->tasks import cycle) and so tests can patch this one seam."""
    from discounts.tasks import send_gift_card_campaign_email

    send_gift_card_campaign_email.delay(discount_code_pk, order_pk)


def _substitute_gift_card_pills(text: str) -> str:
    """Maps the admin editor's bracket pills to Django template var syntax (D7)."""
    return text.replace(_PILL_DISCOUNT, '{{ gift_value }}').replace(_PILL_CODE, '{{ gift_code }}')


def send_gift_card_campaign_reward_email(discount_code: DiscountCode, order) -> bool:
    """
    Renders and sends the gift-card-campaign delivery email (D7) via the single
    send path, emails.service.send_transactional_email(template_id=
    'gift_card_campaign', ...). Never raises — send_transactional_email already
    catches everything and writes the SentEmail audit row regardless of outcome.

    Template precedence (D7, resolved in exactly one place): when the issuing
    campaign has a non-blank email_subject/email_body, it overrides the
    store/platform 'gift_card_campaign' template; blank falls through to the
    normal EmailTemplate-then-file chain inside send_transactional_email.

    Context: gift_value/gift_code (the manual-issue dialog's variable names,
    D7) plus expires_at. 'store' is added automatically by
    send_transactional_email.
    """
    from emails.service import send_transactional_email

    store = discount_code.store
    campaign = discount_code.gift_card_campaign

    context = {
        'gift_value': f'{discount_code.currency} {discount_code.initial_balance}'.strip(),
        'gift_code': discount_code.code,
        'expires_at': discount_code.ends_at,
        'order': order,
    }

    subject_override = ''
    body_html_override = ''
    if campaign is not None:
        if campaign.email_subject:
            subject_override = _substitute_gift_card_pills(campaign.email_subject)
        if campaign.email_body:
            body_html_override = _substitute_gift_card_pills(campaign.email_body)

    return send_transactional_email(
        'gift_card_campaign',
        order.customer_email,
        context,
        store,
        order=order,
        subject_override=subject_override,
        body_html_override=body_html_override,
    )


def void_or_flag_campaign_gift_cards_for_refund(order) -> None:
    """
    ADR-029 D9 refund policy (human-approved 2026-07-11): for gift cards a
    campaign issued for `order` (found via CampaignReward.campaign_id — the
    only linkage from an order back to a campaign-issued code, since
    DiscountCode carries no order FK at issuance time):

    - unspent (no GiftCardTransaction with amount > 0): deactivated
      (is_active=False) with an audit note.
    - partially/fully spent: NEVER clawed back — flagged
      (DiscountCode.refund_flagged=True) for manual review instead.

    Callers must only invoke this for a FULL refund of the triggering order
    (D9: "Partial refunds: no automatic voiding (v1)") and must wrap the call
    in the same nested-savepoint non-fatal posture as other post-transition
    hooks (ADR-010 Q5) — this function itself does not swallow exceptions.
    """
    rewards = (
        CampaignReward.objects.for_store(order.store)
        .filter(
            campaign_id__startswith=f'{_CAMPAIGN_ID_PREFIX}:',
            campaign_id__endswith=f':order:{order.pk}',
        )
        .select_related('discount_code')
    )

    for reward in rewards:
        code = reward.discount_code
        if code.discount_type != DiscountType.GIFT_CARD_AUTO:
            continue

        # ADR-031 addendum (§XIII atomic coupon decrements) — TICKET-051:
        # .update() used to silently bypass StoreScopedManager's isolation
        # raise; now that it raises unconditionally, these two pk-pinned
        # updates must go through .for_store(). code.store is safe to use:
        # `reward` (and therefore `code = reward.discount_code`) came from
        # `CampaignReward.objects.for_store(order.store)` above.
        #
        # `code.transactions` is a reverse-FK related manager (GiftCardTransaction
        # .gift_card -> DiscountCode) — ADR-031 deliberately leaves those
        # raising (only relation-bound M2M managers were relaxed). .exists()
        # on it used to silently bypass the raise entirely (the same
        # AGGREGATE-DOESNT-RAISE family this ticket closes); now that
        # .exists() raises unconditionally, use the established
        # cross_store_unsafe()-on-the-target-model pattern already used a
        # few lines above in restore_discount_on_failed_payment for the
        # identical relation.
        spent = GiftCardTransaction.objects.for_store(code.store).filter(
            gift_card=code, amount__gt=0
        ).exists()
        if spent:
            DiscountCode.objects.for_store(code.store).filter(pk=code.pk).update(
                refund_flagged=True,
                refund_flagged_reason=(
                    f'Order {order.pk} was refunded after this gift card was '
                    'partially or fully spent — flagged for manual review '
                    '(ADR-029 D9). Balance was NOT clawed back.'
                ),
            )
            logger.warning(
                'gift_card_campaign_refund: code %s spent — order %s refunded, flagged for review',
                code.code, order.pk,
            )
        else:
            DiscountCode.objects.for_store(code.store).filter(
                pk=code.pk, is_active=True
            ).update(is_active=False)
            logger.info(
                'gift_card_campaign_refund: code %s deactivated (unspent) — order %s refunded',
                code.code, order.pk,
            )
