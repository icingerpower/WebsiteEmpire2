"""
Upsell capture-window service — three-phase transaction core (ADR-011 / ADR-007 §3).

This module is the single resolution function (§XV-4) for the post-purchase
upsell flow.  All three paths — accept, decline, watchdog expiry — run through
this module so they cannot drift.

Public API:
    accept_upsell(session_token_value, store) -> dict
    decline_upsell(session_token_value, store) -> dict

Shared helpers (also imported by campaigns/tasks.py watchdog):
    _revert_claim(charge, store)

Design invariants (ADR-011 / design-pattern-ideas.txt):
- Phase A (DB guards) commits BEFORE any network call (Phase B).
- accept_attempts increment and token consumption are in Phase A — they survive
  even if Phase B fails, preventing token replay and dead rate-limit counters.
- SELECT … FOR UPDATE on the ORIGINAL OrderCharge row before any state mutation.
- DB-time window check (timezone.now() vs capture_window_expires_at) inside the lock.
- Race-guard conditional UPDATE (status='AUTHORIZED' → 'CAPTURE_IN_PROGRESS',
  affected_rows==1 or abort).
- Processor calls happen AFTER state is claimed — never before.
- Session transitions use conditional UPDATE (state__in=NON_TERMINAL_STATES) for
  idempotency.
- accept_attempts is incremented with F() inside Phase A.
- Raw tokens are NEVER logged — only the SHA-256 hash.
- UpsellRateLimitError and UpsellUnavailableError propagate to the view layer;
  they are never silenced here (§XIII).
- On rate limit (attempts >= 5): session is auto-declined (→ DISMISSED) inside
  Phase A before raising UpsellRateLimitError.
- Soft-fail (T039 FR-M1): on a DEFINITIVE off-session charge failure (processor
  responded with a decline), count CAPTURED UPSELL charges for this order.
  If ≥1 → CONVERTED; if 0 → DISMISSED (never silently continues).
  Rationale: after a card decline, immediately serving the next step would fail again
  off-session. (DECIDED 2026-07-05 — human-approved ASSUMPTION).
- Ambiguous outcome (H2 safety fix): a network failure during the off-session
  charge is NOT a decline — Stripe may have captured before the timeout.  The
  connector raises AmbiguousChargeOutcome; this module first attempts recovery
  via retrieve_charge_by_idempotency_key.  If recovery confirms success, the
  flow continues as a normal success.  If the outcome stays unknown, the UPSELL
  charge is marked AMBIGUOUS_OUTCOME, the session is NOT finalized (stays
  INTERACTION), and the exception propagates.  The capture-window watchdog
  (campaigns/tasks.py pass 5) reconciles AMBIGUOUS_OUTCOME charges later.
  Marking such a charge FAILED would lose money-state: customer charged, goods
  never delivered → chargebacks.
- Multi-step progression (T039): after step N succeeds and accept_next_step is set,
  session stays INTERACTION with current_step=next_step; a new single-use token is
  issued inside Phase C so the response always carries a live token (FR-M4 / §XV-3).
- Per-step audit events (step_served, step_accepted, step_declined) are appended to
  session.data_json OUTSIDE the transaction — audit logging must never block the flow.
"""

import logging
import secrets
from datetime import timedelta
from decimal import Decimal

from django.db import IntegrityError, transaction
from django.db.models import F
from django.utils import timezone

from campaigns.exceptions import UpsellRateLimitError, UpsellUnavailableError
from campaigns.tokens import consume_token
from payments.exceptions import AmbiguousChargeOutcome

logger = logging.getLogger("campaigns.upsell_service")

# Maximum accept attempts before UpsellRateLimitError (ADR-011 ticket spec).
_ACCEPT_ATTEMPTS_LIMIT = 5

# Minutes after which a CAPTURE_IN_PROGRESS charge is considered "stuck"
# and the watchdog reconciles it (ADR-011 Q5 pass 2).
_STUCK_CAPTURE_MINUTES = 10

# Grace period beyond the capture window before emitting the breach alert
# (ADR-011 Q5 pass 4, upsell.watchdog.uncaptured_breach).
_ALERT_GRACE_MINUTES = 15


# ---------------------------------------------------------------------------
# Shared helpers (used by both accept_upsell and the watchdog in tasks.py)
# ---------------------------------------------------------------------------

def _revert_claim(charge, store) -> None:
    """
    Revert an ORIGINAL charge from CAPTURE_IN_PROGRESS back to AUTHORIZED.

    Called when capture fails at Phase B so the watchdog can retry the charge
    on subsequent runs.  Safe to call multiple times (idempotent via status check
    in the UPDATE — won't affect a charge already CAPTURED by another path).
    """
    from orders.models import OrderCharge
    OrderCharge.objects.for_store(store).filter(
        pk=charge.pk,
        status__in=("capture_in_progress",),
    ).update(
        status="authorized",
        capture_claimed_at=None,
    )


