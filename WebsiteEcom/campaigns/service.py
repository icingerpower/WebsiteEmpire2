"""
Campaign eligibility service (ADR-007, ADR-009, ADR-010).

evaluate_eligibility / resolve_campaign_for_store share the single resolution
function logic (§XV-4 / ADR-009 §2): store-owned campaigns shadow platform
campaigns of the same campaign_type. All callers must go through these functions
— never inline the precedence logic in views or tasks.

Precedence rule (ADR-009 §2):
  When both a store-owned and a platform campaign of the same campaign_type are
  active and eligible for the same store, the store-owned campaign(s) shadow
  every platform campaign of that type. Platform campaigns are returned only when
  no store-owned campaign of that type is eligible.

Session creation:
  - create_session(): order-anchored (funnel-type campaigns).
  - create_abandonment_session(): cart-anchored (abandoned_checkout only, ADR-010 Q1).
    One creation path per anchor (§XV-4) — never use create_session() for carts.

Suppression:
  - suppress_abandoned_checkout(order): transitions matching non-terminal
    abandoned-checkout sessions to CONVERTED and cancels their SCHEDULED sends
    (ADR-010 Q5). Idempotent — terminal sessions are left unchanged.

Thank-you page arming:
  - serve_thank_you_step(order, store): called at thank-you render time.
    Handles NOT_STARTED, IMPRESSION, and INTERACTION sessions.  Transitions
    non-INTERACTION sessions idempotently, then issues a fresh single-use
    upsell-act token.  Degrades gracefully (returns None) on any error.
    Returns dict with keys: session, step, token (raw — never log).
    ADR-011 Addendum 2026-07-06.
  - serve_next_order_offer(order, store): called at thank-you render time
    (TH-006 / TICKET-027 item 3).  Issues a single-use storewide "next order"
    discount code when an active STOREWIDE_DISCOUNT campaign exists.  Unlike
    ONE_CLICK_FUNNEL, no CampaignSession is seeded at checkout time for this
    campaign type — the session is created lazily on first render.  Degrades
    gracefully (returns None) on any error.  Returns dict with keys: code,
    discount_pct, expires_at.

Phase 2 v1: targeting-rules filtering is a pass-through (returns all active
campaigns of the type for the store). Full targeting-rules evaluation (product IDs,
collection IDs, min order total, country, frequency caps, exclusion rules) will be
added incrementally as the rule vocabulary is defined (ADR-007 §9).
"""

import logging
import secrets
from datetime import timedelta
from decimal import Decimal

from django.db import IntegrityError, transaction
from django.db.models import Case, IntegerField, Value, When
from django.utils import timezone

logger = logging.getLogger("campaigns.service")


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _get_eligible_campaigns(store, campaign_type: str) -> list:
    """
    Return eligible campaigns for (store, campaign_type) with precedence applied.

    Internal helper used by evaluate_eligibility and resolve_campaign_for_store to
    keep the single resolution function (§XV-4) — the precedence logic lives ONLY here.

    Returns a list (never None). Empty list means no eligible campaign found.
    Sorted by (precedence, created_at) — store-owned first, then platform.
    """
    from campaigns.models import Campaign, CampaignType

    qs = (
        Campaign.objects.for_store(store)
        .filter(campaign_type=campaign_type, is_active=True)
        .annotate(
            precedence=Case(
                When(owner_scope=Campaign.OWNER_SCOPE_STORE, then=Value(0)),
                default=Value(1),
                output_field=IntegerField(),
            )
        )
        .order_by("precedence", "created_at")
    )

    # Funnel-type campaigns require an entry_step. When a step is deleted with
    # SET_NULL, entry_step becomes NULL and the campaign silently renders nothing
    # — an invisible failure (§XV-1 / ADR-009 §1). Exclude such campaigns so
    # callers never receive a funnel with no entry point.
    # abandoned_checkout campaigns are exempt: they have no entry_step by design.
    if campaign_type != CampaignType.ABANDONED_CHECKOUT:
        qs = qs.exclude(entry_step__isnull=True)

    candidates = list(qs)

    if not candidates:
        return []

    # Phase 2 v1: no targeting-rules filtering yet — all candidates are eligible.
    eligible = candidates

    # Apply precedence: if any store-owned campaign is eligible, shadow all platform
    # campaigns of this type (ADR-009 §2).
    has_store_campaign = any(
        c.owner_scope == Campaign.OWNER_SCOPE_STORE for c in eligible
    )
    if has_store_campaign:
        eligible = [c for c in eligible if c.owner_scope == Campaign.OWNER_SCOPE_STORE]

    return eligible


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def evaluate_eligibility(order, campaign_type: str) -> list:
    """
    Return active campaigns of this type eligible for this order, with precedence.

    Precedence (ADR-009 §2): store-owned shadows platform for the same store.
    Returns a list (never None). Empty list means no eligible campaign found.

    Phase 2 v1: targeting-rules filtering is a pass-through.
    """
    return _get_eligible_campaigns(order.store, campaign_type)


