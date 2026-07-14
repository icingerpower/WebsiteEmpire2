"""
Static page rendering + contact/quotation form handling (ADR-018 D1/D3/D4, T029-SP).

static_page_view is called by permalinks.registry.resolve_storefront_path (the
'staticpage' branch) after the Permalink lookup has already resolved the object —
exactly the same call shape as product_page / collection_page in storefront/views.py.

GET renders the page. POST is accepted ONLY for kind=contact / kind=quotation
(D3/D4) — other kinds return 405. Forms POST to the page's own URL through the
catch-all (no new fixed routes, no-JS friendly) and PRG-redirect to '?sent=1' on
success (design-pattern-ideas.txt: no invisible failures — a plain reload must never
silently resubmit).

Anti-spam (pages/antispam.py, ADR-018 D3): honeypot silently succeeds (never reveals
detection to the bot); rate limit re-renders the page with a translated error at
HTTP 429.
"""

import logging

from django.core.exceptions import ValidationError
from django.core.validators import validate_email
from django.http import Http404, HttpResponseNotAllowed, HttpResponseRedirect, JsonResponse
from django.template.response import TemplateResponse
from django.utils.translation import gettext as _

from catalog.models import QuotationRequest, TranslationStatus
from pages.antispam import (
    PUBLIC_FORM_MESSAGE_MAX_LENGTH,
    contact_notification_budget_exceeded,
    honeypot_triggered,
    rate_limit_exceeded,
)
from pages.models import ContactMessage, StaticPageKind, StaticPageTranslation

logger = logging.getLogger(__name__)


def _is_ajax(request) -> bool:
    return request.headers.get("X-Requested-With") == "XMLHttpRequest"


def _get_locale_lang(request) -> str:
    locale = getattr(request, "locale", None)
    if locale is not None:
        lang_obj = getattr(locale, "language", None)
        if lang_obj is not None:
            return getattr(lang_obj, "lang_code", "en")
    return "en"


def _build_storefront_url(request, slug: str) -> str:
    """Mirror storefront.views._build_storefront_url (kept local to avoid a
    views.py <-> views_pages.py import cycle; both derive the same prefix rule)."""
    locale = getattr(request, "locale", None)
    lang_obj = getattr(locale, "language", None) if locale else None
    if lang_obj is not None and getattr(lang_obj, "use_path_prefix", False):
        lang_code = getattr(lang_obj, "lang_code", "")
        if lang_code:
            return f"/{lang_code}/{slug}/" if slug else f"/{lang_code}/"
    return f"/{slug}/" if slug else "/"


def _notify_full_access_employees(store, template_id, context):
    """
    Send a transactional email to every active, full-access StoreEmployee for the
    store (ADR-018 D3, DECIDED 18:P1: all full-access employees — no per-store
    "notification email" setting in v1).

    Never raises: send_transactional_email() itself never raises, and a missing
    employee list is a no-op, not an error — the form must still succeed.

    Security audit F4: gated by a per-store daily notification budget, independent
    of the per-IP rate limit in views — a botnet spread across many source IPs
    would otherwise still amplify ×(active full-access employees) per submission
    with no upper bound. Beyond the cap, the caller's row (ContactMessage) is
    already saved — only this notification send is skipped.
    """
    from emails.service import send_transactional_email
    from stores.models import StoreEmployee

    if contact_notification_budget_exceeded(store):
        logger.info(
            "Contact notification budget exceeded for store id=%s; skipping "
            "employee notification for this submission (message was still saved).",
            getattr(store, "pk", None),
        )
        return

    recipients = list(
        StoreEmployee.objects.filter(
            store=store, full_access=True, is_active=True,
        ).select_related("user").values_list("user__email", flat=True)
    )
    for email in recipients:
        if email:
            send_transactional_email(
                template_id=template_id,
                recipient=email,
                context=context,
                store=store,
            )


