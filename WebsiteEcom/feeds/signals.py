"""
Dirty-marking signal receivers (ADR-026 D3).

Coarse per-store granularity: any of these events marks ALL of a store's
FeedTarget rows dirty (not just the ones touching the changed product) —
regeneration is cheap enough that per-object precision is not worth the
invalidation-audit risk of missing a dependency (§V).

CurrencyRate / CurrencyConverterSettings are platform-global (no store FK):
a rate change marks EVERY store's targets dirty (FEED-008 prices depend on
the whole rate table). Rates change at most daily (ECB task) so the fan-out
is bounded (ADR-026 Risks).

String `sender=` references (e.g. "catalog.Product") are Django's lazy
app_label.ModelName lookup — same pattern already used by
catalog/signals.py's `@receiver(pre_save, sender='catalog.ProductTranslation')`.
"""

import logging

from django.db.models.signals import post_delete, post_save
from django.dispatch import receiver

logger = logging.getLogger(__name__)


def _mark_store_dirty(store) -> None:
    from feeds.models import FeedTarget

    FeedTarget.objects.for_store(store).update(is_dirty=True)


def _mark_all_stores_dirty(reason: str) -> None:
    from feeds.models import FeedTarget

    updated = FeedTarget.objects.cross_store_unsafe().update(is_dirty=True)
    logger.info("feeds: marked %s targets dirty platform-wide (%s)", updated, reason)


# ---------------------------------------------------------------------------
# Per-store catalog/content signals
# ---------------------------------------------------------------------------

@receiver(post_save, sender="catalog.Product")
@receiver(post_delete, sender="catalog.Product")
def _product_changed(sender, instance, **kwargs):
    _mark_store_dirty(instance.store)


@receiver(post_save, sender="catalog.ProductVariant")
@receiver(post_delete, sender="catalog.ProductVariant")
def _variant_changed(sender, instance, **kwargs):
    _mark_store_dirty(instance.store)


@receiver(post_save, sender="catalog.ProductImage")
@receiver(post_delete, sender="catalog.ProductImage")
def _image_changed(sender, instance, **kwargs):
    _mark_store_dirty(instance.store)


@receiver(post_save, sender="catalog.ProductTranslation")
@receiver(post_delete, sender="catalog.ProductTranslation")
def _translation_changed(sender, instance, **kwargs):
    _mark_store_dirty(instance.store)


@receiver(post_save, sender="permalinks.Permalink")
@receiver(post_delete, sender="permalinks.Permalink")
def _permalink_changed(sender, instance, **kwargs):
    _mark_store_dirty(instance.store)


@receiver(post_save, sender="catalog.CollectionProduct")
def _collection_product_changed(sender, instance, **kwargs):
    """
    post_save ONLY — deliberately NOT post_delete (regression found while
    implementing this ADR): catalog/signals.py's apply_smart_rules_for_product
    removes a product from a smart collection via
    `CollectionProduct.objects.for_store(product.store).filter(...).delete()`,
    relying on Django's "fast delete" SQL path — which is only available
    when NO signal receiver is connected to that model's
    pre_delete/post_delete (Django's Collector.can_fast_delete()). Connecting
    a post_delete receiver here would silently downgrade every such call to
    the slow, per-instance delete path — breaking smart-collection
    re-evaluation performance on every Product/ProductVariant save (caught
    by catalog/tests/test_smart_collection_signal.py and
    test_variant_signal_and_command.py).

    (Historical note, ADR-031 addendum / TICKET-051: before that ticket, the
    same call ran on the raw, unscoped manager — the slow path would have
    additionally raised core.managers.IsolationError, not just been slower.
    The call is now explicitly store-scoped via .for_store(), so a slow-path
    delete would merely be a performance regression, not a crash — but it is
    still deliberately avoided.)

    Coarse: only feeds with scope='collections' actually depend on
    membership, but marking the whole store dirty is a cheap, safe
    over-approximation (§V) rather than tracking which FeedConfig scopes
    reference which collection. Membership REMOVALS (whether via the admin
    or the smart-rules re-evaluation above) are still caught within 24h by
    the nightly full rebuild (§V safety net, ADR-026 D3 Risks) — the exact
    "signal-bypassing mutation" scenario that safety net exists for.
    """
    _mark_store_dirty(instance.store)


@receiver(post_save, sender="stores.ShippingCountry")
@receiver(post_delete, sender="stores.ShippingCountry")
def _shipping_country_changed(sender, instance, **kwargs):
    try:
        _mark_store_dirty(instance.store_language.store)
    except Exception:
        # Defensive: if the parent StoreLanguage is mid-cascade-delete, the
        # reconciliation step (feeds/tasks.py._reconcile_matrix) will delete
        # the now-orphaned FeedTarget rows on the next beat tick regardless.
        logger.debug(
            "feeds: could not resolve store for changed ShippingCountry %s",
            instance.pk,
        )


@receiver(post_save, sender="stores.StoreLanguage")
@receiver(post_delete, sender="stores.StoreLanguage")
def _store_language_changed(sender, instance, **kwargs):
    _mark_store_dirty(instance.store)


@receiver(post_save, sender="feeds.FeedConfig")
@receiver(post_delete, sender="feeds.FeedConfig")
def _feed_config_changed(sender, instance, **kwargs):
    _mark_store_dirty(instance.store)


@receiver(post_save, sender="feeds.StoreFeedToken")
def _feed_token_created(sender, instance, created, **kwargs):
    """
    Log the store's first (lazily-created) feed token. A saved StoreFeedToken
    never needs to mark any FeedTarget dirty (ADR-026 D3) — this receiver
    exists purely for the audit-trail log line, not for any dirty-marking.

    Security audit F2: this does NOT fire on rotation, despite the name this
    function used to have. StoreFeedToken.rotate() writes via a queryset
    .update() (a single atomic statement, not a model .save()), and Django
    never sends post_save for .update() — so this receiver only ever runs
    once, on the initial get_or_create_for_store() lazy-creation .save().
    The rotation audit log lives in StoreFeedToken.rotate() itself
    (feeds/models.py) — that is the log line to look for on rotation.
    """
    if created:
        logger.info("feeds: token created for store %s", instance.store_id)


# ---------------------------------------------------------------------------
# Platform-wide currency signals
# ---------------------------------------------------------------------------

@receiver(post_save, sender="currency.CurrencyRate")
def _currency_rate_changed(sender, instance, **kwargs):
    _mark_all_stores_dirty(f"CurrencyRate saved: {instance.currency_id}")


@receiver(post_save, sender="currency.CurrencyConverterSettings")
def _currency_settings_changed(sender, instance, **kwargs):
    _mark_all_stores_dirty("CurrencyConverterSettings saved")
