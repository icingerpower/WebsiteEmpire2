"""
Purchase-pixel idempotency service (ADR-022 D6, consent-gated per ADR-025 D0/D3).
"""

import logging

from orders.models import FiredPixel, PaymentStatus

from pixels.models import Pixel
from pixels.registry import get_provider

logger = logging.getLogger(__name__)


def claim_purchase_pixels(store, order, allowed_categories: frozenset) -> set:
    """
    Claim the Purchase pixel event for every installed+active provider on
    this order WHOSE consent_category is in allowed_categories, exactly once
    per (order, provider) (ADR-022 D6, consent-gated at the claim site per
    ADR-025 D0).

    allowed_categories is REQUIRED (no default) — ADR-025 D0's verification
    found that gating only the render, not the claim, would burn the one-shot
    FiredPixel claim on an unconsented thank-you view: if the shopper later
    consents and reloads, get_or_create() would return created=False and the
    purchase pixel would silently never fire (the exact §XV-1 invisible-
    failure class this correction exists to avoid). Callers derive it from
    consent.state.get_consent(request) — see storefront/views_checkout.py's
    order_thank_you. Passing the full frozenset({"analytics", "marketing"})
    is the one-line rollback (ADR-025 Rollback strategy) that restores
    pre-025 "claim everything installed" behaviour.

    PAID gate (defense in depth, security audit MEDIUM-1): returns an empty
    set immediately if the order is not PAID, even though order_thank_you
    also gates on PAID before calling this function — the `processing`
    redirect path renders thank-you on PENDING orders, and a PENDING render
    must never consume a claim (it would be gone by the time the order later
    settles to PAID).

    For each installed+active Pixel row whose provider's consent_category is
    allowed, FiredPixel.get_or_create(order=order, pixel_type=<provider key>,
    event='purchase') is the atomic claim — the unique_together (order,
    pixel_type, event) constraint makes concurrent thank-you reloads (or two
    racing requests) safe: at most one caller ever observes created=True per
    (order, provider). A row whose category is NOT allowed is skipped
    entirely — its claim stays unconsumed until a future render observes
    consent for it (late-but-real firing, ADR-025 D3 §2).

    Returns the set of provider keys for which THIS call created the claim.
    An empty set means no active+allowed providers claimed anything this
    call (no active providers, no allowed categories, or every allowed
    provider already has a purchase row for this order — reload, back
    button, leaked link revisit, AC-110). PixelsSlotProvider renders a
    provider's purchase snippet only when its key is in this set (and its own
    consent-gated render check also passes independently).
    """
    if order.payment_status != PaymentStatus.PAID:
        return set()

    claimed = set()
    for row in Pixel.objects.for_store(store).filter(is_active=True):
        provider = get_provider(row.provider)
        if provider is None:
            # Row exists but the code plugin was removed — never claim for a
            # provider we cannot classify (§XV-1); PixelsSlotProvider.
            # validate_settings() surfaces this in the launch checklist.
            logger.error(
                "Pixel row store=%s provider=%r has no matching registry entry; "
                "skipping purchase claim for this row.",
                store.pk, row.provider,
            )
            continue
        if provider.consent_category not in allowed_categories:
            continue
        _fired, created = FiredPixel.objects.for_store(store).get_or_create(
            store=store,
            order=order,
            pixel_type=row.provider,
            event="purchase",
        )
        if created:
            claimed.add(row.provider)
    return claimed
