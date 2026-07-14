"""
Add checkout-specific fields to Order and add ZERO_TOTAL to ChargeType (ADR-015 §5).

All schema changes are additive (three nullable/defaulted columns on Order,
one choices-only AlterField on OrderCharge). Safe to run on any existing data.
"""

import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("orders", "0010_alter_ordercharge_status"),
        ("shipping", "0002_seed_carriers_and_catchall_zone"),
    ]

    operations = [
        # Buyer's browsing language at checkout — used for order emails (U16-8).
        migrations.AddField(
            model_name="order",
            name="checkout_language",
            field=models.CharField(
                blank=True,
                default="",
                help_text="Buyer browsing language at checkout (U16-8). Used for order emails.",
                max_length=10,
            ),
        ),
        # Shipping method display name — snapshot; never join the rate table (ADR-002).
        migrations.AddField(
            model_name="order",
            name="shipping_method_name",
            field=models.CharField(
                blank=True,
                default="",
                help_text=(
                    "Display name of the selected shipping method, snapshotted at order creation (TH-002). "
                    "Never join to the rate table for historical display — use this snapshot instead."
                ),
                max_length=100,
            ),
        ),
        # FK to ShippingRate — advisory (analytics/reporting only). SET_NULL on rate deletion.
        migrations.AddField(
            model_name="order",
            name="shipping_rate",
            field=models.ForeignKey(
                blank=True,
                help_text="FK to the selected ShippingRate (advisory — analytics/reporting only). SET_NULL on rate deletion.",
                null=True,
                on_delete=django.db.models.deletion.SET_NULL,
                related_name="orders",
                to="shipping.shippingrate",
            ),
        ),
        # Add ZERO_TOTAL to ChargeType choices (choices-only change — no DB shape change).
        migrations.AlterField(
            model_name="ordercharge",
            name="charge_type",
            field=models.CharField(
                choices=[
                    ("original", "Original"),
                    ("upsell", "Upsell"),
                    ("zero_total", "Zero Total"),
                ],
                default="original",
                max_length=20,
            ),
        ),
    ]
