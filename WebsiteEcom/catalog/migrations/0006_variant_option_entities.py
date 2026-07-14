# Generated for TICKET-024 Phase 1 — Stable variant option entities.
# VariantOption, VariantOptionValue, VariantOptionAssignment give each option
# and value a stable PK so translation records can be keyed to them.
# option_values_json on ProductVariant is preserved for backward compatibility.

import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("catalog", "0005_add_product_collection_translations"),
        ("stores", "0009_alter_store_custom_domain_and_more"),
    ]

    operations = [
        migrations.CreateModel(
            name="VariantOption",
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
                    "name",
                    models.CharField(
                        help_text="Option dimension name, e.g. 'Color', 'Size'.",
                        max_length=100,
                    ),
                ),
                (
                    "position",
                    models.PositiveSmallIntegerField(
                        default=0,
                        help_text="Display order within the product's option list (ascending).",
                    ),
                ),
                (
                    "source_fingerprint",
                    models.CharField(
                        blank=True,
                        help_text="SHA-256 hex digest of the normalized (stripped, lowercased) name.",
                        max_length=64,
                    ),
                ),
                (
                    "manually_edited_at",
                    models.DateTimeField(
                        blank=True,
                        help_text="Set when a human edits this option via admin. NULL = not manually edited.",
                        null=True,
                    ),
                ),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                ("updated_at", models.DateTimeField(auto_now=True)),
                (
                    "product",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="variant_options",
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
                "verbose_name": "variant option",
                "verbose_name_plural": "variant options",
                "ordering": ["position", "pk"],
                "unique_together": {("store", "product", "name")},
            },
        ),
        migrations.CreateModel(
            name="VariantOptionValue",
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
                    "value",
                    models.CharField(
                        help_text="Option value label, e.g. 'Red', 'XL'.",
                        max_length=200,
                    ),
                ),
                (
                    "position",
                    models.PositiveSmallIntegerField(
                        default=0,
                        help_text="Display order within the option (ascending).",
                    ),
                ),
                (
                    "source_fingerprint",
                    models.CharField(
                        blank=True,
                        help_text="SHA-256 hex digest of the normalized (stripped, lowercased) value.",
                        max_length=64,
                    ),
                ),
                (
                    "manually_edited_at",
                    models.DateTimeField(
                        blank=True,
                        help_text="Set when a human edits this value via admin. NULL = not manually edited.",
                        null=True,
                    ),
                ),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                ("updated_at", models.DateTimeField(auto_now=True)),
                (
                    "option",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="values",
                        to="catalog.variantoption",
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
                "verbose_name": "variant option value",
                "verbose_name_plural": "variant option values",
                "ordering": ["position", "pk"],
                "unique_together": {("store", "option", "value")},
            },
        ),
        migrations.CreateModel(
            name="VariantOptionAssignment",
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
                    "option",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="+",
                        to="catalog.variantoption",
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
                    "value",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="+",
                        to="catalog.variantoptionvalue",
                    ),
                ),
                (
                    "variant",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="option_assignments",
                        to="catalog.productvariant",
                    ),
                ),
            ],
            options={
                "verbose_name": "variant option assignment",
                "verbose_name_plural": "variant option assignments",
                "unique_together": {("store", "variant", "option")},
            },
        ),
    ]