def static_page_view(request, page):
    """
    Render a StaticPage (GET) or handle its contact/quotation POST (ADR-018 D1).

    Args:
        page: a StaticPage instance already scoped to the current store.

    Raises Http404 when:
      - The page is not published (defensive; permalink should already be inactive).
      - The request language is not the store default AND no published
        StaticPageTranslation exists for it (ML-002 parity with product_page).

    ML-020 (SEO review M1): a non-default-language render NEVER falls back to the
    default-language text for title/body/seo_title/seo_description. A published
    translation with an empty field renders that field empty rather than serving
    mixed-language content on the translated URL — falling back would be exactly
    the duplicate/thin-content, invisible-failure pattern ML-020 forbids.
    """
    store = getattr(request, "store", None)
    lang = _get_locale_lang(request)

    if not page.is_published:
        raise Http404(f"Static page {page.slug!r} is not published.")

    is_default_lang = lang == getattr(store, "primary_language", None)
    translation = None
    if is_default_lang:
        title = page.title
        body = page.body
        seo_title = page.seo_title
        seo_description = page.seo_description
    else:
        try:
            translation = StaticPageTranslation.objects.for_store(store).get(
                page=page, lang_code=lang, status=TranslationStatus.PUBLISHED,
            )
        except StaticPageTranslation.DoesNotExist:
            raise Http404(
                f"No published translation for static page {page.slug!r} in lang {lang!r}."
            )
        # ML-020: no default-language fallback for indexable text — an empty
        # translated field renders empty, it never serves page.* text on the
        # translated URL (SEO review M1).
        title = translation.title
        body = translation.body
        seo_title = translation.seo_title
        seo_description = translation.seo_description

    if request.method == "POST":
        if page.kind == StaticPageKind.CONTACT:
            return _handle_contact_post(request, page, lang, title, body, seo_title, seo_description)
        if page.kind == StaticPageKind.QUOTATION:
            return _handle_quotation_post(request, page, lang, title, body, seo_title, seo_description)
        return HttpResponseNotAllowed(["GET"])

    return _render_static_page(request, page, title, body, seo_title, seo_description)


def _render_static_page(request, page, title, body, seo_title, seo_description, form_error=None, status=200):
    breadcrumbs = [
        # SEO review L6: "Home" is visible indexable text — must be translated,
        # not hardcoded English, on non-default-language renders (ML-020 in spirit).
        {"label": _("Home"), "url": _build_storefront_url(request, "")},
        {"label": title, "url": None},
    ]
    return TemplateResponse(
        request,
        "storefront/pages/static_page.html",
        {
            "page": page,
            "title": title,
            "body": body,
            "seo_title": seo_title,
            "seo_description": seo_description,
            "breadcrumbs": breadcrumbs,
            "sent": request.GET.get("sent") == "1",
            "form_error": form_error,
            # content_object: allows base.html {% hreflang_tags %} / {% seo_head %} to
            # render — and to honour StaticPage.hreflang_exempt for policy pages (§XI).
            "content_object": page,
            # page_type: used by analytics_beacon tag (TH-043).
            "page_type": "static_page",
        },
        status=status,
    )


def _handle_contact_post(request, page, lang, title, body, seo_title, seo_description):
    """POST handler for kind=contact pages (ADR-018 D3)."""
    if honeypot_triggered(request):
        # Never reveal detection to the bot — behave exactly like a normal success.
        return HttpResponseRedirect(request.path + "?sent=1")

    if rate_limit_exceeded(request, scope="contact"):
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": _("Too many requests. Please try again later.")}, status=429)
        return _render_static_page(
            request, page, title, body, seo_title, seo_description,
            form_error=_("Too many requests. Please try again later."), status=429,
        )

    name = request.POST.get("name", "").strip()
    email = request.POST.get("email", "").strip()
    subject = request.POST.get("subject", "").strip()
    message = request.POST.get("message", "").strip()

    errors = []
    if not email:
        errors.append(_("Email is required."))
    else:
        try:
            validate_email(email)
        except ValidationError:
            errors.append(_("Invalid email address."))
    if not message:
        errors.append(_("Message is required."))

    # Security audit F2: cap field lengths BEFORE .objects.create(). An over-length
    # name/subject silently truncates on SQLite (dev) but raises an unhandled
    # DataError (HTTP 500) on the production PostgreSQL backend — checked against
    # the model's own field.max_length so this can never drift from the schema.
    # `message` is a TextField with no DB-level cap at all, so it is bounded
    # against the shared PUBLIC_FORM_MESSAGE_MAX_LENGTH instead (also limits the
    # email fan-out amplifier, F4).
    name_max = ContactMessage._meta.get_field("name").max_length
    subject_max = ContactMessage._meta.get_field("subject").max_length
    if len(name) > name_max:
        errors.append(_("Name is too long (max %(max)d characters).") % {"max": name_max})
    if len(subject) > subject_max:
        errors.append(_("Subject is too long (max %(max)d characters).") % {"max": subject_max})
    if len(message) > PUBLIC_FORM_MESSAGE_MAX_LENGTH:
        errors.append(
            _("Message is too long (max %(max)d characters).")
            % {"max": PUBLIC_FORM_MESSAGE_MAX_LENGTH}
        )

    if errors:
        error_text = " ".join(str(e) for e in errors)
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": error_text}, status=400)
        return _render_static_page(
            request, page, title, body, seo_title, seo_description,
            form_error=error_text, status=400,
        )

    store = request.store

    contact_message = ContactMessage.objects.create(
        store=store,
        page=page,
        name=name,
        email=email,
        subject=subject,
        message=message,
        lang_code=lang,
    )

    _notify_full_access_employees(
        store,
        "contact_message_received",
        {"contact_message": contact_message},
    )

    if _is_ajax(request):
        return JsonResponse({"ok": True})
    return HttpResponseRedirect(request.path + "?sent=1")


