"""
Celery tasks for the campaigns app (TICKET-021 / ADR-010 Q4, TICKET-028 / ADR-011 Q5).

scan_abandoned_checkouts:
  Global beat task — runs every 5 minutes (configured in webecom/settings/base.py).
  Iterates stores with active abandoned_checkout campaigns; detects new abandoned
  carts; schedules per-step email sends; expires stale sessions. Idempotent: every
  step checks "already done?" before acting (§XV-6).

send_abandoned_checkout_email:
  Per-send worker — one execution per AbandonedCheckoutEmailSend row.
  Uses select_for_update to claim the row atomically; a race guard re-checks
  payment state inside the lock; issues a coupon (if configured); mints the
  resume token; renders and sends the email; updates status. Never silently
  drops failures (§XV-1).

capture_original_charge_now (not a Celery task — called synchronously from the
  payment-confirmation path, see cart/checkout.py 20:P1):
  Immediately captures a non-funnel order's ORIGINAL charge, reusing
  capture_window_watchdog's own claim/capture logic (_watchdog_claim_and_capture)
  so there is exactly one capture implementation. The watchdog remains the
  reconciliation fallback if the inline call fails.

Context design (Celery task args must be JSON-serializable):
  Pass PKs, not model instances.

Retry policy: send task has max_retries=3 with exponential back-off for
transient errors (DB failures, etc.). FAILED status after email delivery failure
is terminal and not retried by Celery — it shows on the dashboard (§XV-1).
"""

import logging
import secrets
from datetime import timedelta
from decimal import Decimal

from celery import shared_task
from django.db import IntegrityError, transaction
from django.utils import timezone

logger = logging.getLogger(__name__)

# The abandoned-checkout session lifetime in days (matches cart lifetime, UF-E).
_SESSION_LIFETIME_DAYS = 30

# Watchdog constants (ADR-011 Q5).
# A CAPTURE_IN_PROGRESS charge older than this is "stuck" and needs reconciliation.
_STUCK_CAPTURE_MINUTES = 10
# Grace period beyond the capture window before the breach alert fires.
_ALERT_GRACE_MINUTES = 15


# ---------------------------------------------------------------------------
# Coupon issuance helper
# ---------------------------------------------------------------------------

def _issue_coupon(store, session, step) -> str:
    """
    Issue a single-use DiscountCode for (session, email_step).

    Idempotent: if a CampaignIssuedCode for (campaign_session, email_step) already
    exists (retry path), returns the existing code string without creating a new one
    (ADR-009 §3 idempotency verbatim).

    Must be called inside a transaction.atomic() context. A nested savepoint is used
    so that an IntegrityError on CampaignIssuedCode creation only rolls back the
    savepoint, not the outer transaction — allowing the fallback get() to succeed.

    Returns the coupon code string (already uppercased by DiscountCode.save()).
    """
    from campaigns.models import CampaignIssuedCode
    from discounts.models import DiscountCode, DiscountType, ProvenanceType, ValueType

    cfg = step.coupon_config_json
    value_type_map = {
        "percent": ValueType.PERCENTAGE,
        "fixed": ValueType.FIXED_AMOUNT,
    }
    value_type = value_type_map.get(cfg.get("value_type", "percent"), ValueType.PERCENTAGE)
    expires_in_days = cfg.get("expires_in_days", 30)
    value = Decimal(str(cfg["value"]))

    # Generate an unpredictable code (hex = alphanumeric, no ambiguous chars).
    code = secrets.token_hex(10).upper()  # 20 characters

    try:
        with transaction.atomic():  # savepoint inside the outer transaction
            discount_code = DiscountCode.objects.create(
                store=store,
                code=code,
                discount_type=DiscountType.COUPON,
                value_type=value_type,
                value=value,
                usage_limit=1,
                stackable=False,
                provenance=ProvenanceType.CAMPAIGN,
                is_active=True,
                ends_at=timezone.now() + timedelta(days=expires_in_days),
            )
            CampaignIssuedCode.objects.create(
                store=store,
                campaign_session=session,
                email_step=step,
                discount_code=discount_code,
            )
            return discount_code.code

    except IntegrityError:
        # The (campaign_session, email_step) unique constraint was violated —
        # this is a retry that hit an already-issued code. Return the existing one.
        existing = (
            CampaignIssuedCode.objects.for_store(store)
            .select_related("discount_code")
            .get(campaign_session=session, email_step=step)
        )
        return existing.discount_code.code


# ---------------------------------------------------------------------------
# Beat task: scan abandoned checkouts
# ---------------------------------------------------------------------------

@shared_task(name="campaigns.tasks.scan_abandoned_checkouts")
def scan_abandoned_checkouts() -> None:
    """
    Scan all stores with active abandoned-checkout campaigns.

    Per ADR-010 Q4 algorithm:
    Step 1: Cross-store read of Campaign definitions to find stores to process.
    Step 2: Per store — resolve the winning campaign via the single resolution
            function (campaigns.service.resolve_campaign_for_store, §XV-4).
    Step 3: Detection — ACTIVE carts past the first-step delay with a customer
            email but no existing abandonment session. In one transaction:
            create_abandonment_session() + Cart.status → ABANDONED.
            IntegrityError (race with another beat run) → skip.
    Step 4: Due-ness — for each non-terminal session, for each email step where
            abandoned_at + send_delay_hours <= now: get_or_create the send record;
            if newly created, enqueue send_abandoned_checkout_email.
    Step 5: Expiry — sessions past abandoned_at + 30d → EXPIRED; SCHEDULED sends
            → CANCELLED.

    The task is idempotent per §XV-6: every step checks "already done?" before
    acting. The per-send AbandonedCheckoutEmailSend row is the correctness boundary.
    """
    from campaigns.models import (
        AbandonedCheckoutEmailSend,
        AbandonedCheckoutEmailSendStatus,
        Campaign,
        CampaignSession,
        CampaignSessionState,
        CampaignType,
        NON_TERMINAL_STATES,
    )
    from campaigns.service import resolve_campaign_for_store
    from cart.models import Cart, CartStatus
    from stores.models import Store

    now = timezone.now()
    non_terminal_list = list(NON_TERMINAL_STATES)

    # Step 1: find stores that have at least one active abandoned_checkout campaign.
    # cross_store_unsafe is sanctioned here: we read campaign *definitions* only,
    # no customer data (ADR-010 Q4 §XV-1 note).
    store_ids = (
        Campaign.objects.cross_store_unsafe()
        .filter(campaign_type=CampaignType.ABANDONED_CHECKOUT, is_active=True)
        .values_list("store_id", flat=True)
        .distinct()
    )
    stores = Store.objects.filter(pk__in=store_ids)

    for store in stores:
        try:
            _scan_store(
                store=store,
                now=now,
                non_terminal_list=non_terminal_list,
                CampaignSession=CampaignSession,
                CampaignSessionState=CampaignSessionState,
                AbandonedCheckoutEmailSend=AbandonedCheckoutEmailSend,
                AbandonedCheckoutEmailSendStatus=AbandonedCheckoutEmailSendStatus,
                Cart=Cart,
                CartStatus=CartStatus,
                resolve_campaign_for_store=resolve_campaign_for_store,
            )
        except Exception:
            logger.exception(
                "scan_abandoned_checkouts: error processing store %s", store.pk
            )
            # Continue with remaining stores — per-store failures must not abort the scan.


