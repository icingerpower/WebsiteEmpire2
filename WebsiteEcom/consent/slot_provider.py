"""
ConsentSlotProvider — bridges consent.ConsentSettings to the already-declared
storefront.slots "consent" slot (ADR-012 D9 / TH-141, ADR-025 D5/D7).

TICKET-048 scope: is_enabled() and validate_settings() are fully implemented.
TICKET-049 scope: render() — server-rendered banner (undecided state only) +
always-rendered hidden preferences panel + inline <style>/<script>, delegated
entirely to storefront/templates/storefront/partials/consent_banner.html
(render_to_string, mirroring chat/slot_provider.py's ChatWidgetSlotProvider —
every string lives in that one template so `makemessages` collects it in a
single pass).

The template lives under storefront/templates/ rather than consent/templates/
(ASSUMPTION, ADR-025 D7 left this an open call): the customer-facing string
catalog for this ticket is storefront/locale/fr/LC_MESSAGES/django.po, and in
this codebase that catalog is refreshed by running `makemessages` from the
`storefront/` app directory — which only walks files under that directory
tree. Placing the template under consent/templates/consent/ would put its
strings outside that scan and require a second, consent-owned locale
directory + a second compilemessages target, adding i18n plumbing no other
part of this ticket needs.
"""

import logging

from django.contrib.contenttypes.models import ContentType
from django.middleware.csrf import get_token
from django.template.loader import render_to_string

from pixels.models import Pixel
from storefront.slots import SlotProvider, register

from consent.models import get_or_create_consent_settings
from consent.state import get_consent

logger = logging.getLogger(__name__)


def _lang_code(request) -> str:
    """Best-effort current storefront language code (mirrors pages_tags._lang_code)."""
    locale = getattr(request, "locale", None)
    lang_obj = getattr(locale, "language", None) if locale else None
    return getattr(lang_obj, "lang_code", "en") if lang_obj else "en"


def _resolve_policy_page_url(context, request, store, policy_page):
    """
    Resolve the storefront URL for ConsentSettings.policy_page (ADR-025 D5:
    "links to the store's privacy/cookie StaticPage when configured...  do
    not hand-build the URL").

    Reuses the exact StaticPage → Permalink → {% permalink_url %} resolution
    chain pages_tags.static_page_links already established (ML-011: a page
    must never be linked if it would 404) instead of duplicating it. Returns
    None when policy_page is unset OR has no active Permalink for the current
    request language — in both cases the caller omits the link entirely
    rather than emitting a broken href (ADR-025 D5 explicit requirement for
    the None case; the "no permalink for this language" case is the same
    ML-011 rule pages_tags already applies, extended here for consistency).
    """
    if policy_page is None:
        return None

    from permalinks.models import Permalink
    from permalinks.templatetags.permalinks_tags import permalink_url

    lang = _lang_code(request)
    ct = ContentType.objects.get_for_model(policy_page.__class__)
    permalink = (
        Permalink.objects.for_store(store)
        .filter(content_type=ct, object_id=policy_page.pk, lang=lang, is_active=True)
        .first()
    )
    if permalink is None:
        return None
    return permalink_url(context, permalink.slug)


