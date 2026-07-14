# Migration: add source_fingerprint to all five translation models (ADR-014 §Q3).
#
# source_fingerprint is SHA-256 hex of the '\x1f'-joined normalized source-field values
# at the time of translation. Blank for existing rows (treated as stale — will be
# re-queued on the next source save). Enables staleness detection and the idempotent
# skip check in the runner (ADR-014 §Q5, §XV-6).
#
# All five translation models receive the same column in one migration so that
# Phase 3 signals and persist_output can be activated together.

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("catalog", "0007_translation_models"),
    ]

    operations = [
        # ProductTranslation
        migrations.AddField(
            model_name="producttranslation",
            name="source_fingerprint",
            field=models.CharField(blank=True, default="", max_length=64),
        ),
        # CollectionTranslation
        migrations.AddField(
            model_name="collectiontranslation",
            name="source_fingerprint",
            field=models.CharField(blank=True, default="", max_length=64),
        ),
        # ProductImageTranslation
        migrations.AddField(
            model_name="productimagetranslation",
            name="source_fingerprint",
            field=models.CharField(blank=True, default="", max_length=64),
        ),
        # VariantOptionTranslation
        migrations.AddField(
            model_name="variantoptiontranslation",
            name="source_fingerprint",
            field=models.CharField(blank=True, default="", max_length=64),
        ),
        # VariantOptionValueTranslation
        migrations.AddField(
            model_name="variantoptionvaluetranslation",
            name="source_fingerprint",
            field=models.CharField(blank=True, default="", max_length=64),
        ),
    ]