def _scan_store(
    store,
    now,
    non_terminal_list,
    CampaignSession,
    CampaignSessionState,
    AbandonedCheckoutEmailSend,
    AbandonedCheckoutEmailSendStatus,
    Cart,
    CartStatus,
    resolve_campaign_for_store,
) -> None:
    """Per-store scan logic extracted for error isolation."""
    # Step 2: resolve the active campaign for this store.
    from campaigns.models import AbandonedCheckoutEmailStep, CampaignType
    campaign = resolve_campaign_for_store(store, CampaignType.ABANDONED_CHECKOUT)
    if campaign is None:
        return  # No active campaign — nothing to do for this store.

    # Use a store-scoped query rather than the related manager (.all() returns a
    # _RaisingQuerySet from StoreScopedManager which raises IsolationError on iteration).
    email_steps = list(
        AbandonedCheckoutEmailStep.objects
        .for_store(store)
        .filter(campaign=campaign)
        .order_by("send_delay_hours")
    )
    if not email_steps:
        return  # Campaign has no email steps — misconfigured but harmless.

    min_delay_hours = email_steps[0].send_delay_hours
    detection_cutoff = now - timedelta(hours=min_delay_hours)

    # Step 3: Detection — find ACTIVE carts past the first delay with no session yet.
    existing_session_cart_ids = (
        CampaignSession.objects.for_store(store)
        .filter(campaign_type=CampaignType.ABANDONED_CHECKOUT, cart__isnull=False)
        .values_list("cart_id", flat=True)
    )

    abandoned_carts = (
        Cart.objects.for_store(store)
        .filter(
            status=CartStatus.ACTIVE,
            expires_at__gt=now,
            updated_at__lte=detection_cutoff,
        )
        .exclude(customer_email="")
        .exclude(pk__in=existing_session_cart_ids)
    )

    from campaigns.service import create_abandonment_session

    for cart in abandoned_carts:
        try:
            with transaction.atomic():
                session = create_abandonment_session(store=store, cart=cart, campaign=campaign)
                Cart.objects.for_store(store).filter(pk=cart.pk).update(
                    status=CartStatus.ABANDONED
                )
                logger.info(
                    "scan_abandoned_checkouts: created session %s for cart %s (store %s)",
                    session.pk,
                    cart.pk,
                    store.pk,
                )
        except IntegrityError:
            # Another beat run beat us to it — skip this cart.
            logger.debug(
                "scan_abandoned_checkouts: session already exists for cart %s — skipping",
                cart.pk,
            )

    # Step 4: Due-ness — for each non-terminal session, schedule due sends.
    active_sessions = (
        CampaignSession.objects.for_store(store)
        .filter(
            campaign_type=CampaignType.ABANDONED_CHECKOUT,
            state__in=non_terminal_list,
            abandoned_at__isnull=False,
        )
        .select_related("campaign")
    )

    for session in active_sessions:
        # Use the session's campaign's steps (handles platform/store shadowing correctly
        # since session.campaign IS the winning campaign). Use a store-scoped query
        # rather than the related manager (avoids _RaisingQuerySet IsolationError).
        session_steps = list(
            AbandonedCheckoutEmailStep.objects
            .for_store(store)
            .filter(campaign=session.campaign)
            .order_by("send_delay_hours")
        )

        for step in session_steps:
            due_time = session.abandoned_at + timedelta(hours=step.send_delay_hours)
            if due_time > now:
                continue  # Not yet due.

            try:
                send, created = AbandonedCheckoutEmailSend.objects.for_store(store).get_or_create(
                    store=store,
                    session=session,
                    email_step=step,
                    defaults={
                        "status": AbandonedCheckoutEmailSendStatus.SCHEDULED,
                        "scheduled_at": due_time,
                    },
                )
            except IntegrityError:
                # Race between two concurrent beat runs — accept the loser's outcome.
                logger.debug(
                    "scan_abandoned_checkouts: send record race for session %s step %s",
                    session.pk,
                    step.pk,
                )
                continue

            if created:
                send_abandoned_checkout_email.delay(send.pk)
                logger.info(
                    "scan_abandoned_checkouts: enqueued send %s (session %s step %s)",
                    send.pk,
                    session.pk,
                    step.pk,
                )

    # Step 5: Expiry — sessions past abandoned_at + SESSION_LIFETIME_DAYS.
    expiry_cutoff = now - timedelta(days=_SESSION_LIFETIME_DAYS)

    expired_sessions = (
        CampaignSession.objects.for_store(store)
        .filter(
            campaign_type=CampaignType.ABANDONED_CHECKOUT,
            state__in=non_terminal_list,
            abandoned_at__lte=expiry_cutoff,
        )
    )

    for session in expired_sessions:
        updated = (
            CampaignSession.objects.for_store(store)
            .filter(pk=session.pk, state__in=non_terminal_list)
            .update(state=CampaignSessionState.EXPIRED)
        )
        if updated:
            AbandonedCheckoutEmailSend.objects.for_store(store).filter(
                session=session,
                status=AbandonedCheckoutEmailSendStatus.SCHEDULED,
            ).update(
                status=AbandonedCheckoutEmailSendStatus.CANCELLED,
                cancelled_at=now,
            )
            logger.info(
                "scan_abandoned_checkouts: expired session %s (store %s)",
                session.pk,
                store.pk,
            )


