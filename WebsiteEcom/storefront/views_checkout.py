"""
Checkout views — Phase 2 (ADR-015 §6, TICKET-029).

URL map (all registered before the <path:slug> catch-all in storefront/urls.py):
  GET  /checkout/                        checkout_view        — render checkout page
  POST /checkout/contact/                checkout_contact_post — contact section
  POST /checkout/address/                checkout_address_post — address section
  POST /checkout/shipping/               checkout_shipping_post — shipping section
  POST /checkout/pay/                    checkout_pay_post    — finalization + PI creation
  GET  /checkout/payment/return/         checkout_payment_return — 3DS/redirect return
  GET  /checkout/paypal/return/          checkout_paypal_return — PayPal buyer-approval return (ADR-020)
  GET  /checkout/paypal/cancel/          checkout_paypal_cancel — PayPal buyer-cancel return (ADR-020)
  GET  /checkout/retry/<signed_token>/   checkout_retry       — failed-payment retry
  POST /checkout/discount/apply/         apply_discount_view  — apply coupon/gift card
  POST /checkout/discount/remove/        remove_discount_view — remove coupon/gift card
  GET  /orders/<public_order_id>/thank-you/ order_thank_you  — confirmation page

Design invariants (ADR-015 §6):
- Core flow (contact → address → shipping → summary) works without JavaScript (NFR-1).
- CSRF protection on all POST views (Django default middleware — no exemptions).
- Cart must belong to request.store; verified at every entry point.
- Order must belong to request.store; verified at every order lookup.
- Checkout page is NEVER created on GET — only POST handlers get-or-create CheckoutState.
- Thank-you page uses a signed token (salt='order-thankyou') as the URL identifier;
  tampering returns 404 with no PII leak.
- Retry view uses a signed token (salt='checkout-retry') validated with max_age.
- processor_type == 'none' in begin_checkout result means zero-total; redirect to thank-you.

PayPal buyer-approval completion flow (ADR-020):
- checkout_payment_return (Stripe-shaped, ?payment_intent=) and checkout_paypal_return
  (PayPal-shaped, ?token=) share their success/decline tail via _finalize_payment_return —
  the extraction is behavior-preserving for Stripe (see storefront/tests/test_checkout.py
  CheckoutPaymentReturnTest for the regression guard).
- checkout_paypal_return authorizes the order synchronously via
  payments.paypal_service.ensure_authorized (also called, as a fallback, by the
  CHECKOUT.ORDER.APPROVED webhook handler for a buyer who approves then closes the tab).
- checkout_paypal_cancel never touches the order: CheckoutState.step stays PAYMENT so the
  existing decline-retry path (_cart_or_redirect) re-admits the shopper.
- _finalize_payment_return (20:P1, DECIDED 2026-07-10): for orders with no funnel campaign
  (Order.capture_immediately=True), calls campaigns.tasks.capture_original_charge_now(order)
  right after stamping authorized_at — captures the payment immediately on confirmation
  instead of leaving it for capture_window_watchdog. Funnel orders are unaffected. The async
  webhook fallback for a buyer who closes the tab lives in payments/webhook_views.py
  (Stripe) and payments/paypal_webhook_views.py (PayPal).

Checkout-language activation (ADR-021 §4): checkout runs on the un-prefixed /checkout/...
URLs above, so on a path-prefixed store request.locale carries the domain's ROOT
language, not the /fr/... language the shopper was actually browsing under. The
activate_checkout_language decorator (applied to checkout_view, the contact/address/
shipping/pay POSTs, checkout_payment_return, checkout_retry, and order_thank_you)
re-activates Django's translation machinery using resolve_checkout_language's
session-sf_lang-first priority before each of those views runs. country_meta_view
takes the language explicitly via a `lang` query param instead (see its own docstring).
"""

import logging
import re
from decimal import Decimal
from functools import wraps

from django.contrib import messages
from django.core import signing
from django.http import Http404, JsonResponse
from django.shortcuts import get_object_or_404, redirect
from django.template.response import TemplateResponse
from django.urls import reverse
from django.utils import timezone, translation
from django.utils.http import url_has_allowed_host_and_scheme
from django.utils.translation import gettext, gettext_lazy as _
from django.views.decorators.http import require_GET, require_POST

from cart.checkout import begin_checkout, CheckoutError, DuplicateCheckoutError
from cart.models import Cart, CartStatus, CheckoutState, CheckoutStep
from django.db.models import Prefetch
from orders.models import Order, OrderItem, OrderItemStatus, PaymentStatus
from shipping.service import resolve_shipping_rates
from storefront.address_meta import get_country_meta
from storefront.forms_checkout import ContactForm, AddressForm, ShippingForm
from storefront.tokens import (
    make_thank_you_token,
    read_thank_you_token,
    make_retry_token,
    read_retry_token,
    CHECKOUT_RETRY_TOKEN_MAX_AGE,
)

logger = logging.getLogger('storefront.checkout')


def _phone_form_kwargs(store) -> dict:
    """
    Return ContactForm keyword args derived from the store's checkout_phone_mode (CO-003).

    Reads store.checkout_phone_mode gracefully (defaults to 'optional' when the attribute
    is absent — e.g. in tests that pre-date the field).

    Returns a dict with exactly the keys ContactForm.__init__ accepts for phone behaviour:
      required_phone — True when mode is 'required'
      hidden_phone   — True when mode is 'hidden'
    """
    mode = getattr(store, 'checkout_phone_mode', 'optional')
    return {
        'required_phone': mode == 'required',
        'hidden_phone': mode == 'hidden',
    }


# Step ordering for gating logic.
_STEP_ORDER = {
    CheckoutStep.CONTACT: 0,
    CheckoutStep.ADDRESS: 1,
    CheckoutStep.SHIPPING: 2,
    CheckoutStep.PAYMENT: 3,
    CheckoutStep.CONFIRMED: 4,
}


def _step_gte(current_step: str, required_step: str) -> bool:
    """Return True when current_step is at or past required_step."""
    return _STEP_ORDER.get(current_step, -1) >= _STEP_ORDER.get(required_step, 0)


def _is_ajax(request) -> bool:
    """Return True when the request carries the XMLHttpRequest sentinel header."""
    return request.headers.get('X-Requested-With') == 'XMLHttpRequest'


def _checkout_steps_context(current_step: str) -> dict:
    """
    Build the steps / completed_steps / current_step context dict for the
    checkout progress indicator (checkout_steps.html partial).

    Used by checkout_view (single-page flow) to drive the progress bar.
    The keys are merged directly into the view context dict.

    Returns a dict with:
        steps           — ordered list of (step_id, translated_label) 2-tuples.
        completed_steps — list of step_ids whose _STEP_ORDER index is strictly
                          below current_step's index (all steps already done).
        current_step    — echoed from the argument for template convenience.
    """
    steps = [
        (CheckoutStep.CONTACT,  _("Contact")),
        (CheckoutStep.ADDRESS,  _("Address")),
        (CheckoutStep.SHIPPING, _("Shipping")),
        (CheckoutStep.PAYMENT,  _("Payment")),
    ]
    current_idx = _STEP_ORDER.get(current_step, -1)
    completed_steps = [
        step_id
        for step_id, _label in steps
        if _STEP_ORDER.get(step_id, -1) < current_idx
    ]
    return {
        "steps": steps,
        "completed_steps": completed_steps,
        "current_step": current_step,
    }


def _get_cart(request):
    """
    Return the ACTIVE cart for the current session/store, or None.

    Never raises — callers redirect on None.
    """
    store = getattr(request, 'store', None)
    session_key = getattr(getattr(request, 'session', None), 'session_key', None)
    if store is None or not session_key:
        return None
    return (
        Cart.objects.for_store(store)
        .filter(session_key=session_key, status=CartStatus.ACTIVE)
        .first()
    )


