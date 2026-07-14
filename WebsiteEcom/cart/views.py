"""
Cart app views.

resume_abandoned_checkout (TICKET-021):
  Validates a signed resume token from an abandoned-checkout email, re-prices
  the cart at current catalog prices, and renders a checkout pre-fill page.

  Per ADR-010 Q3 and Safety H1 (human-approved 2026-07-05):

  GET /checkout/resume/<token>/
    - Validates the signed URL token.
    - Re-prices cart items from the current catalog (AC-072).
    - Transitions the campaign session NOT_STARTED → IMPRESSION (never beyond).
    - Renders resume_checkout.html — the page includes a POST form for the user
      to click "Proceed to Checkout".

  POST /checkout/resume/<token>/
    - Validates the HMAC token from the POST body (request.POST['token']).
    - Transitions the campaign session IMPRESSION → INTERACTION (idempotent).
    - Redirects to /checkout/ (the actual checkout page).

  Separating the INTERACTION transition onto a POST prevents email link scanners
  (automated GET requests) from inflating INTERACTION counts and
  recovery_revenue attribution (Safety H1).

  Validation failures (all render the same page, no oracle, AC-073):
  - BadSignature / SignatureExpired / KeyError / TypeError (malformed or expired token)
  - store in token != request.store (cross-store replay attempt)
  - Cart or session does not exist
  - Cart.expires_at is None or in the past (cart expired independently of token)

  On GET success:
  - Cart items are re-priced from the current catalog.
  - Out-of-stock items are flagged but NOT removed; checkout is blocked.
  - Applied coupon (if any) is re-validated; shown as invalid if expired.
  - Session transitions NOT_STARTED → IMPRESSION (or stays at current state).
  - CONVERTED session → redirect to storefront root.

  Tokens must never appear in logs, analytics events, or pixel payloads (ADR-007 §5).

Cart mutation views (TICKET-CART-MUTATIONS):
  POST /cart/add/      — add_to_cart
  POST /cart/update/   — update_cart_item
  POST /cart/remove/   — remove_cart_item

  All three are POST-only (require_POST → 405 on GET), CSRF-protected (Django
  default), and store-scoped (variant / cart lookups via for_store).

  The update and remove views accept variant_id (matching the cart.html template)
  rather than item_id. Cart items are located by (session_key + store + variant_id).

  unit_price is always read from ProductVariant.price — never from form data.

  Open-redirect prevention: the `next` parameter is only followed when
  django.utils.http.url_has_allowed_host_and_scheme confirms it targets this
  request's own host/scheme (rejects "//evil.com", "\\evil.com", and
  absolute external URLs); otherwise the redirect falls back to /cart/.
"""

import logging
from datetime import timedelta

from django.http import Http404, HttpResponseBadRequest
from django.shortcuts import redirect, render
from django.utils import timezone
from django.utils.http import url_has_allowed_host_and_scheme
from django.views.decorators.http import require_POST

logger = logging.getLogger(__name__)

# Salt must match the value used in the send task when minting the token.
_RESUME_TOKEN_SALT = "abandoned_checkout_resume"
_TOKEN_MAX_AGE = timedelta(days=30)

# Template paths
_TEMPLATE_EXPIRED = "cart/resume_link_expired.html"
_TEMPLATE_CHECKOUT = "cart/resume_checkout.html"


def _noindex(response):
    """
    Add security headers to resume URL responses.

    X-Robots-Tag: prevents resume URLs from being indexed by crawlers.
    Referrer-Policy: prevents the HMAC token from leaking via Referer headers
    to any external resources linked from the resume page (Safety M3).
    """
    response["X-Robots-Tag"] = "noindex, nofollow"
    response["Referrer-Policy"] = "no-referrer"
    return response


def _expired_response(request):
    """Return the "link expired" page — identical for every failure mode (AC-073)."""
    return _noindex(render(request, _TEMPLATE_EXPIRED, status=410))


def _verify_token(token):
    """
    Verify a signed resume token and return its payload dict.

    Returns the payload dict on success, or None on any verification failure.
    Callers must treat None as an expired/invalid token without distinguishing
    the failure reason (AC-073 no-oracle requirement).

    Never logs the token value (ADR-007 §5).
    """
    from django.core import signing

    try:
        return signing.loads(
            token,
            salt=_RESUME_TOKEN_SALT,
            max_age=_TOKEN_MAX_AGE.total_seconds(),
        )
    except (signing.BadSignature, signing.SignatureExpired, KeyError, TypeError):
        # All failure modes produce the same response — no oracle (AC-073).
        # Do NOT log the token value (ADR-007 §5).
        return None


