"""
Signals for the catalog app.

slug_changed signal (ADR-005 §6, TICKET-016):
    Fires on pre_save for any slug-bearing model when the slug has changed on an
    existing instance.  The redirect-table consumer is wired in TICKET-016.

set_published_at_on_publish (T003):
    Fires on pre_save for Product.  When status transitions to 'active' and
    published_at is still None, sets published_at = now().

re_evaluate_smart_collections_on_product_save (TICKET-005):
    Fires on post_save for Product.  Re-evaluates smart collection membership so that
    saving a product immediately updates which smart collections it belongs to.

reapply_rules_on_variant_change (T005):
    Fires on post_save for ProductVariant.  Re-evaluates smart rules for the parent
    product when price, compare_at_price, or inventory_mode changes, so variant-price
    changes are reflected in smart collections without requiring the product to be saved.

Translation → Permalink lifecycle (TICKET-024):
    _pre_save_{product,collection}_translation: cache old status before save.
    _post_save_{product,collection}_translation: on status transition to/from PUBLISHED,
    create/activate or deactivate the corresponding Permalink (AC-104).
    Also handles _slug_hint (set by persist_output slug_only jobs) to update the slug
    on an auto-created permalink when the record is re-saved with the same PUBLISHED status.

Translation job creation (TICKET-024, MCP-050):
    _create_translation_jobs_for_product: fires on Product post_save; creates AiJob rows
    for each non-default StoreLanguage when the product is active.
    Staleness check (ADR-014 §Q5): only creates a job when a published translation is
    missing, stale (source_fingerprint mismatch), or the existing record is not published.
    Manually-overridden stale records are skipped.
    Similar handlers for Collection, VariantOption, VariantOptionValue, ProductImage.

    Flood guard: skip if a NOT_STARTED or IN_PROGRESS AiJob already exists for the same
    (store, job_type, target_model, target_id, lang, sub_type) tuple (MCP-051).
    sub_type is checked in Python on the candidate rows (volumes are small).

    VariantOption/VariantOptionValue signals create a single 'options' sub-type job per
    (product, language) covering ALL options and values (ADR-014 §Q4) instead of one
    job per option/value item.
"""

import hashlib
import logging

from django.db import IntegrityError, transaction
from django.db.models.signals import post_delete, post_save, pre_save
from django.dispatch import Signal, receiver

logger = logging.getLogger(__name__)

# Public signal consumed by TICKET-016 (redirect table)
slug_changed = Signal()  # provides: sender, instance, old_slug, new_slug


@receiver(pre_save)
def detect_slug_change(sender, instance, **kwargs):
    """
    Detects slug changes on any model with a `slug` field.
    Emits `slug_changed` with old_slug and new_slug when the slug has changed
    and the instance already exists in the DB (i.e. it's an update, not a create).
    """
    if not hasattr(instance, 'slug') or not instance.pk:
        return
    try:
        # Use cross_store_unsafe() when available (StoreOwnedModel subclasses) so that
        # the StoreScopedManager isolation guard is bypassed for this internal lookup.
        # Fall back to the default manager for plain models that have a slug field.
        manager = sender.objects
        if hasattr(manager, 'cross_store_unsafe'):
            old = manager.cross_store_unsafe().get(pk=instance.pk)
        else:
            old = manager.get(pk=instance.pk)
    except sender.DoesNotExist:
        return
    if old.slug and old.slug != instance.slug:
        slug_changed.send(
            sender=sender,
            instance=instance,
            old_slug=old.slug,
            new_slug=instance.slug,
        )


@receiver(pre_save, sender='catalog.Product')
def set_published_at_on_publish(sender, instance, **kwargs):
    """
    Sets published_at to now() when a Product transitions to 'active' status
    for the first time (i.e. published_at is still None).  Idempotent: once
    published_at is set it is never overwritten by this signal.
    """
    from django.utils import timezone

    if instance.status in ('active', 'published') and instance.published_at is None:
        instance.published_at = timezone.now()


