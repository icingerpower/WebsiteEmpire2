"""
Data migration: backfill session_key rotation for pre-ADR-016 CONVERTED carts.

After ADR-016, begin_checkout rotates cart.session_key to "converted-{pk}" on
every successful checkout so that get_or_create_cart can create a fresh ACTIVE
cart for the same browser session without violating unique_together(store,
session_key).

This migration applies the same rotation to any CONVERTED row that was created
before ADR-016 (i.e. still has a raw session_key, not the "converted-" sentinel).

Reversal is a no-op: there is no stored mapping from the original session_key back
to the cart pk, and reverting old rows would not be safe (the original key may now
belong to a new ACTIVE cart created after the forward migration ran).
"""

from django.db import migrations


def backfill_converted_session_keys(apps, schema_editor):
    """Rotate session_key to "converted-{pk}" for all un-rotated CONVERTED carts."""
    Cart = apps.get_model('cart', 'Cart')
    # Django's migration framework supplies a historical model whose objects is a
    # plain models.Manager() (no custom scoping). When this function is called from
    # tests with the real apps registry, objects is the store-scoped manager and
    # requires cross_store_unsafe() for unscoped iteration.
    if hasattr(Cart.objects, 'cross_store_unsafe'):
        base_qs = Cart.objects.cross_store_unsafe()
    else:
        base_qs = Cart.objects.all()
    for cart in (
        base_qs.filter(status='converted')
        .exclude(session_key__startswith='converted-')
    ):
        cart.session_key = f'converted-{cart.pk}'
        cart.save(update_fields=['session_key'])


def reverse_backfill(apps, schema_editor):
    """Irreversible — no stored mapping from old session_key to pk."""
    pass


class Migration(migrations.Migration):

    dependencies = [
        ('cart', '0004_checkoutstate'),
    ]

    operations = [
        migrations.RunPython(
            backfill_converted_session_keys,
            reverse_code=reverse_backfill,
        ),
    ]