def resume_abandoned_checkout(request, token):
    """
    Resume an abandoned checkout from a signed email link (ADR-010 Q3).

    Dispatches to the GET or POST handler based on request.method.
    Both handlers share token verification via _verify_token().
    """
    if request.method == "POST":
        return _resume_post(request)
    return _resume_get(request, token)


def _resume_get(request, token):
    """
    GET handler: validate token, re-price cart, render resume page.

    Transitions session NOT_STARTED → IMPRESSION only.
    IMPRESSION → INTERACTION happens on POST (user clicks "Proceed to Checkout").
    CONVERTED session → redirect to storefront.
    """
    from campaigns.models import CampaignSession, CampaignSessionState
    from cart.models import Cart, CartItem
    from catalog.models import InventoryMode

    # Step 1: Verify token from URL path.
    payload = _verify_token(token)
    if payload is None:
        return _expired_response(request)

    try:
        cart_id = payload["cart"]
        session_id = payload["session"]
        store_id = payload["store"]
    except (KeyError, TypeError):
        return _expired_response(request)

    # Step 2: Validate store match (defense in depth against cross-store replay).
    current_store = getattr(request, "store", None)
    if current_store is None or current_store.pk != store_id:
        return _expired_response(request)

    # Step 3: Load cart and session.
    try:
        cart = Cart.objects.for_store(current_store).get(pk=cart_id)
    except Cart.DoesNotExist:
        return _expired_response(request)

    try:
        session = CampaignSession.objects.for_store(current_store).get(pk=session_id)
    except CampaignSession.DoesNotExist:
        return _expired_response(request)

    # Step 4: Cart expiry check (cart lifetime may be shorter than token lifetime).
    # Guard against None expires_at to avoid TypeError on comparison (Safety L2).
    if cart.expires_at is None or cart.expires_at < timezone.now():
        return _expired_response(request)

    # Step 5: CONVERTED session → redirect to storefront (AC-072 edge).
    if session.state == CampaignSessionState.CONVERTED:
        return redirect("/")

    # Step 6: Re-price cart items from the current catalog (AC-072).
    items_context = []
    has_stock_warning = False

    cart_items = (
        CartItem.objects.for_store(current_store)
        .filter(cart=cart)
        .select_related("variant")
    )

    for item in cart_items:
        variant = item.variant
        current_price = variant.price

        # Update the snapshot price to the current catalog price (AC-072).
        if item.unit_price != current_price:
            item.unit_price = current_price
            item.save(update_fields=["unit_price"])

        # Check stock availability.
        in_stock = True
        if variant.inventory_mode == InventoryMode.FIXED_QTY:
            qty = variant.quantity or 0
            in_stock = qty >= item.quantity
        elif variant.inventory_mode == InventoryMode.PRESALE:
            in_stock = True  # Presale items are available for ordering.
        # NO_TRACKING: always in stock.

        if not in_stock:
            has_stock_warning = True

        items_context.append(
            {
                "item": item,
                "current_price": current_price,
                "in_stock": in_stock,
            }
        )

    # Step 7: Re-validate the cart's applied coupon (advisory — full validation at checkout).
    coupon_valid = False
    if cart.discount_code_id:
        coupon_valid = cart.discount_code.is_valid_now()

    # Step 8: Transition session NOT_STARTED → IMPRESSION (idempotent).
    # IMPRESSION → INTERACTION happens only on POST (Safety H1, human-approved 2026-07-05).
    if session.state == CampaignSessionState.NOT_STARTED:
        CampaignSession.objects.for_store(current_store).filter(
            pk=session.pk,
            state=CampaignSessionState.NOT_STARTED,
        ).update(state=CampaignSessionState.IMPRESSION)
        session.state = CampaignSessionState.IMPRESSION

    # Step 9: Render the pre-filled checkout page.
    # Pass resume_token so the POST form can include it in the hidden field.
    context = {
        "cart": cart,
        "items": items_context,
        "has_stock_warning": has_stock_warning,
        "coupon_valid": coupon_valid,
        "discount_code": cart.discount_code if coupon_valid else None,
        "session": session,
        "store": current_store,
        "resume_token": token,
    }
    return _noindex(render(request, _TEMPLATE_CHECKOUT, context))


