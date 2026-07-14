# Generated manually for B3 — coupon + gift card stacking on Order (ADR-002 §3).
# Adds order.gift_card FK alongside the existing order.discount_code (coupon) FK.
# on_delete=PROTECT: prevents accidental gift-card code deletion while orders reference it.

import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('orders', '0004_ordercharge'),
        ('discounts', '0002_initial'),
    ]

    operations = [
        migrations.AddField(
            model_name='order',
            name='gift_card',
            field=models.ForeignKey(
                blank=True,
                help_text=(
                    'Gift card applied to this order — one per order (DECIDED UF-H, ADR-002 §3). '
                    'Applied on the coupon remainder. '
                    'PROTECT prevents accidental gift-card deletion when historical orders reference it.'
                ),
                null=True,
                on_delete=django.db.models.deletion.PROTECT,
                related_name='order_gift_card_set',
                to='discounts.discountcode',
            ),
        ),
    ]
