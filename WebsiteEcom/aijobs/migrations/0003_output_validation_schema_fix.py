"""
Migration 0003: Reshape AiJobOutput and AiJobValidation to match ADR-003.

AiJobOutput before → after:
  REMOVED: job FK (AiJob), output_payload JSONField, accepted_at DateTimeField,
           is_accepted nullable BooleanField
  ADDED:   field_id CharField(100), content TextField,
           is_accepted non-nullable BooleanField(default=True),
           UniqueConstraint(run, field_id)

AiJobValidation before → after:
  REMOVED: output FK (AiJobOutput), reviewer FK (User),
           decision CharField, edited_value TextField
  ADDED:   run FK (AiJobRun), check_name CharField(100),
           passed BooleanField, details_json JSONField,
           severity CharField(10)

All existing rows in both tables are cleared before the schema changes
because this is a development-only migration — no real data exists to preserve,
and the new schemas are incompatible with the old data shapes.
"""

import django.db.models.deletion
from django.db import migrations, models


def clear_outputs_and_validations(apps, schema_editor):
    """
    Delete all existing rows before restructuring.
    AiJobValidation must be cleared first (FK to AiJobOutput).
    """
    AiJobValidation = apps.get_model("aijobs", "AiJobValidation")
    AiJobOutput = apps.get_model("aijobs", "AiJobOutput")
    AiJobValidation.objects.all().delete()
    AiJobOutput.objects.all().delete()


class Migration(migrations.Migration):

    dependencies = [
        (
            "aijobs",
            "0002_rename_aijob_store_status_priority_idx_aijob_store_status_pri_idx",
        ),
    ]

    operations = [
        # ----------------------------------------------------------------
        # Step 0: clear existing rows (no real data to preserve)
        # ----------------------------------------------------------------
        migrations.RunPython(
            clear_outputs_and_validations,
            reverse_code=migrations.RunPython.noop,
        ),

        # ----------------------------------------------------------------
        # AiJobOutput: remove old fields
        # ----------------------------------------------------------------
        migrations.RemoveField(
            model_name="aijoboutput",
            name="job",
        ),
        migrations.RemoveField(
            model_name="aijoboutput",
            name="output_payload",
        ),
        migrations.RemoveField(
            model_name="aijoboutput",
            name="accepted_at",
        ),

        # ----------------------------------------------------------------
        # AiJobOutput: change is_accepted from nullable to non-nullable
        # ----------------------------------------------------------------
        migrations.AlterField(
            model_name="aijoboutput",
            name="is_accepted",
            field=models.BooleanField(
                default=True,
                help_text="True=accepted and applied to the target; False=rejected.",
            ),
        ),

        # ----------------------------------------------------------------
        # AiJobOutput: add new fields (table is empty — preserve_default=False safe)
        # ----------------------------------------------------------------
        migrations.AddField(
            model_name="aijoboutput",
            name="field_id",
            field=models.CharField(
                default="",
                help_text="Stable field identifier, e.g. 'title', 'description', 'bullet_0'.",
                max_length=100,
            ),
            preserve_default=False,
        ),
        migrations.AddField(
            model_name="aijoboutput",
            name="content",
            field=models.TextField(
                default="",
                help_text="AI-generated content for this field.",
            ),
            preserve_default=False,
        ),

        # ----------------------------------------------------------------
        # AiJobOutput: add unique constraint on (run, field_id)
        # ----------------------------------------------------------------
        migrations.AddConstraint(
            model_name="aijoboutput",
            constraint=models.UniqueConstraint(
                fields=["run", "field_id"],
                name="unique_aijoboutput_run_field_id",
            ),
        ),

        # ----------------------------------------------------------------
        # AiJobValidation: remove old fields
        # ----------------------------------------------------------------
        migrations.RemoveField(
            model_name="aijobvalidation",
            name="output",
        ),
        migrations.RemoveField(
            model_name="aijobvalidation",
            name="reviewer",
        ),
        migrations.RemoveField(
            model_name="aijobvalidation",
            name="decision",
        ),
        migrations.RemoveField(
            model_name="aijobvalidation",
            name="edited_value",
        ),

        # ----------------------------------------------------------------
        # AiJobValidation: add new fields (table is empty — preserve_default=False safe)
        # ----------------------------------------------------------------
        migrations.AddField(
            model_name="aijobvalidation",
            name="run",
            field=models.ForeignKey(
                default=None,
                null=True,
                on_delete=django.db.models.deletion.CASCADE,
                related_name="validations",
                to="aijobs.aijobrun",
            ),
            preserve_default=False,
        ),
        # Make run non-nullable now that the column exists.
        migrations.AlterField(
            model_name="aijobvalidation",
            name="run",
            field=models.ForeignKey(
                on_delete=django.db.models.deletion.CASCADE,
                related_name="validations",
                to="aijobs.aijobrun",
            ),
        ),
        migrations.AddField(
            model_name="aijobvalidation",
            name="check_name",
            field=models.CharField(
                default="",
                help_text="Identifier for the check, e.g. 'min_length', 'bracket_balance'.",
                max_length=100,
            ),
            preserve_default=False,
        ),
        migrations.AddField(
            model_name="aijobvalidation",
            name="passed",
            field=models.BooleanField(
                default=True,
                help_text="True if this check passed; False if it failed.",
            ),
            preserve_default=False,
        ),
        migrations.AddField(
            model_name="aijobvalidation",
            name="details_json",
            field=models.JSONField(
                default=dict,
                help_text="Check-specific details, e.g. {'actual': 42, 'minimum': 50}.",
            ),
        ),
        migrations.AddField(
            model_name="aijobvalidation",
            name="severity",
            field=models.CharField(
                choices=[
                    ("ERROR", "Error — blocks commit"),
                    ("WARNING", "Warning — recorded only"),
                ],
                default="ERROR",
                max_length=10,
            ),
        ),
    ]