def _cart_or_redirect(request):
    """
    Return (cart, None) or (None, redirect_to_cart_response).

    Two resolution paths:

    1. Normal path — ACTIVE cart with at least one item.
    2. Decline-retry path (EC-007, PY-013) — no ACTIVE cart, but there is an
       in-progress (non-CONFIRMED) CheckoutState for a CONVERTED cart.  This
       allows the shopper to return to /checkout/ after a payment decline without
       losing their checkout progress.  CONFIRMED is excluded so that a completed
       checkout can never re-enter the flow.
    """
    store = getattr(request, 'store', None)
    session_key = getattr(getattr(request, 'session', None), 'session_key', None)

    if store is None or not session_key:
        return None, redirect(reverse('storefront:cart'))

    # Normal path: active cart with items.
    cart = Cart.objects.for_store(store).filter(
        session_key=session_key, status=CartStatus.ACTIVE
    ).first()
    # ADR-031 addendum audit (TICKET-051): `cart.items` is a reverse-FK
    # related manager (CartItem.cart -> Cart) — ADR-031 deliberately leaves
    # those raising (only relation-bound M2M managers were relaxed).
    # `.exists()` used to silently bypass that raise entirely; scoped
    # explicitly now that it raises unconditionally (same pattern already
    # used a few lines below in this file).
    if cart is not None:
        from cart.models import CartItem
        if CartItem.objects.for_store(store).filter(cart=cart).exists():
            return cart, None

    # Decline-retry path: CONVERTED cart with a non-CONFIRMED CheckoutState.
    # Only PAYMENT / SHIPPING / ADDRESS steps are eligible — CONFIRMED means the
    # checkout completed successfully and the shopper must not re-enter the flow.
    state = (
        CheckoutState.objects.for_store(store)
        .filter(
            cart__session_key=session_key,
            step__in=[CheckoutStep.PAYMENT, CheckoutStep.SHIPPING, CheckoutStep.ADDRESS],
        )
        .select_related('cart')
        .first()
    )
    if state is not None:
        return state.cart, None

    return None, redirect(reverse('storefront:cart'))


def _get_checkout_state(cart):
    """Return the CheckoutState for cart, or None if not yet started."""
    try:
        return cart.checkout_state
    except CheckoutState.DoesNotExist:
        return None


def _safe_next_url(request, raw_next: str) -> str:
    """
    Validate a POST `next` redirect target against open-redirect attacks
    (e.g. ?next=https://evil.example/ bounce — PY-020 hardening).

    Falls back to /checkout/ when raw_next is empty or points outside this
    request's own host/scheme.
    """
    fallback = reverse('storefront:checkout')
    if not raw_next:
        return fallback
    if url_has_allowed_host_and_scheme(raw_next, allowed_hosts={request.get_host()}):
        return raw_next
    return fallback


def _build_discount_summary(cart, shipping_rate) -> dict:
    """
    Compute the display-only discount/shipping/total summary for the checkout
    sidebar (CK-020/CK-021, PY-020).

    Delegates the actual math to cart.checkout.preview_checkout_totals — the
    same coupon-first/gift-card-second formula begin_checkout uses to charge
    the order (single source of truth; a sidebar total that diverges from the
    charged total is worse than no total at all).

    Returns a dict with subtotal/coupon_code/coupon_amount/gift_card_code/
    gift_card_amount/shipping_amount/total (all Decimal except the *_code keys).
    Returns an all-zero summary when cart is None (no active cart yet).
    """
    from cart.checkout import preview_checkout_totals
    from cart.models import CartItem

    if cart is None:
        return {
            'subtotal': Decimal('0'),
            'coupon_code': '',
            'coupon_amount': Decimal('0'),
            'gift_card_code': '',
            'gift_card_amount': Decimal('0'),
            'shipping_amount': Decimal('0'),
            'total': Decimal('0'),
        }

    subtotal = sum(
        (item.line_total for item in CartItem.objects.for_store(cart.store).filter(cart=cart)),
        Decimal('0'),
    )
    return preview_checkout_totals(cart, subtotal, shipping_rate)


def resolve_checkout_language(request) -> str:
    """
    Resolve the language code for this checkout session (U16-3, ADR-015 §6).

    Priority: session 'sf_lang' → request.locale lang_code → 'en'.
    """
    session = getattr(request, 'session', None)
    if session is not None and hasattr(session, 'get'):
        lang = session.get('sf_lang', '')
        if lang:
            return lang
    locale = getattr(request, 'locale', None)
    if locale is not None:
        try:
            return locale.language.lang_code
        except AttributeError:
            pass
    return 'en'


def activate_checkout_language(view):
    """
    Activate the checkout-session language for a buyer-facing checkout view
    (ADR-021 §4, ADR-015 §6).

    Checkout runs on the un-prefixed /checkout/... URLs (see module docstring's URL
    map), which URLconf routing matches BEFORE the <path:slug> catch-all — so on a
    path-prefixed store, resolve_locale never sees the /fr/ prefix the shopper was
    actually browsing under, and request.locale carries the domain's ROOT language
    instead. sf_lang (resolve_checkout_language's first priority) is the only
    carrier of the buyer's real browsing language across that boundary.

    No restore is needed after the view returns: stores.middleware.LocaleMiddleware
    re-activates deterministically at the very start of the NEXT request (ADR-021
    §D1a), so this activation only ever affects the current request/response cycle —
    including a deferred TemplateResponse, since there is nothing racing to undo it.

    Applied to exactly the buyer-facing checkout views named in ADR-021's ticket:
    checkout_view, the contact/address/shipping/pay POST handlers, the (Stripe)
    payment-return view, checkout_retry, and order_thank_you. PayPal's
    return/cancel views and the discount apply/remove views are deliberately NOT
    decorated — they never render a template of their own (pure redirects whose only
    user-visible strings are gettext_lazy messages that resolve later, when a
    decorated view renders them), so there is nothing on those code paths that needs
    the active language changed before it runs.
    """
    @wraps(view)
    def _wrapped(request, *args, **kwargs):
        translation.activate(resolve_checkout_language(request))
        request.LANGUAGE_CODE = translation.get_language()
        return view(request, *args, **kwargs)
    return _wrapped


def _build_country_choices(store):
    """
    Build (code, name) choices from the active shipping zones for this store (SA-002).

    Returns a list of unique country codes from all active rates for this store.
    Sorted alphabetically. Returns an empty list when no zones are configured.
    """
    from shipping.models import ShippingZone, ShippingRate
    zone_ids = set(
        ShippingRate.objects.for_store(store)
        .filter(is_active=True)
        .values_list('zone_id', flat=True)
    )
    if not zone_ids:
        return []
    zones = ShippingZone.objects.filter(pk__in=zone_ids, is_active=True)
    codes = set()
    has_catch_all = False
    for zone in zones:
        if zone.country_codes:
            codes.update(zone.country_codes)
        else:
            has_catch_all = True
    if has_catch_all and not codes:
        # Catch-all zone only — can't enumerate countries; return empty so
        # the template shows an open text field fallback.
        return []
    return sorted((c, c) for c in codes)


# ---------------------------------------------------------------------------
# Country address metadata endpoint — GET /checkout/address/country-meta/
# ---------------------------------------------------------------------------

# Same shape ADR-008 uses for StoreLanguage.lang_code — lowercase ISO 639-1,
# optional region suffix (e.g. 'fr', 'pt-br'). Anything else falls back to
# resolve_checkout_language(request) rather than being passed to override().
_LANG_PARAM_RE = re.compile(r'^[a-z]{2}(-[a-z]{2})?$')


@require_GET
def country_meta_view(request):
    """
    Return JSON address metadata for the given country code (ADR-017 §4, D3).

    GET /checkout/address/country-meta/?country=US&lang=fr
    Response (200 application/json):
      {country, subdivision_mode, subdivision_label, subdivisions, postal_label,
       postal_pattern, postal_example}

    Unknown / missing country param → 200 with DEFAULT_META payload; the JS
    never needs an error branch, matching get_country_meta()'s never-fails contract.

    Language (ADR-021 §3/§Country-meta): the `lang` query param makes the response
    language part of the cache key instead of a hidden dependency on a header this
    endpoint never reads. `lang` is optional and sanitized against
    ^[a-z]{2}(-[a-z]{2})?$ (case-insensitive, lowercased first); missing or invalid
    values fall back to resolve_checkout_language(request) — this keeps the endpoint
    backward compatible with the pre-ADR-021 checkout.js, which did not send `lang`
    at all. Labels are resolved (str()'d out of their gettext_lazy proxies) inside a
    translation.override(lang) block so the response reflects exactly the requested
    language regardless of whatever is active on the current thread.

    No Vary: Accept-Language — this server never reads Accept-Language, and with
    `lang` now in the URL, every language variant already has a distinct cache key
    (ADR-021 §D3). Cache-Control: public, max-age=86400 is unchanged.

    This endpoint is public metadata — it does NOT check the country against
    the store's shipping zones (shippability is enforced by ChoiceField on POST).
    """
    raw_lang = request.GET.get('lang', '').strip().lower()
    lang = raw_lang if _LANG_PARAM_RE.match(raw_lang) else resolve_checkout_language(request)

    country_code = request.GET.get('country', '').strip().upper()

    with translation.override(lang):
        meta = get_country_meta(country_code)
        data = {
            'country': meta.country,
            'subdivision_mode': meta.subdivision_mode,
            'subdivision_label': str(meta.subdivision_label),
            'subdivisions': list(meta.subdivisions),
            'postal_label': str(meta.postal_label),
            'postal_pattern': meta.postal_pattern,
            'postal_example': meta.postal_example,
        }

    response = JsonResponse(data)
    response['Cache-Control'] = 'public, max-age=86400'
    return response