def _append_session_event(store, session, event: dict) -> None:
    """
    Append an audit event dict to session.data_json atomically.

    Uses a read-modify-write pattern that is safe for low-concurrency audit paths
    (one accept/decline per session at a time, enforced by the single-use token).
    For high-concurrency paths a database JSON_APPEND function could be used instead.

    Silently skips on any exception — audit logging must never block the flow.
    """
    from campaigns.models import CampaignSession
    try:
        obj = CampaignSession.objects.for_store(store).filter(pk=session.pk).first()
        if obj is None:
            return
        current = obj.data_json if isinstance(obj.data_json, list) else []
        current.append(event)
        CampaignSession.objects.for_store(store).filter(pk=session.pk).update(data_json=current)
    except Exception:
        logger.warning(
            "upsell._append_session_event: failed for session %s (non-fatal)",
            session.pk, exc_info=True,
        )


def _serve_step(session, step, order) -> dict:
    """
    Issue a new single-use upsell-act token for `step` and build the next_step payload.

    MUST be called INSIDE the calling transaction (Phase C for accept, the decline
    transaction for decline) so the token row and session state update commit atomically
    (FR-M4 / §XV-3 — a response that names a next step always carries a live token).

    Token expires at order.capture_window_expires_at (remaining window, minimum 1 s).
    The window check must already have passed before this call.

    Content falls back to source-language offer_config_json keys when no published
    translation is found (ADR-014 DECIDED 2026-07-05 — show source-language label,
    never 404). Session locale is not yet stored on CampaignSession (T029 scope);
    translation lookup is a future enhancement.

    Never log the returned 'upsell_act_token' value — raw tokens must never be logged
    (ADR-011 Q2 / ADR-007 §5).
    """
    from campaigns.models import StepOfferType, TokenPurpose
    from campaigns.tokens import issue_token

    now = timezone.now()
    if order and order.capture_window_expires_at:
        remaining_secs = max(1, int((order.capture_window_expires_at - now).total_seconds()))
    else:
        # Fallback — window check already ran, so this only fires for windowless orders.
        remaining_secs = 60

    # M3: snapshot the offer amount at token-issue time so accept_upsell can detect
    # admin price edits between impression and accept (design-pattern-ideas.txt §XIV).
    # Storewide-discount steps charge nothing → None (M3 guard skipped).
    # If the step is misconfigured (raises UpsellUnavailableError), default to None
    # rather than crashing Phase C or the decline transaction — the correct error will
    # surface when the customer later tries to accept this step.
    if step.offer_type == StepOfferType.STOREWIDE_DISCOUNT:
        offered_amount_cents = None
    else:
        try:
            offered_amount_cents = _get_upsell_amount_cents(step)
        except UpsellUnavailableError:
            offered_amount_cents = None

    raw = issue_token(
        session,
        TokenPurpose.UPSELL_ACT,
        ttl_seconds=remaining_secs,
        offered_amount_cents=offered_amount_cents,
    )

    cfg = step.offer_config_json
    title = cfg.get("title", "")
    description = cfg.get("description", "")
    cta_label = cfg.get("cta_label", "")
    amount_cents = cfg.get("upsell_amount_cents", 0)
    currency = order.currency if order else ""

    return {
        "step_id": step.pk,
        "offer_type": step.offer_type,
        "title": title,
        "description": description,
        "cta_label": cta_label,
        "amount_cents": amount_cents,
        "currency": currency,
        "upsell_act_token": raw,
    }


# ---------------------------------------------------------------------------
# Public accept / decline API
# ---------------------------------------------------------------------------

