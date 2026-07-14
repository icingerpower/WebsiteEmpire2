"""
Data migration: retire the '__purchase_guard__' FiredPixel sentinel (ADR-022 D6).

Before TICKET-030, order_thank_you burned a single master sentinel row
(FiredPixel(pixel_type='__purchase_guard__', event='purchase')) as the whole
purchase-pixel dedup mechanism. TICKET-030 replaces it with one FiredPixel
row per (order, installed provider, 'purchase') via pixels.service.
claim_purchase_pixels().

Forward: for every existing sentinel row, create one per-provider FiredPixel
row for each Pixel installed+active on that order's store AT MIGRATION TIME,
then delete the sentinel. Without this, every pre-T030 order would late-fire
Purchase on its NEXT thank-you revisit after this ticket ships (the sentinel
being gone would look, to claim_purchase_pixels, like "never claimed") —
wrong-day attribution for orders that are, in reality, long since converted
and reported.

Reverse: recreate one '__purchase_guard__' sentinel per order that has ANY
per-provider purchase FiredPixel row, restoring pre-T030 semantics. The
per-provider rows themselves are left in place on reverse — FiredPixel rows
are bounded (order x provider x event), so there is no growth risk to
unwind, and deleting real per-provider audit rows would only lose history
for no benefit (ADR-022 Rollback strategy).
"""

from django.db import migrations

SENTINEL_TYPE = "__purchase_guard__"
PURCHASE_EVENT = "purchase"


def _unscoped(manager):
    """
    Return an unfiltered queryset for `manager`.

    Historical models obtained via apps.get_model() during a real `migrate`
    run carry a plain models.Manager() (no custom scoping) — `.all()` works
    directly. When this function is exercised from a test against the REAL
    app registry (django.apps.apps), `manager` is the actual StoreScopedManager
    and unscoped iteration requires the explicit cross_store_unsafe() escape
    hatch (ADR-001 §4). Mirrors cart/migrations/0005's pattern.
    """
    if hasattr(manager, "cross_store_unsafe"):
        return manager.cross_store_unsafe()
    return manager.all()


def retire_sentinel(apps, schema_editor):
    FiredPixel = apps.get_model("orders", "FiredPixel")
    Pixel = apps.get_model("pixels", "Pixel")

    sentinels = list(
        _unscoped(FiredPixel.objects).filter(pixel_type=SENTINEL_TYPE, event=PURCHASE_EVENT)
    )
    for sentinel in sentinels:
        active_providers = _unscoped(Pixel.objects).filter(
            store_id=sentinel.store_id, is_active=True
        ).values_list("provider", flat=True)
        for provider in active_providers:
            _unscoped(FiredPixel.objects).get_or_create(
                store_id=sentinel.store_id,
                order_id=sentinel.order_id,
                pixel_type=provider,
                event=PURCHASE_EVENT,
            )
        sentinel.delete()


def reverse_sentinel(apps, schema_editor):
    FiredPixel = apps.get_model("orders", "FiredPixel")

    orders_with_purchase_rows = (
        _unscoped(FiredPixel.objects)
        .filter(event=PURCHASE_EVENT)
        .exclude(pixel_type=SENTINEL_TYPE)
        .values_list("store_id", "order_id")
        .distinct()
    )
    for store_id, order_id in orders_with_purchase_rows:
        _unscoped(FiredPixel.objects).get_or_create(
            store_id=store_id,
            order_id=order_id,
            pixel_type=SENTINEL_TYPE,
            event=PURCHASE_EVENT,
        )


class Migration(migrations.Migration):

    dependencies = [
        ("orders", "0012_order_capture_immediately"),
        ("pixels", "0001_initial"),
    ]

    operations = [
        migrations.RunPython(retire_sentinel, reverse_code=reverse_sentinel),
    ]