# ---------------------------------------------------------------------------
# Main checkout page — GET /checkout/
# ---------------------------------------------------------------------------

def _get_stripe_publishable_key(store) -> str:
    """
    Return the Stripe publishable key (client_id) for the store's organization.

    Used by checkout_view to pass data-publishable-key to the Stripe Elements mount
    point in checkout.html (ADR-015 §6 PY-003). Returns '' when not configured —
    checkout.js guards against this and skips Elements initialization.

    Lookup strategy: first active Stripe ProcessorAccount for the store's
    organization, ordered by pk for determinism.
    """
    org = getattr(store, 'organization', None)
    if org is None:
        return ''
    try:
        from payments.models import ProcessorAccount, ProcessorType
        account = (
            ProcessorAccount.objects
            .filter(organization_id=org.pk, processor_type=ProcessorType.STRIPE, is_active=True)
            .order_by('pk')
            .first()
        )
        return account.client_id if account else ''
    except Exception:
        logger.exception('checkout_view: failed to resolve Stripe publishable key for store %s', store.pk)
        return ''


def _checkout_page_context(
    request,
    cart,
    state,
    current_step: str,
    *,
    contact_form=None,
    address_form=None,
    shipping_form=None,
) -> dict:
    """
    Build the COMPLETE context for storefront/pages/checkout.html (ADR-015 §6,
    CK-001/CK-020/CK-021) — the single source of truth for this template's context.

    checkout_view (GET) AND every POST error re-render (checkout_contact_post /
    checkout_address_post / checkout_shipping_post) call this so an invalid
    submission never drops the progress bar, currency notice, or discount
    chip/summary — every render path produces the exact same shape of context,
    overriding only the form that actually failed validation.

    current_step drives which sections/data are built — it is NOT necessarily
    state.step: an error re-render deliberately renders as if current_step were
    the step being edited, even when state.step is further along (e.g.
    re-editing address after already reaching shipping must not show the
    shipping/payment sections in that same render). checkout_view passes
    state.step (or CONTACT when there is no state yet); the POST error paths
    pass the literal step whose form just failed.

    contact_form / address_form / shipping_form: pass the INVALID bound form
    from a failed POST to have it (with its errors) rendered in place of a
    fresh default. Leave None to get a fresh unbound instance built exactly
    like a normal GET would (pre-filled from `state` when available).

    Does NOT fire EVT_INITIATE_CHECKOUT or any other GET-only side effect —
    callers are responsible for those before invoking this helper.
    """
    store = request.store
    _phone_kwargs = _phone_form_kwargs(store)

    if contact_form is None:
        contact_form = ContactForm(
            initial={
                'email': state.email if state else '',
                'phone': state.phone if state else '',
                'newsletter_opt_in': state.newsletter_opt_in if state else False,
            },
            **_phone_kwargs,
        )

    if address_form is None:
        if state is not None and _step_gte(current_step, CheckoutStep.ADDRESS):
            addr = state.shipping_address or {}
            # Split name back into first/last for the address form.
            name_parts = addr.get('name', '').split(' ', 1)
            address_form = AddressForm(
                initial={
                    'first_name': name_parts[0] if name_parts else '',
                    'last_name': name_parts[1] if len(name_parts) > 1 else '',
                    'line1': addr.get('line1', ''),
                    'line2': addr.get('line2', ''),
                    'city': addr.get('city', ''),
                    'state': addr.get('state', ''),
                    'postal_code': addr.get('postal_code', ''),
                    'country': addr.get('country', ''),
                },
                country_choices=_build_country_choices(store),
            )
        else:
            address_form = AddressForm(country_choices=_build_country_choices(store))

    shipping_rates = []
    if state is not None and _step_gte(current_step, CheckoutStep.SHIPPING):
        country = (state.shipping_address or {}).get('country', '')
        shipping_rates = resolve_shipping_rates(store, country)
        if shipping_form is None:
            initial_rate = (
                str(state.shipping_rate_id) if state.shipping_rate_id else None
            )
            shipping_form = ShippingForm(
                initial={'shipping_rate_id': initial_rate},
                shipping_rates=shipping_rates,
            )

    # Stripe publishable key — only fetched when the shopper has reached the
    # payment step (avoids a DB hit on every earlier GET /checkout/).
    publishable_key = ''
    if state is not None and _step_gte(current_step, CheckoutStep.PAYMENT):
        publishable_key = _get_stripe_publishable_key(store)

    # Sidebar discount entry forms: hidden per-type once that type is already
    # applied (CK-022 stacking gating) — a coupon applied does not hide the
    # gift-card entry, and vice versa.
    cart_has_coupon = bool(cart and getattr(cart, 'discount_code_id', None))
    cart_has_gift_card = bool(cart and getattr(cart, 'gift_card_id', None))

    # SM-002: resolve the rate PK that should be pre-checked in the shipping radio group.
    # ShippingForm.__init__ sets initial['shipping_rate_id'] to cheapest when no
    # prior selection — this applies whether shipping_form is the default or
    # an overridden (bound) invalid form.
    _preselected_str = shipping_form.initial.get('shipping_rate_id') if shipping_form else None
    preselected_rate_id = int(_preselected_str) if _preselected_str else None

    shipping_rate = state.shipping_rate if state else None

    return {
        'cart': cart,
        'checkout_state': state,
        'contact_form': contact_form,
        'address_form': address_form,
        'shipping_form': shipping_form,
        'shipping_rates': shipping_rates,
        'step': current_step,
        # Progress indicator (checkout_steps.html)
        **_checkout_steps_context(current_step),
        # Sidebar — cart_items / cart_total come from storefront_context processor;
        # shipping_rate must be passed explicitly (not in the processor).
        'shipping_rate': shipping_rate,
        # Discount code entry forms (PY-020/PY-021/CK-022): hidden per-type
        # once that type is already applied.
        'cart_has_coupon': cart_has_coupon,
        'cart_has_gift_card': cart_has_gift_card,
        # Discounted totals summary (CK-020/CK-021) — subtotal/coupon/gift
        # card/shipping/total, reusing begin_checkout's exact discount math.
        'discount_summary': _build_discount_summary(cart, shipping_rate),
        # Currency notice (ML-004) — shown in checkout_sidebar.html.
        'order_currency': getattr(store, 'default_currency', ''),
        # SM-002: rate PK (int) for the pre-checked radio; None when no rates available.
        'preselected_rate_id': preselected_rate_id,
        # Stripe Elements (PY-003) — client_secret is empty at GET time;
        # checkout.js guards against missing credentials and is inert here.
        'publishable_key': publishable_key,
        'client_secret': '',
        'payment_return_url': request.build_absolute_uri(
            reverse('storefront:checkout-payment-return')
        ),
        # trust_badge_location: explicit, never-sniffed context key consumed by
        # badges.slot_provider.SecurityBadgeSlotProvider (ADR-030 D4). Set here
        # (not just in checkout_view) so every POST error re-render that reuses
        # this helper keeps the badge in place too.
        'trust_badge_location': 'checkout',
    }


@require_GET
@activate_checkout_language
def checkout_view(request):
    """
    Render the unified checkout page (ADR-015 §6, CK-001).

    Reads CheckoutState to determine which section is open. Never creates rows.
    Empty / CONVERTED cart → redirect to cart page (EC-001).

    Context is built by _checkout_page_context — see its docstring for the
    full shape (also the single source of truth used by the POST error
    re-renders in checkout_contact_post / checkout_address_post /
    checkout_shipping_post).
    """
    cart, redirect_response = _cart_or_redirect(request)
    if redirect_response is not None:
        return redirect_response

    store = request.store
    checkout_state = _get_checkout_state(cart)

    # Fire EVT_INITIATE_CHECKOUT once per checkout session (ADR-015 §2, CK-003).
    # Conditional UPDATE (WHERE initiate_event_fired=False) is idempotent under
    # concurrent requests: only the first successful UPDATE fires the event.
    # GET-only side effect — deliberately not part of _checkout_page_context.
    #
    # ADR-022 fix (security-audit MEDIUM-1 wiring): record_initiate_checkout —
    # and the InitiateCheckout pixel event below — must run AFTER the
    # conditional UPDATE succeeds, keyed on updated == 1, never before it.
    # The previous ordering called record_initiate_checkout unconditionally
    # before attempting the claim, so two concurrent first GETs could both
    # ingest the server-side event even though only one of them can ever win
    # the UPDATE — the "loser" still recorded a checkout start that the claim
    # says never happened.
    pixel_events = []
    if checkout_state is not None and not checkout_state.initiate_event_fired:
        updated = CheckoutState.objects.for_store(store).filter(
            pk=checkout_state.pk,
            initiate_event_fired=False,
        ).update(initiate_event_fired=True)
        if updated == 1:
            checkout_state.initiate_event_fired = True

            from analytics.events import record_initiate_checkout
            _session_key = getattr(getattr(request, 'session', None), 'session_key', None) or ''
            record_initiate_checkout(store.pk, _session_key)

            # InitiateCheckout pixel event (ADR-022 D4/D5) — inherits this
            # exact server-side dedup claim; a checkout reload never re-enters
            # this branch, so PixelsSlotProvider never re-fires it. Pinterest
            # has no native InitiateCheckout event and renders nothing for it
            # (ADR-022 D4).
            from cart.models import CartItem
            from pixels.events import build_initiate_checkout_payload, catalog_item_id

            cart_items = list(
                CartItem.objects.for_store(store).filter(cart=cart)
                .select_related('variant__product')
            )
            pixel_events.append((
                'initiate_checkout',
                build_initiate_checkout_payload(
                    content_ids=[
                        catalog_item_id(ci.variant.product, ci.variant) for ci in cart_items
                    ],
                    value=sum((ci.line_total for ci in cart_items), Decimal('0')),
                    currency=getattr(store, 'default_currency', '') or 'USD',
                    num_items=sum(ci.quantity for ci in cart_items),
                ),
            ))

    current_step = checkout_state.step if checkout_state else CheckoutStep.CONTACT

    context = _checkout_page_context(request, cart, checkout_state, current_step)
    context['pixel_events'] = pixel_events

    return TemplateResponse(
        request,
        'storefront/pages/checkout.html',
        context,
    )


