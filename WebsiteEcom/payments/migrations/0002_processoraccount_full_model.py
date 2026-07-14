"""
Migration 0002: Replace stub ProcessorAccount with full model (TICKET-017).

Changes from 0001 stub:
- Remove store FK (ProcessorAccount is NOT StoreOwnedModel — owned by Organization).
- Add organization FK (to stores.Organization, CASCADE).
- Add processor_type, display_name, is_active, is_test_mode.
- Add api_key, api_secret, webhook_secret (EncryptedCharField = TextField, encrypted at rest).
- Add account_id, client_id (plain CharField, non-secret routing identifiers).
- Add created_at, updated_at timestamps.
- Add conditional UniqueConstraint: unique (organization, processor_type) where is_active=True.

The one-off default for created_at is django.utils.timezone.now — safe because the
stub ProcessorAccount table was empty in all environments (it was declared only to
resolve the lazy FK in orders.Order).
"""

import django.db.models.deletion
import django.utils.timezone
import payments.fields
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("payments", "0001_initial"),
        ("stores", "0003_alter_field_metadata"),
    ]

    operations = [
        # 1. Remove the store FK from the stub StoreOwnedModel base.
        migrations.RemoveField(
            model_name="processoraccount",
            name="store",
        ),
        # 2. Add organization FK.
        migrations.AddField(
            model_name="processoraccount",
            name="organization",
            field=models.ForeignKey(
                help_text="Organization (legal entity) that owns these credentials.",
                on_delete=django.db.models.deletion.CASCADE,
                related_name="processor_accounts",
                to="stores.organization",
                # Temporary default for existing rows — table was empty in practice.
                default=None,
                null=True,
            ),
            preserve_default=False,
        ),
        # 3. Make organization non-nullable now that it exists.
        migrations.AlterField(
            model_name="processoraccount",
            name="organization",
            field=models.ForeignKey(
                help_text="Organization (legal entity) that owns these credentials.",
                on_delete=django.db.models.deletion.CASCADE,
                related_name="processor_accounts",
                to="stores.organization",
            ),
        ),
        # 4. Add processor_type.
        migrations.AddField(
            model_name="processoraccount",
            name="processor_type",
            field=models.CharField(
                choices=[("stripe", "Stripe"), ("paypal", "PayPal")],
                default="stripe",
                help_text="Payment processor family (e.g. stripe, paypal).",
                max_length=50,
            ),
            preserve_default=False,
        ),
        # 5. Add display_name.
        migrations.AddField(
            model_name="processoraccount",
            name="display_name",
            field=models.CharField(
                default="",
                help_text="Human-readable label shown in /superadmin/ lists.",
                max_length=255,
            ),
            preserve_default=False,
        ),
        # 6. Add is_active.
        migrations.AddField(
            model_name="processoraccount",
            name="is_active",
            field=models.BooleanField(
                default=True,
                help_text=(
                    "Manual block flag.  False = excluded from routing unconditionally, "
                    "regardless of health-probe status."
                ),
            ),
        ),
        # 7. Add is_test_mode.
        migrations.AddField(
            model_name="processoraccount",
            name="is_test_mode",
            field=models.BooleanField(
                default=True,
                help_text=(
                    "True = sandbox/test credentials; False = live production credentials. "
                    "Never mix test and live credentials in the same account row."
                ),
            ),
        ),
        # 8. Add encrypted credential fields (EncryptedCharField — stored as TEXT, encrypted at rest).
        migrations.AddField(
            model_name="processoraccount",
            name="api_key",
            field=payments.fields.EncryptedCharField(
                blank=True,
                default="",
                help_text="Primary API key / secret key (encrypted at rest).",
            ),
        ),
        migrations.AddField(
            model_name="processoraccount",
            name="api_secret",
            field=payments.fields.EncryptedCharField(
                blank=True,
                default="",
                help_text="API secret / client secret (encrypted at rest).",
            ),
        ),
        migrations.AddField(
            model_name="processoraccount",
            name="webhook_secret",
            field=payments.fields.EncryptedCharField(
                blank=True,
                default="",
                help_text="Webhook signing secret for validating inbound events (encrypted at rest).",
            ),
        ),
        # 9. Add non-secret routing identifiers.
        migrations.AddField(
            model_name="processoraccount",
            name="account_id",
            field=models.CharField(
                blank=True,
                default="",
                help_text="Stripe Connect account ID (e.g. acct_xxxx) or equivalent.",
                max_length=255,
            ),
        ),
        migrations.AddField(
            model_name="processoraccount",
            name="client_id",
            field=models.CharField(
                blank=True,
                default="",
                help_text="PayPal client ID or equivalent public identifier.",
                max_length=255,
            ),
        ),
        # 10. Add timestamps.
        migrations.AddField(
            model_name="processoraccount",
            name="created_at",
            field=models.DateTimeField(
                auto_now_add=True,
                default=django.utils.timezone.now,
            ),
            preserve_default=False,
        ),
        migrations.AddField(
            model_name="processoraccount",
            name="updated_at",
            field=models.DateTimeField(auto_now=True),
        ),
        # 11. Add conditional UniqueConstraint.
        migrations.AddConstraint(
            model_name="processoraccount",
            constraint=models.UniqueConstraint(
                condition=models.Q(is_active=True),
                fields=["organization", "processor_type"],
                name="unique_org_processor_active",
            ),
        ),
    ]
