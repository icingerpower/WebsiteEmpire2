"""
Signals for the pages app (ADR-018 D1 §Permalink lifecycle / §4 AiJob triggers / D5 seeding).

Connected via PagesConfig.ready().

0. Seeding — post_save on Store (created=True): seeds the 7 draft StaticPages for
   every newly created store (ADR-018 D5). The idempotent `seed_static_pages`
   management command (pages/management/commands/seed_static_pages.py) covers
   existing stores and backfills/repairs — both call the same pages/seeding.py
   helper so the seed set is defined exactly once.

1. Publish/unpublish (source language) — post_save on StaticPage:
   Mirrors permalinks/signals.py's on_product_created + sync_permalink_on_status_change,
   combined into one handler since both concerns (create-on-first-publish,
   reactivate/deactivate-on-update) live together for this model.
   Path convention: path = slug (root-level, like Product — no 'pages/' prefix).
   Unpublishing deactivates ALL permalinks for the page (every language), exactly like
   a Product leaving 'active' status — mirrors sync_permalink_on_status_change.
   Republishing reactivates the default-language permalink AND every translated
   permalink whose StaticPageTranslation is still PUBLISHED (SEO review H1 fix).
   This is deliberately NOT the Product blanket-reactivate: Product's
   sync_permalink_on_status_change reactivates every inactive permalink regardless
   of translation state, which would incorrectly resurrect a translated URL whose
   translation was reverted to DRAFT while the page was unpublished. Filtering by
   translation status is correct for both models; only StaticPage's translations
   can independently regress to DRAFT after the page itself was unpublished.

2. Translations — pre_save/post_save on StaticPageTranslation:
   Delegates to catalog.signals._sync_translation_permalink (already generic — takes
   content_model_class, object_id, slug_factory). Publishing a translation activates
   the translated Permalink; reverting to draft deactivates it (404, AC-104 parity).

3. Slug changes on StaticPage are handled by the existing unfiltered
   catalog.signals.detect_slug_change (pre_save on any model with slug+pk) +
   permalinks.signals.handle_slug_change (extended with a StaticPage branch there).
   Nothing to do here.

4. AiJob translation triggers — post_save on StaticPage when is_published=True:
   one job per enabled non-default StoreLanguage, sub_type='static_page'. Reuses
   catalog.signals helpers (_active_non_default_languages, _get_default_lang,
   _flood_guard) rather than forking their logic (design-pattern-ideas.txt §XV-4).
"""

import logging

from django.db import IntegrityError, transaction
from django.db.models.signals import post_save, pre_save
from django.dispatch import receiver

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# 0. Seeding — post_save on Store creation (ADR-018 D5)
# ---------------------------------------------------------------------------

@receiver(post_save, sender="stores.Store")
def seed_static_pages_for_new_store(sender, instance, created, **kwargs):
    """Seed the 7 draft StaticPages for every newly created store (ADR-018 D5)."""
    if not created:
        return
    from pages.seeding import seed_pages_for_store
    seed_pages_for_store(instance)


# ---------------------------------------------------------------------------
# 1. StaticPage publish/unpublish → Permalink lifecycle (default language)
# ---------------------------------------------------------------------------

def _reactivate_published_translation_permalinks(store, ct, page_id):
    """
    Reactivate (never create) the Permalink for every language whose
    StaticPageTranslation is currently PUBLISHED (SEO review H1 fix).

    Called on republish, in addition to the default-language reactivation, so a
    page that was unpublished with an already-PUBLISHED FR translation gets its
    FR permalink back instead of 404ing forever. Deliberately filtered by
    translation status rather than a Product-style blanket reactivate: a
    translation that regressed to DRAFT while the page was unpublished must stay
    inactive on republish (AC-104 parity holds even across an unpublish/republish
    cycle).

    Only reactivates existing rows — creation on first publish is already handled
    by _post_save_static_page_translation (fired when the translation itself
    transitions to PUBLISHED), so there is nothing to create here.
    """
    from catalog.models import TranslationStatus
    from pages.models import StaticPageTranslation
    from permalinks.models import Permalink

    published_langs = list(
        StaticPageTranslation.objects.cross_store_unsafe()
        .filter(store=store, page_id=page_id, status=TranslationStatus.PUBLISHED)
        .values_list("lang_code", flat=True)
    )
    if not published_langs:
        return
    Permalink.objects.cross_store_unsafe().filter(
        store=store, content_type=ct, object_id=page_id, lang__in=published_langs,
    ).update(is_active=True)


@receiver(post_save, sender="pages.StaticPage")
def sync_static_page_permalink(sender, instance, created, **kwargs):
    """
    Keep the Permalink lifecycle in sync with StaticPage.is_published.

    - is_published=True  → create (if missing) or reactivate the default-language
      Permalink for (store, store.primary_language, slug); auto_created=True.
      Also reactivates every translated permalink whose StaticPageTranslation is
      PUBLISHED (see _reactivate_published_translation_permalinks — SEO review H1).
    - is_published=False → deactivate ALL permalinks for this page (every language),
      mirroring Product's sync_permalink_on_status_change — a fully-unpublished page
      must 404 in every translated URL too.

    Uses cross_store_unsafe() — required in signal context (no view-scoped manager).
    """
    from django.contrib.contenttypes.models import ContentType
    from permalinks.models import Permalink, PermalinkTrigger

    store = instance.store
    ct = ContentType.objects.get_for_model(sender)

    if not instance.is_published:
        Permalink.objects.cross_store_unsafe().filter(
            store=store, content_type=ct, object_id=instance.pk,
        ).update(is_active=False)
        return

    lang = store.primary_language
    existing = (
        Permalink.objects.cross_store_unsafe()
        .filter(store=store, content_type=ct, object_id=instance.pk, lang=lang)
        .first()
    )
    if existing is not None:
        if not existing.is_active:
            existing.is_active = True
            existing.save()
    else:
        try:
            with transaction.atomic():
                Permalink(
                    store=store,
                    lang=lang,
                    content_type=ct,
                    object_id=instance.pk,
                    slug=instance.slug,
                    is_active=True,
                    auto_created=True,
                    trigger=PermalinkTrigger.MANUAL,
                ).save()
        except IntegrityError:
            logger.warning(
                "Auto-create Permalink for StaticPage pk=%s lang=%s failed: slug %r "
                "already taken. Store admin must set the slug manually.",
                instance.pk, lang, instance.slug,
            )

    _reactivate_published_translation_permalinks(store, ct, instance.pk)