# ---------------------------------------------------------------------------
# Contact step — POST /checkout/contact/
# ---------------------------------------------------------------------------

@require_POST
@activate_checkout_language
def checkout_contact_post(request):
    """
    Save contact information and advance to ADDRESS step.

    On validation error: re-render checkout page with form errors (no redirect).
    On success: 302 → /checkout/ with step=ADDRESS.
    Side effects: get-or-create CheckoutState, write Cart.customer_email (CO-002).
    """
    cart, redirect_response = _cart_or_redirect(request)
    if redirect_response is not None:
        return redirect_response

    store = request.store
    _phone_kwargs = _phone_form_kwargs(store)
    form = ContactForm(request.POST, **_phone_kwargs)

    if not form.is_valid():
        return TemplateResponse(
            request,
            'storefront/pages/checkout.html',
            _checkout_page_context(
                request, cart, _get_checkout_state(cart), CheckoutStep.CONTACT,
                contact_form=form,
            ),
        )

    email = form.cleaned_data['email'].strip().lower()
    phone = form.cleaned_data.get('phone', '').strip()
    newsletter_opt_in = form.cleaned_data.get('newsletter_opt_in', False)
    checkout_language = resolve_checkout_language(request)

    # Get-or-create CheckoutState (only created on POST, never GET — ADR-015 §2).
    # Must use .for_store(store) to satisfy ADR-001 §4 store isolation.
    state, _ = CheckoutState.objects.for_store(store).get_or_create(
        cart=cart,
        defaults={'store': store},
    )
    state.email = email
    state.phone = phone
    state.newsletter_opt_in = newsletter_opt_in
    state.checkout_language = checkout_language
    if not _step_gte(state.step, CheckoutStep.ADDRESS):
        state.step = CheckoutStep.ADDRESS
    state.save(update_fields=['email', 'phone', 'newsletter_opt_in', 'checkout_language', 'step', 'updated_at'])

    # Write customer email to cart for abandoned-checkout recovery (CO-002).
    Cart.objects.for_store(store).filter(pk=cart.pk).update(customer_email=email)

    return redirect(reverse('storefront:checkout'))


# ---------------------------------------------------------------------------
# Address step — POST /checkout/address/
# ---------------------------------------------------------------------------

@require_POST
@activate_checkout_language
def checkout_address_post(request):
    """
    Save shipping address and advance to SHIPPING step.

    Guard: CheckoutState must exist with step >= ADDRESS.
    Editing the address clears the previously-selected shipping_rate (SM-001).
    On validation error: re-render checkout page with form errors.
    On success: 302 → /checkout/ with step=SHIPPING.
    """
    cart, redirect_response = _cart_or_redirect(request)
    if redirect_response is not None:
        return redirect_response

    store = request.store
    state = _get_checkout_state(cart)

    if state is None or not _step_gte(state.step, CheckoutStep.ADDRESS):
        return redirect(reverse('storefront:checkout'))

    form = AddressForm(request.POST, country_choices=_build_country_choices(store))

    if not form.is_valid():
        return TemplateResponse(
            request,
            'storefront/pages/checkout.html',
            _checkout_page_context(
                request, cart, state, CheckoutStep.ADDRESS,
                address_form=form,
            ),
        )

    address = form.to_address_dict()

    # Editing the address clears the shipping rate (SM-001) and regresses step to SHIPPING.
    state.shipping_address = address
    state.shipping_rate = None
    state.billing_address = {}  # reset billing override on address change
    if not _step_gte(state.step, CheckoutStep.SHIPPING):
        state.step = CheckoutStep.SHIPPING
    else:
        # Address changed — re-open shipping step for re-selection.
        state.step = CheckoutStep.SHIPPING
    state.save(update_fields=['shipping_address', 'shipping_rate', 'billing_address', 'step', 'updated_at'])

    return redirect(reverse('storefront:checkout'))


# ---------------------------------------------------------------------------
# Shipping step — POST /checkout/shipping/
# ---------------------------------------------------------------------------

@require_POST
@activate_checkout_language
def checkout_shipping_post(request):
    """
    Save selected shipping rate and advance to PAYMENT step.

    Guard: CheckoutState must exist with step >= SHIPPING.
    The submitted shipping_rate_id is server-re-validated against resolve_shipping_rates
    for the stored address country (OF-002 — client is never trusted for rate eligibility).
    On invalid rate: 400 (advisory failure, shopper must re-select).
    On success: 302 → /checkout/ with step=PAYMENT.
    """
    from django.http import HttpResponseBadRequest

    cart, redirect_response = _cart_or_redirect(request)
    if redirect_response is not None:
        return redirect_response

    store = request.store
    state = _get_checkout_state(cart)

    if state is None or not _step_gte(state.step, CheckoutStep.SHIPPING):
        return redirect(reverse('storefront:checkout'))

    # Server re-resolve available rates for the stored address country.
    country = (state.shipping_address or {}).get('country', '')
    available_rates = resolve_shipping_rates(store, country)

    form = ShippingForm(request.POST, shipping_rates=available_rates)

    if not form.is_valid():
        # A non-empty submission that failed choice-validation means the submitted
        # rate_id is not in the server-resolved list — treat as a tampering attempt
        # and return 400 (OF-002). An empty submission (no rate selected) is a user
        # error: re-render with form errors (200).
        submitted_id = request.POST.get('shipping_rate_id', '').strip()
        if submitted_id:
            return HttpResponseBadRequest('Invalid shipping method selection.')
        # SM-002: cheapest rate is pre-checked on re-render (form.__init__ sets initial;
        # _checkout_page_context resolves preselected_rate_id from shipping_form.initial).
        return TemplateResponse(
            request,
            'storefront/pages/checkout.html',
            _checkout_page_context(
                request, cart, state, CheckoutStep.SHIPPING,
                shipping_form=form,
            ),
        )

    rate_pk = form.get_rate_pk()
    available_rate_ids = {r.pk for r in available_rates}

    # Server-side re-validation (OF-002 — never trust client rate_id for price/eligibility).
    if rate_pk is None or rate_pk not in available_rate_ids:
        return HttpResponseBadRequest('Invalid shipping method selection.')

    # Save the selected rate.
    try:
        selected_rate = next(r for r in available_rates if r.pk == rate_pk)
    except StopIteration:
        return HttpResponseBadRequest('Invalid shipping method selection.')

    state.shipping_rate = selected_rate
    if not _step_gte(state.step, CheckoutStep.PAYMENT):
        state.step = CheckoutStep.PAYMENT
    state.save(update_fields=['shipping_rate', 'step', 'updated_at'])

    return redirect(reverse('storefront:checkout'))


# ---------------------------------------------------------------------------
# Pay step — POST /checkout/pay/
# ---------------------------------------------------------------------------