def _resume_post(request):
    """
    POST handler: validate token from POST body, advance IMPRESSION → INTERACTION,
    redirect to /checkout/.

    Reading the token from POST data (rather than the URL path) allows the form
    to be submitted without the HMAC token appearing in the Referer header of the
    subsequent /checkout/ request.

    CSRF is enforced by Django's CsrfViewMiddleware (no @csrf_exempt here).
    """
    from campaigns.models import CampaignSession, CampaignSessionState

    # Token comes from the hidden form field, not from the URL parameter.
    post_token = request.POST.get("token", "")

    payload = _verify_token(post_token)
    if payload is None:
        return _expired_response(request)

    try:
        session_id = payload["session"]
        store_id = payload["store"]
    except (KeyError, TypeError):
        return _expired_response(request)

    current_store = getattr(request, "store", None)
    if current_store is None or current_store.pk != store_id:
        return _expired_response(request)

    try:
        session = CampaignSession.objects.for_store(current_store).get(pk=session_id)
    except CampaignSession.DoesNotExist:
        return _expired_response(request)

    # CONVERTED session → redirect to storefront (consistent with GET handler).
    if session.state == CampaignSessionState.CONVERTED:
        return redirect("/")

    # Advance IMPRESSION → INTERACTION (idempotent: filter on current state).
    now = timezone.now()
    if session.state == CampaignSessionState.IMPRESSION:
        CampaignSession.objects.for_store(current_store).filter(
            pk=session.pk,
            state=CampaignSessionState.IMPRESSION,
        ).update(state=CampaignSessionState.INTERACTION, started_at=now)
        session.state = CampaignSessionState.INTERACTION

    return redirect("/checkout/")


# ---------------------------------------------------------------------------
# Cart mutation helpers
# ---------------------------------------------------------------------------

_CART_DEFAULT_REDIRECT = "/cart/"


def _safe_redirect(request, next_url: str) -> str:
    """
    Return next_url if it is a safe same-site redirect target; otherwise /cart/.

    Delegates to Django's own url_has_allowed_host_and_scheme (the same
    guard storefront.views_checkout._safe_next_url uses) rather than a
    hand-rolled startswith("/") check — the hand-rolled version accepted
    backslash-prefixed URLs like "/\\evil.com", which browsers normalize to
    "//evil.com" (a protocol-relative external redirect). PY-020 hardening.
    """
    if next_url and url_has_allowed_host_and_scheme(
        next_url, allowed_hosts={request.get_host()}
    ):
        return next_url
    return _CART_DEFAULT_REDIRECT


# ---------------------------------------------------------------------------
# Cart mutation views
# ---------------------------------------------------------------------------


def _validate_page_version_id(request, product_id: int) -> int | None:
    """
    Validate the ATC form's page_version_id field (TICKET-042 / ADR-028 §6,
    Safety gate requirement): must be an integer identifying a
    ProductPageVersion belonging to THIS store and THIS product, else stored
    as NULL — never trusted blindly into a stamp or any SQL/FK.

    Public input (a hidden form field a browser could tamper with); a
    mismatched or malformed value is silently ignored (None), matching the
    codebase's existing "silent cap / degrade" convention for advisory form
    fields (e.g. add_to_cart's own quantity cap) rather than a 400 that would
    block an otherwise-valid add-to-cart.
    """
    raw = request.POST.get("page_version_id", "")
    if not raw:
        return None
    try:
        candidate = int(raw)
    except (ValueError, TypeError):
        return None

    from catalog.models import ProductPageVersion
    if ProductPageVersion.objects.for_store(request.store).filter(
        pk=candidate, product_id=product_id,
    ).exists():
        return candidate
    return None


