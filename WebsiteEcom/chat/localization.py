"""
Read-only localization helpers shared by chat/tools.py (ADR-024 Decision 3).

Fallback rule (same rule the storefront itself uses, ADR-008/ADR-014): a
published translation for the session locale is used when it exists; otherwise
the source-language field is returned. Never raises/404s on a missing
translation — that rule is specific to the *rendered storefront page*
(storefront/views.py product_page), not to chat tool results, which must
always produce *some* grounded answer.

Every function here is read-only (SELECT only) — required by ADR-024 Decision 7
/ AC-CHAT-04 (the tool layer performs no INSERT/UPDATE/DELETE).
"""

from django.contrib.contenttypes.models import ContentType

from catalog.models import CollectionTranslation, ProductTranslation, TranslationStatus
from pages.models import StaticPageTranslation
from permalinks.models import Permalink
from permalinks.resolver import base_url
from stores.models import StoreLanguage


def localized_product_fields(store, product, lang_code: str) -> tuple[str, str]:
    """Return (title, description) for `product`, using a published translation
    for lang_code when one exists, else the source-language fields."""
    if lang_code and lang_code != store.primary_language:
        translation = (
            ProductTranslation.objects.for_store(store)
            .filter(product=product, lang_code=lang_code, status=TranslationStatus.PUBLISHED)
            .first()
        )
        if translation is not None:
            return (translation.title or product.title), (translation.description or product.description)
    return product.title, product.description


def localized_collection_fields(store, collection, lang_code: str) -> tuple[str, str]:
    """Return (title, description) for `collection`, same fallback rule as products."""
    if lang_code and lang_code != store.primary_language:
        translation = (
            CollectionTranslation.objects.for_store(store)
            .filter(collection=collection, lang_code=lang_code, status=TranslationStatus.PUBLISHED)
            .first()
        )
        if translation is not None:
            return (
                (translation.title or collection.title),
                (translation.description or collection.description),
            )
    return collection.title, collection.description


def localized_static_page_fields(store, page, lang_code: str) -> tuple[str, str]:
    """Return (title, body) for `page`, same fallback rule as products."""
    if lang_code and lang_code != store.primary_language:
        translation = (
            StaticPageTranslation.objects.for_store(store)
            .filter(page=page, lang_code=lang_code, status=TranslationStatus.PUBLISHED)
            .first()
        )
        if translation is not None:
            return (translation.title or page.title), (translation.body or page.body)
    return page.title, page.body


def absolute_permalink_url(store, obj, lang_code: str) -> str | None:
    """
    Return the absolute storefront URL for `obj` (a Product or Collection instance),
    trying lang_code first and falling back to the store's primary language.

    Returns None when no active Permalink / configured StoreLanguage can be found
    (e.g. the object was never published) — callers must treat this as "no link
    available", not as an error.
    """
    content_type = ContentType.objects.get_for_model(obj)
    for candidate_lang in (lang_code, store.primary_language):
        if not candidate_lang:
            continue
        permalink = (
            Permalink.objects.for_store(store)
            .filter(
                content_type=content_type,
                object_id=obj.pk,
                lang=candidate_lang,
                is_active=True,
            )
            .first()
        )
        if permalink is None:
            continue
        store_language = (
            StoreLanguage.objects.select_related("domain")
            .filter(store=store, lang_code=candidate_lang)
            .first()
        )
        if store_language is None:
            continue
        return f"{base_url(store_language)}/{permalink.slug}/"
    return None