@require_POST
@activate_checkout_language
def checkout_pay_post(request):
    """
    Create Order + PaymentIntent and render the payment confirmation page.

    Guard: CheckoutState must exist with step >= PAYMENT.
    Flow:
      - If CheckoutState.order is a stale PENDING/never-authorized order and the cart
        fingerprint changed, void it first (ADR-015 §3 fast path).
      - Call begin_checkout(...) with data from CheckoutState.
      - Zero-total (processor_type='none'): 302 → thank-you.
      - DuplicateCheckoutError: 302 → existing order's thank-you.
      - CheckoutError: 302 → /checkout/ with error message.
      - Success: render checkout_payment.html with client_secret + processor_type.
    """
    cart, redirect_response = _cart_or_redirect(request)
    if redirect_response is not None:
        return redirect_response

    store = request.store
    state = _get_checkout_state(cart)

    if state is None or not _step_gte(state.step, CheckoutStep.PAYMENT):
        return redirect(reverse('storefront:checkout'))

    if not state.email:
        return redirect(reverse('storefront:checkout'))

    if not state.shipping_address:
        return redirect(reverse('storefront:checkout'))

    # Fast-path void of stale prior PENDING order (ADR-015 §3).
    # Only applicable when the cart fingerprint changed (new items/quantities)
    # so a new order is about to be created while a prior one holds resources.
    if state.order_id is not None:
        try:
            prior_order = Order.objects.cross_store_unsafe().get(pk=state.order_id)
            if (
                prior_order.payment_status == PaymentStatus.PENDING
                and prior_order.authorized_at is None
            ):
                from orders.service import void_pending_order
                void_pending_order(prior_order)
        except Order.DoesNotExist:
            pass

    # UTM data from session (if available).
    session = getattr(request, 'session', None)
    utm_data = session.get('utm_data') if session and hasattr(session, 'get') else None

    try:
        result = begin_checkout(
            cart=cart,
            email=state.email,
            shipping_address=state.shipping_address,
            shipping_rate_id=state.shipping_rate_id,
            phone=state.phone,
            billing_address=state.billing_address or None,
            checkout_language=state.checkout_language or resolve_checkout_language(request),
            utm_data=utm_data,
            # ADR-020 D2: PayPal puts these in the order's experience_context so
            # the buyer is redirected back here after approving/cancelling.
            # Stripe ignores both — request.build_absolute_uri is cheap and safe
            # to compute unconditionally.
            approval_return_url=request.build_absolute_uri(
                reverse('storefront:checkout-paypal-return')
            ),
            approval_cancel_url=request.build_absolute_uri(
                reverse('storefront:checkout-paypal-cancel')
            ),
        )
    except DuplicateCheckoutError as exc:
        # Cart already submitted — redirect to existing order's thank-you (EC-007, ADR-015 §7).
        existing = exc.existing_order
        token = make_thank_you_token(existing.pk)
        thank_you_url = reverse('storefront:order-thank-you', args=[token])
        if _is_ajax(request):
            return JsonResponse({
                'redirect_url': request.build_absolute_uri(thank_you_url),
            })
        return redirect(thank_you_url)
    except CheckoutError as exc:
        if _is_ajax(request):
            return JsonResponse({'error': str(exc)}, status=400)
        messages.error(request, str(exc))
        return redirect(reverse('storefront:checkout'))

    # Update CheckoutState with the created order.
    order_id = result['order_id']

    # Propagate newsletter opt-in to Customer record (CO-004).
    # Only updates an existing row — never creates one.
    # Customer is guaranteed to exist here: begin_checkout called get_or_create_customer.
    if state.newsletter_opt_in:
        from customers.models import Customer
        Customer.objects.for_store(store).filter(
            email=state.email,
        ).update(accepts_marketing=True)

    # Zero-total: no payment needed — mark confirmed immediately and redirect to thank-you.
    if result.get('processor_type') == 'none':
        # LOW-1 (CHECKOUT_BATCH_2_AUDIT.md): .update() bypasses auto_now, so
        # updated_at must be stamped explicitly or the GC staleness clock keeps
        # reflecting the last form-step save() instead of this order/PI creation.
        # ADR-031 addendum audit (TICKET-051): .update() used to silently
        # bypass the isolation raise — scoped via .for_store(store), the
        # variable already in scope from this view (store = request.store).
        CheckoutState.objects.for_store(store).filter(pk=state.pk).update(
            order_id=order_id,
            step=CheckoutStep.CONFIRMED,
            updated_at=timezone.now(),
        )
        try:
            order = Order.objects.for_store(store).get(pk=order_id)
        except Order.DoesNotExist:
            return redirect(reverse('storefront:cart'))
        token = make_thank_you_token(order.pk)
        thank_you_url = reverse('storefront:order-thank-you', args=[token])
        if _is_ajax(request):
            return JsonResponse({
                'redirect_url': request.build_absolute_uri(thank_you_url),
            })
        return redirect(thank_you_url)

    # PI-backed order: record the order FK but keep step=PAYMENT.
    # CONFIRMED is only set in checkout_payment_return once the PI is authorized
    # (succeeded / requires_capture).  Keeping step=PAYMENT here enables the
    # decline-retry path in _cart_or_redirect (EC-007 / PY-013): if Stripe
    # declines the card and redirects back, _cart_or_redirect finds this state
    # (step=PAYMENT on a CONVERTED cart) and returns the cart instead of
    # redirecting to /cart/.
    # LOW-1 (CHECKOUT_BATCH_2_AUDIT.md): stamp updated_at explicitly — see note above.
    # ADR-031 addendum audit (TICKET-051): scoped via .for_store(store) —
    # see the identical note on the zero-total branch above.
    CheckoutState.objects.for_store(store).filter(pk=state.pk).update(
        order_id=order_id,
        updated_at=timezone.now(),
    )

    # Normal path: PI-backed order (Stripe or PayPal).
    try:
        order = Order.objects.for_store(store).get(pk=order_id)
    except Order.DoesNotExist:
        if _is_ajax(request):
            # gettext (eager), not gettext_lazy: this string is serialized to
            # JSON immediately — a lazy proxy must not leak into the response.
            return JsonResponse({'error': gettext('Order not found. Please try again.')}, status=500)
        messages.error(request, _('Order not found. Please try again.'))
        return redirect(reverse('storefront:checkout'))

    # ADR-020 D7: branch on the connector's approval_flow capability, never on
    # processor_type — checkout_pay_post has no idea (and must not care) which
    # concrete connector produced this result. Default 'sdk' keeps any caller
    # that predates this key (or a future connector that forgets to set it)
    # on the existing Stripe-shaped behavior below.
    if result.get('approval_flow', 'sdk') == 'redirect':
        # PayPal: client_secret carries the buyer approval URL — server-generated
        # by the connector from PayPal's own order-creation response, never from
        # client input, so redirecting straight to it has no open-redirect surface.
        # AJAX: checkout.js already does `window.location.href = data.redirect_url`
        # for the zero-total/duplicate-checkout paths above — zero JS change needed.
        if _is_ajax(request):
            return JsonResponse({'redirect_url': result['client_secret']})
        return redirect(result['client_secret'])

    # AJAX path: return client_secret + return_url so checkout.js can mount
    # Stripe Elements and call stripe.confirmPayment() (2-step flow, ADR-015 §6).
    if _is_ajax(request):
        return JsonResponse({
            'client_secret': result['client_secret'],
            'return_url': request.build_absolute_uri(
                reverse('storefront:checkout-payment-return')
            ),
        })

    # Non-AJAX fallback: render payment confirmation page with client_secret
    # (progressive-enhancement; keeps the old checkout_payment.html path alive).
    # publishable_key is passed so checkout.js can initialize Stripe Elements
    # on page load using the pre-loaded data-client-secret (non-AJAX path).
    return TemplateResponse(
        request,
        'storefront/pages/checkout_payment.html',
        {
            'order': order,
            'client_secret': result['client_secret'],
            'processor_type': result['processor_type'],
            'payment_return_url': request.build_absolute_uri(
                reverse('storefront:checkout-payment-return')
            ),
            'publishable_key': _get_stripe_publishable_key(store),
            'cart': cart,
        },
    )


# ---------------------------------------------------------------------------
# Payment return — GET /checkout/payment/return/
# ---------------------------------------------------------------------------