@require_POST
def add_to_cart(request):
    """
    POST /cart/add/

    Form fields:
      variant_id       — ProductVariant PK (required)
      quantity         — positive integer (default 1)
      next             — optional relative redirect URL
      page_version_id  — optional ProductPageVersion PK (TICKET-042, ADR-028 §6):
                          set by product.html's hidden field when the add-to-cart
                          form was rendered on a page-version URL. Validated by
                          _validate_page_version_id (must belong to this store +
                          product) before being stamped onto the CartItem — never
                          trusted blindly (Safety gate).

    Validates the variant belongs to request.store (404 if not).
    Checks inventory for FIXED_QTY variants: 400 if stock is zero, silent cap
    if the requested quantity exceeds available stock.
    unit_price is always snapshotted from variant.price (never from form data).
    """
    from cart.models import CartStatus
    from cart.service import add_item, get_or_create_cart
    from catalog.models import InventoryMode, ProductVariant

    variant_id_str = request.POST.get("variant_id", "")
    quantity_str = request.POST.get("quantity", "1")
    next_url = request.POST.get("next", "")

    # Validate quantity.
    try:
        quantity = int(quantity_str)
        if quantity <= 0:
            return HttpResponseBadRequest("Quantity must be a positive integer.")
    except (ValueError, TypeError):
        return HttpResponseBadRequest("Quantity must be a positive integer.")

    # Validate variant_id format.
    try:
        variant_id = int(variant_id_str)
    except (ValueError, TypeError):
        raise Http404

    # Load variant with store isolation (404 if not in this store).
    try:
        variant = ProductVariant.objects.for_store(request.store).get(pk=variant_id)
    except ProductVariant.DoesNotExist:
        raise Http404

    # Inventory check for FIXED_QTY variants only.
    if variant.inventory_mode == InventoryMode.FIXED_QTY:
        available = variant.quantity or 0
        if available == 0:
            return HttpResponseBadRequest("Out of stock.")
        if available < quantity:
            quantity = available  # cap silently to available stock

    page_version_id = _validate_page_version_id(request, variant.product_id)

    # Ensure the session has a key so the cart can be keyed to it.
    if not request.session.session_key:
        request.session.save()
    session_key = request.session.session_key

    # Get or create the cart for this session + store.
    cart = get_or_create_cart(request.store, session_key)

    # Delegate to service (handles SOLD_OUT, ASK_WHEN_AVAILABLE, QUOTATION).
    try:
        add_item(cart, variant_id, quantity, page_version_id=page_version_id)
    except ValueError as exc:
        return HttpResponseBadRequest(str(exc))

    return redirect(_safe_redirect(request, next_url))


@require_POST
def update_cart_item(request):
    """
    POST /cart/update/

    Form fields:
      variant_id  — ProductVariant PK identifying the line item (matches cart.html)
      quantity    — non-negative integer (0 = remove the item)
      next        — optional relative redirect URL

    Scopes the cart lookup to request.store + session_key (404 if not found).
    Scopes the CartItem lookup to the store and that cart (404 if not found).
    """
    from cart.models import Cart, CartItem
    from catalog.models import InventoryMode

    variant_id_str = request.POST.get("variant_id", "")
    quantity_str = request.POST.get("quantity", "")
    next_url = request.POST.get("next", "")

    # Validate variant_id.
    try:
        variant_id = int(variant_id_str)
    except (ValueError, TypeError):
        raise Http404

    # Validate quantity.
    try:
        quantity = int(quantity_str)
        if quantity < 0:
            return HttpResponseBadRequest("Quantity cannot be negative.")
    except (ValueError, TypeError):
        return HttpResponseBadRequest("Quantity must be an integer.")

    # No session → no cart → 404.
    if not request.session.session_key:
        raise Http404

    # Load cart scoped to this store + session.
    try:
        cart = Cart.objects.for_store(request.store).get(
            session_key=request.session.session_key
        )
    except Cart.DoesNotExist:
        raise Http404

    # Load item scoped to this store and cart.
    try:
        item = CartItem.objects.for_store(request.store).get(
            cart=cart,
            variant_id=variant_id,
        )
    except CartItem.DoesNotExist:
        raise Http404

    if quantity == 0:
        item.delete()
    else:
        # Apply inventory cap for FIXED_QTY variants.
        variant = item.variant
        if variant.inventory_mode == InventoryMode.FIXED_QTY:
            available = variant.quantity or 0
            if available < quantity:
                quantity = available
        item.quantity = quantity
        item.save(update_fields=["quantity"])

    return redirect(_safe_redirect(request, next_url))


@require_POST
def remove_cart_item(request):
    """
    POST /cart/remove/

    Form fields:
      variant_id  — ProductVariant PK identifying the line item (matches cart.html)
      next        — optional relative redirect URL

    Scopes the cart lookup to request.store + session_key (404 if not found).
    Scopes the CartItem lookup to the store and that cart (404 if not found).
    """
    from cart.models import Cart, CartItem

    variant_id_str = request.POST.get("variant_id", "")
    next_url = request.POST.get("next", "")

    # Validate variant_id.
    try:
        variant_id = int(variant_id_str)
    except (ValueError, TypeError):
        raise Http404

    # No session → no cart → 404.
    if not request.session.session_key:
        raise Http404

    # Load cart scoped to this store + session.
    try:
        cart = Cart.objects.for_store(request.store).get(
            session_key=request.session.session_key
        )
    except Cart.DoesNotExist:
        raise Http404

    # Load item scoped to this store and cart.
    try:
        item = CartItem.objects.for_store(request.store).get(
            cart=cart,
            variant_id=variant_id,
        )
    except CartItem.DoesNotExist:
        raise Http404

    item.delete()

    return redirect(_safe_redirect(request, next_url))
