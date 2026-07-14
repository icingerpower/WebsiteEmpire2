"""
Stores app middleware (ADR-008, ADR-021).

LocaleMiddleware — resolve host + path prefix → (store, domain, language) once per
request and set request.locale + request.store.  Excluded paths (admin, static, media)
are passed through unchanged so that the existing HostResolutionMiddleware result
remains the sole source of request.store on those paths.

This middleware ships in the settlement period described in ADR-008 §Rollback: both
HostResolutionMiddleware (legacy) and LocaleMiddleware (new) run in parallel.
LocaleMiddleware overwrites request.store for storefront paths; admin paths keep
the legacy value.  Once full agreement is confirmed, the legacy middleware will be removed.

ADR-021: this is also the SINGLE activation point for Django's gettext translation
machinery on storefront requests. It activates settings.LANGUAGE_CODE as the very
first statement of every request (kills thread-local leakage from whatever language
the previous request on this worker thread left active — including for the excluded
paths below and for resolve_locale's own error paths), then re-activates the resolved
StoreLanguage's lang_code once resolution succeeds. There is deliberately no
translation.deactivate() anywhere — see ADR-021 §D1a: a deactivate in the response
phase would run before a deferred TemplateResponse actually renders, producing the
wrong language for that render.
"""

import re

from django.conf import settings
from django.http import HttpResponse, HttpResponsePermanentRedirect
from django.utils import translation

# Matches a bare language-prefix path: /fr or /pt-br — no trailing slash, no further segments.
# ADR-008 §2a: these must redirect 301 to /fr/ before locale resolution runs.
# \Z (not $) prevents matching /fr%0A-style paths that decode to a trailing newline.
_BARE_LANG_PREFIX_RE = re.compile(r"^/[a-z]{2}(-[a-z]{2})?\Z")