@require_GET
@activate_checkout_language
def checkout_payment_return(request):
    """
    3DS redirect return and post-confirm PI status verification (ADR-015 §6, PY-012).

    Stripe redirects here after 3DS or direct-confirm with:
      ?payment_intent=pi_xxx&redirect_status=succeeded|failed|...

    Server-side: retrieve PI status from the processor (never trust query params).
    succeeded / requires_capture → 302 → thank-you.
    failed / requires_payment_method → 302 → /checkout/ with decline message (PY-013).

    The success/decline tail (everything once `order` is resolved) is shared with
    checkout_paypal_return via _finalize_payment_return (ADR-020 D1) — extracted
    statement-for-statement so Stripe's behavior here is bit-identical to before
    the extraction (see storefront/tests/test_checkout.py CheckoutPaymentReturnTest).
    """
    store = request.store
    payment_intent_id = request.GET.get('payment_intent', '')
    redirect_status = request.GET.get('redirect_status', '')

    if not payment_intent_id:
        return redirect(reverse('storefront:checkout'))

    # Load order by PI id, scoped to this store (security: never trust PI id from
    # another store's order; store isolation enforced by for_store).
    try:
        order = Order.objects.for_store(store).get(
            processor_payment_intent_id=payment_intent_id
        )
    except Order.DoesNotExist:
        return redirect(reverse('storefront:checkout'))

    # Skip the processor round-trip for an order already in a terminal state —
    # _finalize_payment_return's own PAID/CANCELLED guards make pi_status moot,
    # and avoiding the call here preserves the pre-ADR-020 behavior exactly
    # (no processor round-trip for a back-button/voided return).
    if order.payment_status in (PaymentStatus.PAID, PaymentStatus.CANCELLED):
        pi_status = ''
    else:
        pi_status = _retrieve_pi_status(order)

    return _finalize_payment_return(request, order, pi_status, redirect_status)


def _finalize_payment_return(request, order, pi_status: str, fallback_status: str = ''):
    """
    Shared success/decline tail for checkout_payment_return AND checkout_paypal_return
    (ADR-020 D1/D3) — extracted verbatim from checkout_payment_return.

    pi_status must already be resolved by the caller (via _retrieve_pi_status for
    Stripe, or payments.paypal_service.ensure_authorized for PayPal). Passing ''
    is always safe, including for an order whose payment_status is already PAID
    or CANCELLED — the guards below only look at order.payment_status first.

    fallback_status: the raw redirect_status query param (Stripe only) — used as
    the decline-message key only when pi_status itself isn't a decline status
    checkout_paypal_return has no equivalent and passes ''.
    """
    store = request.store

    # Back-button from thank-you (EC-007): if the order is already PAID the webhook
    # has fired; skip the processor round-trip and redirect directly to thank-you.
    if order.payment_status == PaymentStatus.PAID:
        token = make_thank_you_token(order.pk)
        return redirect(reverse('storefront:order-thank-you', args=[token]))

    # LOW-2 (CHECKOUT_BATCH_2_AUDIT.md): the order may have been voided (GC task
    # or fast-path void) between PI confirmation and this return — e.g. the void
    # ran, the best-effort PI cancel failed because the PI had already succeeded
    # at the processor, and the shopper's browser now returns here. Stamping
    # authorized_at / showing thank-you for a CANCELLED order would let a voided
    # checkout look confirmed; send the shopper back to the cart instead.
    if order.payment_status == PaymentStatus.CANCELLED:
        messages.error(request, _('This checkout has expired. Please try again.'))
        return redirect(reverse('storefront:cart'))

    if pi_status in ('succeeded', 'requires_capture', 'processing'):
        # Record authorization time — PAID status is set exclusively by the
        # payment_intent.succeeded webhook (PY-003/PY-010, ADR-015 §6) or, for
        # PayPal, the PAYMENT.CAPTURE.COMPLETED webhook.
        # Stamping PAID here at authorization is wrong for requires_capture
        # (manual-capture flow: money not yet moved) and creates a race for
        # succeeded (webhook may not have fired yet; the idempotent guard in
        # _handle_payment_intent_succeeded handles the concurrent case).
        # The thank-you page handles the still-PENDING state gracefully.
        Order.objects.for_store(store).filter(
            pk=order.pk,
            authorized_at__isnull=True,
        ).update(authorized_at=timezone.now())
        # Advance CheckoutState to CONFIRMED: payment authorized — the
        # shopper must not be able to re-enter the checkout flow for this cart.
        # LOW-1 (CHECKOUT_BATCH_2_AUDIT.md): stamp updated_at explicitly — .update()
        # bypasses auto_now, see note in checkout_pay_post.
        CheckoutState.objects.for_store(store).filter(
            order_id=order.pk,
            step=CheckoutStep.PAYMENT,
        ).update(step=CheckoutStep.CONFIRMED, updated_at=timezone.now())
        # 20:P1 (DECIDED 2026-07-10): orders with no funnel campaign are captured
        # right here — the buyer-return path, shared by Stripe and PayPal — instead
        # of waiting for capture_window_watchdog to reach capture_window_expires_at.
        # Idempotent and never raises: safe even on a back-button re-entry or a
        # race with the async webhook fallback below.
        if order.capture_immediately:
            from campaigns.tasks import capture_original_charge_now
            capture_original_charge_now(order)
        token = make_thank_you_token(order.pk)
        return redirect(reverse('storefront:order-thank-you', args=[token]))

    # Payment failed or declined.
    decline_message = _map_decline_status(fallback_status or pi_status)
    messages.error(request, decline_message)
    return redirect(reverse('storefront:checkout'))


# ---------------------------------------------------------------------------
# PayPal return — GET /checkout/paypal/return/
# ---------------------------------------------------------------------------

@require_GET
def checkout_paypal_return(request):
    """
    PayPal buyer-approval return (ADR-020 D1/D3).

    PayPal redirects here after the buyer approves the order on PayPal's site:
      ?token=<paypal_order_id>&PayerID=<payer_id>

    token is the PayPal order id, stored as Order.processor_payment_intent_id at
    checkout_pay_post time — resolved exactly like Stripe's ?payment_intent=
    (bearer-style identifier; PayerID is never read — nothing from the query
    string is trusted for money state, only used to look up `order`).

    Missing/unknown token, or a token belonging to another store's order → 302
    /checkout/, no processor call, no PII leak (same posture as
    checkout_payment_return for an unresolved payment_intent).

    Authorizes the order via payments.paypal_service.ensure_authorized — the same
    function the CHECKOUT.ORDER.APPROVED webhook calls as a fallback for a buyer
    who approves then closes the tab — then shares checkout_payment_return's
    success/decline tail via _finalize_payment_return (ADR-020 D1).
    """
    store = request.store
    token = request.GET.get('token', '')

    if not token:
        return redirect(reverse('storefront:checkout'))

    try:
        order = Order.objects.for_store(store).get(processor_payment_intent_id=token)
    except Order.DoesNotExist:
        return redirect(reverse('storefront:checkout'))

    # Skip the authorize call for an order already in a terminal state — mirrors
    # checkout_payment_return's guard above and avoids issuing a PayPal authorize
    # call against an order this site has already paid or voided.
    if order.payment_status in (PaymentStatus.PAID, PaymentStatus.CANCELLED):
        status = ''
    else:
        from payments.paypal_service import ensure_authorized
        status = ensure_authorized(order)

    return _finalize_payment_return(request, order, status)


# ---------------------------------------------------------------------------
# PayPal cancel — GET /checkout/paypal/cancel/
# ---------------------------------------------------------------------------

@require_GET
def checkout_paypal_cancel(request):
    """
    PayPal buyer-cancel return (ADR-020 D6).

    PayPal redirects here when the buyer cancels approval on PayPal's site:
      ?token=<paypal_order_id>

    Missing/unknown token, or a token belonging to another store's order → plain
    302 /checkout/, no message (nothing to report — same posture as an unresolved
    return token).

    Leaves everything as-is by design: CheckoutState.step stays PAYMENT (the
    existing decline-retry path in _cart_or_redirect re-opens checkout), and the
    order stays PENDING with no authorization (no funds held) — the existing
    fast-path void in checkout_pay_post (or the GC task) reclaims it on the next
    submit. No new state, no new cleanup path (§XV-4).
    """
    store = request.store
    token = request.GET.get('token', '')

    if not token:
        return redirect(reverse('storefront:checkout'))

    try:
        Order.objects.for_store(store).get(processor_payment_intent_id=token)
    except Order.DoesNotExist:
        return redirect(reverse('storefront:checkout'))

    messages.error(
        request,
        _(
            'Your PayPal payment was cancelled. You have not been charged — '
            'you can try again or choose another payment method.'
        ),
    )
    return redirect(reverse('storefront:checkout'))


def _retrieve_pi_status(order) -> str:
    """
    Retrieve the raw PaymentIntent status from the processor for checkout_retry.

    Uses retrieve_payment_intent_raw_status() (not retrieve_payment_intent_status())
    so that Stripe-native statuses like 'requires_payment_method' reach the caller
    intact. retrieve_payment_intent_status() maps everything to 'authorized'/'captured'
    for the capture watchdog — that mapping is wrong for the retry path.
    Returns '' on missing IDs or any connector error (checkout_retry treats '' as retryable).
    """
    if not order.processor_payment_intent_id or order.processor_account_id is None:
        return ''
    try:
        from payments.factory import get_connector
        connector = get_connector(order.processor_account)
        return connector.retrieve_payment_intent_raw_status(order.processor_payment_intent_id)
    except Exception:
        logger.exception(
            "checkout_retry: failed to retrieve raw PI status %s for order %s",
            order.processor_payment_intent_id,
            order.pk,
        )
        return ''