# ---------------------------------------------------------------------------
# Worker task: send one abandoned-checkout email
# ---------------------------------------------------------------------------

@shared_task(name="campaigns.tasks.send_abandoned_checkout_email", bind=True, max_retries=3)
def send_abandoned_checkout_email(self, send_id: int) -> None:
    """
    Send one abandoned-checkout email and update the AbandonedCheckoutEmailSend record.

    Per ADR-010 Q4 worker algorithm — all steps run inside ONE DB transaction:

    1. Atomic claim: select_for_update on the send row, check status=SCHEDULED.
       If status != SCHEDULED (another worker claimed it or it was cancelled): exit.
    2. Race guard (inside the lock): re-check session not terminal, cart not CONVERTED,
       no PAID order for session.customer_email since session.abandoned_at.
       Any hit → CANCELLED, call suppress_abandoned_checkout if a paid order is found.
    3. Coupon issuance (if step.coupon_config_json): create DiscountCode +
       CampaignIssuedCode; IntegrityError → reuse existing code (idempotency).
    4. Mint resume token (django.core.signing, salt='abandoned_checkout_resume').
    5. Render subject/body as Django templates with cart context.
    6. Send via emails.service.send_campaign_email (writes SentEmail audit row).
    7. On success: status=SENT, sent_at=now, session → IMPRESSION (from NOT_STARTED).
       On failure: status=FAILED (dashboardable, §XV-1 — never silent drop).

    The entire send + DB update is inside one transaction: if an unhandled exception
    occurs, the transaction rolls back and status remains SCHEDULED, allowing Celery
    retry. The only edge case is email-sent-but-commit-failed: on retry the coupon
    is reused and a second email is sent (rare; accepted trade-off per ADR-010 Q4).
    """
    from campaigns.models import (
        AbandonedCheckoutEmailSend,
        AbandonedCheckoutEmailSendStatus,
        CampaignSession,
        CampaignSessionState,
        NON_TERMINAL_STATES,
    )
    from campaigns.service import suppress_abandoned_checkout
    from cart.models import Cart, CartStatus
    from emails.service import send_campaign_email
    from orders.models import Order, PaymentStatus

    try:
        with transaction.atomic():
            # Step 1: Atomic claim.
            try:
                send = (
                    AbandonedCheckoutEmailSend.objects.cross_store_unsafe()
                    .select_for_update()
                    .select_related("session", "email_step", "store")
                    .get(pk=send_id, status=AbandonedCheckoutEmailSendStatus.SCHEDULED)
                )
            except AbandonedCheckoutEmailSend.DoesNotExist:
                logger.info(
                    "send_abandoned_checkout_email: send %s not SCHEDULED — skipping",
                    send_id,
                )
                return

            session = send.session
            step = send.email_step
            store = send.store
            now = timezone.now()

            # Step 2: Race guard — re-check inside the lock.
            session.refresh_from_db()

            if session.state not in NON_TERMINAL_STATES:
                # Session reached a terminal state (CONVERTED, DISMISSED, EXPIRED).
                # Cancel ALL SCHEDULED sends for this session — not just the current one
                # — so sibling sends don't stay stuck in SCHEDULED (race-guard consistency).
                AbandonedCheckoutEmailSend.objects.for_store(store).filter(
                    session=session,
                    status=AbandonedCheckoutEmailSendStatus.SCHEDULED,
                ).update(status=AbandonedCheckoutEmailSendStatus.CANCELLED, cancelled_at=now)
                logger.info(
                    "send_abandoned_checkout_email: session %s is terminal — "
                    "cancelled all SCHEDULED sends",
                    session.pk,
                )
                return

            if session.cart_id:
                cart_converted = (
                    Cart.objects.for_store(store)
                    .filter(pk=session.cart_id, status=CartStatus.CONVERTED)
                    .exists()
                )
                if cart_converted:
                    # Cancel ALL SCHEDULED sends for this session — not just the current one
                    # — so sibling sends don't stay stuck in SCHEDULED (race-guard consistency).
                    AbandonedCheckoutEmailSend.objects.for_store(store).filter(
                        session=session,
                        status=AbandonedCheckoutEmailSendStatus.SCHEDULED,
                    ).update(
                        status=AbandonedCheckoutEmailSendStatus.CANCELLED, cancelled_at=now
                    )
                    logger.info(
                        "send_abandoned_checkout_email: cart %s converted — "
                        "cancelled all SCHEDULED sends for session %s",
                        session.cart_id,
                        session.pk,
                    )
                    return

            if session.customer_email and session.abandoned_at:
                paid_order = (
                    Order.objects.for_store(store)
                    .filter(
                        payment_status=PaymentStatus.PAID,
                        customer_email=session.customer_email,
                        created_at__gte=session.abandoned_at,
                    )
                    .first()
                )
                if paid_order is not None:
                    AbandonedCheckoutEmailSend.objects.for_store(store).filter(pk=send_id).update(
                        status=AbandonedCheckoutEmailSendStatus.CANCELLED,
                        cancelled_at=now,
                    )
                    suppress_abandoned_checkout(paid_order)
                    logger.info(
                        "send_abandoned_checkout_email: PAID order found — "
                        "cancelling send %s, suppressing sessions",
                        send_id,
                    )
                    return

            # Step 3: Coupon issuance (if configured, idempotent).
            coupon_code_str = ""
            if step.coupon_config_json:
                coupon_code_str = _issue_coupon(store=store, session=session, step=step)

            # Step 4: Mint resume token (stateless, salt='abandoned_checkout_resume').
            from django.core import signing
            from django.conf import settings

            resume_token = signing.dumps(
                {"cart": session.cart_id, "session": session.pk, "store": store.pk},
                salt="abandoned_checkout_resume",
            )

            # Build resume URL from the store's primary domain.
            custom_domain = (getattr(store, "custom_domain", "") or "").strip()
            if custom_domain:
                store_host = custom_domain
            else:
                apex = getattr(settings, "PLATFORM_APEX_DOMAIN", "localhost")
                store_host = f"{store.subdomain}.{apex}"
            resume_url = f"https://{store_host}/checkout/resume/{resume_token}/"

            # Step 5: Build email context (cart items at current prices — read-only here).
            from cart.models import CartItem
            cart_items = []
            if session.cart_id:
                cart_items = list(
                    CartItem.objects.for_store(store)
                    .filter(cart_id=session.cart_id)
                    .select_related("variant")
                )

            context = {
                "cart_items": cart_items,
                "resume_url": resume_url,
                "coupon_code": coupon_code_str,
                "store": store,
            }

            # Step 6: Send via emails.service (writes SentEmail audit row).
            success = send_campaign_email(
                subject_template=step.subject,
                body_template=step.body_template,
                recipient_email=session.customer_email,
                context=context,
                store=store,
            )

            # Step 7: Update status (inside the same transaction — atomic with coupon).
            if success:
                AbandonedCheckoutEmailSend.objects.for_store(store).filter(pk=send_id).update(
                    status=AbandonedCheckoutEmailSendStatus.SENT,
                    sent_at=now,
                )
                # Transition to IMPRESSION (idempotent — only advances from NOT_STARTED).
                CampaignSession.objects.for_store(store).filter(
                    pk=session.pk,
                    state=CampaignSessionState.NOT_STARTED,
                ).update(state=CampaignSessionState.IMPRESSION)
                logger.info(
                    "send_abandoned_checkout_email: sent %s to %s (session %s)",
                    send_id,
                    session.customer_email[:3] + "***",
                    session.pk,
                )
            else:
                # FAILED is terminal and dashboardable (§XV-1 — never a silent drop).
                AbandonedCheckoutEmailSend.objects.for_store(store).filter(pk=send_id).update(
                    status=AbandonedCheckoutEmailSendStatus.FAILED,
                )
                logger.error(
                    "send_abandoned_checkout_email: send_campaign_email returned False "
                    "for send %s — marked FAILED",
                    send_id,
                )

    except Exception as exc:
        logger.exception(
            "send_abandoned_checkout_email: unhandled error for send %s: %s",
            send_id,
            exc,
        )
        try:
            raise self.retry(
                countdown=2 ** self.request.retries * 60,
                exc=exc,
            )
        except self.MaxRetriesExceededError:
            logger.error(
                "send_abandoned_checkout_email: max retries exceeded for send %s — "
                "marking FAILED",
                send_id,
            )
            # The inner @transaction.atomic was rolled back, so SCHEDULED status
            # was never overwritten. Write FAILED in a fresh (auto-commit) statement
            # so the row doesn't stay stuck at SCHEDULED forever (§XV-1).
            # store is not guaranteed to be in scope here (exception may have
            # occurred before send was loaded) — pk is trusted (came from our beat task).
            AbandonedCheckoutEmailSend.objects.cross_store_unsafe().filter(pk=send_id).update(
                status=AbandonedCheckoutEmailSendStatus.FAILED,
            )
            raise  # propagate so Celery marks the task as failed, not succeeded


