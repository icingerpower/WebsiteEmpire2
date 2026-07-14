# Generated manually for B3 — coupon + gift card stacking on Cart (ADR-002 §3).
# Adds cart.gift_card FK alongside the existing cart.discount_code (coupon) FK.

import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('cart', '0001_initial'),
        ('discounts', '0002_initial'),
    ]

    operations = [
        migrations.AddField(
            model_name='cart',
            name='gift_card',
            field=models.ForeignKey(
                blank=True,
                help_text=(
                    'Gift card applied to this cart (B3 stacking field). '
                    'Applied on the coupon remainder at checkout. '
                    'One gift card per order — DECIDED UF-H, ADR-002 §3.'
                ),
                null=True,
                on_delete=django.db.models.deletion.SET_NULL,
                related_name='cart_gift_card_set',
                to='discounts.discountcode',
            ),
        ),
    ]
