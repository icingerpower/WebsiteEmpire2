# Full Order schema + OrderItem, OrderFulfillment, FiredPixel.
# Replaces the bare stub (0001_initial) that existed only to satisfy the
# discounts.GiftCardTransaction.order FK during TICKET-008.

import django.db.models.deletion
import django.utils.timezone
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("catalog", "0002_collection_collectionproduct_and_more"),
        ("discounts", "0002_initial"),
        ("orders", "0001_initial"),
        ("payments", "0001_initial"),
        ("stores", "0003_alter_field_metadata"),
    ]

    operations = [
        # ── Expand Order ─────────────────────────────────────────────────────
        migrations.AddField(
            model_name="order",
            name="order_number",
            field=models.CharField(default="", max_length=50),
            preserve_default=False,
        ),
        migrations.AddField(
            model_name="order",
            name="idempotency_key",
            field=models.CharField(default="", max_length=255, unique=True),
            preserve_default=False,
        ),
        migrations.AddField(
            model_name="order",
            name="payment_status",
            field=models.CharField(
                choices=[
                    ("pending", "Pending"),
                    ("paid", "Paid"),
                    ("partially_refunded", "Partially Refunded"),
                    ("refunded", "Refunded"),
                    ("failed", "Failed"),
                    ("cancelled", "Cancelled"),
                ],
                default="pending",
                max_length=30,
            ),
        ),
        migrations.AddField(
            model_name="order",
            name="fulfillment_status",
            field=models.CharField(
                choices=[
                    ("not_sent", "Not Sent"),
                    ("sent_to_fulfillment", "Sent to Fulfillment"),
                    ("partially_shipped", "Partially Shipped"),
                    ("shipped", "Shipped"),
                ],
                default="not_sent",
                max_length=30,
            ),
        ),
        migrations.AddField(
            model_name="order",
            name="customer_email",
            field=models.EmailField(default="", max_length=254),
            preserve_default=False,
        ),
        migrations.AddField(
            model_name="order",
            name="customer_name",
            field=models.CharField(blank=True, default="", max_length=255),
        ),
        migrations.AddField(
            model_name="order",
            name="phone",
            field=models.CharField(blank=True, default="", max_length=50),
        ),
        migrations.AddField(
            model_name="order",
            name="shipping_address",
            field=models.JSONField(default=dict),
        ),
        migrations.AddField(
            model_name="order",
            name="billing_address",
            field=models.JSONField(default=dict),
        ),
        migrations.AddField(
            model_name="order",
            name="subtotal",
            field=models.DecimalField(decimal_places=2, default=0, max_digits=12),
        ),
        migrations.AddField(
            model_name="order",
            name="discount_amount",
            field=models.DecimalField(decimal_places=2, default=0, max_digits=12),
        ),
        migrations.AddField(
            model_name="order",
            name="shipping_amount",
            field=models.DecimalField(decimal_places=2, default=0, max_digits=12),
        ),
        migrations.AddField(
            model_name="order",
            name="total",
            field=models.DecimalField(decimal_places=2, default=0, max_digits=12),
        ),
        migrations.AddField(
            model_name="order",
            name="currency",
            field=models.CharField(default="USD", max_length=3),
        ),
        migrations.AddField(
            model_name="order",
            name="discount_code",
            field=models.ForeignKey(
                blank=True,
                null=True,
                on_delete=django.db.models.deletion.SET_NULL,
                related_name="orders",
                to="discounts.discountcode",
            ),
        ),
        migrations.AddField(
            model_name="order",
            name="notes",
            field=models.TextField(blank=True, default=""),
        ),
        migrations.AddField(
            model_name="order",
            name="capture_window_expires_at",
            field=models.DateTimeField(blank=True, null=True),
        ),
        migrations.AddField(
            model_name="order",
            name="processor_account",
            field=models.ForeignKey(
                blank=True,
                null=True,
                on_delete=django.db.models.deletion.SET_NULL,
                related_name="orders",
                to="payments.processoraccount",
            ),
        ),
        # Attribution columns (ADR-004)
        migrations.AddField(
            model_name="order",
            name="utm_source",
            field=models.CharField(blank=True, default="", max_length=255),
        ),
        migrations.AddField(
            model_name="order",
            name="utm_medium",
            field=models.CharField(blank=True, default="", max_length=255),
        ),
        migrations.AddField(
            model_name="order",
            name="utm_campaign",
            field=models.CharField(blank=True, default="", max_length=255),
        ),
        migrations.AddField(
            model_name="order",
            name="utm_term",
            field=models.CharField(blank=True, default="", max_length=255),
        ),
        migrations.AddField(
            model_name="order",
            name="utm_content",
            field=models.CharField(blank=True, default="", max_length=255),
        ),
        migrations.AddField(
            model_name="order",
            name="first_referrer",
            field=models.URLField(blank=True, default="", max_length=500),
        ),
        migrations.AddField(
            model_name="order",
            name="landing_page",
            field=models.CharField(blank=True, default="", max_length=500),
        ),
        migrations.AddField(
            model_name="order",
            name="created_at",
            field=models.DateTimeField(
                auto_now_add=True,
                default=django.utils.timezone.now,
            ),
            preserve_default=False,
        ),
        migrations.AddField(
            model_name="order",
            name="updated_at",
            field=models.DateTimeField(auto_now=True),
        ),
        # Order constraints and indexes
        migrations.AlterUniqueTogether(
            name="order",
            unique_together={("store", "order_number")},
        ),
        migrations.AddIndex(
            model_name="order",
            index=models.Index(
                fields=["store", "payment_status"],
                name="orders_orde_store_i_632358_idx",
            ),
        ),
        migrations.AddIndex(
            model_name="order",
            index=models.Index(
                fields=["store", "created_at"],
                name="orders_orde_store_i_8955ea_idx",
            ),
        ),
        migrations.AddIndex(
            model_name="order",
            index=models.Index(
                fields=["idempotency_key"],
                name="orders_orde_idempot_ab5556_idx",
            ),
        ),
        # ── OrderItem ────────────────────────────────────────────────────────
        migrations.CreateModel(
            name="OrderItem",
            fields=[
                (
                    "id",
                    models.BigAutoField(
                        auto_created=True,
                        primary_key=True,
                        serialize=False,
                        verbose_name="ID",
                    ),
                ),
                (
                    "store",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.PROTECT,
                        to="stores.store",
                    ),
                ),
                (
                    "order",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="items",
                        to="orders.order",
                    ),
                ),
                (
                    "product_variant",
                    models.ForeignKey(
                        null=True,
                        on_delete=django.db.models.deletion.SET_NULL,
                        related_name="order_items",
                        to="catalog.productvariant",
                    ),
                ),
                ("product_name", models.CharField(max_length=512)),
                ("variant_title", models.CharField(default="", max_length=255)),
                ("sku", models.CharField(blank=True, default="", max_length=255)),
                ("quantity", models.PositiveIntegerField(default=1)),
                ("unit_price", models.DecimalField(decimal_places=2, max_digits=12)),
                (
                    "discount_amount",
                    models.DecimalField(decimal_places=2, default=0, max_digits=12),
                ),
                ("line_total", models.DecimalField(decimal_places=2, max_digits=12)),
            ],
            options={
                "verbose_name": "order item",
                "verbose_name_plural": "order items",
                "abstract": False,
            },
        ),
        migrations.AddIndex(
            model_name="orderitem",
            index=models.Index(
                fields=["order"],
                name="orders_orde_order_i_5d347b_idx",
            ),
        ),
        # ── OrderFulfillment ─────────────────────────────────────────────────
        migrations.CreateModel(
            name="OrderFulfillment",
            fields=[
                (
                    "id",
                    models.BigAutoField(
                        auto_created=True,
                        primary_key=True,
                        serialize=False,
                        verbose_name="ID",
                    ),
                ),
                (
                    "store",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.PROTECT,
                        to="stores.store",
                    ),
                ),
                (
                    "order",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="fulfillments",
                        to="orders.order",
                    ),
                ),
                (
                    "tracking_number",
                    models.CharField(blank=True, default="", max_length=255),
                ),
                ("carrier", models.CharField(blank=True, default="", max_length=100)),
                (
                    "tracking_url",
                    models.URLField(blank=True, default="", max_length=500),
                ),
                ("shipped_at", models.DateTimeField(blank=True, null=True)),
                ("notes", models.CharField(blank=True, default="", max_length=255)),
                (
                    "created_at",
                    models.DateTimeField(auto_now_add=True),
                ),
            ],
            options={
                "verbose_name": "order fulfillment",
                "verbose_name_plural": "order fulfillments",
                "abstract": False,
            },
        ),
        # ── FiredPixel ───────────────────────────────────────────────────────
        migrations.CreateModel(
            name="FiredPixel",
            fields=[
                (
                    "id",
                    models.BigAutoField(
                        auto_created=True,
                        primary_key=True,
                        serialize=False,
                        verbose_name="ID",
                    ),
                ),
                (
                    "store",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.PROTECT,
                        to="stores.store",
                    ),
                ),
                (
                    "order",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="fired_pixels",
                        to="orders.order",
                    ),
                ),
                (
                    "pixel_type",
                    models.CharField(
                        help_text="Pixel provider, e.g. 'facebook', 'ga', 'snapchat'.",
                        max_length=50,
                    ),
                ),
                (
                    "event",
                    models.CharField(
                        help_text="Conversion event name, e.g. 'purchase', 'add_to_cart'.",
                        max_length=100,
                    ),
                ),
                (
                    "fired_at",
                    models.DateTimeField(auto_now_add=True),
                ),
            ],
            options={
                "verbose_name": "fired pixel",
                "verbose_name_plural": "fired pixels",
                "abstract": False,
            },
        ),
        migrations.AlterUniqueTogether(
            name="firedpixel",
            unique_together={("order", "pixel_type", "event")},
        ),
    ]