# ---------------------------------------------------------------------------
# Beat task: upsell capture-window watchdog (ADR-011 Q5, TICKET-028)
# ---------------------------------------------------------------------------

@shared_task(name="campaigns.tasks.capture_window_watchdog")
def capture_window_watchdog() -> None:
    """
    Watchdog for the post-purchase upsell capture window (ADR-011 Q5, every 2 min).

    Four idempotent passes (each a safe no-op if already done, §XV-6):

    Pass 1 — Expired-window capture:
        ORIGINAL AUTHORIZED charges whose order.capture_window_expires_at < NOW().
        For each: atomic claim (AUTHORIZED → CAPTURE_IN_PROGRESS WHERE affected=1),
        capture via processor, mark CAPTURED, void all PENDING_CAPTURE items,
        transition non-terminal session → EXPIRED.

    Pass 2 — Stuck CAPTURE_IN_PROGRESS reconciliation:
        Charges in CAPTURE_IN_PROGRESS for more than _STUCK_CAPTURE_MINUTES minutes.
        Reconcile with processor first: if actually captured (response was lost),
        record CAPTURED and finalize; only if genuinely uncaptured, re-issue the
        capture with the same idempotency key (safe, §XV-6).

    Pass 3 — Session finalization:
        Non-terminal sessions past expires_at whose ORIGINAL charge is CAPTURED.
        Transition: ≥1 UPSELL CAPTURED → CONVERTED; else → EXPIRED.
        Void remaining PENDING_CAPTURE items.

    Pass 4 — Breach alerting (§XV-1):
        ORIGINAL charges still AUTHORIZED past capture_window_expires_at + 15 min.
        Emit logger.error with stable tag "upsell.watchdog.uncaptured_breach".
        An authorization must never be left to expire — this is the money-losing
        invisible failure ADR-007 §2 names.

    Pass 5 — Ambiguous-outcome reconciliation (H2 safety fix):
        UPSELL charges in AMBIGUOUS_OUTCOME (network failure mid-charge — the
        processor may have captured before the timeout).  For each: resolve the
        real outcome via retrieve_charge_by_idempotency_key.  Confirmed success
        → CAPTURED (with the real PI id); confirmed non-success → FAILED;
        still unknown → left AMBIGUOUS_OUTCOME for the next run.  Session
        finalization stays with pass 3 (single resolution function, §XV-4).

    Pass 6 — Phase C crash recovery (M2 safety fix):
        UPSELL charges stuck AUTHORIZED longer than _STUCK_CAPTURE_MINUTES where
        a PI may already have been created (Phase B succeeded but Phase C's
        transaction crashed).  Resolve via retrieve_charge_by_idempotency_key on
        the "upsell:<order_pk>:<step_pk>" key written to processor_payment_intent_id
        in Phase A.  Confirmed success → CAPTURED; confirmed failure → FAILED;
        None + past window → FAILED; None + within window → leave for next run.

    All passes use cross_store_unsafe() — the watchdog is a background process
    scanning across all stores (same pattern as scan_abandoned_checkouts, ADR-010).
    """
    from campaigns.models import (
        CampaignSession,
        CampaignSessionState,
        NON_TERMINAL_STATES,
    )
    from orders.models import ChargeStatus, ChargeType, OrderCharge, OrderItemStatus
    from payments.factory import get_connector

    now = timezone.now()
    non_terminal_list = list(NON_TERMINAL_STATES)

    # ------------------------------------------------------------------
    # Pass 1: Expired-window capture
    # ------------------------------------------------------------------
    expired_window_charges = list(
        OrderCharge.objects.cross_store_unsafe()
        .filter(
            charge_type=ChargeType.ORIGINAL,
            status=ChargeStatus.AUTHORIZED,
            order__capture_window_expires_at__lt=now,
            order__capture_window_expires_at__isnull=False,
        )
        .select_related("order", "order__processor_account", "store")
    )

    for charge in expired_window_charges:
        try:
            _watchdog_claim_and_capture(charge=charge, now=now, logger=logger,
                                        CampaignSession=CampaignSession,
                                        CampaignSessionState=CampaignSessionState,
                                        ChargeStatus=ChargeStatus,
                                        OrderCharge=OrderCharge,
                                        OrderItemStatus=OrderItemStatus,
                                        non_terminal_list=non_terminal_list,
                                        get_connector=get_connector)
        except Exception:
            logger.exception(
                "capture_window_watchdog: pass 1 error for charge %s", charge.pk
            )

    # ------------------------------------------------------------------
    # Pass 2: Stuck CAPTURE_IN_PROGRESS reconciliation
    # ------------------------------------------------------------------
    stuck_cutoff = now - timedelta(minutes=_STUCK_CAPTURE_MINUTES)
    stuck_charges = list(
        OrderCharge.objects.cross_store_unsafe()
        .filter(
            status=ChargeStatus.CAPTURE_IN_PROGRESS,
            capture_claimed_at__lt=stuck_cutoff,
            capture_claimed_at__isnull=False,
        )
        .select_related("order", "order__processor_account", "store")
    )

    for charge in stuck_charges:
        try:
            _watchdog_reconcile_stuck(charge=charge, now=now, logger=logger,
                                      ChargeStatus=ChargeStatus,
                                      OrderCharge=OrderCharge,
                                      get_connector=get_connector)
        except Exception:
            logger.exception(
                "capture_window_watchdog: pass 2 error for charge %s", charge.pk
            )

    # ------------------------------------------------------------------
    # Pass 3: Session finalization
    # ------------------------------------------------------------------
    finalization_sessions = list(
        CampaignSession.objects.cross_store_unsafe()
        .filter(
            state__in=non_terminal_list,
            expires_at__lt=now,
            expires_at__isnull=False,
            order__isnull=False,
        )
        .select_related("order", "store")
    )

    for session in finalization_sessions:
        try:
            _watchdog_finalize_session(
                session=session, now=now, logger=logger,
                CampaignSession=CampaignSession,
                CampaignSessionState=CampaignSessionState,
                ChargeStatus=ChargeStatus,
                ChargeType=ChargeType,
                OrderCharge=OrderCharge,
                OrderItemStatus=OrderItemStatus,
                non_terminal_list=non_terminal_list,
            )
        except Exception:
            logger.exception(
                "capture_window_watchdog: pass 3 error for session %s", session.pk
            )

    # ------------------------------------------------------------------
    # Pass 4: Breach alerting
    # ------------------------------------------------------------------
    alert_cutoff = now - timedelta(minutes=_ALERT_GRACE_MINUTES)
    breach_charges = (
        OrderCharge.objects.cross_store_unsafe()
        .filter(
            charge_type=ChargeType.ORIGINAL,
            status=ChargeStatus.AUTHORIZED,
            order__capture_window_expires_at__lt=alert_cutoff,
            order__capture_window_expires_at__isnull=False,
            breach_alerted=False,  # Only charges not yet alerted (ADR-011 Q5 pass 4 dedup).
        )
        .select_related("order")
    )

    for charge in breach_charges:
        # Conditional UPDATE: prevents two concurrent watchdog runs from each emitting
        # the same alert.  Only the run whose UPDATE affects 1 row emits the log.
        alerted = (
            OrderCharge.objects.cross_store_unsafe()
            .filter(pk=charge.pk, breach_alerted=False)
            .update(breach_alerted=True)
        )
        if alerted != 1:
            continue  # Another watchdog run already emitted this alert.
        logger.error(
            "upsell.watchdog.uncaptured_breach: ORIGINAL charge %s for order %s "
            "is still AUTHORIZED more than %d minutes past window expiry. "
            "Authorization risk: processor may auto-void. Investigate immediately.",
            charge.pk,
            charge.order_id,
            _ALERT_GRACE_MINUTES,
        )

    # ------------------------------------------------------------------
    # Pass 5: Ambiguous-outcome reconciliation (H2)
    # ------------------------------------------------------------------
    ambiguous_charges = list(
        OrderCharge.objects.cross_store_unsafe()
        .filter(
            charge_type=ChargeType.UPSELL,
            status=ChargeStatus.AMBIGUOUS_OUTCOME,
        )
        .select_related("order", "order__processor_account", "store")
    )

    for charge in ambiguous_charges:
        try:
            _watchdog_reconcile_ambiguous(charge=charge, now=now, logger=logger,
                                          ChargeStatus=ChargeStatus,
                                          OrderCharge=OrderCharge,
                                          get_connector=get_connector)
        except Exception:
            logger.exception(
                "capture_window_watchdog: pass 5 error for charge %s", charge.pk
            )

    # ------------------------------------------------------------------
    # Pass 6: UPSELL charges stuck AUTHORIZED — Phase C crash recovery (M2)
    # ------------------------------------------------------------------
    # When accept_upsell Phase C crashes after Phase B has already captured the
    # upsell charge, Phase C's transaction rolls back and the charge stays AUTHORIZED.
    # The Phase C except block writes a minimal CAPTURED row (autocommit), but this
    # pass is the safety net for cases where even that write failed or the feature was
    # not yet deployed when the crash occurred.
    #
    # Idempotency key "upsell:<order_pk>:<step_pk>" was stored in
    # processor_payment_intent_id at Phase A creation time, enabling recovery even
    # when processor_payment_intent_id was never updated to the real PI id.
    stuck_upsell_threshold = now - timedelta(minutes=_STUCK_CAPTURE_MINUTES)
    stuck_upsell_authorized = list(
        OrderCharge.objects.cross_store_unsafe()
        .filter(
            charge_type=ChargeType.UPSELL,
            status=ChargeStatus.AUTHORIZED,
            created_at__lt=stuck_upsell_threshold,
        )
        .select_related("order", "order__processor_account", "store")
    )

    for charge in stuck_upsell_authorized:
        if not charge.campaign_step_id or not charge.order_id:
            continue
        try:
            _watchdog_reconcile_upsell_authorized(
                charge=charge, now=now, logger=logger,
                ChargeStatus=ChargeStatus,
                OrderCharge=OrderCharge,
                get_connector=get_connector,
            )
        except Exception:
            logger.exception(
                "capture_window_watchdog: pass 6 error for charge %s", charge.pk
            )