def _retrieve_pi_client_secret(order) -> str:
    """
    Retrieve the client_secret for an existing PaymentIntent via the connector.

    Used by checkout_retry (PY-013 / EC-007) so the shopper can retry with a new
    card on the same PI without creating a new intent.

    Returns '' when the PI ID is absent, the processor account is not set, or any
    error occurs — checkout_retry degrades gracefully with an empty client_secret.
    """
    if not order.processor_payment_intent_id or order.processor_account_id is None:
        return ''
    try:
        from payments.factory import get_connector
        connector = get_connector(order.processor_account)
        return connector.retrieve_payment_intent_client_secret(
            order.processor_payment_intent_id
        )
    except Exception:
        logger.exception(
            "checkout_retry: failed to retrieve PI client_secret %s for order %s",
            order.processor_payment_intent_id,
            order.pk,
        )
        return ''


def _map_decline_status(status: str) -> str:
    """Map a Stripe redirect_status or PI status to a user-visible decline message (PY-013)."""
    _DECLINE_MESSAGES = {
        'requires_payment_method': _('Your payment was not accepted. Please try a different card.'),
        'failed': _('Your payment was declined. Please try a different card.'),
        'canceled': _('Your payment was cancelled.'),
    }
    return _DECLINE_MESSAGES.get(
        status,
        _('Your payment could not be processed. Please try again.'),
    )


def _build_payment_summary(order) -> str:
    """
    Build a human-readable payment method summary for the thank-you page (TH-002).

    Uses only stored data — no processor API calls.
    Returns '' when no processor account is set (graceful degradation).

    Mapping:
    - PayPal → "PayPal"
    - Stripe + card_last4 → "card ending in {last4}"
    - Stripe, no last4 → "Stripe"
    - Other → processor_type display name
    """
    processor_account = order.processor_account
    if processor_account is None:
        return ''
    try:
        from payments.models import ProcessorType
        if processor_account.processor_type == ProcessorType.PAYPAL:
            return _('PayPal')
        if processor_account.processor_type == ProcessorType.STRIPE:
            if order.card_last4:
                return _('card ending in %(last4)s') % {'last4': order.card_last4}
            return _('Stripe')
        return processor_account.get_processor_type_display()
    except Exception:
        logger.exception(
            'order_thank_you: failed to build payment summary for order %s', order.pk
        )
        return ''


def _build_discount_lines(order, store) -> list:
    """
    Build per-code discount line dicts for the thank-you page (TH-002).

    Returns a list of {'code': str, 'amount': Decimal} dicts.
    Coupon is listed first, gift card second (spec §3 apply order).

    Gift card spend is determined via GiftCardTransaction rows linked to this order.
    Coupon amount = order.discount_amount minus gift card spend.

    Gracefully returns an empty list on any error.
    """
    from decimal import Decimal
    discount_lines = []

    # Gift card spend for this specific order (via the GiftCardTransaction ledger).
    gc_amount = Decimal('0.00')
    if order.gift_card_id is not None:
        try:
            from django.db.models import Sum
            from discounts.models import GiftCardTransaction
            result = (
                GiftCardTransaction.objects
                .for_store(store)
                .filter(order=order, gift_card_id=order.gift_card_id)
                .aggregate(total=Sum('amount'))
            )
            gc_amount = result['total'] or Decimal('0.00')
        except Exception:
            logger.exception(
                'order_thank_you: failed to load gift card transactions for order %s', order.pk
            )

    # Coupon line: total discount_amount minus the gift card spend.
    if order.discount_code_id is not None:
        coupon_amount = order.discount_amount - gc_amount
        if coupon_amount > 0:
            discount_lines.append({
                'code': order.discount_code.code,
                'amount': coupon_amount,
            })

    # Gift card line (positive spend amounts only).
    if order.gift_card_id is not None and gc_amount > 0:
        discount_lines.append({
            'code': order.gift_card.code,
            'amount': gc_amount,
        })

    return discount_lines


# ---------------------------------------------------------------------------
# Retry view — GET /checkout/retry/<signed_token>/
# ---------------------------------------------------------------------------

@require_GET
@activate_checkout_language
def checkout_retry(request, signed_token: str):
    """
    Failed-payment retry view (ADR-015 §4, U16-2).

    Validates the signed token, loads the Order, and renders the payment section
    with the existing PI's client_secret so the shopper can retry with a new card.

    Token is invalid / expired → 404 (no PII leak).
    Order already paid → redirect to thank-you.
    Order cancelled / GC-voided → render expired page.
    Order PENDING → render retry payment form.
    """
    store = request.store

    # Validate signed token.
    try:
        data = read_retry_token(signed_token)
        order_id = data['order_id']
    except (signing.BadSignature, signing.SignatureExpired, KeyError):
        raise Http404("Invalid or expired checkout retry link.")

    # Load order scoped to this store (store isolation check).
    try:
        order = Order.objects.for_store(store).get(pk=order_id)
    except Order.DoesNotExist:
        raise Http404("Order not found.")

    # Already paid: redirect to thank-you.
    if order.payment_status == PaymentStatus.PAID:
        token = make_thank_you_token(order.pk)
        return redirect(reverse('storefront:order-thank-you', args=[token]))

    # Voided / cancelled: show friendly expired page.
    if order.payment_status == PaymentStatus.CANCELLED:
        return TemplateResponse(
            request,
            'storefront/pages/checkout_retry_expired.html',
            {'order': order},
        )

    # Retrieve current PI status to determine if we can re-use the existing PI.
    pi_status = _retrieve_pi_status(order)
    client_secret = ''

    if pi_status in ('requires_payment_method', 'requires_confirmation', ''):
        # PI is re-usable — Stripe allows retrying the same intent with a new card.
        # Retrieve client_secret via the connector (PY-013, EC-007).
        # Degrades gracefully to '' if the connector call fails.
        client_secret = _retrieve_pi_client_secret(order)
        logger.info(
            "checkout_retry: order %s PI %s status=%s — retry available",
            order.pk,
            order.processor_payment_intent_id,
            pi_status,
        )
    else:
        # Unexpected PI state — show expired page for safety.
        return TemplateResponse(
            request,
            'storefront/pages/checkout_retry_expired.html',
            {'order': order, 'pi_status': pi_status},
        )

    return TemplateResponse(
        request,
        'storefront/pages/checkout_retry.html',
        {
            'order': order,
            'client_secret': client_secret,
            'processor_type': order.processor_account.processor_type if order.processor_account_id else '',
            'payment_return_url': request.build_absolute_uri(
                reverse('storefront:checkout-payment-return')
            ),
        },
    )


# ---------------------------------------------------------------------------
# Discount code apply — POST /checkout/discount/apply/
# ---------------------------------------------------------------------------

@require_POST
def apply_discount_view(request):
    """
    Apply a discount code (coupon or gift card) to the current cart (PY-020/PY-021).

    Cart-level application is advisory: the DiscountCode FK is set on the cart and
    re-validated atomically inside begin_checkout (ADR-002 §4).  This view does NOT
    increment times_used — that happens at order finalisation.

    On success: redirect to `next` (defaults to /checkout/).
    On error: flash a message and redirect to `next`.
    """
    from discounts.models import DiscountCode, DiscountType

    store = request.store
    # PY-020 hardening: `next` is untrusted client input — validate host/scheme
    # before ever passing it to redirect() (open-redirect fix).
    next_url = _safe_next_url(request, request.POST.get('next', ''))
    code_str = request.POST.get('code', '').strip().upper()

    cart = _get_cart(request)
    if cart is None:
        return redirect(next_url)

    if not code_str:
        return redirect(next_url)

    # Look up the code, scoped to this store (store isolation).
    try:
        code_obj = DiscountCode.objects.for_store(store).get(code=code_str)
    except DiscountCode.DoesNotExist:
        messages.error(request, _('Invalid discount code.'))
        return redirect(next_url)

    if not code_obj.is_valid_now():
        messages.error(request, _('This discount code is no longer valid.'))
        return redirect(next_url)

    # Set the advisory FK on the cart (gift card → cart.gift_card; coupon → cart.discount_code).
    # Applying a coupon clears any previous coupon but leaves an existing gift card untouched.
    if code_obj.discount_type == DiscountType.COUPON:
        Cart.objects.for_store(store).filter(pk=cart.pk).update(discount_code=code_obj)
    else:
        # PY-021: only one gift card per order (UF-H) — reject a second one
        # outright rather than silently replacing the first.
        if cart.gift_card_id is not None:
            messages.error(request, _('Only one gift card may be applied per order.'))
            return redirect(next_url)
        Cart.objects.for_store(store).filter(pk=cart.pk).update(gift_card=code_obj)

    return redirect(next_url)


