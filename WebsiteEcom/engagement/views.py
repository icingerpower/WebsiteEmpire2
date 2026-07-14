"""
Public storefront views for lead capture (ADR-027 D3, TICKET-034).

Three endpoints, all store-scoped, mounted under /overlay/ in storefront/urls.py
(before the <path:slug> catch-all — "overlay" is in
permalinks.models.RESERVED_TOP_LEVEL_SLUGS):

  POST /overlay/signup/        overlay_signup  — the antispam-gated signup POST.
  POST /overlay/event/         overlay_event   — best-effort impression counter.
  GET  /overlay/confirm/<tok>/ overlay_confirm — double opt-in confirmation link
                                                  (DE-targeting stores, ADR-027 D3
                                                  fallback: "confirmed_at field +
                                                  one confirmation email template +
                                                  one signed-token confirm view").

Social proof (T035) has no public endpoint by design (ADR-027 D7) — nothing here.
"""

import logging

from django.core import signing
from django.core.exceptions import ValidationError
from django.core.validators import validate_email
from django.db import IntegrityError, transaction
from django.db.models import F
from django.http import (
    Http404,
    HttpResponse,
    HttpResponseBadRequest,
    HttpResponseNotAllowed,
    JsonResponse,
)
from django.utils import timezone
from django.utils.translation import gettext as _t
from django.views.decorators.http import require_POST

from pages.antispam import honeypot_triggered, rate_limit_exceeded

from customers.service import get_or_create_customer
from engagement import service
from engagement.models import LeadCaptureCampaign, LeadSignup

logger = logging.getLogger(__name__)

_CONFIRM_SALT = "engagement_lead_capture_confirm"
_CONFIRM_MAX_AGE_SECONDS = 7 * 86400  # 7 days
_NAME_MAX_LENGTH = 255


def _lang_code(request) -> str:
    """Best-effort current storefront language code (each app owns its own copy)."""
    locale = getattr(request, "locale", None)
    lang_obj = getattr(locale, "language", None) if locale else None
    return getattr(lang_obj, "lang_code", "en") if lang_obj else "en"


def _send_confirmation_email(store, campaign, signup, lang):
    """
    Send the double opt-in confirmation email (DE-targeting stores only,
    ADR-027 D3 fallback). Never raises — logs and swallows, mirroring
    send_transactional_email's own "never raises" contract one level up.
    """
    try:
        from django.conf import settings

        from emails.service import send_transactional_email

        token = signing.dumps({"lead_signup_id": signup.pk}, salt=_CONFIRM_SALT)

        # Build an absolute URL from the store's own domain (mirrors
        # campaigns/tasks.py's abandoned-checkout resume_url construction —
        # a relative path is meaningless inside an email client).
        custom_domain = (getattr(store, "custom_domain", "") or "").strip()
        if custom_domain:
            store_host = custom_domain
        else:
            apex = getattr(settings, "PLATFORM_APEX_DOMAIN", "localhost")
            store_host = f"{store.subdomain}.{apex}"
        confirm_path = f"https://{store_host}/overlay/confirm/{token}/"

        send_transactional_email(
            template_id="lead_capture_confirm",
            recipient=signup.email,
            context={
                "campaign_name": campaign.name,
                "confirm_url": confirm_path,
            },
            store=store,
        )
    except Exception:
        logger.exception(
            "engagement: failed to send double opt-in confirmation email for "
            "LeadSignup %s (campaign %s, store %s).",
            signup.pk, campaign.pk, store.pk,
        )