def apply_smart_rules_for_product(product):
    """
    Re-evaluate smart collection membership for a single product.

    For every smart collection in the same store:
    - If the product matches the rules → ensure a CollectionProduct row exists.
    - If the product no longer matches → delete the CollectionProduct row if present.

    Called from:
    - re_evaluate_smart_collections_on_product_save (post_save on Product)
    - reapply_rules_on_variant_change (post_save on ProductVariant)
    - apply_smart_rules_all management command (nightly batch)

    Imports are deferred to avoid circular import issues at module load time.
    """
    from catalog.models import Collection, CollectionProduct
    from catalog.smart_rules import evaluate_product

    smart_collections = Collection.objects.for_store(product.store).filter(
        collection_type='smart'
    )
    for col in smart_collections:
        matches = evaluate_product(product, col.smart_rules, col.smart_rules_match)
        if matches:
            # cross_store_unsafe() is required here: the StoreScopedManager's base
            # get_queryset() returns a _RaisingQuerySet whose .get() path calls
            # _fetch_all() and raises IsolationError.  Using cross_store_unsafe()
            # gives a plain QuerySet; the lookup is already constrained by the exact
            # collection and product PKs, so no cross-tenant bleed is possible.
            CollectionProduct.objects.cross_store_unsafe().get_or_create(
                collection=col,
                product=product,
                defaults={'store': product.store, 'sort_key': ''},
            )
        else:
            # ADR-031 addendum audit (TICKET-051): this relied on delete()'s
            # fast-delete SQL path (Collector.can_fast_delete()) silently
            # bypassing StoreScopedManager's isolation raise entirely — the
            # exact "fast-path delete()" hole the addendum closes. Now that
            # _RaisingQuerySet.delete() always raises, this must scope
            # explicitly. `product.store` is safe: `col` came from
            # `Collection.objects.for_store(product.store)` above, so
            # `col.store == product.store` by construction.
            CollectionProduct.objects.for_store(product.store).filter(
                collection=col, product=product
            ).delete()


@receiver(post_save, sender='catalog.Product')
def re_evaluate_smart_collections_on_product_save(sender, instance, **kwargs):
    """
    Re-evaluates smart collection membership for the saved product (TICKET-005).
    Delegates to apply_smart_rules_for_product for the actual logic.
    """
    apply_smart_rules_for_product(instance)


@receiver(post_save, sender='catalog.ProductVariant')
def reapply_rules_on_variant_change(sender, instance, **kwargs):
    """
    Re-evaluate smart rules for the parent product when a variant's price or
    inventory changes (T005).

    Price-matching smart rules use the lowest active variant price.  A variant
    save that changes price without touching the parent product would otherwise
    leave smart collection memberships stale until the next nightly job.

    Optimization: when update_fields is provided (e.g. from bulk updates), skip
    if none of the price/inventory fields are included to avoid unnecessary DB work.
    """
    update_fields = kwargs.get('update_fields')
    if update_fields is not None:
        relevant = {'price', 'compare_at_price', 'inventory_mode'}
        if not (relevant & set(update_fields)):
            return
    apply_smart_rules_for_product(instance.product)


# ---------------------------------------------------------------------------
# Translation → Permalink lifecycle (TICKET-024, AC-104)
# ---------------------------------------------------------------------------

def _cache_translation_old_status(sender, instance):
    """
    Cache the translation's current status before save so that post_save can
    compare old vs new status and detect PUBLISHED transitions.

    Uses cross_store_unsafe() — the lookup is by PK and is internally scoped by
    the instance's own store FK, so no cross-tenant bleed is possible.
    """
    if not instance.pk:
        instance._old_status = None
        return
    manager = sender.objects
    qs = manager.cross_store_unsafe() if hasattr(manager, 'cross_store_unsafe') else manager.all()
    try:
        old = qs.get(pk=instance.pk)
        instance._old_status = old.status
    except sender.DoesNotExist:
        instance._old_status = None


