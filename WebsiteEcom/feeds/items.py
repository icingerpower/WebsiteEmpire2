"""
Shared feed-item builder (ADR-026 D4/D5/D6, TICKET-033-A).

build_feed_items(target) is the ONLY place a feed item dict is assembled;
feeds/providers.py only reshapes/renames these values (item_attributes()) —
no per-provider recomputation of money, IDs, or URLs (D4: "Both columns
below are emitted from the same values").

Variant handling (D5, drift-critical): one item per active ProductVariant,
`id = catalog_item_id(product, variant)` imported directly from
`pixels.events` — the pixels already emit variant-level content_ids
everywhere, so the feed MUST present the same ID space or dynamic
retargeting breaks silently. Re-implementing the f-string locally is
forbidden; feeds/tests/test_drift.py asserts this module resolves the
SAME function object.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from decimal import Decimal

from django.contrib.contenttypes.models import ContentType
from django.utils.html import strip_tags

from catalog.models import (
    Collection,
    CollectionProduct,
    InventoryMode,
    Product,
    ProductImage,
    ProductTranslation,
    ProductVariant,
    TranslationStatus,
)
from currency.resolver import display_amount
from feeds.currencies import feed_display_currency
from feeds.models import FeedConfig, FeedScope
from permalinks.models import Permalink
from permalinks.resolver import base_url
# NON-NEGOTIABLE (ADR-026 D5): the ONLY permitted source of g:id. Never
# re-implement the "{product.pk}_{variant.pk}" f-string locally — see
# feeds/tests/test_drift.py.
from pixels.events import catalog_item_id

_TITLE_MAX_CHARS = 150
_DESCRIPTION_MAX_CHARS = 5000
_MAX_ADDITIONAL_IMAGES = 10


@dataclass
class FeedBuildResult:
    """Return value of build_feed_items(): the item list + exclusion audit trail."""

    items: list[dict] = field(default_factory=list)
    exclusions: list[dict] = field(default_factory=list)

    @property
    def excluded_count(self) -> int:
        return len(self.exclusions)


def _exclude(exclusions: list[dict], reason: str, product_id: int, variant_id: int | None = None) -> None:
    """Append one persisted exclusion entry (AC-211: reason must be visible, never silent)."""
    exclusions.append({"reason": reason, "product_id": product_id, "variant_id": variant_id})


def absolute_media_url(store_language, file_field) -> str:
    """
    Return an absolute URL for a media FileField (ADR-026 D4 "one helper
    absolute_media_url(store_language, file)").

    If the storage backend already returns an absolute URL (S3 in
    production — MEDIA_URL is itself an https:// prefix there), use it
    as-is. Otherwise (local FileSystemStorage, relative MEDIA_URL) compose
    scheme+host from the feed's OWN StoreLanguage domain.
    """
    url = file_field.url
    if url.startswith("http://") or url.startswith("https://"):
        return url
    return f"https://{store_language.domain.host}{url}"


def _format_money(amount: Decimal, currency_code: str) -> str:
    """'{amount} {CCY}' — Decimal only, never float (D4)."""
    return f"{amount} {currency_code}"


def _availability_for(variant) -> tuple[str | None, str | None, str | None]:
    """
    Map inventory_mode -> (availability, availability_date_iso, exclusion_reason)
    per ADR-026 D4b. Exactly one of (availability, exclusion_reason) is set;
    availability_date is only ever set alongside availability='preorder'.
    """
    mode = variant.inventory_mode

    if mode == InventoryMode.NO_TRACKING:
        return "in stock", None, None
    if mode == InventoryMode.FIXED_QTY:
        if variant.quantity and variant.quantity > 0:
            return "in stock", None, None
        return "out of stock", None, None  # AC-212: included, not excluded
    if mode == InventoryMode.SOLD_OUT:
        return "out of stock", None, None  # AC-212: included, not excluded
    if mode == InventoryMode.FAKE_SERVER:
        return "in stock", None, None
    if mode == InventoryMode.PRESALE:
        if variant.presale_ships_at:
            return "preorder", variant.presale_ships_at.isoformat(), None
        return None, None, "presale_no_date"
    if mode == InventoryMode.ASK_WHEN_AVAILABLE:
        return "out of stock", None, None
    if mode == InventoryMode.QUOTATION:
        return None, None, "quotation_mode"

    # Defensive: an unknown/future inventory mode is excluded rather than
    # silently guessing an availability value (§XV-1).
    return None, None, "unknown_inventory_mode"


def build_feed_items(target) -> FeedBuildResult:
    """
    Build the FeedItem list + exclusion audit trail for one FeedTarget
    (ADR-026 D4/D5/D6).

    Raises feeds.currencies.FeedCurrencyError when the target country has no
    usable display currency — callers (feeds/tasks.py) catch this and set the
    whole target to ERROR (FEED-008, loud failure, never a silent fallback).
    """
    store = target.store
    store_language = target.store_language
    lang_code = store_language.lang_code
    is_source_language = store_language.is_default

    # FEED-008: resolve once per target — every item in this feed is priced
    # in the SAME currency (raises loudly before any item is built).
    currency_code = feed_display_currency(store, target.country_code)

    feed_config = FeedConfig.objects.for_store(store).get(provider=target.provider)

    products_qs = Product.objects.for_store(store).filter(status=Product.STATUS_ACTIVE)
    if feed_config.scope == FeedScope.COLLECTIONS:
        # feed_config.collections (M2M to catalog.Collection, a StoreOwnedModel)
        # cannot be read via the bare related manager — Collection.objects is a
        # StoreScopedManager, so .collections.all()/.values_list() raises
        # IsolationError on iteration (core/managers.py). Query explicitly
        # scoped instead (ADR-001 §4).
        collection_ids = list(
            Collection.objects.for_store(store)
            .filter(feed_configs=feed_config)
            .values_list("pk", flat=True)
        )
        product_ids = list(
            CollectionProduct.objects.for_store(store)
            .filter(collection_id__in=collection_ids)
            .values_list("product_id", flat=True)
            .distinct()
        )
        products_qs = products_qs.filter(pk__in=product_ids)

    content_type = ContentType.objects.get_for_model(Product)
    result = FeedBuildResult()

    for product in products_qs.order_by("pk").iterator():
        # D6 point 3: no active Permalink for (product, feed language) ->
        # excluded, reason 'no_translation'. This is the SAME gate as
        # g:link's "canonical primary URL" rule — no separate lookup exists.
        permalink = (
            Permalink.objects.for_store(store)
            .filter(content_type=content_type, object_id=product.pk, lang=lang_code, is_active=True)
            .first()
        )
        if permalink is None:
            _exclude(result.exclusions, "no_translation", product.pk)
            continue

        if is_source_language:
            # D4: "source Product.title for the store's source language" —
            # the store's own default StoreLanguage reads Product's
            # untranslated fields directly, never ProductTranslation.
            title_source = product.title
            description_source = product.description
        else:
            translation = (
                ProductTranslation.objects.for_store(store)
                .filter(product=product, lang_code=lang_code, status=TranslationStatus.PUBLISHED)
                .first()
            )
            if translation is None:
                # Belt-and-suspenders (D6): a non-default-language Permalink is
                # only ever created when a translation is published
                # (catalog/signals.py), so this should be unreachable — but
                # never trust that invariant silently; re-check and exclude
                # loudly rather than emit an item with no title (§XV-1).
                _exclude(result.exclusions, "no_translation", product.pk)
                continue
            title_source = translation.title
            description_source = translation.description

        # D6 point 4: zero images -> excluded, reason 'no_image' (Google
        # rejects imageless items).
        images = list(
            ProductImage.objects.for_store(store)
            .filter(product=product)
            .order_by("-is_primary", "display_order")
        )
        if not images:
            _exclude(result.exclusions, "no_image", product.pk)
            continue

        link = f"{base_url(store_language)}/{permalink.slug}/"
        image_link = absolute_media_url(store_language, images[0].image)
        additional_image_links = [
            absolute_media_url(store_language, img.image)
            for img in images[1 : 1 + _MAX_ADDITIONAL_IMAGES]
        ]

        description_text = strip_tags(description_source or "") or title_source
        description_text = description_text[:_DESCRIPTION_MAX_CHARS]

        variants = list(
            ProductVariant.objects.for_store(store)
            .filter(product=product, is_active=True)
            .order_by("position", "pk")
        )
        multi_variant = len(variants) > 1

        for variant in variants:
            availability, availability_date, exclusion_reason = _availability_for(variant)
            if exclusion_reason is not None:
                _exclude(result.exclusions, exclusion_reason, product.pk, variant.pk)
                continue

            title = title_source
            if multi_variant and variant.title:
                title = f"{title_source} — {variant.title}"
            title = title[:_TITLE_MAX_CHARS]

            # Human decision 2026-07-11 (Risks guard over the D4 table literal):
            # the compare-at/sale-price pair is only ever emitted when
            # compare_at_price is STRICTLY greater than price. Any other
            # relationship (equal, or compare_at_price lower than price —
            # e.g. a stale/incorrectly-entered compare_at) falls back to
            # g:price = variant.price alone, with no sale_price. Reading
            # compare_at_price unconditionally into regular_base (as before)
            # would silently under-report the price to the feed whenever
            # compare_at_price < price.
            if variant.compare_at_price is not None and variant.compare_at_price > variant.price:
                regular_amount = display_amount(variant.compare_at_price, store.default_currency, currency_code)
                price_str = _format_money(regular_amount, currency_code)
                sale_amount = display_amount(variant.price, store.default_currency, currency_code)
                sale_price_str = _format_money(sale_amount, currency_code)
            else:
                regular_amount = display_amount(variant.price, store.default_currency, currency_code)
                price_str = _format_money(regular_amount, currency_code)
                sale_price_str = None

            brand = product.vendor or None
            mpn = variant.sku or None

            result.items.append({
                "id": catalog_item_id(product, variant),
                "item_group_id": str(product.pk),
                "title": title,
                "description": description_text,
                "link": link,
                "image_link": image_link,
                "additional_image_links": additional_image_links,
                "price": price_str,
                "sale_price": sale_price_str,
                "availability": availability,
                "availability_date": availability_date,
                "condition": "new",
                "brand": brand,
                "mpn": mpn,
                "identifier_exists": None if (brand and mpn) else "no",
            })

    return result