def accept_upsell(session_token_value: str, store) -> dict:
    """
    Accept the current upsell offer for this session.

    Three committed phases (ADR-011 safety fix — Fix 1):

    Phase A (own committed transaction): DB guards only — no network calls.
      Consumes token, increments accept_attempts, claims or asserts the ORIGINAL
      charge (step 1: AUTHORIZED → CAPTURE_IN_PROGRESS; steps 2..N: assert CAPTURED),
      creates the UPSELL charge row.  Commits before Phase B.
      On rate limit (attempts >= 5): auto-declines the session and sets a flag;
      raises UpsellRateLimitError after Phase A commits.

    Phase B (no transaction): network calls — capture original (step 1 only),
      create upsell off-session charge.

    Phase C (own committed transaction): finalize state based on Phase B outcomes.
      On original capture failure (step 1 only): reverts ORIGINAL → AUTHORIZED,
      deletes UPSELL row.
      On upsell charge failure (soft-fail): count CAPTURED UPSELL charges;
        if ≥1 → CONVERTED (prior revenue preserved); if 0 → DISMISSED.
      On success, non-terminal step (accept_next_step set): session stays INTERACTION,
        current_step advances, recovery_revenue accumulates, new token issued, next_step
        payload returned.
      On success, terminal step: session → CONVERTED with accumulated recovery_revenue.

    Returns:
      {'status': 'accepted', 'order_charge_id': <pk>, 'next_step': {…}|null}
      {'status': 'soft_fail', 'message': '…', 'next_step': null}

    Raises:
        CampaignSessionToken.DoesNotExist  — invalid / expired / consumed token
        UpsellRateLimitError               — 6th+ accept attempt (HTTP 403)
        UpsellUnavailableError             — ineligible state (HTTP 410)
    """
    from campaigns.models import (
        CampaignSession,
        CampaignSessionState,
        CampaignSessionToken,
        NON_TERMINAL_STATES,
        StepOfferType,
        TokenPurpose,
    )
    from orders.models import ChargeStatus, ChargeType, OrderCharge
    from payments.factory import get_connector
    from payments.models import ProcessorType

    # -----------------------------------------------------------------------
    # Phase A: DB guards — committed before any network call.
    # -----------------------------------------------------------------------
    _rate_limited = False
    # Variables populated in Phase A's else-branch, used in Phase B and Phase C.
    connector = None
    original_charge = None
    upsell_charge = None
    order = None
    step = None
    upsell_amount_cents = 0
    _is_first_accept = False  # True when ORIGINAL is AUTHORIZED (first accept in session)
    # FR-S3 (T039): storewide_discount steps issue a coupon and charge nothing.
    _is_storewide_discount = False
    issued_coupon_code = None

    with transaction.atomic():
        # Step 1: Consume token (identifies the session, prevents replay).
        # Never log session_token_value.
        token = consume_token(session_token_value, TokenPurpose.UPSELL_ACT)

        # Step 2: Validate store match (cross-store token is treated as invalid).
        if token.store_id != store.pk:
            raise CampaignSessionToken.DoesNotExist("Store mismatch on token.")

        # Step 3: Lock session row for rate-limit check and atomic operations.
        session = (
            CampaignSession.objects.for_store(store)
            .select_for_update()
            .get(pk=token.campaign_session_id)
        )

        # Step 4: Rate limit — on the 6th+ attempt (accept_attempts >= 5), auto-decline.
        if session.accept_attempts >= _ACCEPT_ATTEMPTS_LIMIT:
            logger.warning(
                "upsell.accept: rate limit reached for session %s (attempts=%d)",
                session.pk,
                session.accept_attempts,
            )
            CampaignSession.objects.for_store(store).filter(
                pk=session.pk,
                state__in=list(NON_TERMINAL_STATES),
            ).update(
                state=CampaignSessionState.DISMISSED,
                converted_at=timezone.now(),
            )
            _rate_limited = True
        else:
            # Step 5: Increment accept_attempts atomically.
            CampaignSession.objects.for_store(store).filter(pk=session.pk).update(
                accept_attempts=F("accept_attempts") + 1
            )

            # Step 6: Session must be in INTERACTION state.
            if session.state != CampaignSessionState.INTERACTION:
                raise UpsellUnavailableError(
                    f"Session {session.pk} is in state {session.state!r}, "
                    "expected INTERACTION."
                )

            # Step 7: Get current step.
            step = session.current_step
            if step is None:
                raise UpsellUnavailableError(
                    f"Session {session.pk} has no current_step set."
                )

            # FR-S3 (T039): storewide_discount steps issue a coupon and charge nothing.
            # Determined here so off-session eligibility guards (step 11) can be skipped.
            _is_storewide_discount = (step.offer_type == StepOfferType.STOREWIDE_DISCOUNT)

            # Step 8: Get order anchor.
            order = session.order
            if order is None:
                raise UpsellUnavailableError(
                    f"Session {session.pk} has no order anchor."
                )

            # Step 9: Lock the ORIGINAL charge and determine step number.
            # Step 1: ORIGINAL is AUTHORIZED → claim it (first accept in session).
            # Steps 2..N: ORIGINAL is CAPTURED → assert it, skip capture in Phase B.
            # Any other status → ineligible.
            original_charge = (
                OrderCharge.objects.for_store(store)
                .select_for_update()
                .filter(order=order, charge_type=ChargeType.ORIGINAL)
                .first()
            )
            if original_charge is None:
                raise UpsellUnavailableError(
                    f"No ORIGINAL charge found for order {order.pk}."
                )

            if original_charge.status == ChargeStatus.AUTHORIZED:
                _is_first_accept = True
            elif original_charge.status == ChargeStatus.CAPTURED:
                _is_first_accept = False
            else:
                raise UpsellUnavailableError(
                    f"ORIGINAL charge {original_charge.pk} is in status "
                    f"{original_charge.status!r} — expected AUTHORIZED (step 1) "
                    "or CAPTURED (step 2+)."
                )

            # Step 10: DB-time window check (inside the lock).
            now = timezone.now()
            if order.capture_window_expires_at and now >= order.capture_window_expires_at:
                raise UpsellUnavailableError(
                    f"Capture window expired for order {order.pk} "
                    f"(expired at {order.capture_window_expires_at})."
                )

            # M3 guard: verify the amount has not changed since the token was issued.
            # Prevents admin price edits between impression and accept from silently
            # charging an amount the customer never saw (design-pattern-ideas.txt §XIV).
            # Runs only for non-storewide steps with a stored snapshot (offered_amount_cents
            # is None for storewide_discount tokens and for tokens issued before M3).
            if token.offered_amount_cents is not None:
                current_amount = _get_upsell_amount_cents(step)
                if current_amount != token.offered_amount_cents:
                    raise UpsellUnavailableError(
                        "Offer price changed since it was shown to you. "
                        "Please refresh the page to see the current offer."
                    )

            # Step 11: Eligibility guards (vault, capture mode, off-session support).
            # storewide_discount steps charge nothing — skip off-session guards for them.
            # The ORIGINAL capture (step 1) still needs a connector, so always get one.
            if order.processor_account is None:
                raise UpsellUnavailableError(
                    f"Order {order.pk} has no processor account pinned."
                )

            pa = order.processor_account
            connector = get_connector(pa)

            if not _is_storewide_discount:
                if pa.processor_type == ProcessorType.PAYPAL:
                    if pa.paypal_capture_mode == "immediate":
                        raise UpsellUnavailableError(
                            "PayPal immediate-capture mode is funnel-ineligible."
                        )
                    if not pa.paypal_vault_enabled:
                        raise UpsellUnavailableError(
                            "PayPal vault not enabled on this account. "
                            "Off-session upsell charges require paypal_vault_enabled=True."
                        )

                if not connector.supports_off_session_charge():
                    raise UpsellUnavailableError(
                        f"Processor {pa.processor_type!r} does not support off-session charges."
                    )

                if not order.processor_payment_method_id:
                    raise UpsellUnavailableError(
                        "No stored payment method for off-session charge. "
                        "Vault token is set by the payment confirmation webhook (T029)."
                    )

            # Step 12: Atomic claim — ORIGINAL AUTHORIZED → CAPTURE_IN_PROGRESS (step 1 only).
            if _is_first_accept:
                claimed = (
                    OrderCharge.objects.for_store(store)
                    .filter(pk=original_charge.pk, status=ChargeStatus.AUTHORIZED)
                    .update(
                        status=ChargeStatus.CAPTURE_IN_PROGRESS,
                        capture_claimed_at=now,
                    )
                )
                if claimed != 1:
                    raise UpsellUnavailableError(
                        f"ORIGINAL charge {original_charge.pk} was already claimed "
                        "by another process."
                    )

            # Step 13: Get upsell amount and create UPSELL OrderCharge row.
            # storewide_discount steps charge nothing — skip charge creation for them.
            if not _is_storewide_discount:
                upsell_amount_cents = _get_upsell_amount_cents(step)
                idempotency_key = f"upsell:{order.pk}:{step.pk}"

                try:
                    with transaction.atomic():  # savepoint for idempotency guard
                        upsell_charge = OrderCharge.objects.create(
                            store=store,
                            order=order,
                            charge_type=ChargeType.UPSELL,
                            status=ChargeStatus.AUTHORIZED,
                            amount_cents=upsell_amount_cents,
                            campaign_step_id=step.pk,
                            processor_payment_intent_id=idempotency_key,
                        )
                except IntegrityError:
                    # Retry path: the (order, campaign_step_id) partial unique constraint fired.
                    upsell_charge = (
                        OrderCharge.objects.for_store(store)
                        .get(
                            order=order,
                            campaign_step_id=step.pk,
                            charge_type=ChargeType.UPSELL,
                        )
                    )
                    logger.info(
                        "upsell.accept: recovered existing UPSELL charge %s on retry "
                        "(order=%s step=%s)",
                        upsell_charge.pk, order.pk, step.pk,
                    )
    # Phase A commits here.

    # Raise rate-limit error after Phase A has committed the auto-decline.
    if _rate_limited:
        raise UpsellRateLimitError(
            f"Too many accept attempts for session {session.pk}."
        )

    # -----------------------------------------------------------------------
    # Phase B: network calls — no transaction wrapper.
    # -----------------------------------------------------------------------
    if _is_first_accept:
        # Step 1: capture the ORIGINAL authorization.
        capture_idempotency_key = f"capture:{order.pk}"
        capture_result = connector.capture_payment_intent(
            payment_intent_id=order.processor_payment_intent_id,
            idempotency_key=capture_idempotency_key,
        )

        if not capture_result.success:
            # Original capture failed — revert claim and clean up UPSELL row if any.
            with transaction.atomic():
                _revert_claim(original_charge, store)
                if upsell_charge is not None:
                    OrderCharge.objects.for_store(store).filter(pk=upsell_charge.pk).delete()
            logger.error(
                "upsell.accept: ORIGINAL capture failed for order %s: %s",
                order.pk, capture_result.error_message,
            )
            raise UpsellUnavailableError(
                f"Original charge capture failed: {capture_result.error_message}"
            )
    else:
        capture_result = None  # Steps 2..N: ORIGINAL already CAPTURED, skip.

    # Phase B step 2: off-session charge.
    # storewide_discount steps issue a coupon and charge nothing — skip entirely.
    upsell_result = None
    if not _is_storewide_discount:
        try:
            upsell_result = connector.create_off_session_charge(
                payment_method_id=order.processor_payment_method_id,
                amount=upsell_amount_cents,
                currency=order.currency,
                customer_id=order.processor_customer_id,
                metadata={
                    "order_id": str(order.pk),
                    "step_id": str(step.pk),
                    # Written into the intent metadata so that
                    # retrieve_charge_by_idempotency_key can find the intent again
                    # after a network failure (H2 recovery path).
                    "idempotency_key": idempotency_key,
                },
                idempotency_key=idempotency_key,
            )
        except AmbiguousChargeOutcome:
            # H2: the network failed mid-charge — Stripe may or may not have captured.
            # This is NOT a decline; the soft-fail path (FAILED + session finalized)
            # must not run.  First try to resolve the real outcome.
            recovered = connector.retrieve_charge_by_idempotency_key(idempotency_key)
            if recovered is not None and recovered.success:
                # The charge DID go through before the timeout — continue as success.
                logger.warning(
                    "upsell.accept: charge outcome recovered as SUCCESS after network "
                    "failure (order=%s step=%s charge=%s)",
                    order.pk, step.pk, upsell_charge.pk,
                )
                upsell_result = recovered
            else:
                # Outcome still unknown (recovery found nothing, failed, or returned a
                # non-succeeded intent).  Persist the unknown state and leave the
                # session UNFINALIZED — the capture-window watchdog reconciles it.
                with transaction.atomic():
                    if _is_first_accept and capture_result is not None and capture_result.success:
                        # The ORIGINAL capture (step 1) succeeded in Phase B before the
                        # ambiguous upsell charge — record it so the money-state of the
                        # original order is not lost when we re-raise.
                        OrderCharge.objects.for_store(store).filter(pk=original_charge.pk).update(
                            status=ChargeStatus.CAPTURED,
                            processor_charge_id=capture_result.processor_charge_id,
                            captured_at=timezone.now(),
                        )
                    OrderCharge.objects.for_store(store).filter(pk=upsell_charge.pk).update(
                        status=ChargeStatus.AMBIGUOUS_OUTCOME,
                    )
                logger.error(
                    "upsell.accept: UPSELL charge outcome UNKNOWN after network failure "
                    "— charge %s marked AMBIGUOUS_OUTCOME, session %s NOT finalized "
                    "(order=%s step=%s). Watchdog will reconcile.",
                    upsell_charge.pk, session.pk, order.pk, step.pk,
                )
                _append_session_event(
                    store, session,
                    {
                        "event": "upsell_charge_ambiguous",
                        "step_id": step.pk,
                        "ts": timezone.now().isoformat(),
                    },
                )
                raise

    # -----------------------------------------------------------------------
    # Phase C: finalize — own committed transaction.
    # -----------------------------------------------------------------------
    now = timezone.now()
    next_step_payload = None
    _step_pk = step.pk
    _next_step_pk = None

    try:
        with transaction.atomic():
            if _is_first_accept and capture_result is not None:
                # ORIGINAL is confirmed captured (step 1 only).
                OrderCharge.objects.for_store(store).filter(pk=original_charge.pk).update(
                    status=ChargeStatus.CAPTURED,
                    processor_charge_id=capture_result.processor_charge_id,
                    captured_at=now,
                )
                logger.info(
                    "upsell.accept: captured ORIGINAL charge %s for order %s",
                    original_charge.pk, order.pk,
                )

            if _is_storewide_discount:
                # FR-S3 (T039): free coupon step — issue code, advance session, no charge.
                issued_coupon_code = _maybe_issue_coupon(store=store, session=session, step=step)
                if step.accept_next_step_id is not None:
                    # Non-terminal accept: session stays INTERACTION, advance current_step.
                    next_step = step.accept_next_step
                    CampaignSession.objects.for_store(store).filter(
                        pk=session.pk,
                        state__in=list(NON_TERMINAL_STATES),
                    ).update(
                        state=CampaignSessionState.INTERACTION,
                        current_step=next_step,
                    )
                    # Issue a new single-use token inside the transaction (FR-M4).
                    next_step_payload = _serve_step(session, next_step, order)
                    _next_step_pk = next_step.pk
                    logger.info(
                        "upsell.accept: storewide_discount — session %s advanced to step %s "
                        "(order=%s next_step=%s)",
                        session.pk, next_step.pk, order.pk, next_step.pk,
                    )
                else:
                    # Terminal accept: session → CONVERTED (coupon was the final step).
                    CampaignSession.objects.for_store(store).filter(
                        pk=session.pk,
                        state__in=list(NON_TERMINAL_STATES),
                    ).update(
                        state=CampaignSessionState.CONVERTED,
                        converted_at=now,
                    )
                    logger.info(
                        "upsell.accept: storewide_discount — session %s CONVERTED "
                        "(order=%s step=%s)",
                        session.pk, order.pk, step.pk,
                    )

            elif upsell_result.success:
                # Write the real processor intent id (FR-W3) so that deferred webhook events
                # (arriving with the processor's own id) can look up and promote this UPSELL row.
                OrderCharge.objects.for_store(store).filter(pk=upsell_charge.pk).update(
                    status=ChargeStatus.CAPTURED,
                    processor_charge_id=upsell_result.processor_charge_id,
                    processor_payment_intent_id=(
                        upsell_result.processor_payment_intent_id
                        or upsell_result.processor_charge_id
                    ),
                    captured_at=now,
                )
                captured_amount = Decimal(upsell_amount_cents) / 100
                logger.info(
                    "upsell.accept: UPSELL charge %s captured (order=%s step=%s)",
                    upsell_charge.pk, order.pk, step.pk,
                )

                # Multi-step progression (T039 FR-M1 / FR-M2):
                # Refresh the step's accept_next_step_id without an extra DB query —
                # step.accept_next_step_id is the FK field already loaded in memory.
                if step.accept_next_step_id is not None:
                    # Non-terminal accept: session stays INTERACTION, advance current_step,
                    # accumulate recovery_revenue. converted_at is NOT set (not yet CONVERTED).
                    next_step = step.accept_next_step
                    CampaignSession.objects.for_store(store).filter(
                        pk=session.pk,
                        state__in=list(NON_TERMINAL_STATES),
                    ).update(
                        state=CampaignSessionState.INTERACTION,
                        current_step=next_step,
                        recovery_revenue=F("recovery_revenue") + captured_amount,
                    )
                    # Issue a new single-use token inside the transaction (FR-M4).
                    next_step_payload = _serve_step(session, next_step, order)
                    _next_step_pk = next_step.pk
                    logger.info(
                        "upsell.accept: session %s advanced to step %s "
                        "(order=%s next_step=%s)",
                        session.pk, next_step.pk, order.pk, next_step.pk,
                    )
                else:
                    # Terminal accept: session → CONVERTED.
                    CampaignSession.objects.for_store(store).filter(
                        pk=session.pk,
                        state__in=list(NON_TERMINAL_STATES),
                    ).update(
                        state=CampaignSessionState.CONVERTED,
                        converted_at=now,
                        recovery_revenue=F("recovery_revenue") + captured_amount,
                    )
                    logger.info(
                        "upsell.accept: session %s CONVERTED (order=%s step=%s "
                        "upsell_charge=%s)",
                        session.pk, order.pk, step.pk, upsell_charge.pk,
                    )

            else:
                # Soft-fail: ORIGINAL is intact and CAPTURED; off-session UPSELL charge failed.
                # Set UPSELL charge to FAILED.
                OrderCharge.objects.for_store(store).filter(pk=upsell_charge.pk).update(
                    status=ChargeStatus.FAILED,
                )
                logger.warning(
                    "upsell.accept: UPSELL off-session charge failed for order %s "
                    "step %s: %s",
                    order.pk, step.pk, upsell_result.error_message,
                )

                # Soft-fail finalization (T039 FR-M1 DECIDED 2026-07-05):
                # Count CAPTURED UPSELL charges for this order (the just-failed one is FAILED,
                # not included). If ≥1 prior capture → CONVERTED; if 0 → DISMISSED.
                from orders.models import ChargeStatus as CS, ChargeType as CT
                captured_upsell_count = (
                    OrderCharge.objects.for_store(store)
                    .filter(
                        order=order,
                        charge_type=CT.UPSELL,
                        status=CS.CAPTURED,
                    )
                    .count()
                )
                if captured_upsell_count >= 1:
                    soft_fail_state = CampaignSessionState.CONVERTED
                else:
                    soft_fail_state = CampaignSessionState.DISMISSED

                CampaignSession.objects.for_store(store).filter(
                    pk=session.pk,
                    state__in=list(NON_TERMINAL_STATES),
                ).update(
                    state=soft_fail_state,
                    converted_at=now,
                )
                logger.info(
                    "upsell.accept: soft-fail — session %s → %s "
                    "(captured_upsell_count=%d order=%s step=%s)",
                    session.pk, soft_fail_state, captured_upsell_count, order.pk, step.pk,
                )
        # Phase C commits here.

    except Exception:
        # Phase B succeeded — money moved but the DB record is incomplete because
        # Phase C's transaction rolled back.  Write the minimum state in autocommit
        # statements so the charge is not invisible to the watchdog (pass 6).
        # Never log token values — only PKs and IDs (ADR-011 Q2 / ADR-007 §5).
        logger.error(
            "upsell.accept.phase_c_failed: writing minimal CAPTURED state; "
            "watchdog pass 6 will reconcile (order=%s step=%s)",
            order.pk, step.pk,
            exc_info=True,
        )
        # Only update if Phase B actually captured the upsell charge.
        # upsell_result is None for storewide_discount steps (no charge) and when
        # Phase B was not reached; upsell_charge is only set for non-storewide steps.
        if upsell_result is not None and getattr(upsell_result, "success", False):
            OrderCharge.objects.cross_store_unsafe().filter(
                pk=upsell_charge.pk,
            ).update(
                status=ChargeStatus.CAPTURED,
                processor_payment_intent_id=(
                    upsell_result.processor_payment_intent_id or ""
                ),
                captured_at=timezone.now(),
            )
        raise  # Re-raise so Celery / the caller sees the failure.

    # -----------------------------------------------------------------------
    # Post-Phase-C: audit events (outside transaction — non-fatal).
    # -----------------------------------------------------------------------
    _now_str = now.isoformat()

    if _is_storewide_discount:
        _append_session_event(
            store, session, {"event": "step_accepted", "step_id": _step_pk, "ts": _now_str}
        )
        if _next_step_pk is not None:
            _append_session_event(
                store, session, {"event": "step_served", "step_id": _next_step_pk, "ts": _now_str}
            )
        return {
            "status": "accepted",
            "order_charge_id": None,
            "issued_coupon_code": issued_coupon_code,
            "next_step": next_step_payload,
        }
    elif upsell_result.success:
        _append_session_event(
            store, session, {"event": "step_accepted", "step_id": _step_pk, "ts": _now_str}
        )
        if _next_step_pk is not None:
            _append_session_event(
                store, session, {"event": "step_served", "step_id": _next_step_pk, "ts": _now_str}
            )
        return {
            "status": "accepted",
            "order_charge_id": upsell_charge.pk,
            "issued_coupon_code": None,
            "next_step": next_step_payload,
        }
    else:
        _append_session_event(
            store, session, {"event": "upsell_charge_failed", "ts": _now_str}
        )
        return {"status": "soft_fail", "message": "Payment could not be processed", "next_step": None}