def _sync_translation_permalink(instance, content_model_class, object_id, slug_factory):
    """
    Manage Permalink lifecycle on a translation PUBLISHED / DRAFT transition,
    and update the auto-created permalink slug when _slug_hint is set (slug_only jobs).

    PUBLISHED (new or staying PUBLISHED with _slug_hint):
      - If a Permalink (active or inactive) exists for (store, lang, content_type, pk):
          - Activate it (set is_active=True) if inactive.
          - If _slug_hint is set and the permalink is auto_created=True, update the slug.
            When the OLD slug was already active/routable (i.e. a real translated URL
            was live before this save), a SlugRedirect old→new is created via the same
            shared helper used by the default-language slug-change path
            (permalinks.redirects.create_slug_redirect — §XV-4, STATIC_PAGES_SEO_REVIEW.md
            H2), with identical loop-rejection / chain-collapse semantics.  When the
            permalink was NOT active before this save (e.g. the translation was never
            published, or was unpublished and is only now being republished), no redirect
            is created — there was no live URL for anyone to have followed.
      - If no Permalink exists: create one using _slug_hint > slug_factory() and
          is_active=True, auto_created=True.
          On IntegrityError (slug collision) log a warning — admin must set slug manually.

    DRAFT (was PUBLISHED):
      - Deactivate any active Permalink for this (store, lang, content_type, pk).
        The translated URL will return 404 until the translation is re-published.

    Called only when old_status != new_status OR _slug_hint is set.
    """
    from catalog.models import Product, TranslationStatus
    from django.contrib.contenttypes.models import ContentType
    from permalinks.models import Permalink, PermalinkTrigger
    from permalinks.redirects import create_slug_redirect

    old_status = getattr(instance, '_old_status', None)
    new_status = instance.status
    # Read slug_hint from the real model field first (ProductTranslation), then fall
    # back to the transient _slug_hint attribute (CollectionTranslation, legacy callers).
    slug_hint = getattr(instance, 'slug_hint', None) or getattr(instance, '_slug_hint', None)
    published = TranslationStatus.PUBLISHED

    # Early exit: no status change and no slug hint means nothing to do.
    if new_status == old_status and not slug_hint:
        return

    ct = ContentType.objects.get_for_model(content_model_class)

    if new_status == published:
        # Transition TO published (or re-save of published with slug hint)
        existing = (
            Permalink.objects
            .for_store(instance.store)
            .filter(lang=instance.lang_code, content_type=ct, object_id=object_id)
            .first()
        )
        if existing is not None:
            was_active = existing.is_active
            old_slug = existing.slug
            changed = False
            slug_changing = False
            if not existing.is_active:
                existing.is_active = True
                changed = True
            # Update slug from hint if provided and permalink was auto-created (ADR-014 §Q6).
            if slug_hint and existing.auto_created:
                from core.slugs import make_slug
                new_slug = make_slug(slug_hint)
                if new_slug and new_slug != existing.slug:
                    existing.slug = new_slug
                    changed = True
                    slug_changing = True
            if changed:
                if slug_changing and was_active:
                    # The old translated URL was live before this save — redirect it
                    # (H2). was_active (captured before mutation) is the gate, not the
                    # post-mutation is_active, so republishing a never-live translation
                    # with a new slug does not manufacture a 301 for an unvisitable URL.
                    with transaction.atomic():
                        existing.save()
                        create_slug_redirect(
                            instance.store,
                            old_slug,
                            existing.slug,
                            instance.lang_code,
                        )
                        # TICKET-042 / ADR-028 §2: cascade the rename to this
                        # product's ProductPageVersion permalinks in the same
                        # language (Product translations only — Collections have
                        # no page versions).
                        if content_model_class is Product:
                            from permalinks.registry import rename_page_version_permalinks_for_language
                            rename_page_version_permalinks_for_language(
                                instance.store, object_id, instance.lang_code, old_slug, existing.slug,
                            )
                else:
                    existing.save()
        else:
            # Determine slug: use hint first, then factory fallback.
            if slug_hint:
                from core.slugs import make_slug
                slug = make_slug(slug_hint) or slug_factory()
            else:
                slug = slug_factory()
            try:
                with transaction.atomic():
                    Permalink(
                        store=instance.store,
                        lang=instance.lang_code,
                        content_type=ct,
                        object_id=object_id,
                        slug=slug,
                        is_active=True,
                        auto_created=True,
                        trigger=PermalinkTrigger.SLUG_CHANGE,
                    ).save()
            except IntegrityError:
                logger.warning(
                    "Auto-create Permalink for %s pk=%s lang=%s failed: slug %r already taken. "
                    "Store admin must set the slug manually.",
                    content_model_class.__name__, object_id, instance.lang_code, slug,
                )

        # TICKET-042 / ADR-028 §2: a newly (re)activated product-language permalink
        # (whether just reactivated above, freshly created above, or already
        # PUBLISHED with no change this call) means every active ProductPageVersion
        # of this product should now have a permalink in this language too —
        # covers "translated product permalink published later" (late-translation
        # activation). Idempotent and cheap (capped at MAX_VERSIONS_PER_PRODUCT).
        if content_model_class is Product:
            from catalog.models import ProductPageVersion
            from permalinks.registry import sync_page_version_permalinks

            for version in ProductPageVersion.objects.for_store(instance.store).filter(
                product_id=object_id, is_active=True,
            ):
                sync_page_version_permalinks(version)

    elif old_status == published:
        # Transition FROM published — deactivate Permalink so the URL 404s (AC-104)
        (
            Permalink.objects
            .for_store(instance.store)
            .filter(lang=instance.lang_code, content_type=ct, object_id=object_id, is_active=True)
            .update(is_active=False)
        )


