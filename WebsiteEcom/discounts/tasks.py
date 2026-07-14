"""
Celery tasks for automated gift-card campaigns (ADR-029, TICKET-045).

process_gift_card_campaigns(order_id):
  Enqueued via transaction.on_commit from BOTH payment-processor PAID webhook
  handlers (payments/webhook_views.py::_handle_payment_intent_succeeded,
  payments/paypal_webhook_views.py::_handle_capture_completed) — the same
  money-state gate already used for suppress_abandoned_checkout / pixels
  (ADR-010 Q5). Never triggered by a signal. Idempotent: re-running it for the
  same order can only ever issue zero additional cards, because issuance
  itself is guarded by discounts.service.issue_campaign_reward_for_order's
  CampaignReward uniqueness (ADR-002 §5 — the "765×$5 incident guard").

send_gift_card_campaign_email(discount_code_id, order_id):
  Per-card delivery worker — enqueued via transaction.on_commit from inside
  issue_campaign_reward_for_order's own atomic block, so it only ever fires
  for a card whose issuance actually committed (D4 step 5). At-least-once
  delivery: a Celery retry after a successful send double-sends the
  (idempotent, harmless) email — accepted per ADR-029 Risks.

Context design (Celery task args must be JSON-serializable): pass PKs, not
model instances, exactly like emails/tasks.py and campaigns/tasks.py.
"""

import logging

from celery import shared_task

logger = logging.getLogger(__name__)


@shared_task(name='discounts.tasks.process_gift_card_campaigns', bind=True, max_retries=3)
def process_gift_card_campaigns(self, order_id):
    """
    ADR-029 D4 steps 1-3: re-check the order is really PAID (the task may run
    late), apply the D9 self-gifting brake, then dispatch to every published
    order_paid campaign whose trigger_scope matches this order.

    D9 self-gifting brake: Order.total IS the processor-charged amount — it is
    computed post-gift-card-deduction at checkout (cart/checkout.py), and an
    order whose total is fully covered by gift card/coupon never creates a
    PaymentIntent/PayPal authorization at all (it is marked PAID directly via
    the ZERO_TOTAL charge path in cart/checkout.py, which does NOT enqueue this
    task). The order.total <= 0 check below is therefore defense-in-depth for
    any future PAID call site that might enqueue this task for such an order —
    per D4/D9 it must never issue a reward in that case.
    """
    from orders.models import Order, PaymentStatus

    try:
        order = Order.objects.cross_store_unsafe().select_related('store').get(pk=order_id)
    except Order.DoesNotExist:
        logger.warning('process_gift_card_campaigns: order %s not found', order_id)
        return

    if order.payment_status != PaymentStatus.PAID:
        logger.info(
            'process_gift_card_campaigns: order %s is not PAID (status=%s) — skipping',
            order.pk, order.payment_status,
        )
        return

    if order.total <= 0:
        logger.info(
            'process_gift_card_campaigns: order %s fully covered by gift card/coupon '
            '(total=%s) — self-gifting brake, no campaign issuance (D9)',
            order.pk, order.total,
        )
        return

    recipient_email = (order.customer_email or '').strip().lower()
    if not recipient_email:
        logger.warning(
            'process_gift_card_campaigns: order %s has no customer_email — cannot issue',
            order.pk,
        )
        return

    from discounts.models import GiftCardCampaign, GiftCardCampaignStatus, GiftCardCampaignTrigger
    from discounts.service import campaign_matches_order, issue_campaign_reward_for_order

    campaigns = GiftCardCampaign.objects.for_store(order.store).filter(
        status=GiftCardCampaignStatus.PUBLISHED,
        trigger_event=GiftCardCampaignTrigger.ORDER_PAID,
    )
    for campaign in campaigns:
        if not campaign_matches_order(campaign, order):
            continue
        issue_campaign_reward_for_order(campaign, order, recipient_email)


@shared_task(name='discounts.tasks.send_gift_card_campaign_email', bind=True, max_retries=3)
def send_gift_card_campaign_email(self, discount_code_id, order_id):
    """
    Renders and sends the gift-card-campaign delivery email for one issued
    code (D7). Resolves the DiscountCode and Order from their PKs (Celery task
    args must be JSON-serializable — never pass model instances).

    Failure here never claws back the already-issued/committed gift card — the
    card remains valid and redeemable; the SentEmail audit row (written inside
    send_transactional_email) shows the FAILED status for operator visibility.
    """
    from discounts.models import DiscountCode
    from discounts.service import send_gift_card_campaign_reward_email
    from orders.models import Order

    try:
        discount_code = DiscountCode.objects.cross_store_unsafe().select_related(
            'store', 'gift_card_campaign'
        ).get(pk=discount_code_id)
    except DiscountCode.DoesNotExist:
        logger.error(
            'send_gift_card_campaign_email: discount code %s not found — dropping email',
            discount_code_id,
        )
        return

    try:
        order = Order.objects.cross_store_unsafe().get(pk=order_id)
    except Order.DoesNotExist:
        logger.error(
            'send_gift_card_campaign_email: order %s not found — dropping email for code %s',
            order_id, discount_code.code,
        )
        return

    send_gift_card_campaign_reward_email(discount_code, order)