def decline_upsell(session_token_value: str, store) -> dict:
    """
    Decline the current upsell offer for this session (T039 multi-step aware).

    Validates the upsell-act token and advances the session:

    Non-terminal decline (session.current_step.decline_next_step is set):
      Session stays INTERACTION, current_step moves to the decline branch, a new
      single-use token is issued, response carries next_step payload.

    Terminal decline (decline_next_step is null):
      Session finalizes: DISMISSED if 0 UPSELL charges CAPTURED for this order;
      CONVERTED (with accumulated recovery_revenue) if ≥1 CAPTURED — mirrors the
      watchdog finalization rule (ADR-011 Q5 pass 3) so two paths cannot disagree
      on the final state (§XV-4).

    If the session is already terminal (another tab accepted, or watchdog fired):
      Safe no-op — returns {'status': 'declined', 'next_step': null}.

    Returns {'status': 'declined', 'next_step': {…}|null}.

    Raises:
        CampaignSessionToken.DoesNotExist  — invalid / expired / consumed token
        UpsellUnavailableError             — window expired on non-terminal decline
    """
    from campaigns.models import (
        CampaignSession,
        CampaignSessionState,
        CampaignSessionToken,
        NON_TERMINAL_STATES,
        TokenPurpose,
    )
    from orders.models import ChargeStatus, ChargeType, OrderCharge

    _step_pk = None
    _next_step_pk = None
    _session = None
    _result = None

    with transaction.atomic():
        # Consume the upsell-act token.  Never log session_token_value.
        token = consume_token(session_token_value, TokenPurpose.UPSELL_ACT)

        if token.store_id != store.pk:
            raise CampaignSessionToken.DoesNotExist("Store mismatch on token.")

        # Lock the session row so no concurrent accept or watchdog can race.
        _session = (
            CampaignSession.objects.for_store(store)
            .select_for_update()
            .get(pk=token.campaign_session_id)
        )

        # If already terminal (another tab accepted, or watchdog fired) — safe no-op.
        if _session.state not in NON_TERMINAL_STATES:
            logger.debug(
                "upsell.decline: session %s already terminal (%s) — no-op",
                _session.pk, _session.state,
            )
            _result = {"status": "declined", "next_step": None}
        else:
            step = _session.current_step
            order = _session.order
            now = timezone.now()

            if step is None:
                # No step set — finalize as DISMISSED (defensive path).
                CampaignSession.objects.for_store(store).filter(
                    pk=_session.pk,
                    state__in=list(NON_TERMINAL_STATES),
                ).update(state=CampaignSessionState.DISMISSED, converted_at=now)
                logger.warning(
                    "upsell.decline: session %s has no current_step — DISMISSED",
                    _session.pk,
                )
                _result = {"status": "declined", "next_step": None}
            elif step.decline_next_step_id is not None:
                # Non-terminal decline: check capture window before advancing.
                if order and order.capture_window_expires_at and now >= order.capture_window_expires_at:
                    raise UpsellUnavailableError(
                        f"Capture window expired for order {order.pk} — "
                        "cannot advance to next decline step."
                    )
                next_step = step.decline_next_step
                # Session stays INTERACTION, current_step moves to decline branch.
                CampaignSession.objects.for_store(store).filter(
                    pk=_session.pk,
                    state__in=list(NON_TERMINAL_STATES),
                ).update(
                    state=CampaignSessionState.INTERACTION,
                    current_step=next_step,
                )
                # Issue new single-use token inside the transaction (FR-M4).
                next_step_payload = _serve_step(_session, next_step, order)
                _step_pk = step.pk
                _next_step_pk = next_step.pk
                logger.info(
                    "upsell.decline: session %s advanced to decline branch step %s",
                    _session.pk, next_step.pk,
                )
                _result = {"status": "declined", "next_step": next_step_payload}
            else:
                # Terminal decline: finalize based on prior UPSELL captures.
                _step_pk = step.pk
                if order:
                    captured_upsell_count = (
                        OrderCharge.objects.for_store(store)
                        .filter(
                            order=order,
                            charge_type=ChargeType.UPSELL,
                            status=ChargeStatus.CAPTURED,
                        )
                        .count()
                    )
                else:
                    captured_upsell_count = 0

                if captured_upsell_count >= 1:
                    final_state = CampaignSessionState.CONVERTED
                else:
                    final_state = CampaignSessionState.DISMISSED

                CampaignSession.objects.for_store(store).filter(
                    pk=_session.pk,
                    state__in=list(NON_TERMINAL_STATES),
                ).update(state=final_state, converted_at=now)

                logger.info(
                    "upsell.decline: session %s → %s (captured_upsell_count=%d)",
                    _session.pk, final_state, captured_upsell_count,
                )
                _result = {"status": "declined", "next_step": None}
    # Transaction commits here.

    # Post-transaction: audit events (non-fatal).
    if _step_pk and _session is not None:
        _append_session_event(
            store, _session,
            {"event": "step_declined", "step_id": _step_pk, "ts": timezone.now().isoformat()},
        )
    if _next_step_pk and _session is not None:
        _append_session_event(
            store, _session,
            {"event": "step_served", "step_id": _next_step_pk, "ts": timezone.now().isoformat()},
        )

    return _result


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _get_upsell_amount_cents(step) -> int:
    """
    Resolve the upsell charge amount in smallest currency unit from the step config.

    Reads 'upsell_amount_cents' from offer_config_json.  Raises UpsellUnavailableError
    if the key is absent or zero — the admin must configure this before activating the
    campaign (ADR-011 / T028 scope: per-offer-type amount resolution is T029).

    ASSUMPTION: offer_config_json['upsell_amount_cents'] holds the integer amount.
    Full per-offer-type calculation (product lookup, variant pricing) is out of T028 scope.
    """
    amount = step.offer_config_json.get("upsell_amount_cents", 0)
    if not isinstance(amount, int) or amount <= 0:
        raise UpsellUnavailableError(
            f"Step {step.pk} offer_config_json has no valid 'upsell_amount_cents'. "
            "Configure an integer > 0 before activating the campaign."
        )
    return amount