@receiver(pre_save, sender='catalog.ProductTranslation')
def _pre_save_product_translation(sender, instance, **kwargs):
    """Cache old status before ProductTranslation save (see _sync_translation_permalink)."""
    _cache_translation_old_status(sender, instance)


@receiver(post_save, sender='catalog.ProductTranslation')
def _post_save_product_translation(sender, instance, created, **kwargs):
    """
    Sync Permalink lifecycle when ProductTranslation status changes (TICKET-024).

    Slug fallback priority (when no slug_hint is set):
      1. Translated title (instance.title) → derive slug from the translated title.
      2. Source product slug (instance.product.slug) → when translated title is blank.

    This ensures that a newly published translation for a non-default language gets a
    permalink slug derived from the translated title, not the source-language slug.
    """
    from catalog.models import Product
    from core.slugs import make_slug

    def _slug_factory():
        translated_title = (instance.title or "").strip()
        if translated_title:
            return make_slug(translated_title)
        return instance.product.slug

    _sync_translation_permalink(
        instance=instance,
        content_model_class=Product,
        object_id=instance.product_id,
        slug_factory=_slug_factory,
    )


@receiver(pre_save, sender='catalog.CollectionTranslation')
def _pre_save_collection_translation(sender, instance, **kwargs):
    """Cache old status before CollectionTranslation save (see _sync_translation_permalink)."""
    _cache_translation_old_status(sender, instance)


@receiver(post_save, sender='catalog.CollectionTranslation')
def _post_save_collection_translation(sender, instance, created, **kwargs):
    """Sync Permalink lifecycle when CollectionTranslation status changes (TICKET-024)."""
    from catalog.models import Collection
    _sync_translation_permalink(
        instance=instance,
        content_model_class=Collection,
        object_id=instance.collection_id,
        slug_factory=lambda: f"collections/{instance.collection.slug}",
    )


# ---------------------------------------------------------------------------
# ProductPageVersion → Permalink lifecycle (TICKET-042, ADR-028 §2)
# ---------------------------------------------------------------------------

@receiver(pre_save, sender='catalog.ProductPageVersion')
def _cache_page_version_old_active(sender, instance, **kwargs):
    """
    Cache is_active before save so post_save can detect a True → False
    transition (deactivation) versus a fresh/reactivated True (sync).

    Uses cross_store_unsafe() — the lookup is by PK, internally scoped by the
    instance's own store FK, so no cross-tenant bleed is possible (same pattern
    as _cache_translation_old_status above).
    """
    if not instance.pk:
        instance._old_is_active = None
        return
    try:
        old = sender.objects.cross_store_unsafe().get(pk=instance.pk)
        instance._old_is_active = old.is_active
    except sender.DoesNotExist:
        instance._old_is_active = None


@receiver(post_save, sender='catalog.ProductPageVersion')
def _post_save_page_version(sender, instance, created, **kwargs):
    """
    Sync Permalink lifecycle on ProductPageVersion creation, reactivation,
    slug_suffix rename, or deactivation (TICKET-042, ADR-028 §2).

    - is_active=True (created, reactivated, or just renamed): create/reactivate/
      rename the Permalink rows for every language with an active product
      permalink (permalinks.registry.sync_page_version_permalinks).
    - is_active transitions True → False: deactivate the Permalink rows and
      302-redirect them to the primary product URL
      (permalinks.registry.deactivate_page_version_permalinks). A version
      created directly with is_active=False (never live) triggers nothing here.
    """
    from permalinks.registry import deactivate_page_version_permalinks, sync_page_version_permalinks

    old_active = getattr(instance, '_old_is_active', None)
    if instance.is_active:
        sync_page_version_permalinks(instance)
    elif old_active:
        deactivate_page_version_permalinks(instance)


