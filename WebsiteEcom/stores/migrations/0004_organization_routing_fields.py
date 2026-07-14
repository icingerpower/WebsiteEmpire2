"""
Migration 0004: Add routing identity and cap fields to Organization (TICKET-017R).

All new fields are nullable or have defaults — this migration is safe to apply
to any existing database without data migration.

New fields on Organization (ADR-006-R §1.1):
  legal_name, display_name, registration_country — identity fields
  settlement_currencies, coverage_areas_json — JSON list fields
  statement_descriptor — card-statement text
  monthly_threshold, current_month_volume, volume_month — monthly cap accounting
  status — lifecycle state (draft/active)

New CheckConstraint:
  organization_default_no_cap: when is_default=True, monthly_threshold must be NULL.
  (The default org is the uncapped catch-all — AF-C6.)
"""

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("stores", "0003_alter_field_metadata"),
    ]

    operations = [
        # --- Identity fields ---
        migrations.AddField(
            model_name="organization",
            name="legal_name",
            field=models.CharField(
                blank=True,
                default="",
                help_text="Legal entity name. 'name' remains the internal admin label.",
                max_length=255,
            ),
        ),
        migrations.AddField(
            model_name="organization",
            name="display_name",
            field=models.CharField(
                blank=True,
                default="",
                help_text="Buyer-facing name shown on receipts and emails.",
                max_length=255,
            ),
        ),
        migrations.AddField(
            model_name="organization",
            name="registration_country",
            field=models.CharField(
                blank=True,
                default="",
                help_text="ISO 3166-1 alpha-2 registration country code.",
                max_length=2,
            ),
        ),
        migrations.AddField(
            model_name="organization",
            name="settlement_currencies",
            field=models.JSONField(
                default=list,
                help_text="List of ISO 4217 codes this org settles in, e.g. ['EUR', 'USD'].",
            ),
        ),
        migrations.AddField(
            model_name="organization",
            name="coverage_areas_json",
            field=models.JSONField(
                default=list,
                help_text=(
                    "Informational area tokens (EU, ROW, country codes) for the super-admin UI. "
                    "Routing reads rule conditions_json, not this field."
                ),
            ),
        ),
        migrations.AddField(
            model_name="organization",
            name="statement_descriptor",
            field=models.CharField(
                blank=True,
                default="",
                help_text="Card-statement text. Processor maximum is 22 characters.",
                max_length=22,
            ),
        ),
        # --- Monthly cap fields ---
        migrations.AddField(
            model_name="organization",
            name="monthly_threshold",
            field=models.DecimalField(
                blank=True,
                decimal_places=2,
                help_text=(
                    "Monthly volume cap. NULL = no cap. "
                    "The default org must always be NULL (enforced by DB constraint)."
                ),
                max_digits=12,
                null=True,
            ),
        ),
        migrations.AddField(
            model_name="organization",
            name="current_month_volume",
            field=models.DecimalField(
                decimal_places=2,
                default=0,
                help_text="Running volume for volume_month. Incremented via F() at auth confirmation.",
                max_digits=12,
            ),
        ),
        migrations.AddField(
            model_name="organization",
            name="volume_month",
            field=models.DateField(
                blank=True,
                help_text="First day of the month covered by current_month_volume.",
                null=True,
            ),
        ),
        # --- Lifecycle status ---
        migrations.AddField(
            model_name="organization",
            name="status",
            field=models.CharField(
                choices=[("active", "active"), ("draft", "draft")],
                default="draft",
                help_text="Draft orgs are excluded from routing entirely.",
                max_length=10,
            ),
        ),
        # --- DB CheckConstraint: default org must have NULL monthly_threshold ---
        migrations.AddConstraint(
            model_name="organization",
            constraint=models.CheckConstraint(
                condition=models.Q(is_default=False) | models.Q(monthly_threshold__isnull=True),
                name="organization_default_no_cap",
            ),
        ),
    ]