def _maybe_issue_coupon(store, session, step):
    """
    Issue a DiscountCode + CampaignIssuedCode for storewide_discount steps.

    Only `storewide_discount` offer steps issue a next-order coupon code on accept
    (ADR-009 §3). Other offer types do not issue codes and are silently skipped.

    Returns the coupon code string when a code is issued (or already exists on retry),
    or None when the step is not a storewide_discount or is misconfigured.

    Idempotent: on retry, the UniqueConstraint(campaign_session, step) fires and
    the existing code is looked up and returned (ADR-009 §3 / ADR-011 Q2).

    Must be called inside a transaction.atomic() context.
    """
    from campaigns.models import StepOfferType

    if step.offer_type != StepOfferType.STOREWIDE_DISCOUNT:
        return None

    cfg = step.offer_config_json
    # storewide_discount schema (ADR-009 §3):
    #   discount_pct  — optional float (0 < x <= 100)
    #   discount_fixed — optional float > 0
    #   expires_in_days — int >= 1 (default 30)
    discount_pct = cfg.get("discount_pct")
    discount_fixed = cfg.get("discount_fixed")
    if not discount_pct and not discount_fixed:
        return None  # misconfigured step — skip silently (not a crash)

    from campaigns.models import CampaignIssuedCode
    from discounts.models import DiscountCode, DiscountType, ProvenanceType, ValueType

    expires_in_days = cfg.get("expires_in_days", 30)
    if discount_pct:
        value_type = ValueType.PERCENTAGE
        value = Decimal(str(discount_pct))
    else:
        value_type = ValueType.FIXED_AMOUNT
        value = Decimal(str(discount_fixed))

    code = secrets.token_hex(10).upper()  # 20 uppercase hex characters, unpredictable

    try:
        with transaction.atomic():  # savepoint: isolate IntegrityError from outer tx
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
                step=step,
                discount_code=discount_code,
            )
        return code
    except IntegrityError:
        # Retry path: UniqueConstraint(campaign_session, step) fired — code exists.
        logger.debug(
            "upsell.coupon: idempotency hit for session %s step %s — reusing existing code",
            session.pk, step.pk,
        )
        existing = (
            CampaignIssuedCode.objects.for_store(store)
            .filter(campaign_session=session, step=step)
            .select_related("discount_code")
            .first()
        )
        if existing is not None:
            return existing.discount_code.code
        return None
