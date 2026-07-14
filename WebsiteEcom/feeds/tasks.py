"""
Celery tasks: debounced beat regeneration + nightly full rebuild (ADR-026 D2/D3).

regenerate_dirty_feeds — beat task, every FEEDS_REGENERATE_INTERVAL_MIN
    minutes (default 30, FEED-005): reconciles the FeedTarget matrix, then
    regenerates every dirty target. Idempotent and safe to re-run at any
    point (§XV-6) — each target's own is_dirty flag is the correctness
    boundary.

rebuild_all_feeds — nightly full rebuild (§V safety net for mutation paths
    that bypass signals: bulk update(), raw SQL, future imports). Reconciles,
    marks EVERY target dirty, then regenerates via the same per-target
    routine (single generation code path, §XV-4 — never a second copy).

Both tasks share _process_target(), the ONLY place a FeedTarget's file is
(re)generated — FeedProvider.run() (feeds/registry.py) calls this too, so
there is exactly one generation code path regardless of caller.
"""

import logging
import os
import tempfile

from celery import shared_task
from django.conf import settings
from django.utils import timezone

from feeds.currencies import FeedCurrencyError
from feeds.items import build_feed_items
from feeds.registry import get_provider
from feeds.renderer import render_feed

logger = logging.getLogger(__name__)


def _reconcile_matrix() -> None:
    """
    Create missing FeedTarget rows and delete orphaned ones (ADR-026 D2).

    A target is wanted iff: its FeedConfig row is_enabled AND its
    StoreLanguage is_enabled AND its country_code is one of that
    StoreLanguage's ShippingCountry rows. Deleting an orphaned target also
    deletes its file — a target whose language was disabled or country
    removed must stop being served (ML-012); the resulting 403/404 is the
    visible state, never a zombie file.
    """
    from feeds.models import FeedConfig, FeedTarget, FeedTargetStatus
    from stores.models import ShippingCountry, Store, StoreLanguage

    for store in Store.objects.active():
        enabled_configs = list(FeedConfig.objects.for_store(store).filter(is_enabled=True))
        enabled_provider_keys = {c.provider for c in enabled_configs}

        wanted: set[tuple[str, int, str]] = set()
        for feed_config in enabled_configs:
            store_languages = StoreLanguage.objects.filter(store=store, is_enabled=True)
            for sl in store_languages:
                countries = ShippingCountry.objects.filter(store_language=sl).values_list(
                    "country_code", flat=True
                )
                for country_code in countries:
                    wanted.add((feed_config.provider, sl.pk, country_code))
                    FeedTarget.objects.for_store(store).get_or_create(
                        store=store,
                        provider=feed_config.provider,
                        store_language_id=sl.pk,
                        country_code=country_code,
                        defaults={"status": FeedTargetStatus.PENDING, "is_dirty": True},
                    )

        for target in FeedTarget.objects.for_store(store).all():
            key = (target.provider, target.store_language_id, target.country_code)
            if target.provider not in enabled_provider_keys or key not in wanted:
                _delete_target_file(target)
                target.delete()


def _delete_target_file(target) -> None:
    """Best-effort removal of a target's generated file — never raises."""
    if not target.file_path:
        return
    try:
        if os.path.exists(target.file_path):
            os.remove(target.file_path)
    except OSError:
        logger.exception("feeds: failed to remove file for target %s", target.pk)


def _feed_file_path(target) -> str:
    """
    FEEDS_ROOT/<store_id>/<provider>/<country>-<lang>.xml (ADR-026 D3).

    FEEDS_ROOT is a dedicated directory OUTSIDE MEDIA_ROOT, never web-served
    (security audit F3 — MEDIA_ROOT is published at /media/, which would make
    the feeds/views.py token gate bypassable via /media/feeds/...). See
    webecom/settings/base.py::FEEDS_ROOT for the full rationale.

    Lowercase country segment matches the public URL exactly
    (feeds/urls.py); FeedTarget.country_code itself stays uppercase
    (mirrors ShippingCountry.country_code).
    """
    directory = os.path.join(settings.FEEDS_ROOT, str(target.store_id), target.provider)
    filename = f"{target.country_code.lower()}-{target.store_language.lang_code}.xml"
    return os.path.join(directory, filename)