@receiver(post_delete, sender='catalog.ProductPageVersion')
def _post_delete_page_version(sender, instance, **kwargs):
    """
    301-redirect any still-active Permalink rows for a deleted ProductPageVersion
    to the primary product URL (TICKET-042, ADR-028 §2 "version deleted" —
    permanent, so Pinterest link equity consolidates rather than 404ing, §XII).
    """
    from permalinks.registry import delete_page_version_permalinks
    delete_page_version_permalinks(instance)


# ---------------------------------------------------------------------------
# Translation job creation (TICKET-024, MCP-050, MCP-051)
# ---------------------------------------------------------------------------

def _active_non_default_languages(store):
    """
    Return the list of enabled non-default StoreLanguage lang_codes for the store.

    Uses a single queryset; all FK lookups are avoided.  Returns an empty list
    when the store has no non-default languages configured (safe no-op).
    """
    from stores.models import StoreLanguage
    return list(
        StoreLanguage.objects
        .filter(store=store, is_default=False, is_enabled=True)
        .values_list("lang_code", flat=True)
    )


def _flood_guard(store, job_type, target_model, target_id, lang, sub_type=""):
    """
    Return True if a non-terminal (NOT_STARTED or IN_PROGRESS) AiJob already
    exists for this (store, job_type, target_model, target_id, lang, sub_type) tuple.

    When True, the caller must skip job creation to avoid flooding the queue
    (MCP-051, ADR-014 §Q5).  cross_store_unsafe() is required because we have no
    request context inside a signal.

    When sub_type is provided, the check is done in Python on the candidate rows
    (volumes are small per ADR-014 §Q5) to avoid a JSONField lookup that may be
    slow or unsupported on all DB backends.
    """
    from aijobs.models import AiJob, AiJobStatus
    pending = AiJob.objects.cross_store_unsafe().filter(
        store=store,
        job_type=job_type,
        target_model=target_model,
        target_id=target_id,
        lang=lang,
        status__in=[AiJobStatus.NOT_STARTED, AiJobStatus.IN_PROGRESS],
    )
    if not sub_type:
        return pending.exists()
    # Check sub_type in Python (small N, avoids JSONField DB query)
    for job in pending:
        if (job.input_payload or {}).get("sub_type") == sub_type:
            return True
    return False


def _get_default_lang(store):
    """Return the default language code for the store, or 'en' as a safe fallback."""
    from stores.models import StoreLanguage
    try:
        return (
            StoreLanguage.objects
            .filter(store=store, is_default=True)
            .values_list("lang_code", flat=True)
            .get()
        )
    except StoreLanguage.DoesNotExist:
        return "en"


def _product_fingerprint(product):
    """
    Return the SHA-256 hex fingerprint of the product's translatable source fields.

    Field order: (title, description, seo_title, seo_description) — must be
    consistent with the fingerprint stored by persist_output_from_text.
    """
    from catalog.models import translation_fingerprint
    return translation_fingerprint(
        product.title or "",
        product.description or "",
        product.seo_title or "",
        product.seo_description or "",
    )


def _collection_fingerprint(collection):
    """
    Return the SHA-256 hex fingerprint of the collection's translatable source fields.

    Field order: (title, description, seo_title, seo_description).
    """
    from catalog.models import translation_fingerprint
    return translation_fingerprint(
        collection.title or "",
        collection.description or "",
        collection.seo_title or "",
        collection.seo_description or "",
    )


def _should_queue_product_translation(product, lang_code):
    """
    Return True if a new 'product' translation job should be created for the given
    product and target language (staleness check, ADR-014 §Q5).

    Rules:
      - No published translation record for this lang → True (new translation needed)
      - Published record + no fingerprint (legacy row, treat as stale) → True if not manually overridden
      - Published record + fingerprint matches current → False (up to date)
      - Published record + fingerprint mismatch + manually_edited_at NULL → True (stale)
      - Published record + fingerprint mismatch + manually_edited_at set → False (stale but overridden)
      - Non-published record (DRAFT) → True (not published yet, re-queue)
    """
    from catalog.models import ProductTranslation, TranslationStatus

    try:
        tr = ProductTranslation.objects.cross_store_unsafe().get(
            store=product.store, product=product, lang_code=lang_code
        )
    except ProductTranslation.DoesNotExist:
        return True  # No translation at all

    if tr.status != TranslationStatus.PUBLISHED:
        return True  # Not published yet

    if not tr.source_fingerprint:
        # Legacy row with no fingerprint: treat as stale unless manually edited.
        return tr.manually_edited_at is None

    current_fp = _product_fingerprint(product)
    if tr.source_fingerprint == current_fp:
        return False  # Up to date

    # Fingerprint mismatch: re-queue only if not manually overridden.
    return tr.manually_edited_at is None