class LocaleMiddleware:
    """
    Calls resolve_locale(host, path) once per request and sets:
      request.locale  → RequestLocale(store, domain, language, path)
      request.store   → locale.store  (ADR-001 §2 request.store contract)

    Returns 410 Gone for disabled languages (ML-012).
    Returns 404 for unknown hosts (resolve_locale raises Http404, Django handles it).

    Excluded prefixes (admin, static, media) are passed through untouched so that
    the super-admin surface and asset serving remain independent of StoreDomain rows.

    ADR-021: also activates Django's translation machinery — settings.LANGUAGE_CODE
    as a leak-proof baseline on every request, then the resolved language once
    resolve_locale succeeds. Sets response['Content-Language'] (setdefault — never
    clobbers a view that set its own, e.g. the checkout sf_lang decorator's language)
    from translation.get_language() at response time, not from the resolved code, so
    a downstream sf_lang re-activation is reflected.
    """

    EXCLUDED_PREFIXES = (
        "/admin/",
        "/superadmin/",
        "/static/",
        "/media/",
        # Non-storefront server-to-server endpoints: webhook callbacks (Stripe, PayPal)
        # and analytics beacon.  These are called from external servers or JS snippets
        # that do not carry a store host, so locale resolution is not applicable.
        "/webhooks/",
        "/_analytics/",
        # Employee invite accept-link (ADR-033 D4b, TICKET-047): the email may
        # be opened from any host, and the target store is resolved from the
        # StoreEmployee row encoded in the URL, not from the Host header — no
        # StoreDomain lookup should gate reaching this view.
        "/invite/",
    )

    # ADR-015 §6 fixed, un-prefixed storefront routes — registered bare (before the
    # <path:slug> catch-all), so resolve_locale ALWAYS resolves them to the domain's
    # ROOT language, regardless of what language the shopper was actually browsing
    # under. This is precisely why ADR-015 introduced sf_lang in the first place
    # (see ADR-021 §Context). Bug fixed while implementing/testing ADR-021's T5
    # requirement: writing session['sf_lang'] unconditionally on EVERY resolved
    # request — including a bare /checkout/ request itself — overwrote the buyer's
    # earlier /fr/... browsing-language write with the root language BEFORE
    # storefront/views_checkout.py's activate_checkout_language decorator ever got
    # to read it, in the SAME request/response cycle. That silently made
    # resolve_checkout_language's "session sf_lang first" priority a no-op for
    # every checkout/cart/search/thank-you visit — sf_lang could never be anything
    # but the root language by the time any of those views read it. Excluding
    # these routes from the WRITE (they still get the ordinary translation
    # activation above) restores ADR-015's original design intent; it does not
    # change resolve_locale, activation, or any other resolved behavior.
    SF_LANG_WRITE_EXEMPT_PREFIXES = (
        "/checkout/",
        "/cart/",
        "/search/",
        "/orders/",
        # SF-LANG-UPSELL (BUG_TESTS/BUG_TESTS.csv): the post-purchase upsell widget
        # (storefront/templates/storefront/partials/upsell_widget.html) POSTs to
        # this bare, un-prefixed endpoint from the thank-you page and then calls
        # window.location.reload() on the SAME (already-loaded) thank-you URL —
        # it never navigates to a fresh /fr/... URL that would re-set sf_lang.
        # Repro: buyer browses /fr/... (sf_lang='fr') -> thank-you renders French
        # -> POST /campaigns/upsell/accept/ (or decline/) resolves to the domain's
        # ROOT language and, without this exemption, overwrote sf_lang with it ->
        # the reload's GET to the thank-you URL (itself exempt via "/orders/"
        # above) then reads the now-clobbered root sf_lang -> re-renders English.
        # See campaigns/tests/test_sf_lang_upsell_regression.py.
        "/campaigns/upsell/",
        # Product engagement forms (storefront/urls.py "products/<slug>/notify-me/"
        # and ".../quotation/", T029 TH-082) — same fixed-route, un-prefixed-action
        # shape as checkout/cart/campaigns above. Today's redirect-to-referer
        # response (non-AJAX) happens to self-heal on the next real navigation, but
        # exempting them keeps this list correct by construction rather than by
        # today's specific reload behavior — caught by the drift test below
        # (SfLangWriteExemptionCoversUrlpatternsTest) when these routes were found
        # uncategorized. CAVEAT: if a future ADR ever moves genuine product
        # *browsing* pages under a literal "/products/" prefix, this entry would
        # need to be narrowed — it must only ever cover fixed action endpoints,
        # never a real browsing entry point.
        "/products/",
        # Currency selection endpoint (storefront/urls.py "currency/set/",
        # ADR-023 §4). Same fixed, un-prefixed action-endpoint shape as the
        # entries above: it 302s straight back to whatever prefixed page the
        # shopper POSTed from, which self-heals sf_lang on the very next
        # request — but exempting it here keeps this list correct BY
        # CONSTRUCTION (caught by SfLangWriteExemptionCoversUrlpatternsTest
        # below) rather than by today's specific redirect-then-reload
        # behavior, exactly like the "/products/" entry above.
        "/currency/",
        # AI sales-assistant chat endpoints (webecom/urls.py "chat/", ADR-024,
        # TICKET-038). Same fixed, un-prefixed action-endpoint shape as the
        # entries above: POST /chat/session/ and POST /chat/message/ are called
        # via fetch() from any prefixed page (e.g. /fr/some-product/), never a
        # real page navigation — exempting them here keeps this list correct BY
        # CONSTRUCTION (caught by SfLangWriteExemptionCoversUrlpatternsTest)
        # rather than relying on the widget's own JS never triggering a
        # follow-up navigation that would read a just-clobbered sf_lang,
        # exactly like the "/products/" and "/currency/" entries above.
        "/chat/",
        # Consent decision endpoint (webecom/urls.py "_consent/", ADR-025,
        # TICKET-048). Same fixed, un-prefixed action-endpoint shape as the
        # entries above: POST /_consent/ is called via fetch() from the
        # banner/panel on any prefixed page — exempted (rather than added to
        # EXCLUDED_PREFIXES like "/_analytics/") because consent/views.py DOES
        # need LocaleMiddleware to run and set request.locale (it reads the
        # resolved language for ConsentRecord.lang); it must simply never
        # overwrite session['sf_lang'] with the domain's root language the way
        # a real page navigation would.
        "/_consent/",
        # Catalog feed files (webecom/urls.py "feeds/", ADR-026, TICKET-033).
        # GET /feeds/<provider>/<country>-<lang>.xml is fetched by Google
        # Merchant Center / Meta's crawler on their own schedule, never by a
        # shopper's browser — there is no browsing session to protect, and the
        # feed's own language comes from the URL segment, never from
        # session['sf_lang']. Exempting it here (rather than EXCLUDED_PREFIXES)
        # because feeds/views.py's feed_view still wants request.locale set by
        # LocaleMiddleware (store/domain/language resolution, ADR-026 D2); it
        # must simply never overwrite a concurrent shopper's sf_lang session
        # value with whatever language the feed URL happened to resolve to.
        "/feeds/",
        # Lead capture overlay endpoints (webecom/urls.py "overlay/", ADR-027
        # D1/D3, TICKET-034). Same fixed, un-prefixed action-endpoint shape as
        # "/chat/"/"/_consent/" above: POST /overlay/signup/ and
        # /overlay/event/ are called via fetch() from the popup's JS on any
        # prefixed page (e.g. /fr/some-product/), and GET /overlay/confirm/
        # <token>/ is a one-off email-link visit — none of these are a real
        # storefront page navigation. Exempted (rather than added to
        # EXCLUDED_PREFIXES) because engagement/views.py still wants
        # request.locale set by LocaleMiddleware (used to stamp LeadSignup.lang
        # and to resolve translated campaign content) — it must simply never
        # overwrite a concurrent shopper's sf_lang session value with the
        # domain's root language.
        "/overlay/",
    )

    def __init__(self, get_response):
        self._get_response = get_response

    def __call__(self, request):
        # ADR-021: deterministic baseline on EVERY request, before anything else runs.
        # This is what makes the excluded-prefix passthrough below leak-proof — without
        # it, a worker thread that just served a French storefront request would render
        # the very next /admin/ request (which bypasses the rest of this method) in
        # French, because Django's translation state is a thread-local that otherwise
        # only changes when something explicitly activates a new language.
        translation.activate(settings.LANGUAGE_CODE)
        request.LANGUAGE_CODE = translation.get_language()

        path = request.path_info

        # Skip admin / static / media paths — they don't go through storefront routing.
        for prefix in self.EXCLUDED_PREFIXES:
            if path.startswith(prefix):
                return self._get_response(request)

        # ADR-008 §2a: /fr (bare lang prefix, no trailing slash) → 301 /fr/
        # Must run before resolve_locale so the resolver never sees a path that
        # looks like a bare language code.
        if _BARE_LANG_PREFIX_RE.match(path):
            qs = request.META.get("QUERY_STRING", "")
            target = path + "/" + ("?" + qs if qs else "")
            return HttpResponsePermanentRedirect(target)

        host = request.get_host()

        # Import here to avoid circular imports at module load time (stores app
        # and permalinks app both load early in the app registry).
        from permalinks.resolver import GoneError, resolve_locale

        try:
            locale = resolve_locale(host, path)
        except GoneError:
            return HttpResponse(status=410)
        # Http404 propagates naturally — Django's exception handler converts it to a 404.

        request.locale = locale
        request.store = locale.store

        # ADR-021: re-activate with the resolved StoreLanguage now that resolution
        # succeeded. lang_code is never attacker- or typo-controlled here — it always
        # comes from an actual StoreLanguage row matched by resolve_locale, never a
        # bare regex (ADR-021 §D5) — so activate() never receives arbitrary input on
        # this path.
        translation.activate(locale.language.lang_code)
        request.LANGUAGE_CODE = translation.get_language()

        # Persist browsing language to session so checkout can read it (ADR-015 §6 ML-001).
        # SessionMiddleware runs before LocaleMiddleware so request.session is available here.
        # We write only when the value changed to avoid unnecessary session saves, and we
        # skip the write entirely on the fixed un-prefixed routes in
        # SF_LANG_WRITE_EXEMPT_PREFIXES — see that constant's docstring for why.
        language_code = locale.language.lang_code
        session = getattr(request, 'session', None)
        if (
            session is not None
            and session.get('sf_lang') != language_code
            and not path.startswith(self.SF_LANG_WRITE_EXEMPT_PREFIXES)
        ):
            session['sf_lang'] = language_code

        response = self._get_response(request)
        # ADR-021: Content-Language reflects the ACTUALLY active language at response
        # time (translation.get_language()), not the resolved code captured above — a
        # checkout view's activate_checkout_language decorator may have re-activated
        # sf_lang in between. setdefault: never clobber a header a view already set.
        response.headers.setdefault('Content-Language', translation.get_language())
        return response