# ---------------------------------------------------------------------------
# Watchdog helper functions (not tasks — called directly from the watchdog)
# ---------------------------------------------------------------------------

def _watchdog_claim_and_capture(
    charge, now, logger, CampaignSession, CampaignSessionState,
    ChargeStatus, OrderCharge, OrderItemStatus, non_terminal_list, get_connector,
):
    """
    Pass 1: atomically claim one expired ORIGINAL AUTHORIZED charge and capture it.

    Conditional UPDATE prevents double-capture when two watchdog runs overlap.
    """
    order = charge.order
    store = charge.store

    # Atomic claim: UPDATE WHERE status=AUTHORIZED → affected=1 means we won.
    claimed = (
        OrderCharge.objects.cross_store_unsafe()
        .filter(pk=charge.pk, status=ChargeStatus.AUTHORIZED)
        .update(
            status=ChargeStatus.CAPTURE_IN_PROGRESS,
            capture_claimed_at=now,
        )
    )
    if claimed != 1:
        # Another process already claimed this charge — skip.
        logger.debug(
            "capture_window_watchdog: charge %s already claimed — skipping", charge.pk
        )
        return

    # Connector for this processor account.
    connector = get_connector(order.processor_account)
    idempotency_key = f"capture:{order.pk}"

    capture_result = connector.capture_payment_intent(
        payment_intent_id=order.processor_payment_intent_id,
        idempotency_key=idempotency_key,
    )

    if capture_result.success:
        with transaction.atomic():
            OrderCharge.objects.cross_store_unsafe().filter(pk=charge.pk).update(
                status=ChargeStatus.CAPTURED,
                processor_charge_id=capture_result.processor_charge_id,
                captured_at=now,
            )
            # Void all PENDING_CAPTURE items for this order.
            from orders.models import OrderItem
            OrderItem.objects.cross_store_unsafe().filter(
                order=order,
                status=OrderItemStatus.PENDING_CAPTURE,
            ).update(status=OrderItemStatus.VOIDED)

            # Transition session → EXPIRED (only from non-terminal states).
            CampaignSession.objects.cross_store_unsafe().filter(
                order=order,
                state__in=non_terminal_list,
            ).update(
                state=CampaignSessionState.EXPIRED,
            )
        logger.info(
            "capture_window_watchdog: captured ORIGINAL charge %s for order %s",
            charge.pk, order.pk,
        )
    else:
        # Capture failed — revert to AUTHORIZED so future watchdog passes can retry.
        from campaigns.upsell_service import _revert_claim
        _revert_claim(charge, charge.store)
        logger.error(
            "capture_window_watchdog: capture failed for charge %s (order %s): %s",
            charge.pk, order.pk, capture_result.error_message,
        )