def _should_queue_collection_translation(collection, lang_code):
    """
    Return True if a new 'collection' translation job should be created.
    Mirrors _should_queue_product_translation for Collection.
    """
    from catalog.models import CollectionTranslation, TranslationStatus

    try:
        tr = CollectionTranslation.objects.cross_store_unsafe().get(
            store=collection.store, collection=collection, lang_code=lang_code
        )
    except CollectionTranslation.DoesNotExist:
        return True

    if tr.status != TranslationStatus.PUBLISHED:
        return True

    if not tr.source_fingerprint:
        return tr.manually_edited_at is None

    current_fp = _collection_fingerprint(collection)
    if tr.source_fingerprint == current_fp:
        return False

    return tr.manually_edited_at is None


def _build_product_payload(product, source_lang, target_lang):
    """Build the input_payload dict for a Product translation job."""
    return {
        "sub_type": "product",
        "source_lang": source_lang,
        "target_lang": target_lang,
        "store_name": product.store.name,
        "fields": {
            "title": product.title,
            "description": product.description,
            "seo_title": product.seo_title,
            "seo_description": product.seo_description,
        },
        "length_limits": {
            "seo_title": 60,
            "seo_description": 160,
        },
    }


def _build_collection_payload(collection, source_lang, target_lang):
    """Build the input_payload dict for a Collection translation job."""
    return {
        "sub_type": "collection",
        "source_lang": source_lang,
        "target_lang": target_lang,
        "store_name": collection.store.name,
        "fields": {
            "title": collection.title,
            "description": collection.description,
            "seo_title": collection.seo_title,
            "seo_description": collection.seo_description,
        },
        "length_limits": {
            "seo_title": 60,
            "seo_description": 160,
        },
    }


def _build_options_payload(product, source_lang, target_lang):
    """
    Build the input_payload dict for an 'options' matrix translation job.

    Collects ALL VariantOptions and their VariantOptionValues for the product so
    that the AI translates the full option matrix in one pass (ADR-014 §Q4).
    """
    from catalog.models import VariantOption

    options_qs = (
        VariantOption.objects.cross_store_unsafe()
        .filter(product=product)
        .prefetch_related('values')
        .order_by('position', 'id')
    )
    options_data = []
    for opt in options_qs:
        values_data = [
            {"value_pk": v.pk, "value": v.value}
            for v in opt.values.order_by('position', 'id')
        ]
        options_data.append({
            "option_pk": opt.pk,
            "name": opt.name,
            "values": values_data,
        })

    return {
        "sub_type": "options",
        "source_lang": source_lang,
        "target_lang": target_lang,
        "store_name": product.store.name,
        "options": options_data,
    }


def _create_translation_jobs(store, target_model, target_id, payload_factory, sub_type=""):
    """
    Create one AiJob per enabled non-default StoreLanguage for the given target.

    Always uses the 'translation' job type (catalog.ai_jobs.TRANSLATION_JOB_TYPE).
    Flood guard (MCP-051): skips creation when a NOT_STARTED or IN_PROGRESS job
    already exists for the same (store, job_type, target_model, target_id, lang, sub_type).
    Uses aijobs.service.create_job() — the only sanctioned job-creation path.

    Imports are deferred to avoid circular imports: signals.py is imported by
    apps.py ready() which runs before all app registrations complete.
    """
    from catalog.ai_jobs import TRANSLATION_JOB_TYPE
    from aijobs.service import create_job

    lang_codes = _active_non_default_languages(store)
    source_lang = _get_default_lang(store)
    for lang_code in lang_codes:
        if _flood_guard(store, TRANSLATION_JOB_TYPE, target_model, target_id, lang_code, sub_type):
            continue
        payload = payload_factory(source_lang, lang_code)
        create_job(
            store=store,
            job_type=TRANSLATION_JOB_TYPE,
            target_model=target_model,
            target_id=target_id,
            input_payload=payload,
            lang=lang_code,
            created_by="trigger",
        )


