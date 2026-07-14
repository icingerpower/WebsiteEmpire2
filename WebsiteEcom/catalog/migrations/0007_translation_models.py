# Generated for TICKET-024 Phase 2 — Additional translation models.
# Adds ProductImageTranslation, VariantOptionTranslation, VariantOptionValueTranslation.
# Also adds manually_edited_at to ProductTranslation and CollectionTranslation.

import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("aijobs", "0004_alter_aijob_job_type"),
        ("catalog", "0006_variant_option_entities"),
        ("stores", "0009_alter_store_custom_domain_and_more"),
    ]

    operations = [
        # manually_edited_at on existing translation models
        migrations.AddField(
            model_name="producttranslation",
            name="manually_edited_at",
            field=models.DateTimeField(blank=True, null=True),
        ),
        migrations.AddField(
            model_name="collectiontranslation",
            name="manually_edited_at",
            field=models.DateTimeField(blank=True, null=True),
        ),
        # ProductImageTranslation
        migrations.CreateModel(
            name="ProductImageTranslation",
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
                    "lang_code",
                    models.CharField(
                        help_text="ISO 639-1 language code — e.g. 'fr', 'pt-br'.",
                        max_length=10,
                    ),
                ),
                ("alt_text", models.CharField(blank=True, max_length=300)),
                (
                    "status",
                    models.CharField(
                        choices=[("draft", "Draft"), ("published", "Published")],
                        default="draft",
                        max_length=20,
                    ),
                ),
                ("manually_edited_at", models.DateTimeField(blank=True, null=True)),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                ("updated_at", models.DateTimeField(auto_now=True)),
                (
                    "ai_job",
                    models.ForeignKey(
                        blank=True,
                        null=True,
                        on_delete=django.db.models.deletion.SET_NULL,
                        related_name="product_image_translations",
                        to="aijobs.aijob",
                    ),
                ),
                (
                    "image",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="translations",
                        to="catalog.productimage",
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
                "verbose_name": "product image translation",
                "verbose_name_plural": "product image translations",
                "unique_together": {("store", "image", "lang_code")},
            },
        ),
        # VariantOptionTranslation
        migrations.CreateModel(
            name="VariantOptionTranslation",
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
                    "lang_code",
                    models.CharField(
                        help_text="ISO 639-1 language code — e.g. 'fr', 'pt-br'.",
                        max_length=10,
                    ),
                ),
                (
                    "name",
                    models.CharField(
                        blank=True,
                        help_text="Translated option name. Falls back to VariantOption.name when blank/unpublished.",
                        max_length=100,
                    ),
                ),
                (
                    "status",
                    models.CharField(
                        choices=[("draft", "Draft"), ("published", "Published")],
                        default="draft",
                        max_length=20,
                    ),
                ),
                ("manually_edited_at", models.DateTimeField(blank=True, null=True)),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                ("updated_at", models.DateTimeField(auto_now=True)),
                (
                    "ai_job",
                    models.ForeignKey(
                        blank=True,
                        null=True,
                        on_delete=django.db.models.deletion.SET_NULL,
                        related_name="variant_option_translations",
                        to="aijobs.aijob",
                    ),
                ),
                (
                    "option",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="translations",
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
                "verbose_name": "variant option translation",
                "verbose_name_plural": "variant option translations",
                "unique_together": {("store", "option", "lang_code")},
            },
        ),
        # VariantOptionValueTranslation
        migrations.CreateModel(
            name="VariantOptionValueTranslation",
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
                    "lang_code",
                    models.CharField(
                        help_text="ISO 639-1 language code — e.g. 'fr', 'pt-br'.",
                        max_length=10,
                    ),
                ),
                (
                    "value",
                    models.CharField(
                        blank=True,
                        help_text="Translated value label. Falls back to VariantOptionValue.value when blank/unpublished.",
                        max_length=200,
                    ),
                ),
                (
                    "status",
                    models.CharField(
                        choices=[("draft", "Draft"), ("published", "Published")],
                        default="draft",
                        max_length=20,
                    ),
                ),
                ("manually_edited_at", models.DateTimeField(blank=True, null=True)),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                ("updated_at", models.DateTimeField(auto_now=True)),
                (
                    "ai_job",
                    models.ForeignKey(
                        blank=True,
                        null=True,
                        on_delete=django.db.models.deletion.SET_NULL,
                        related_name="variant_option_value_translations",
                        to="aijobs.aijob",
                    ),
                ),
                (
                    "option_value",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="translations",
                        to="catalog.variantoptionvalue",
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
                "verbose_name": "variant option value translation",
                "verbose_name_plural": "variant option value translations",
                "unique_together": {("store", "option_value", "lang_code")},
            },
        ),
    ]