# ---------------------------------------------------------------------------
# Immediate capture for non-funnel orders (20:P1, DECIDED 2026-07-10)
# ---------------------------------------------------------------------------

def capture_original_charge_now(order) -> None:
    """
    Immediately capture `order`'s ORIGINAL AUTHORIZED charge (20:P1).

    Called from the payment-confirmation path — NOT from begin_checkout, where
    payment isn't confirmed yet — for orders with no funnel campaign
    (order.capture_immediately=True):
      - storefront.views_checkout._finalize_payment_return (Stripe + PayPal
        buyer-return path, both processors share this function).
      - payments.webhook_views._handle_payment_intent_authorized (Stripe
        payment_intent.amount_capturable_updated — async fallback for a buyer
        who closes the tab before returning).
      - payments.paypal_webhook_views._handle_authorization_created
        (PAYMENT.AUTHORIZATION.CREATED — async fallback, same reasoning).

    Reuses _watchdog_claim_and_capture — the exact atomic-claim + processor
    capture + idempotency-key ("capture:{order_id}") logic the
    capture_window_watchdog uses for Pass 1 — so there is only ONE capture code
    path in the codebase (design-pattern-ideas §XV-4), not a second
    implementation racing the first.

    No-op when there is no ORIGINAL AUTHORIZED charge for this order (already
    captured/claimed by a concurrent webhook delivery or the watchdog, or a
    zero-total order with no OrderCharge of this type at all) — the underlying
    atomic claim inside _watchdog_claim_and_capture would reach the same
    conclusion, but the extra SELECT here avoids an unnecessary connector
    lookup/log line on the common idempotent-replay path.

    Never raises: on any failure the charge is reverted to AUTHORIZED (by
    _watchdog_claim_and_capture's own except-branch) and capture_window_expires_at
    is left untouched on the order, so the next capture_window_watchdog run
    (every 2 minutes) captures it as the reconciliation fallback (ADR-011 Q5).
    Callers must not treat a call to this function as money-safe on its own —
    the watchdog is the backstop that makes the whole design money-safe.
    """
    from campaigns.models import CampaignSession, CampaignSessionState, NON_TERMINAL_STATES
    from orders.models import ChargeStatus, ChargeType, OrderCharge, OrderItemStatus
    from payments.factory import get_connector

    charge = (
        OrderCharge.objects.cross_store_unsafe()
        .filter(order=order, charge_type=ChargeType.ORIGINAL, status=ChargeStatus.AUTHORIZED)
        .select_related('order', 'order__processor_account', 'store')
        .first()
    )
    if charge is None:
        logger.debug(
            "capture_original_charge_now: no ORIGINAL AUTHORIZED charge for order %s "
            "— already captured/claimed or none exists, nothing to do",
            order.pk,
        )
        return

    now = timezone.now()
    try:
        _watchdog_claim_and_capture(
            charge=charge, now=now, logger=logger,
            CampaignSession=CampaignSession,
            CampaignSessionState=CampaignSessionState,
            ChargeStatus=ChargeStatus,
            OrderCharge=OrderCharge,
            OrderItemStatus=OrderItemStatus,
            non_terminal_list=list(NON_TERMINAL_STATES),
            get_connector=get_connector,
        )
    except Exception:
        # Belt-and-suspenders: _watchdog_claim_and_capture already catches the
        # connector call and reverts to AUTHORIZED on failure, but this call site
        # must never propagate into a webhook handler or a return-view request —
        # doing so would turn a payment-confirmation success into a 500.
        logger.exception(
            "capture_original_charge_now: inline capture failed for order %s — "
            "left for capture_window_watchdog fallback (capture_window_expires_at "
            "still set)",
            order.pk,
        )


