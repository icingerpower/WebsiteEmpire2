"""
Business logic for T034 (lead capture) and T035 (social proof) — ADR-027.

Kept out of views.py/slot_providers.py so both the public views and the
SlotProvider render() paths share one implementation (§XV-4 — one function,
several call sites).
"""

import logging
from datetime import timedelta

from django.core.cache import cache
from django.db import IntegrityError, transaction
from django.db.models import F
from django.utils import timezone

from engagement.models import (
    LeadCaptureCampaign,
    LeadCaptureCampaignTranslation,
    NAMED_SOCIAL_PROOF_MODES_ENABLED,
    SocialProofDisplayMode,
    get_or_create_social_proof_settings,
)

logger = logging.getLogger(__name__)

# Session-flag key prefix for the once-per-session visitor counter (D5).
_VISITOR_SESSION_KEY_PREFIX = "_engagement_lc_visited_"

# Cache TTL for the social-proof feed (ADR-027 D7): "cached for 60 s per
# (store, lang, display_mode)".
_SOCIAL_PROOF_CACHE_TTL_SECONDS = 60

# Over-fetch bound for the PAID-orders window scan — bounds the query even on
# a very busy store; entries beyond this are simply never considered (D7 does
# not require exhaustive scanning, only "newest first, capped at max_items").
_SOCIAL_PROOF_ORDER_SCAN_LIMIT = 200


# ---------------------------------------------------------------------------
# T034 — campaign selection, translation resolution, excluded paths
# ---------------------------------------------------------------------------


def select_active_campaign(store):
    """
    Return the most recently created active LeadCaptureCampaign for store, or
    None (ADR-027 D4: "among active campaigns, render exactly one — the most
    recently created active campaign").
    """
    return (
        LeadCaptureCampaign.objects.for_store(store)
        .filter(is_active=True)
        .order_by("-created_at")
        .first()
    )


def resolve_campaign_content(campaign, lang_code: str) -> dict:
    """
    Resolve the displayed campaign copy for lang_code (ADR-027 D9): a
    published LeadCaptureCampaignTranslation row for lang_code if one exists,
    else the campaign's own base fields — the CampaignStepTranslation
    fallback rule verbatim.
    """
    translation = (
        LeadCaptureCampaignTranslation.objects.for_store(campaign.store)
        .filter(
            campaign=campaign,
            lang_code=lang_code,
            status=LeadCaptureCampaignTranslation.STATUS_PUBLISHED,
        )
        .first()
    )

    def _pick(field):
        if translation is not None:
            value = getattr(translation, field)
            if value:
                return value
        return getattr(campaign, field)

    return {
        "headline": _pick("headline"),
        "body": _pick("body"),
        "cta_label": _pick("cta_label"),
        "dismiss_label": _pick("dismiss_label"),
        "success_message": _pick("success_message"),
    }


def resolve_excluded_paths(store, campaign) -> list:
    """
    Resolve campaign.excluded_pages (Permalink rows) into a de-duplicated,
    sorted list of URL paths covering ALL active languages of the same target
    page (ADR-027 D2/D4) — never store raw path strings on the model.

    For each selected Permalink, every other active Permalink pointing at the
    same (content_type, object_id) is also included, so a merchant excluding
    the English homepage also suppresses the overlay on its French/German
    editions.
    """
    from django.db.models import Q

    from permalinks.models import Permalink
    from stores.models import StoreLanguage

    # campaign.excluded_pages (the M2M manager) is built on Permalink's own
    # StoreScopedManager, but relation-bound M2M access is safe by
    # construction (ADR-031, core/managers.py): ManyRelatedManager pins every
    # row to this campaign's through rows, and core/m2m_guard.py guarantees a
    # through row can never link to another store's Permalink. Reading it
    # directly is therefore equivalent to (and simpler than) re-deriving the
    # same rows via Permalink.objects.for_store(store).filter(...).
    selected = list(campaign.excluded_pages.filter(is_active=True))
    if not selected:
        return []

    content_keys = {(p.content_type_id, p.object_id) for p in selected}
    query = Q()
    for content_type_id, object_id in content_keys:
        query |= Q(content_type_id=content_type_id, object_id=object_id)

    all_permalinks = Permalink.objects.for_store(store).filter(query, is_active=True)

    lang_rows = {lang.lang_code: lang for lang in StoreLanguage.objects.filter(store=store)}

    paths = set()
    for permalink in all_permalinks:
        lang_row = lang_rows.get(permalink.lang)
        use_prefix = bool(lang_row and lang_row.use_path_prefix)
        if use_prefix:
            path = f"/{permalink.lang}/{permalink.slug}/" if permalink.slug else f"/{permalink.lang}/"
        else:
            path = f"/{permalink.slug}/" if permalink.slug else "/"
        paths.add(path)

    return sorted(paths)