def resolve_campaign_for_store(store, campaign_type: str):
    """
    Return the first eligible campaign for (store, campaign_type), or None.

    Used by the abandoned-checkout scan task to determine which campaign should
    create sessions for a store's abandoned carts (§XV-4 — single resolution
    function). Takes the store directly (no order needed) while reusing the
    same precedence logic as evaluate_eligibility.

    Returns the Campaign instance with the highest precedence (store-owned before
    platform, then by created_at), or None if no active campaign exists.
    """
    candidates = _get_eligible_campaigns(store, campaign_type)
    return candidates[0] if candidates else None


def create_session(store, order, campaign):
    """
    Create a CampaignSession for (order, campaign), writing the denormalized
    campaign_type at creation time (ADR-009 §6).

    Returns the created CampaignSession instance. Raises IntegrityError if a
    session for this (order, campaign) already exists — callers must guard against
    duplicate creation (e.g. via get_or_create at the call site when idempotency
    is needed).
    """
    if order.store_id != store.pk or campaign.store_id != store.pk:
        raise ValueError(
            f"create_session: store mismatch — store={store.pk}, "
            f"order.store={order.store_id}, campaign.store={campaign.store_id}"
        )

    from campaigns.models import CampaignSession

    session = CampaignSession(
        store=store,
        order=order,
        campaign=campaign,
        campaign_type=campaign.campaign_type,
    )
    session.save()
    return session


def create_abandonment_session(store, cart, campaign):
    """
    Create a cart-anchored CampaignSession for an abandoned checkout (ADR-010 Q1).

    Analogous to create_session() but anchored on the cart, not an order. Sets:
    - cart = cart (anchor FK)
    - customer_email = cart.customer_email (denormalized for cross-session suppression)
    - campaign_type = campaign.campaign_type (denormalized; drives uniqueness constraint)
    - state = NOT_STARTED
    - abandoned_at = cart.updated_at (frozen reference time for delay calculations)

    Returns the created CampaignSession.

    Raises ValueError on store mismatch.
    Raises IntegrityError if a session for (cart, campaign_type) already exists
    (enforced by the conditional UniqueConstraint). The caller (beat scan) must
    treat IntegrityError as "another beat run already created this session — skip."
    """
    if cart.store_id != store.pk or campaign.store_id != store.pk:
        raise ValueError(
            f"create_abandonment_session: store mismatch — store={store.pk}, "
            f"cart.store={cart.store_id}, campaign.store={campaign.store_id}"
        )

    from campaigns.models import CampaignSession, CampaignSessionState

    session = CampaignSession(
        store=store,
        cart=cart,
        customer_email=cart.customer_email,
        campaign=campaign,
        campaign_type=campaign.campaign_type,
        state=CampaignSessionState.NOT_STARTED,
        abandoned_at=cart.updated_at,
    )
    session.save()
    return session