def _watchdog_reconcile_stuck(charge, now, logger, ChargeStatus, OrderCharge, get_connector):
    """
    Pass 2: reconcile a CAPTURE_IN_PROGRESS charge stuck > _STUCK_CAPTURE_MINUTES minutes.

    Three steps (ADR-011 Q5 pass 2):

    1. Atomic re-claim: UPDATE capture_claimed_at=now WHERE status=CAPTURE_IN_PROGRESS.
       Prevents two concurrent watchdog runs from both re-issuing the capture.
       0 rows affected → already resolved by another process; skip.

    2. Retrieve the current status from the processor BEFORE re-issuing.
       If captured (the original call succeeded but the response was lost):
       record CAPTURED without a second processor call — no double-charge risk.

    3. If genuinely uncaptured: re-issue capture with the same idempotency key (safe,
       §XV-6). On failure, revert to AUTHORIZED so future passes can pick it up.
    """
    order = charge.order

    # Atomic re-claim: two concurrent watchdog runs each try to UPDATE the same row.
    # Only one succeeds (affected_rows==1); the other skips.
    re_claimed = (
        OrderCharge.objects.cross_store_unsafe()
        .filter(pk=charge.pk, status=ChargeStatus.CAPTURE_IN_PROGRESS)
        .update(capture_claimed_at=now)
    )
    if re_claimed != 1:
        logger.debug(
            "capture_window_watchdog: stuck charge %s already resolved — skipping", charge.pk
        )
        return

    connector = get_connector(order.processor_account)
    idempotency_key = f"capture:{order.pk}"

    # Retrieve processor status before re-issuing the capture.
    # The capture may have succeeded but the response was lost before we recorded it.
    processor_status = connector.retrieve_payment_intent_status(
        order.processor_payment_intent_id
    )

    if processor_status == "captured":
        # Already captured at the processor — record without re-issuing.
        # processor_charge_id is unknown from the lost response; use payment_intent_id
        # as the audit reference until a full reconcile GET is implemented (T029).
        OrderCharge.objects.cross_store_unsafe().filter(pk=charge.pk).update(
            status=ChargeStatus.CAPTURED,
            processor_charge_id=order.processor_payment_intent_id,
            captured_at=now,
        )
        logger.info(
            "capture_window_watchdog: stuck charge %s was already captured at processor "
            "(order %s) — recorded without re-issuing",
            charge.pk, order.pk,
        )
        return

    # Genuinely uncaptured: re-issue with the same idempotency key (safe, §XV-6).
    capture_result = connector.capture_payment_intent(
        payment_intent_id=order.processor_payment_intent_id,
        idempotency_key=idempotency_key,
    )

    if capture_result.success:
        OrderCharge.objects.cross_store_unsafe().filter(pk=charge.pk).update(
            status=ChargeStatus.CAPTURED,
            processor_charge_id=capture_result.processor_charge_id,
            captured_at=now,
        )
        logger.info(
            "capture_window_watchdog: reconciled stuck charge %s → CAPTURED (order %s)",
            charge.pk, order.pk,
        )
    else:
        # Revert to AUTHORIZED so future watchdog passes can pick this charge up again.
        from campaigns.upsell_service import _revert_claim
        _revert_claim(charge, charge.store)
        logger.error(
            "capture_window_watchdog: stuck charge %s still failing (order %s): %s",
            charge.pk, order.pk, capture_result.error_message,
        )