def record_visitor(request, campaign) -> None:
    """
    Increment LeadCaptureCampaign.visitors_count once per session per
    campaign (ADR-027 D5: "unique storefront sessions to which the campaign
    was served ... incremented server-side once per session per campaign via
    a session flag — no beacon needed").

    Never raises — called from SlotProvider.render(), which must not raise;
    a missing/broken session simply skips the increment.
    """
    session = getattr(request, "session", None)
    if session is None:
        return
    key = f"{_VISITOR_SESSION_KEY_PREFIX}{campaign.pk}"
    getter = getattr(session, "get", None)
    if getter is None or getter(key):
        return
    # ADR-031 addendum audit (TICKET-051): .update() used to silently bypass
    # the isolation raise — scoped via .for_store(campaign.store) (no
    # `store` variable in scope here, only the campaign instance).
    LeadCaptureCampaign.objects.for_store(campaign.store).filter(pk=campaign.pk).update(
        visitors_count=F("visitors_count") + 1
    )
    try:
        session[key] = True
    except TypeError:
        # Some lightweight test session doubles are not item-assignable.
        pass


def record_impression(store, campaign_id) -> bool:
    """
    Atomically increment impressions_count via F() (ADR-027 D5 — client-
    reported, best-effort). Returns True when a matching campaign of this
    store was found and incremented, False otherwise (caller returns 404).
    """
    updated = (
        LeadCaptureCampaign.objects.for_store(store)
        .filter(pk=campaign_id)
        .update(impressions_count=F("impressions_count") + 1)
    )
    return bool(updated)


def is_de_targeting_store(store) -> bool:
    """
    Human decision 2026-07-11 (ADR-027 D3 left the exact detection mechanism
    to implementation): a store is treated as "selling into Germany" — and
    therefore gated into the double opt-in fallback described in D3 — when
    EITHER:

      1. it has an active StoreLanguage(lang_code='de') row (it literally
         serves a German storefront edition, ML-005), OR
      2. it ships to Germany from any of its enabled storefront editions —
         a ShippingCountry(country_code='DE') row attached to an enabled
         StoreLanguage (ADR-008 §1: ShippingCountry rows hang off
         StoreLanguage, never off Store directly, so this is a join through
         store_language__store). This covers e.g. an English-only storefront
         that still declares Germany as a target market.
    """
    from stores.models import ShippingCountry, StoreLanguage

    if StoreLanguage.objects.filter(store=store, lang_code="de", is_enabled=True).exists():
        return True

    return ShippingCountry.objects.filter(
        store_language__store=store,
        store_language__is_enabled=True,
        country_code="DE",
    ).exists()


def issue_reward(store, campaign, email: str):
    """
    Idempotently issue campaign.reward_discount_code to email (ADR-027 D3
    step 5 / ADR-002 §5). Returns the code string, or None if the campaign has
    no reward configured.

    The DiscountCode is shared (linked once per campaign, not generated per
    lead), so the returned code is always campaign.reward_discount_code.code
    regardless of whether this call created the CampaignReward row or hit the
    unique-constraint race — no need to re-fetch on IntegrityError.
    """
    if campaign.reward_discount_code_id is None:
        return None

    from discounts.models import CampaignReward

    try:
        with transaction.atomic():
            CampaignReward.objects.for_store(store).create(
                store=store,
                campaign_id=f"lead_capture:{campaign.pk}",
                recipient_email=email,
                discount_code=campaign.reward_discount_code,
            )
    except IntegrityError:
        # Already issued to this email for this campaign — idempotent no-op.
        pass

    return campaign.reward_discount_code.code


