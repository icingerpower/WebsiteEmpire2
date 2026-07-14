"""
Storefront views for back-in-stock notifications and quotation requests
(T029 TH-082).

Both endpoints are POST-only, CSRF-required, store-scoped.  They return JSON
for XMLHttpRequest submissions (progressive enhancement, TH-063) and redirect
to the referring page for plain form POST (non-JS fallback).

Anti-spam retrofit (ADR-018 D3, T029-SP): both endpoints now go through the same
pages.antispam helpers used by the new contact/quotation StaticPage forms — one
function, four call sites (design-pattern-ideas.txt §XV-4), not a forked copy.
Honeypot is not applicable here (these endpoints have no honeypot field in their
existing partials) — only the rate limit is retrofitted.
"""

import logging

from django.core.exceptions import ValidationError
from django.core.validators import validate_email
from django.db import IntegrityError, transaction
from django.http import Http404, HttpResponseBadRequest, HttpResponseNotAllowed, JsonResponse
from django.shortcuts import redirect
from django.utils.translation import gettext as _t

from catalog.models import Product, QuotationRequest, StockNotification
from pages.antispam import PUBLIC_FORM_MESSAGE_MAX_LENGTH, rate_limit_exceeded

logger = logging.getLogger(__name__)


def _is_ajax(request) -> bool:
    """Return True when the request carries the XMLHttpRequest header."""
    return request.headers.get("X-Requested-With") == "XMLHttpRequest"


def _rate_limited_response(request, ajax: bool):
    """
    Shared 429 response for the rate-limit retrofit (ADR-018 D3).

    JSON {"ok": false} 429 for the AJAX path; plain HTTP 429 with a translated
    message for the non-JS fallback.
    """
    message = _t("Too many requests. Please try again later.")
    if ajax:
        return JsonResponse({"ok": False, "error": str(message)}, status=429)
    from django.http import HttpResponse
    return HttpResponse(str(message), status=429)


def notify_me_view(request, product_slug):
    """
    POST-only.  Register customer email for back-in-stock notification.

    Accepts:
        email (str) — customer email address.

    Returns:
        JSON {"ok": true}  on success (AJAX).
        Redirect to referer  on success (non-AJAX).
        HTTP 405  on non-POST.
        HTTP 400  when email is missing or invalid.
        HTTP 404  when the product slug is not found for this store.

    Duplicate registrations (same store/product/email) are silently ignored
    — the endpoint is idempotent.
    """
    if request.method != "POST":
        return HttpResponseNotAllowed(["POST"])

    store = getattr(request, "store", None)
    if store is None:
        return HttpResponseBadRequest("Store not found.")

    if rate_limit_exceeded(request, scope="notify_me"):
        return _rate_limited_response(request, _is_ajax(request))

    try:
        product = Product.objects.for_store(store).get(slug=product_slug)
    except Product.DoesNotExist:
        raise Http404(f"Product {product_slug!r} not found for this store.")

    email = request.POST.get("email", "").strip()
    if not email:
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": "Email is required."}, status=400)
        return HttpResponseBadRequest("Email is required.")

    try:
        validate_email(email)
    except ValidationError:
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": "Invalid email address."}, status=400)
        return HttpResponseBadRequest("Invalid email address.")

    # Security audit F2: validate_email() checks format, not length — a
    # "valid-looking" address longer than the column can still raise an
    # unhandled DataError (HTTP 500) on PostgreSQL. Checked against the
    # model's own field.max_length so it can never drift from the schema.
    email_max = StockNotification._meta.get_field("email").max_length
    if len(email) > email_max:
        error = f"Email is too long (max {email_max} characters)."
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": error}, status=400)
        return HttpResponseBadRequest(error)

    try:
        with transaction.atomic():
            StockNotification.objects.create(store=store, product=product, email=email)
    except IntegrityError:
        pass  # Already registered — silently ignore (idempotent).

    if _is_ajax(request):
        return JsonResponse({"ok": True})
    return redirect(request.META.get("HTTP_REFERER") or "/")


def quotation_request_view(request, product_slug):
    """
    POST-only.  Submit a quotation request for a quote-only product.

    Accepts:
        email         (str)  — customer email address (required).
        customer_name (str)  — customer full name (optional).
        message       (str)  — free-text message (optional).

    Quantity is not a customer-facing field (human decision 18:P2, 2026-07-10):
    quotation-mode products are not cartable, so a requested quantity is not
    meaningful at request time. Any "quantity" POST value is ignored — every
    row is created with the model default (quantity=1).

    Returns:
        JSON {"ok": true}  on success (AJAX).
        Redirect to referer  on success (non-AJAX).
        HTTP 405  on non-POST.
        HTTP 400  when email is missing or invalid.
        HTTP 404  when the product slug is not found for this store.
    """
    if request.method != "POST":
        return HttpResponseNotAllowed(["POST"])

    store = getattr(request, "store", None)
    if store is None:
        return HttpResponseBadRequest("Store not found.")

    if rate_limit_exceeded(request, scope="quotation"):
        return _rate_limited_response(request, _is_ajax(request))

    try:
        product = Product.objects.for_store(store).get(slug=product_slug)
    except Product.DoesNotExist:
        raise Http404(f"Product {product_slug!r} not found for this store.")

    customer_email = request.POST.get("email", "").strip()
    if not customer_email:
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": "Email is required."}, status=400)
        return HttpResponseBadRequest("Email is required.")

    try:
        validate_email(customer_email)
    except ValidationError:
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": "Invalid email address."}, status=400)
        return HttpResponseBadRequest("Invalid email address.")

    # Security audit F2: same rationale as notify_me_view above — validate_email()
    # checks format only, and QuotationRequest.message is an uncapped TextField.
    customer_email_max = QuotationRequest._meta.get_field("customer_email").max_length
    if len(customer_email) > customer_email_max:
        error = f"Email is too long (max {customer_email_max} characters)."
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": error}, status=400)
        return HttpResponseBadRequest(error)

    customer_name = request.POST.get("customer_name", "").strip()
    message = request.POST.get("message", "").strip()

    customer_name_max = QuotationRequest._meta.get_field("customer_name").max_length
    if len(customer_name) > customer_name_max:
        error = f"Name is too long (max {customer_name_max} characters)."
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": error}, status=400)
        return HttpResponseBadRequest(error)
    if len(message) > PUBLIC_FORM_MESSAGE_MAX_LENGTH:
        error = f"Message is too long (max {PUBLIC_FORM_MESSAGE_MAX_LENGTH} characters)."
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": error}, status=400)
        return HttpResponseBadRequest(error)

    # Human decision 18:P2 (2026-07-10): quotation-mode products cannot be
    # added to the cart, so quantity is not meaningful at request time. The
    # customer-facing form has no quantity input — always create with the
    # model default (quantity=1). Do not read request.POST for it: the field
    # is intentionally not accepted from the client (mass-assignment guard).
    QuotationRequest.objects.create(
        store=store,
        product=product,
        customer_name=customer_name,
        customer_email=customer_email,
        message=message,
    )

    if _is_ajax(request):
        return JsonResponse({"ok": True})
    return redirect(request.META.get("HTTP_REFERER") or "/")