@receiver(post_save, sender='catalog.Product')
def _create_translation_jobs_for_product(sender, instance, **kwargs):
    """
    Create AiJobs for Product text-field translation when the product is active.

    Staleness check (ADR-014 §Q5): for each non-default language, only queues a job
    when a published translation is missing, stale (source_fingerprint mismatch), or
    not yet published.  Manually-overridden stale records are skipped.

    Flood guard prevents re-queueing when a pending or in-flight job already exists.
    """
    if instance.status != 'active':
        return

    from catalog.ai_jobs import TRANSLATION_JOB_TYPE
    from aijobs.service import create_job

    lang_codes = _active_non_default_languages(instance.store)
    source_lang = _get_default_lang(instance.store)

    for lang_code in lang_codes:
        # Staleness check: only queue if the translation is missing or stale.
        if not _should_queue_product_translation(instance, lang_code):
            continue
        if _flood_guard(
            instance.store, TRANSLATION_JOB_TYPE,
            "catalog.Product", instance.pk, lang_code, "product"
        ):
            continue
        payload = _build_product_payload(instance, source_lang, lang_code)
        create_job(
            store=instance.store,
            job_type=TRANSLATION_JOB_TYPE,
            target_model="catalog.Product",
            target_id=instance.pk,
            input_payload=payload,
            lang=lang_code,
            created_by="trigger",
        )


@receiver(post_save, sender='catalog.Collection')
def _create_translation_jobs_for_collection(sender, instance, **kwargs):
    """
    Create AiJobs for Collection text-field translation when the collection is published.

    Staleness check mirrors the Product handler.
    """
    if not instance.is_published:
        return

    from catalog.ai_jobs import TRANSLATION_JOB_TYPE
    from aijobs.service import create_job

    lang_codes = _active_non_default_languages(instance.store)
    source_lang = _get_default_lang(instance.store)

    for lang_code in lang_codes:
        if not _should_queue_collection_translation(instance, lang_code):
            continue
        if _flood_guard(
            instance.store, TRANSLATION_JOB_TYPE,
            "catalog.Collection", instance.pk, lang_code, "collection"
        ):
            continue
        payload = _build_collection_payload(instance, source_lang, lang_code)
        create_job(
            store=instance.store,
            job_type=TRANSLATION_JOB_TYPE,
            target_model="catalog.Collection",
            target_id=instance.pk,
            input_payload=payload,
            lang=lang_code,
            created_by="trigger",
        )


@receiver(post_save, sender='catalog.VariantOption')
def _create_translation_jobs_for_variant_option(sender, instance, **kwargs):
    """
    Create a single 'options' matrix AiJob per language for the parent product when
    a VariantOption is saved and the product is active (ADR-014 §Q4).

    One job per (product, language) covers ALL options and values — not one job per
    option. The flood guard prevents duplicate jobs for the same product+lang+sub_type.
    """
    if instance.product.status != 'active':
        return
    product = instance.product
    _create_translation_jobs(
        store=instance.store,
        target_model="catalog.Product",
        target_id=product.pk,
        payload_factory=lambda src, tgt: _build_options_payload(product, src, tgt),
        sub_type="options",
    )


@receiver(post_save, sender='catalog.VariantOptionValue')
def _create_translation_jobs_for_variant_option_value(sender, instance, **kwargs):
    """
    Create a single 'options' matrix AiJob per language for the parent product when
    a VariantOptionValue is saved and the product is active (ADR-014 §Q4).

    Mirrors _create_translation_jobs_for_variant_option — both go through the
    same product-level job with sub_type='options'.
    """
    if instance.option.product.status != 'active':
        return
    product = instance.option.product
    _create_translation_jobs(
        store=instance.store,
        target_model="catalog.Product",
        target_id=product.pk,
        payload_factory=lambda src, tgt: _build_options_payload(product, src, tgt),
        sub_type="options",
    )


@receiver(post_save, sender='catalog.ProductImage')
def _create_translation_jobs_for_product_image(sender, instance, **kwargs):
    """
    Create AiJobs for ProductImage alt-text translation when the parent product is active.

    Only creates jobs when the source alt_text is non-empty — there is nothing to
    translate on a blank alt text.
    """
    if instance.product.status != 'active':
        return
    if not instance.alt_text:
        return
    _create_translation_jobs(
        store=instance.store,
        target_model="catalog.ProductImage",
        target_id=instance.pk,
        payload_factory=lambda src, tgt: {
            "sub_type": "image",
            "source_lang": src,
            "target_lang": tgt,
            "store_name": instance.store.name,
            "fields": {"alt_text": instance.alt_text},
        },
        sub_type="image",
    )