def suppress_abandoned_checkout(order) -> None:
    """
    Suppress abandoned-checkout sessions when an order is paid (ADR-010 Q5).

    Called from the payment confirmation path immediately after payment_status → PAID
    commits. Must NOT be called from a post_save signal (ADR-010 Q5 rationale).

    Matching: sessions in order.store with campaign_type='abandoned_checkout' and
    customer_email = order.customer_email (cross-session, AC-182). There is no
    order → cart FK, so cart-based matching is not performed.

    For each matching non-terminal session:
    1. Transitions to CONVERTED (converted_at=now).
    2. Sets recovery_revenue = order.total when attributed (INTERACTION reached, or
       order.discount_code is linked to this session via CampaignIssuedCode).
       Sets recovery_revenue = 0 otherwise.
    3. Cancels all SCHEDULED AbandonedCheckoutEmailSend rows (status=CANCELLED).

    Idempotent: terminal sessions are filtered out by state__in=NON_TERMINAL_STATES,
    so calling this function twice on the same order is a safe no-op.
    """
    from campaigns.models import (
        AbandonedCheckoutEmailSend,
        AbandonedCheckoutEmailSendStatus,
        CampaignIssuedCode,
        CampaignSession,
        CampaignSessionState,
        NON_TERMINAL_STATES,
    )

    now = timezone.now()
    store = order.store

    non_terminal_list = list(NON_TERMINAL_STATES)

    sessions = list(
        CampaignSession.objects.for_store(store).filter(
            campaign_type="abandoned_checkout",
            state__in=non_terminal_list,
            customer_email=order.customer_email,
        )
    )

    for session in sessions:
        # Determine attribution: INTERACTION reached, or order used an issued coupon
        # linked to this session (ADR-010 Q1 attribution rule).
        has_interaction = session.state == CampaignSessionState.INTERACTION
        has_attributed_code = False
        if order.discount_code_id is not None:
            has_attributed_code = (
                CampaignIssuedCode.objects.for_store(store)
                .filter(
                    campaign_session=session,
                    discount_code_id=order.discount_code_id,
                )
                .exists()
            )

        recovery_revenue = (
            order.total if (has_interaction or has_attributed_code) else Decimal("0")
        )

        # Transition to CONVERTED (only if still non-terminal — idempotency guard).
        updated = CampaignSession.objects.for_store(store).filter(
            pk=session.pk,
            state__in=non_terminal_list,
        ).update(
            state=CampaignSessionState.CONVERTED,
            converted_at=now,
            recovery_revenue=recovery_revenue,
        )

        if updated:
            # Cancel all SCHEDULED sends for this session (AC-182).
            AbandonedCheckoutEmailSend.objects.for_store(store).filter(
                session=session,
                status=AbandonedCheckoutEmailSendStatus.SCHEDULED,
            ).update(
                status=AbandonedCheckoutEmailSendStatus.CANCELLED,
                cancelled_at=now,
            )


def serve_thank_you_step(order, store):
    """
    Called at thank-you page render time.  Transitions the funnel session to
    INTERACTION (idempotent) and issues a fresh single-use token for the
    current upsell step.

    Returns None if: no active session, payment not PAID, window expired,
    no current step, or any unexpected error (degrade gracefully — the
    thank-you page must never 500 because of the funnel).

    Returns dict with keys: session, step, token (raw — pass to template,
    never log).

    Design notes (ADR-011 Addendum 2026-07-06):
    - Handles NOT_STARTED, IMPRESSION, and INTERACTION sessions.
    - Wraps all DB work in a single transaction.atomic() so select_for_update()
      is always inside an atomic block and the state transition + token issuance
      commit atomically (FR-M4 / §XV-3).
    - Uses session.expires_at (denormalized Order.capture_window_expires_at) to
      evaluate window expiry without a JOIN.
    - Multiple live single-use tokens per session may coexist; issue_token does
      NOT supersede prior tokens.  Whoever POSTs first wins.
    - Raw token is NEVER logged — only passed back in the returned dict.
    """
    try:
        with transaction.atomic():
            from campaigns.models import (
                CampaignSession,
                CampaignSessionState,
                CampaignType,
                TokenPurpose,
            )
            from campaigns.tokens import issue_token
            from orders.models import PaymentStatus

            # Find active ONE_CLICK_FUNNEL session for this order.
            try:
                session = (
                    CampaignSession.objects
                    .for_store(store)
                    .select_for_update()
                    .get(
                        order=order,
                        campaign_type=CampaignType.ONE_CLICK_FUNNEL,
                        state__in=[
                            CampaignSessionState.NOT_STARTED,
                            CampaignSessionState.IMPRESSION,
                            CampaignSessionState.INTERACTION,
                        ],
                    )
                )
            except CampaignSession.DoesNotExist:
                return None

            # Guards — exit early rather than serving a stale/invalid state.
            if not session.current_step:
                return None
            if not session.expires_at:
                return None
            if timezone.now() >= session.expires_at:
                return None
            # Only serve after payment is confirmed.
            if order.payment_status != PaymentStatus.PAID:
                return None

            # Cache step so we hold the FK object through the transition.
            step = session.current_step

            # Idempotent NOT_STARTED/IMPRESSION → INTERACTION transition.
            # Conditional UPDATE makes this a no-op when already INTERACTION
            # (0 rows updated is not an error — we still issue a fresh token).
            if session.state in (
                CampaignSessionState.NOT_STARTED,
                CampaignSessionState.IMPRESSION,
            ):
                now = timezone.now()
                CampaignSession.objects.for_store(store).filter(
                    pk=session.pk,
                    state__in=[
                        CampaignSessionState.NOT_STARTED,
                        CampaignSessionState.IMPRESSION,
                    ],
                ).update(
                    state=CampaignSessionState.INTERACTION,
                    started_at=now,
                )
                session.refresh_from_db()

            # Issue a fresh upsell-act token valid for the remaining window.
            # Must run inside the transaction (FR-M4 / §XV-3).
            if order.capture_window_expires_at:
                remaining_secs = max(
                    1,
                    int((order.capture_window_expires_at - timezone.now()).total_seconds()),
                )
            else:
                remaining_secs = 60  # Fallback — windowless order

            raw = issue_token(session, TokenPurpose.UPSELL_ACT, ttl_seconds=remaining_secs)

        # Return outside the atomic block; raw token is never logged here.
        return {
            "session": session,
            "step": step,
            "token": raw,
        }

    except Exception:
        logger.warning(
            "serve_thank_you_step failed — degrading gracefully",
            exc_info=True,
        )
        return None