def _write_feed_file(provider, target, items) -> str:
    """
    Render `items` to a temp file in the SAME directory as the final path,
    then atomically os.replace() it into place (ADR-026 D3) — a fetcher never
    observes a partial file.
    """
    final_path = _feed_file_path(target)
    directory = os.path.dirname(final_path)
    os.makedirs(directory, exist_ok=True)

    fd, tmp_path = tempfile.mkstemp(dir=directory, prefix=".tmp-", suffix=".xml")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            render_feed(provider, target.store, target.store_language, items, fh)
        os.replace(tmp_path, final_path)
    except Exception:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)
        raise
    return final_path


def _process_target(target) -> None:
    """
    Regenerate one FeedTarget's file (ADR-026 D3): REGENERATING -> build ->
    atomic write -> UP_TO_DATE, or ERROR + last_error on any failure. On
    ERROR, file_path is left UNTOUCHED — the previous good file (if any)
    keeps being served; a bad generation never blanks a live ad catalog
    (§XV-1).

    is_dirty is cleared BEFORE generation runs, so a change arriving mid-run
    re-sets it and the next beat tick regenerates again (no lost update,
    §XV-6).
    """
    from feeds.models import FeedTarget, FeedTargetStatus

    FeedTarget.objects.for_store(target.store).filter(pk=target.pk).update(
        status=FeedTargetStatus.REGENERATING, is_dirty=False
    )

    provider = get_provider(target.provider)
    if provider is None:
        FeedTarget.objects.for_store(target.store).filter(pk=target.pk).update(
            status=FeedTargetStatus.ERROR,
            last_error=f"No registered feed provider for key {target.provider!r}.",
        )
        return

    try:
        result = build_feed_items(target)
        file_path = _write_feed_file(provider, target, result.items)
    except FeedCurrencyError as exc:
        logger.error("feeds: currency error for target %s: %s", target.pk, exc)
        FeedTarget.objects.for_store(target.store).filter(pk=target.pk).update(
            status=FeedTargetStatus.ERROR, last_error=str(exc)
        )
        return
    except Exception as exc:
        logger.exception("feeds: unhandled error generating target %s", target.pk)
        FeedTarget.objects.for_store(target.store).filter(pk=target.pk).update(
            status=FeedTargetStatus.ERROR, last_error=str(exc)[:2000]
        )
        return

    FeedTarget.objects.for_store(target.store).filter(pk=target.pk).update(
        status=FeedTargetStatus.UP_TO_DATE,
        last_generated_at=timezone.now(),
        last_error="",
        item_count=len(result.items),
        excluded_count=result.excluded_count,
        exclusions_json=result.exclusions,
        file_path=file_path,
    )


def _regenerate_all_dirty() -> None:
    """Regenerate every currently-dirty FeedTarget, across all stores."""
    from feeds.models import FeedTarget

    dirty_target_ids = list(
        FeedTarget.objects.cross_store_unsafe().filter(is_dirty=True).values_list("pk", flat=True)
    )
    for target_id in dirty_target_ids:
        try:
            target = (
                FeedTarget.objects.cross_store_unsafe()
                .select_related("store", "store_language", "store_language__domain")
                .get(pk=target_id)
            )
        except FeedTarget.DoesNotExist:
            continue  # deleted by a concurrent reconciliation — nothing to do
        try:
            _process_target(target)
        except Exception:
            logger.exception("feeds: failed to regenerate target %s", target_id)


@shared_task(name="feeds.tasks.regenerate_dirty_feeds")
def regenerate_dirty_feeds() -> None:
    """Debounced beat task (FEED-005 default: every 30 min)."""
    _reconcile_matrix()
    _regenerate_all_dirty()


@shared_task(name="feeds.tasks.rebuild_all_feeds")
def rebuild_all_feeds() -> None:
    """Nightly full rebuild (FEED-005 §V safety net)."""
    from feeds.models import FeedTarget

    _reconcile_matrix()
    FeedTarget.objects.cross_store_unsafe().update(is_dirty=True)
    _regenerate_all_dirty()