# ---------------------------------------------------------------------------
# Discount code removal — POST /checkout/discount/remove/
# ---------------------------------------------------------------------------

@require_POST
def remove_discount_view(request):
    """
    Remove an applied coupon or gift card from the cart (CK-021 chip removal).

    POST /checkout/discount/remove/ with kind=coupon|gift_card. Clears the
    matching FK on the cart and redirects back to checkout. No `next` param —
    this is a fixed sidebar action, unlike apply_discount_view. An unknown or
    missing cart/kind is a no-op redirect (defensive — never 500s on a
    malformed or replayed form).
    """
    store = request.store
    kind = request.POST.get('kind', '').strip()

    cart = _get_cart(request)
    if cart is not None:
        if kind == 'coupon':
            Cart.objects.for_store(store).filter(pk=cart.pk).update(discount_code=None)
        elif kind == 'gift_card':
            Cart.objects.for_store(store).filter(pk=cart.pk).update(gift_card=None)

    return redirect(reverse('storefront:checkout'))


# ---------------------------------------------------------------------------
# Thank-you page — GET /orders/<public_order_id>/thank-you/
# ---------------------------------------------------------------------------


def _resolve_consent_allowed_categories(request) -> frozenset:
    """
    ADR-025 D3/D0: the set of consent categories currently allowed for this
    shopper, used to gate claim_purchase_pixels()'s claims (not just the
    render — see that function's docstring for why).

    Routed through consent.state.resolve_consent_for_pixels() rather than
    get_consent() directly (RM-1 fix, KNOWN_RISKS.md item 1 / ADR-025 D3
    dated correction 2026-07-11) — the same single resolution point
    pixels/slot_provider.py's render-path gate uses, so a store with
    consent disabled AND the non-EU acknowledgment signed (D6) has ALL
    categories allowed here too, instead of the claim silently and
    permanently never firing (the cookie never gets set on that store, so
    get_consent() alone would resolve UNDECIDED forever). See
    resolve_consent_for_pixels()'s docstring for the exact resolution rule.

    Imported locally (mirrors pixels/slot_provider.py's own local import) to
    avoid an app-loading-order import cycle, and wrapped in try/except
    ImportError per the ADR-025 "Rollback strategy" fail-closed shim: if the
    `consent` app/package is ever removed entirely, this falls back to an
    empty frozenset (no categories allowed) rather than raising or silently
    claiming everything — a botched rollback must dark-launch pixels, never
    fire them unconsented.
    """
    try:
        from consent.state import resolve_consent_for_pixels
    except ImportError:
        return frozenset()
    consent = resolve_consent_for_pixels(request)
    return frozenset(category for category in ("analytics", "marketing") if consent.allows(category))


@require_GET
@activate_checkout_language
def order_thank_you(request, public_order_id: str):
    """
    Order confirmation / thank-you page (ADR-015 §6, spec TH-001/TH-002).

    public_order_id is a signed token (salt='order-thankyou') encoding the order PK.
    Tampered token → 404 (no PII leak).
    Store mismatch → 404 (isolation — store A token cannot access store B order).

    Only ACTIVE OrderItems are shown (PENDING_CAPTURE items excluded until promoted
    by the webhook, per ADR-011 Q6 / ADR-007 §6 provisional invisibility).
    """
    store = request.store

    # Validate signed token.
    try:
        data = read_thank_you_token(public_order_id)
        order_id = data['order_id']
    except (signing.BadSignature, KeyError):
        raise Http404("Invalid order link.")

    # Load order scoped to this store (store isolation — for_store enforces the store FK).
    # select_related covers processor_account (payment summary), discount_code and
    # gift_card (per-code discount lines) to avoid N+1 queries (TH-002).
    # OrderItem is a StoreOwnedModel: the prefetch must use a store-scoped queryset
    # (ADR-001 §4) so the related manager's _RaisingQuerySet is never invoked directly.
    order = get_object_or_404(
        Order.objects.for_store(store).select_related(
            'processor_account',
            'discount_code',
            'gift_card',
        ).prefetch_related(
            Prefetch('items', queryset=OrderItem.objects.for_store(store))
        ),
        pk=order_id,
    )

    # Only show confirmed items (ACTIVE); provisional upsell items stay invisible
    # until the capture webhook promotes them (ADR-007 §6, ADR-011 Q6).
    # order.items.all() uses the prefetch cache populated above — no second DB query.
    active_items = [
        item for item in order.items.all()
        if item.status == OrderItemStatus.ACTIVE
    ]

    # ---------------------------------------------------------------------------
    # Upsell widget context (spec TH/T029 Phase 3 — ADR-011 Addendum 2026-07-06)
    #
    # Issue a fresh single-use token at render time (Option B — approved).
    # Multiple live tokens per session coexist safely; whoever POSTs first wins.
    # serve_thank_you_step degrades gracefully — returns None on any error so
    # this page never 500s because of the funnel.
    # ---------------------------------------------------------------------------
    from campaigns.service import serve_next_order_offer, serve_thank_you_step

    upsell_ctx = serve_thank_you_step(order, store)
    upsell_session = upsell_ctx["session"] if upsell_ctx else None
    upsell_step = upsell_ctx["step"] if upsell_ctx else None
    upsell_token = upsell_ctx["token"] if upsell_ctx else None

    # Storewide next-order discount offer (TH-006 / TICKET-027 item 3).
    # serve_next_order_offer degrades gracefully — returns None on any error so
    # this page never 500s because of the campaign.
    next_order_offer = serve_next_order_offer(order, store)

    # Purchase-pixel idempotency (ADR-022 D6, replacing the ADR-015 §8 Phase 4
    # '__purchase_guard__' sentinel — retired by orders/migrations/
    # 0013_retire_purchase_guard_sentinel.py). claim_purchase_pixels() inserts
    # one FiredPixel row per (order, installed+active provider, 'purchase');
    # the unique_together(order, pixel_type, event) constraint makes
    # concurrent thank-you reloads safe, exactly like the old sentinel did.
    #
    # PAID gate here is defense in depth (security audit MEDIUM-1,
    # CHECKOUT_BATCH_2_AUDIT.md): the `processing` redirect path in
    # checkout_payment_return sends shoppers here before the payment webhook
    # lands, so a PENDING render must never consume a claim — otherwise it is
    # gone by the time the order later becomes PAID and the pixel never
    # fires. claim_purchase_pixels() re-checks PAID internally too.
    #
    # purchase_pixel_providers is the explicit ADR-022 D5 context key:
    # PixelsSlotProvider renders a provider's Purchase snippet only when that
    # provider's key is in this set (created=True this render — reload/back
    # button/leaked-link revisits render nothing, AC-110).
    purchase_pixel_providers = set()
    pixel_events = []
    if order.payment_status == PaymentStatus.PAID:
        from pixels.events import build_purchase_payload
        from pixels.service import claim_purchase_pixels

        # ADR-025 D0/D3: the claim itself is consent-gated, not only the
        # render — see claim_purchase_pixels()'s docstring for why gating
        # only PixelsSlotProvider.render() would silently burn the claim.
        allowed_categories = _resolve_consent_allowed_categories(request)
        purchase_pixel_providers = claim_purchase_pixels(
            store, order, allowed_categories=allowed_categories,
        )
        pixel_events.append(('purchase', build_purchase_payload(order)))

    # PIX:P1 (human decision 2026-07-11): the thank-you page's real URL
    # contains public_order_id, a never-expiring signed token (read_thank_you_token
    # above). Sending that URL to ad platforms via the pixels' automatic
    # page_view would ship the token to every configured provider forever.
    # page_url_override is a normalized, tokenless, absolute URL consumed by
    # PixelsSlotProvider.render() (pixels/slot_provider.py) and passed to
    # PixelProvider.render_base() (pixels/registry.py) — only the GA4
    # provider currently has a supported override for the automatically-
    # collected page URL (gtag's page_location); see docs/adr/ADR-022-pixel-
    # integrations.md for the honest residual-exposure statement covering the
    # other four providers, which still read document.location.href verbatim.
    page_url_override = request.build_absolute_uri('/orders/thank-you/')

    return TemplateResponse(
        request,
        'storefront/pages/thank_you.html',
        {
            'order': order,
            'page_url_override': page_url_override,
            'items': active_items,
            'payment_summary': _build_payment_summary(order),
            'discount_lines': _build_discount_lines(order, store),
            'upsell_session': upsell_session,
            'upsell_step': upsell_step,
            'upsell_token': upsell_token,
            'next_order_offer': next_order_offer,
            'pixel_events': pixel_events,
            'purchase_pixel_providers': purchase_pixel_providers,
        },
    )