# ---------------------------------------------------------------------------
# 2. StaticPageTranslation publish/unpublish → translated Permalink lifecycle
# ---------------------------------------------------------------------------

def _cache_translation_old_status(sender, instance):
    """Cache the translation's current status before save (see catalog/signals.py)."""
    if not instance.pk:
        instance._old_status = None
        return
    try:
        old = sender.objects.cross_store_unsafe().get(pk=instance.pk)
        instance._old_status = old.status
    except sender.DoesNotExist:
        instance._old_status = None


@receiver(pre_save, sender="pages.StaticPageTranslation")
def _pre_save_static_page_translation(sender, instance, **kwargs):
    """Cache old status before StaticPageTranslation save (see _sync_translation_permalink)."""
    _cache_translation_old_status(sender, instance)


@receiver(post_save, sender="pages.StaticPageTranslation")
def _post_save_static_page_translation(sender, instance, created, **kwargs):
    """
    Sync Permalink lifecycle when StaticPageTranslation status changes.

    Slug fallback priority (when no slug_hint is set):
      1. Translated title (instance.title) → derive slug from the translated title.
      2. Source page slug (instance.page.slug) → when translated title is blank.
    """
    from catalog.signals import _sync_translation_permalink
    from core.slugs import make_slug
    from pages.models import StaticPage

    def _slug_factory():
        translated_title = (instance.title or "").strip()
        if translated_title:
            return make_slug(translated_title)
        return instance.page.slug

    _sync_translation_permalink(
        instance=instance,
        content_model_class=StaticPage,
        object_id=instance.page_id,
        slug_factory=_slug_factory,
    )


# ---------------------------------------------------------------------------
# 4. AiJob translation triggers (ADR-018 D1 §4)
# ---------------------------------------------------------------------------

def _static_page_fingerprint(page):
    """SHA-256 fingerprint of the page's translatable source fields (fixed order)."""
    from pages.models import static_page_fingerprint
    return static_page_fingerprint(page)


def _should_queue_static_page_translation(page, lang_code):
    """
    Return True if a new 'static_page' translation job should be created for the
    given page and target language (staleness check — mirrors
    catalog.signals._should_queue_product_translation exactly).
    """
    from catalog.models import TranslationStatus
    from pages.models import StaticPageTranslation

    try:
        tr = StaticPageTranslation.objects.cross_store_unsafe().get(
            store=page.store, page=page, lang_code=lang_code
        )
    except StaticPageTranslation.DoesNotExist:
        return True  # No translation at all

    if tr.status != TranslationStatus.PUBLISHED:
        return True  # Not published yet

    if not tr.source_fingerprint:
        # Legacy row with no fingerprint: treat as stale unless manually edited.
        return tr.manually_edited_at is None

    current_fp = _static_page_fingerprint(page)
    if tr.source_fingerprint == current_fp:
        return False  # Up to date

    # Fingerprint mismatch: re-queue only if not manually overridden.
    return tr.manually_edited_at is None


def _build_static_page_payload(page, source_lang, target_lang):
    """Build the input_payload dict for a StaticPage translation job."""
    return {
        "sub_type": "static_page",
        "source_lang": source_lang,
        "target_lang": target_lang,
        "store_name": page.store.name,
        "fields": {
            "title": page.title,
            "body": page.body,
            "seo_title": page.seo_title,
            "seo_description": page.seo_description,
        },
        "length_limits": {
            "seo_title": 60,
            "seo_description": 160,
        },
    }


@receiver(post_save, sender="pages.StaticPage")
def create_translation_jobs_for_static_page(sender, instance, **kwargs):
    """
    Create AiJobs for StaticPage text-field translation when the page is published.

    Staleness check: only queues a job when a published translation is missing,
    stale (source_fingerprint mismatch), or not yet published. Manually-overridden
    stale records are skipped. Flood guard prevents re-queueing when a pending or
    in-flight job already exists (reuses catalog.signals helpers — §XV-4).
    """
    if not instance.is_published:
        return

    from aijobs.service import create_job
    from catalog.ai_jobs import TRANSLATION_JOB_TYPE
    from catalog.signals import _active_non_default_languages, _flood_guard, _get_default_lang

    lang_codes = _active_non_default_languages(instance.store)
    source_lang = _get_default_lang(instance.store)

    for lang_code in lang_codes:
        if not _should_queue_static_page_translation(instance, lang_code):
            continue
        if _flood_guard(
            instance.store, TRANSLATION_JOB_TYPE,
            "pages.StaticPage", instance.pk, lang_code, "static_page",
        ):
            continue
        payload = _build_static_page_payload(instance, source_lang, lang_code)
        create_job(
            store=instance.store,
            job_type=TRANSLATION_JOB_TYPE,
            target_model="pages.StaticPage",
            target_id=instance.pk,
            input_payload=payload,
            lang=lang_code,
            created_by="trigger",
        )