def _watchdog_reconcile_ambiguous(charge, now, logger, ChargeStatus, OrderCharge, get_connector):
    """
    Pass 5: resolve one AMBIGUOUS_OUTCOME UPSELL charge (H2 safety fix).

    The charge was created by the upsell accept flow; its network call to the
    processor failed mid-charge, so nobody knows whether the customer was
    charged.  The idempotency key ("upsell:<order_pk>:<step_pk>") was stored in
    the intent metadata at charge time, so the outcome can be resolved via
    retrieve_charge_by_idempotency_key:

    - ChargeResult(success=True)  → the customer WAS charged: promote to
      CAPTURED with the real PI id (conditional UPDATE, idempotent — a
      concurrent payment_intent.succeeded webhook may already have promoted it).
    - ChargeResult(success=False) → definitive non-success at the processor:
      mark FAILED.
    - None → still unknown (intent not found yet, or lookup failed): leave the
      charge AMBIGUOUS_OUTCOME for the next watchdog run — never guess.

    Session finalization is NOT done here: pass 3 finalizes sessions past their
    window based on CAPTURED upsell charges (single resolution function, §XV-4).
    """
    order = charge.order
    idempotency_key = f"upsell:{charge.order_id}:{charge.campaign_step_id}"

    connector = get_connector(order.processor_account)
    result = connector.retrieve_charge_by_idempotency_key(idempotency_key)

    if result is None:
        logger.info(
            "capture_window_watchdog: charge %s (order %s) outcome still unknown "
            "— left AMBIGUOUS_OUTCOME for next run",
            charge.pk, charge.order_id,
        )
        return

    if result.success:
        updated = (
            OrderCharge.objects.cross_store_unsafe()
            .filter(pk=charge.pk, status=ChargeStatus.AMBIGUOUS_OUTCOME)
            .update(
                status=ChargeStatus.CAPTURED,
                processor_charge_id=result.processor_charge_id,
                processor_payment_intent_id=(
                    result.processor_payment_intent_id or result.processor_charge_id
                ),
                captured_at=now,
            )
        )
        if updated:
            logger.info(
                "capture_window_watchdog: AMBIGUOUS charge %s reconciled → CAPTURED "
                "(order %s)",
                charge.pk, charge.order_id,
            )
    else:
        updated = (
            OrderCharge.objects.cross_store_unsafe()
            .filter(pk=charge.pk, status=ChargeStatus.AMBIGUOUS_OUTCOME)
            .update(status=ChargeStatus.FAILED)
        )
        if updated:
            logger.info(
                "capture_window_watchdog: AMBIGUOUS charge %s reconciled → FAILED "
                "(order %s): %s",
                charge.pk, charge.order_id, result.error_message,
            )


def _watchdog_reconcile_upsell_authorized(
    charge, now, logger, ChargeStatus, OrderCharge, get_connector,
):
    """
    Pass 6: resolve one UPSELL charge stuck AUTHORIZED (M2 / Phase C crash recovery).

    accept_upsell Phase C runs its DB writes in a transaction.  If that transaction
    crashes AFTER Phase B already captured the upsell charge, Phase C rolls back and
    the charge is left AUTHORIZED even though money moved.

    The Phase C except block attempts an immediate autocommit recovery, but this pass
    is the watchdog safety net.

    The idempotency key "upsell:<order_pk>:<step_pk>" was written into
    processor_payment_intent_id in Phase A, so retrieve_charge_by_idempotency_key
    can find the real outcome even when processor_payment_intent_id was never
    updated to the real Stripe PI id.

    Outcomes:
    - ChargeResult(success=True)         → CAPTURED with real PI ids.
    - ChargeResult(success=False)        → FAILED (definitive processor non-success).
    - None + past capture window         → FAILED (PI was never created; crash was
                                           pre-Phase B and window has now closed).
    - None + within capture window       → left AUTHORIZED for the next watchdog run.

    The conditional UPDATE (filter status=AUTHORIZED) is idempotent: if another
    process (Phase C except block, concurrent watchdog) already promoted the charge,
    affected_rows==0 and we skip silently.
    """
    order = charge.order
    idempotency_key = f"upsell:{charge.order_id}:{charge.campaign_step_id}"

    connector = get_connector(order.processor_account)
    result = connector.retrieve_charge_by_idempotency_key(idempotency_key)

    if result is None:
        # No PI found at the processor.
        # If we are past the capture window, the charge will never run — mark FAILED.
        if order.capture_window_expires_at and now > order.capture_window_expires_at:
            updated = (
                OrderCharge.objects.cross_store_unsafe()
                .filter(pk=charge.pk, status=ChargeStatus.AUTHORIZED)
                .update(status=ChargeStatus.FAILED)
            )
            if updated:
                logger.info(
                    "capture_window_watchdog: pass 6 — stuck AUTHORIZED UPSELL charge %s "
                    "→ FAILED (no PI found at processor, past window, order %s)",
                    charge.pk, charge.order_id,
                )
        # else: still within window — leave AUTHORIZED for the next watchdog run.
        return

    if result.success:
        updated = (
            OrderCharge.objects.cross_store_unsafe()
            .filter(pk=charge.pk, status=ChargeStatus.AUTHORIZED)
            .update(
                status=ChargeStatus.CAPTURED,
                processor_charge_id=result.processor_charge_id,
                processor_payment_intent_id=(
                    result.processor_payment_intent_id or result.processor_charge_id
                ),
                captured_at=now,
            )
        )
        if updated:
            logger.info(
                "capture_window_watchdog: pass 6 — stuck AUTHORIZED UPSELL charge %s "
                "→ CAPTURED (Phase C crash recovery, order %s)",
                charge.pk, charge.order_id,
            )
    else:
        updated = (
            OrderCharge.objects.cross_store_unsafe()
            .filter(pk=charge.pk, status=ChargeStatus.AUTHORIZED)
            .update(status=ChargeStatus.FAILED)
        )
        if updated:
            logger.info(
                "capture_window_watchdog: pass 6 — stuck AUTHORIZED UPSELL charge %s "
                "→ FAILED (order %s): %s",
                charge.pk, charge.order_id, result.error_message,
            )


def _watchdog_finalize_session(
    session, now, logger, CampaignSession, CampaignSessionState,
    ChargeStatus, ChargeType, OrderCharge, OrderItemStatus, non_terminal_list,
):
    """
    Pass 3: finalize a non-terminal session past its capture window.

    Transitions to CONVERTED if ≥1 UPSELL charge is CAPTURED; else → EXPIRED.
    Voids remaining PENDING_CAPTURE items.
    """
    order = session.order

    has_captured_upsell = OrderCharge.objects.cross_store_unsafe().filter(
        order=order,
        charge_type=ChargeType.UPSELL,
        status=ChargeStatus.CAPTURED,
    ).exists()

    new_state = (
        CampaignSessionState.CONVERTED
        if has_captured_upsell
        else CampaignSessionState.EXPIRED
    )

    updated = CampaignSession.objects.cross_store_unsafe().filter(
        pk=session.pk,
        state__in=non_terminal_list,
    ).update(state=new_state)

    if updated:
        from orders.models import OrderItem
        OrderItem.objects.cross_store_unsafe().filter(
            order=order,
            status=OrderItemStatus.PENDING_CAPTURE,
        ).update(status=OrderItemStatus.VOIDED)

        logger.info(
            "capture_window_watchdog: session %s finalized → %s (order %s)",
            session.pk, new_state, order.pk,
        )
