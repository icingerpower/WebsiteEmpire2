# T029 TH-082 — StockNotification and QuotationRequest models.

import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("catalog", "0009_add_slug_hint_to_product_translation"),
        ("stores", "0010_theme_engine_extend_store_theme_customization"),
    ]

    operations = [
        migrations.CreateModel(
            name="StockNotification",
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
                ("email", models.EmailField(max_length=254)),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                ("notified_at", models.DateTimeField(blank=True, null=True)),
                (
                    "product",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="stock_notifications",
                        to="catalog.product",
                    ),
                ),
                (
                    "store",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.PROTECT,
                        to="stores.store",
                    ),
                ),
            ],
            options={
                "verbose_name": "stock notification",
                "verbose_name_plural": "stock notifications",
                "unique_together": {("store", "product", "email")},
            },
        ),
        migrations.CreateModel(
            name="QuotationRequest",
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
                ("customer_name", models.CharField(blank=True, default="", max_length=200)),
                ("customer_email", models.EmailField(max_length=254)),
                ("message", models.TextField(blank=True, default="")),
                ("quantity", models.PositiveIntegerField(default=1)),
                (
                    "status",
                    models.CharField(
                        choices=[
                            ("pending", "Pending"),
                            ("responded", "Responded"),
                            ("closed", "Closed"),
                        ],
                        default="pending",
                        max_length=20,
                    ),
                ),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                (
                    "product",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="quotation_requests",
                        to="catalog.product",
                    ),
                ),
                (
                    "store",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.PROTECT,
                        to="stores.store",
                    ),
                ),
            ],
            options={
                "verbose_name": "quotation request",
                "verbose_name_plural": "quotation requests",
            },
        ),
        migrations.AddIndex(
            model_name="stocknotification",
            index=models.Index(
                fields=["store", "notified_at"],
                name="catalog_stkn_store_notified_idx",
            ),
        ),
        migrations.AddIndex(
            model_name="quotationrequest",
            index=models.Index(
                fields=["store", "status"],
                name="catalog_qreq_store_status_idx",
            ),
        ),
        migrations.AddIndex(
            model_name="quotationrequest",
            index=models.Index(
                fields=["store", "created_at"],
                name="catalog_qreq_store_created_idx",
            ),
        ),
    ]