def overlay_signup(request):
    """
    POST /overlay/signup/ — ADR-027 D3.

    Antispam (pages/antispam.py, reused verbatim, ADR-018 D3):
      - honeypot_triggered() -> silent success, no rows written, info log only.
      - rate_limit_exceeded(scope="lead_capture") -> HTTP 429, plain message.

    Returns JSON {"ok": true, "action", "message_html", "redirect_url", "code"}
    on success; the view never reveals whether an email had already signed up
    (idempotent duplicate submit returns the same success payload, D3 step 4).
    """
    if request.method != "POST":
        return HttpResponseNotAllowed(["POST"])

    store = getattr(request, "store", None)
    if store is None:
        return HttpResponseBadRequest("Store not found.")

    if honeypot_triggered(request):
        logger.info("engagement: honeypot triggered on overlay signup for store %s.", store.pk)
        # Security audit F5: same key set as a real success payload (below) —
        # a bot comparing response shapes must not be able to tell it was
        # honeypotted (pages/antispam.py's "never reveal detection" contract).
        # No real campaign/code lookup happens here (that would defeat the
        # point of short-circuiting before any DB write); redirect_url/code
        # are plausible-but-useless placeholders, never a real discount code.
        return JsonResponse(
            {
                "ok": True,
                "action": "display_message",
                "message_html": "",
                "redirect_url": "",
                "code": None,
            }
        )

    if rate_limit_exceeded(request, scope="lead_capture"):
        return HttpResponse(str(_t("Too many requests. Please try again later.")), status=429)

    campaign_id = request.POST.get("campaign_id")
    try:
        campaign = LeadCaptureCampaign.objects.for_store(store).get(pk=campaign_id, is_active=True)
    except (LeadCaptureCampaign.DoesNotExist, ValueError, TypeError):
        raise Http404("Unknown or inactive campaign.")

    email = request.POST.get("email", "").strip()
    if not email:
        return HttpResponseBadRequest("Email is required.")
    try:
        validate_email(email)
    except ValidationError:
        return HttpResponseBadRequest("Invalid email address.")

    name = request.POST.get("name", "").strip()[:_NAME_MAX_LENGTH]
    lang = _lang_code(request)

    customer, _created = get_or_create_customer(store, email, first_name=name)
    de_targeting = service.is_de_targeting_store(store)

    signup = None
    is_new_signup = True
    try:
        with transaction.atomic():
            signup = LeadSignup.objects.for_store(store).create(
                store=store, campaign=campaign, email=email, customer=customer, lang=lang,
            )
            code = service.issue_reward(store, campaign, email)
            # ADR-031 addendum audit (TICKET-051): .update() used to silently
            # bypass the isolation raise — scoped via .for_store(store).
            LeadCaptureCampaign.objects.for_store(store).filter(pk=campaign.pk).update(
                conversions_count=F("conversions_count") + 1
            )
    except IntegrityError:
        # (store, campaign, email) already exists — idempotent duplicate submit
        # (ADR-027 D3 step 4): same success payload, conversions_count untouched.
        is_new_signup = False
        code = campaign.reward_discount_code.code if campaign.reward_discount_code_id else None

    if is_new_signup:
        existing_tags = customer.tags or []
        new_tags = [tag for tag in campaign.signup_tags if tag not in existing_tags]
        update_fields = []
        if new_tags:
            customer.tags = existing_tags + new_tags
            update_fields.append("tags")
        # ADR-027 D3: single opt-in by default. DE-targeting stores withhold
        # accepts_marketing until the double opt-in confirmation link is
        # clicked (overlay_confirm below) — capture != send.
        if not de_targeting and not customer.accepts_marketing:
            customer.accepts_marketing = True
            update_fields.append("accepts_marketing")
        if update_fields:
            update_fields.append("updated_at")
            customer.save(update_fields=update_fields)

        if de_targeting and signup is not None:
            _send_confirmation_email(store, campaign, signup, lang)

    content = service.resolve_campaign_content(campaign, lang)
    message_html = content["success_message"].replace("{code}", code or "")

    return JsonResponse(
        {
            "ok": True,
            "action": campaign.post_signup_action,
            "message_html": message_html,
            "redirect_url": campaign.redirect_url,
            "code": code,
        }
    )


@require_POST
def overlay_event(request):
    """
    POST /overlay/event/ — best-effort impression counter (ADR-027 D5).

    Client-reported, therefore best-effort (spoofable/lossy — merchant stats
    only, never billing). Rate-limited generously (scope="lead_capture_event").
    """
    store = getattr(request, "store", None)
    if store is None:
        return HttpResponseBadRequest("Store not found.")

    if rate_limit_exceeded(request, scope="lead_capture_event", limit=60):
        return HttpResponse(status=429)

    campaign_id = request.POST.get("campaign_id")
    try:
        found = service.record_impression(store, campaign_id)
    except (ValueError, TypeError):
        found = False
    if not found:
        raise Http404("Unknown campaign.")

    return HttpResponse(status=204)


def overlay_confirm(request, token):
    """
    GET /overlay/confirm/<token>/ — double opt-in confirmation link (ADR-027
    D3 fallback, DE-targeting stores only).

    Sets LeadSignup.confirmed_at and, only now, Customer.accepts_marketing=True
    — the marketing SEND gate this whole fallback exists for (capture != send).
    Bad/expired/tampered tokens get a plain error response, never a 500.
    """
    try:
        payload = signing.loads(token, salt=_CONFIRM_SALT, max_age=_CONFIRM_MAX_AGE_SECONDS)
    except (signing.BadSignature, signing.SignatureExpired):
        return HttpResponse(str(_t("This confirmation link is invalid or has expired.")), status=400)

    try:
        signup = LeadSignup.objects.cross_store_unsafe().select_related("customer").get(
            pk=payload.get("lead_signup_id")
        )
    except LeadSignup.DoesNotExist:
        return HttpResponse(str(_t("This confirmation link is invalid or has expired.")), status=400)

    # Security audit F4: bind the confirm token to the requesting store. The
    # signed payload only carries the LeadSignup pk, so without this check a
    # token minted for store A's signup would also be accepted when presented
    # on store B's host — breaking the codebase's "every DB lookup is
    # store-scoped" convention (cross_store_unsafe() above is exactly why this
    # check is needed). Same response/status as a bad token — no oracle
    # revealing that the token is otherwise valid.
    store = getattr(request, "store", None)
    if store is not None and signup.store_id != store.pk:
        return HttpResponse(str(_t("This confirmation link is invalid or has expired.")), status=400)

    if signup.confirmed_at is None:
        signup.confirmed_at = timezone.now()
        signup.save(update_fields=["confirmed_at"])
        if signup.customer is not None and not signup.customer.accepts_marketing:
            signup.customer.accepts_marketing = True
            signup.customer.save(update_fields=["accepts_marketing", "updated_at"])

    return HttpResponse(str(_t("Thanks — your subscription is confirmed.")), status=200)