def _handle_quotation_post(request, page, lang, title, body, seo_title, seo_description):
    """
    POST handler for kind=quotation pages — general (non-product) quotation
    (ADR-018 D4).

    Security audit F8: uses scope="quotation_general", NOT "quotation" — that
    scope belongs to the product-anchored quotation form
    (storefront/views_product_forms.py quotation_request_view). Before this fix
    both forms shared one (store, IP) counter, so submissions to the store-level
    quotation page and to any product's quotation form drew down the same budget.
    Harmless (only ever stricter) but not the intended independent-budget design,
    and worth keeping distinct so a future change to one limit doesn't silently
    affect the other.
    """
    if honeypot_triggered(request):
        return HttpResponseRedirect(request.path + "?sent=1")

    if rate_limit_exceeded(request, scope="quotation_general"):
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": _("Too many requests. Please try again later.")}, status=429)
        return _render_static_page(
            request, page, title, body, seo_title, seo_description,
            form_error=_("Too many requests. Please try again later."), status=429,
        )

    email = request.POST.get("email", "").strip()
    if not email:
        error_text = _("Email is required.")
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": str(error_text)}, status=400)
        return _render_static_page(
            request, page, title, body, seo_title, seo_description,
            form_error=error_text, status=400,
        )
    try:
        validate_email(email)
    except ValidationError:
        error_text = _("Invalid email address.")
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": str(error_text)}, status=400)
        return _render_static_page(
            request, page, title, body, seo_title, seo_description,
            form_error=error_text, status=400,
        )

    customer_name = request.POST.get("customer_name", "").strip()
    message = request.POST.get("message", "").strip()

    # Security audit F2: same length-cap rationale as _handle_contact_post above.
    customer_name_max = QuotationRequest._meta.get_field("customer_name").max_length
    if len(customer_name) > customer_name_max:
        error_text = _("Name is too long (max %(max)d characters).") % {"max": customer_name_max}
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": str(error_text)}, status=400)
        return _render_static_page(
            request, page, title, body, seo_title, seo_description,
            form_error=error_text, status=400,
        )
    if len(message) > PUBLIC_FORM_MESSAGE_MAX_LENGTH:
        error_text = _("Message is too long (max %(max)d characters).") % {
            "max": PUBLIC_FORM_MESSAGE_MAX_LENGTH
        }
        if _is_ajax(request):
            return JsonResponse({"ok": False, "error": str(error_text)}, status=400)
        return _render_static_page(
            request, page, title, body, seo_title, seo_description,
            form_error=error_text, status=400,
        )

    # Human decision 18:P2 (2026-07-10): quotation-mode products cannot be
    # added to the cart, so quantity is not meaningful at request time. The
    # customer-facing form has no quantity input — always create with the
    # model default (quantity=1). Do not read request.POST for it: the field
    # is intentionally not accepted from the client (mass-assignment guard).
    QuotationRequest.objects.create(
        store=request.store,
        product=None,
        customer_name=customer_name,
        customer_email=email,
        message=message,
    )

    if _is_ajax(request):
        return JsonResponse({"ok": True})
    return HttpResponseRedirect(request.path + "?sent=1")