# ---------------------------------------------------------------------------
# T035 — social proof feed
# ---------------------------------------------------------------------------


def _time_bucket_kind_and_n(delta):
    """Coarsen a timedelta into a (kind, n) pair — no translated text here."""
    minutes = max(int(delta.total_seconds() // 60), 0)
    if minutes < 5:
        return "few", None
    if minutes < 60:
        return "minutes", minutes
    hours = minutes // 60
    if hours < 24:
        return "hours", hours
    days = hours // 24
    return "days", days


def _relative_time_bucket(delta) -> str:
    """
    Coarsen a timedelta into a localized relative bucket string (ADR-027 D7:
    "a few minutes ago", "2 hours ago"). The raw datetime never reaches the
    client — only this bucket string does.

    Rendered through storefront/templates/storefront/partials/
    social_proof_time_bucket.html (D9: platform-owned widget-chrome strings
    live in storefront templates, not engagement's own Python/locale, so the
    existing `makemessages` pass run from storefront/ collects them in the
    same catalog as every other slot-provider partial).
    """
    from django.template.loader import render_to_string

    kind, n = _time_bucket_kind_and_n(delta)
    return render_to_string(
        "storefront/partials/social_proof_time_bucket.html", {"kind": kind, "n": n}
    ).strip()


def _resolve_display_info(order, display_mode: str) -> dict:
    """
    Resolve the buyer-display fields for one order under display_mode
    (ADR-027 D6). Returns a dict with an explicit "mode" key so the caller can
    tell anonymous fallback apart from the requested named mode:

      {"mode": "anonymous", "country": "FR"}
      {"mode": "first_name", "first_name": "Marie"}
      {"mode": "first_name_city", "first_name": "Marie", "city": "Lyon", "country": "FR"}

    Hard privacy rules (never violated, regardless of display_mode):
    surname, full name, email, order number, and amounts are never read from
    here at all — only customer.first_name / shipping_address['name'] (first
    token only) / shipping_address['city'] / shipping_address['country'].

    Falls back to "anonymous" when: display_mode is anonymous; the order's
    customer has anonymized_at set (named modes exclude anonymized customers,
    D6); or the resolved first name is blank.
    """
    address = order.shipping_address or {}
    country = (address.get("country") or "").strip()

    customer = order.customer
    anonymized = customer is not None and customer.anonymized_at is not None

    if display_mode == SocialProofDisplayMode.ANONYMOUS or anonymized:
        return {"mode": "anonymous", "country": country}

    first_name = ""
    if customer is not None and customer.first_name:
        first_name = customer.first_name.strip()
    if not first_name:
        addr_name = (address.get("name") or "").strip()
        if addr_name:
            first_name = addr_name.split(" ", 1)[0]

    if not first_name:
        # Blank first name -> anonymous entry (D6 explicit rule).
        return {"mode": "anonymous", "country": country}

    if display_mode == SocialProofDisplayMode.FIRST_NAME_CITY:
        city = (address.get("city") or "").strip()
        return {"mode": "first_name_city", "first_name": first_name, "city": city, "country": country}

    return {"mode": "first_name", "first_name": first_name}


def _compose_sentence(display_info: dict, product_title: str, time_bucket: str) -> str:
    """
    Compose the final, fully-baked display string for one social-proof entry.
    Only this string reaches the client (D7: "the client receives only final
    display strings").

    Rendered through storefront/templates/storefront/partials/
    social_proof_sentence.html (D9 — see _relative_time_bucket's docstring for
    why platform-owned strings live there rather than here).
    """
    from django.template.loader import render_to_string

    context = {
        "mode": display_info["mode"],
        "first_name": display_info.get("first_name", ""),
        "city": display_info.get("city", ""),
        "country": display_info.get("country", ""),
        "product_title": product_title,
        "time_bucket": time_bucket,
    }
    return render_to_string("storefront/partials/social_proof_sentence.html", context).strip()


def _resolve_product_title(store, product, lang: str) -> str:
    from catalog.models import ProductTranslation, TranslationStatus

    translation = (
        ProductTranslation.objects.for_store(store)
        .filter(product=product, lang_code=lang, status=TranslationStatus.PUBLISHED)
        .first()
    )
    if translation is not None and translation.title:
        return translation.title
    return product.title


def _resolve_product_permalink(store, product, lang: str):
    from django.contrib.contenttypes.models import ContentType

    from permalinks.models import Permalink

    content_type = ContentType.objects.get_for_model(product.__class__)
    return (
        Permalink.objects.for_store(store)
        .filter(content_type=content_type, object_id=product.pk, lang=lang, is_active=True)
        .first()
    )


def _build_social_proof_entries(store, settings_row, effective_mode, context, lang: str) -> list:
    from permalinks.templatetags.permalinks_tags import permalink_url

    from orders.models import Order, OrderItem, PaymentStatus

    now = timezone.now()
    window_start = now - timedelta(hours=settings_row.window_hours)

    orders = (
        Order.objects.for_store(store)
        .filter(payment_status=PaymentStatus.PAID, placed_at__gte=window_start)
        .exclude(placed_at__isnull=True)
        .select_related("customer")
        .order_by("-placed_at")[:_SOCIAL_PROOF_ORDER_SCAN_LIMIT]
    )

    entries = []
    for order in orders:
        if len(entries) >= settings_row.max_items:
            break

        # order.items (the reverse FK manager) is built on OrderItem's own
        # StoreScopedManager and therefore raises on direct iteration/first()
        # (core/managers.py) — go through OrderItem.objects.for_store()
        # instead, same reasoning as the excluded_pages fix above.
        first_item = (
            OrderItem.objects.for_store(store)
            .filter(order=order)
            .select_related("product_variant__product")
            .order_by("id")
            .first()
        )
        if first_item is None or first_item.product_variant is None:
            continue
        product = first_item.product_variant.product

        permalink = _resolve_product_permalink(store, product, lang)
        if permalink is None:
            # ML-011: never link a product that would 404 in this language.
            continue

        title = _resolve_product_title(store, product, lang)
        url = permalink_url(context, permalink.slug)
        display_info = _resolve_display_info(order, effective_mode)
        time_bucket = _relative_time_bucket(now - order.placed_at)
        text = _compose_sentence(display_info, title, time_bucket)

        entries.append({"text": text, "product_url": url})

    return entries


def get_social_proof_entries(store, context, lang: str) -> list:
    """
    Return the (cached) list of social-proof entries for (store, lang).

    Empty window -> empty list, NEVER fabricated entries (ADR-027 D6 — the
    absolute no-fabrication rule). Disabled store -> empty list, no query.
    """
    settings_row = get_or_create_social_proof_settings(store)
    if not settings_row.is_enabled:
        return []

    # D6 gating: named modes are not rendered until the legal flag flips,
    # regardless of what is stored on display_mode (defense in depth — the
    # model's clean() already blocks saving a named mode while the flag is
    # off, but this direct override protects against any row written before
    # the flag existed, or written outside the admin form).
    effective_mode = settings_row.display_mode
    if not NAMED_SOCIAL_PROOF_MODES_ENABLED:
        effective_mode = SocialProofDisplayMode.ANONYMOUS

    cache_key = f"engagement:social_proof:{store.pk}:{lang}:{effective_mode}"
    cached = cache.get(cache_key)
    if cached is not None:
        return cached

    entries = _build_social_proof_entries(store, settings_row, effective_mode, context, lang)
    cache.set(cache_key, entries, _SOCIAL_PROOF_CACHE_TTL_SECONDS)
    return entries