def serve_next_order_offer(order, store):
    """
    Called at thank-you page render time (TH-006 / TICKET-027 item 3).

    Issues a single-use storewide "next order" discount code when an active
    STOREWIDE_DISCOUNT campaign exists for the store. Mirrors serve_thank_you_step's
    graceful-degradation contract: any error, or the absence of an eligible
    campaign, returns None rather than raising — the thank-you page must never
    500 because of this feature.

    Unlike ONE_CLICK_FUNNEL, no CampaignSession is created for STOREWIDE_DISCOUNT
    campaigns at checkout time (cart/checkout.py only seeds funnel-campaign
    sessions) — this function creates the session lazily, on first thank-you
    render, via create_session() (single resolution / single creation function,
    §XV-4).

    Idempotent: reloading the thank-you page returns the SAME code. Both the
    CampaignSession and the CampaignIssuedCode are get-or-created using the T021
    idempotency pattern (campaigns/tasks.py::_issue_coupon — try create inside a
    savepoint, fall back to a read on IntegrityError) so a retried render never
    issues a second DiscountCode for the same order.

    Only serves after payment is confirmed (same PAID guard as serve_thank_you_step).

    Returns dict with keys: code, discount_pct, expires_at. Returns None when:
    no active STOREWIDE_DISCOUNT campaign, no entry_step (or entry_step is not a
    STOREWIDE_DISCOUNT offer), payment not PAID, or any unexpected error.
    """
    try:
        from campaigns.models import (
            CampaignIssuedCode,
            CampaignSession,
            CampaignType,
            StepOfferType,
        )
        from discounts.models import DiscountCode, DiscountType, ProvenanceType, ValueType
        from orders.models import PaymentStatus

        # Only serve after payment is confirmed.
        if order.payment_status != PaymentStatus.PAID:
            return None

        campaign = resolve_campaign_for_store(store, CampaignType.STOREWIDE_DISCOUNT)
        if campaign is None:
            return None

        # entry_step carries the offer config (discount_pct, expires_in_days).
        # _get_eligible_campaigns already excludes campaigns with no entry_step,
        # but the step's offer_type is not guaranteed to match this campaign type
        # (defensive — never trust config shape blindly).
        step = campaign.entry_step
        if step is None or step.offer_type != StepOfferType.STOREWIDE_DISCOUNT:
            return None

        # Get-or-create the CampaignSession for (order, campaign). IntegrityError
        # means a concurrent/retried render already created it — the
        # (order, campaign_type) UniqueConstraint is the ON-CONFLICT target.
        try:
            with transaction.atomic():  # savepoint
                session = create_session(store, order, campaign)
        except IntegrityError:
            session = (
                CampaignSession.objects.for_store(store)
                .get(order=order, campaign_type=CampaignType.STOREWIDE_DISCOUNT)
            )

        cfg = step.offer_config_json or {}
        discount_pct = cfg.get("discount_pct", 10)
        expires_in_days = cfg.get("expires_in_days", 30)

        # Get-or-create the issued code (T021 idempotency pattern — mirrors
        # campaigns.tasks._issue_coupon). uniq_issued_code_per_session_step is the
        # ON-CONFLICT target; IntegrityError rolls back this whole savepoint
        # (including the DiscountCode insert above it), so re-reading is safe —
        # no orphaned DiscountCode is left behind.
        try:
            with transaction.atomic():  # savepoint
                code = secrets.token_hex(10).upper()  # 20 characters
                discount_code = DiscountCode.objects.create(
                    store=store,
                    code=code,
                    discount_type=DiscountType.COUPON,
                    value_type=ValueType.PERCENTAGE,
                    value=Decimal(str(discount_pct)),
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
        except IntegrityError:
            existing = (
                CampaignIssuedCode.objects.for_store(store)
                .select_related("discount_code")
                .get(campaign_session=session, step=step)
            )
            discount_code = existing.discount_code

        return {
            "code": discount_code.code,
            "discount_pct": discount_pct,
            "expires_at": discount_code.ends_at,
        }

    except Exception:
        logger.warning(
            "serve_next_order_offer failed — degrading gracefully",
            exc_info=True,
        )
        return None