class ConsentSlotProvider(SlotProvider):
    slot = "consent"
    key = "consent"
    name = "Cookie consent banner"
    required_settings: list = []

    def is_enabled(self, store) -> bool:
        return get_or_create_consent_settings(store).is_enabled

    def render(self, context) -> str:
        """
        Render the banner (only when undecided) + the always-present hidden
        preferences panel (ADR-025 D5).

        Context contract passed to the template — every value server-computed
        so the inline JS never has to re-derive consent state client-side
        (§XV-4 single resolution point, consent.state.get_consent):

          show_banner       — bool, True only when `not consent.decided`
                               (ADR-025 D3: undecided OR a stale/expired
                               cookie, both of which get_consent() already
                               collapses to decided=False).
          analytics_pre / marketing_pre / am_allowed_pre
                            — "1"/"0" strings reflecting the CURRENT server-
                               known state, used both as the panel toggles'
                               initial checked/unchecked state (so reopening
                               "manage cookies" shows the shopper's actual
                               saved choice, not a reset-to-default — ADR-025
                               D5 "withdrawal as easy as granting") AND as
                               data-* attributes on the root element so the
                               inline JS can compute "at least one category
                               newly granted" (ADR-025 D3 reload-on-grant)
                               without any additional request.
          policy_url        — resolved StaticPage URL or None (link omitted).
        """
        request = context.get("request")
        store = getattr(request, "store", None)
        if store is None or request is None:
            return ""

        consent = get_consent(request)
        settings_row = get_or_create_consent_settings(store)
        policy_url = _resolve_policy_page_url(context, request, store, settings_row.policy_page)
        show_banner = not consent.decided

        # F1 (security audit CONSENT_AUDIT.md): guarantee the csrftoken cookie
        # is actually set on the response whenever the banner will render.
        # Without this, a form-free page (no other {% csrf_token %} anywhere
        # on it) leaves the banner's JS with no cookie to read, so its own
        # POST /_consent/ 403s and consent becomes un-grantable from that
        # page (fail-closed but feature-defeating). get_token() marks the
        # response to set the cookie; it does not itself render anything.
        # Scoped to show_banner (not the always-rendered hidden manage
        # panel) per the audit's fix — a decided shopper who already has a
        # working cookie flow does not need one forced.
        if show_banner:
            get_token(request)

        return render_to_string(
            "storefront/partials/consent_banner.html",
            {
                "request": request,
                "show_banner": show_banner,
                "analytics_pre": "1" if consent.analytics else "0",
                "marketing_pre": "1" if consent.marketing else "0",
                "am_allowed_pre": "0" if consent.am_objected else "1",
                "policy_url": policy_url,
            },
            request=request,
        )

    def validate_settings(self, store) -> list:
        """
        Launch-checklist errors (ADR-025 D6).

        Blocking: consent disabled, no non-EU acknowledgment, AND at least
        one active Pixel row exists — this is the single check that flips
        pixels from "not launch-ready for EU storefronts" (KNOWN_RISKS item
        1) to EU-ready, by refusing to let a store launch pixels with no
        consent gate and no acknowledged non-EU exemption.

        Non-blocking: consent enabled but no policy_page configured (the
        banner still functions and is still legally sufficient — the link
        to the store's cookie policy is just absent).

        ASSUMPTION: validate_settings()'s contract elsewhere in this codebase
        (see PixelsSlotProvider.validate_settings, SlotProvider docstring) is
        an unqualified list[str] consumed as blocking launch-checklist
        errors — there is no existing severity mechanism (blocking vs.
        warning) anywhere in the launch-checklist architecture to reuse or
        extend without a schema change that no other provider needs. Rather
        than invent a new return shape used by nobody else, the non-blocking
        item is returned in the SAME list, prefixed "(warning)" so a human
        reading the checklist can immediately tell it apart from a blocking
        entry. If the launch-checklist consumer later grows real severity
        plumbing, this prefix convention should be replaced platform-wide,
        not just here.
        """
        settings_row = get_or_create_consent_settings(store)
        errors = []

        if (
            not settings_row.is_enabled
            and not settings_row.non_eu_acknowledged
            and Pixel.objects.for_store(store).filter(is_active=True).exists()
        ):
            errors.append(
                "Active pixels require the consent banner (or an explicit non-EU "
                "acknowledgment)."
            )

        if settings_row.is_enabled and settings_row.policy_page_id is None:
            errors.append(
                "(warning) Consent is enabled but no cookie/privacy policy page is "
                "set — the banner will have no policy link."
            )

        return errors


register(ConsentSlotProvider())
