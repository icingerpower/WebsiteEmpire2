# Generated for TICKET-027 (ADR-009): additive campaign model delta.
#
# Operations (all additive — no columns dropped or modified):
#   1. Campaign.owner_scope  — store vs platform discriminator
#   2. Campaign.entry_step   — explicit funnel entry point FK (nullable, SET_NULL)
#   3. CampaignSession.campaign_type — denormalized CharField (blank, backfilled below)
#   4. CampaignIssuedCode     — attribution + idempotency link table
#   5. CampaignStepTranslation — customer-visible translated content
#   6. RunPython: backfill CampaignSession.campaign_type from campaign.campaign_type
#   7. UniqueConstraint on (CampaignSession.order, CampaignSession.campaign_type)
#      Added AFTER the backfill so existing rows have non-empty campaign_type values.

import django.db.models.deletion
from django.db import migrations, models


def backfill_campaign_session_campaign_type(apps, schema_editor):
    """
    Backfill CampaignSession.campaign_type from the related Campaign.

    At migration time, all existing sessions have campaign_type='' (blank default).
    We update them in batches per campaign to minimise the number of UPDATE statements.
    """
    Campaign = apps.get_model("campaigns", "Campaign")
    CampaignSession = apps.get_model("campaigns", "CampaignSession")

    for campaign in Campaign.objects.all():
        CampaignSession.objects.filter(
            campaign=campaign,
            campaign_type="",
        ).update(campaign_type=campaign.campaign_type)


def noop_reverse(apps, schema_editor):
    """Reverse of the backfill: no-op (clearing the denormalized field is harmless)."""
    pass


class Migration(migrations.Migration):

    dependencies = [
        ("aijobs", "0004_alter_aijob_job_type"),
        ("campaigns", "0001_initial"),
        ("discounts", "0004_alter_discountcode_times_used"),
    ]

    operations = [
        # --- 1. Campaign.owner_scope ---
        migrations.AddField(
            model_name="campaign",
            name="owner_scope",
            field=models.CharField(
                choices=[
                    ("store", "Store admin"),
                    ("platform", "Platform (super-admin)"),
                ],
                default="store",
                max_length=10,
                help_text=(
                    "Scope of the campaign creator. 'store' = store admin owns this "
                    "campaign; 'platform' = super-admin created it. Precedence: "
                    "store-owned shadows platform campaigns of the same type for the "
                    "same store."
                ),
            ),
        ),
        # --- 2. Campaign.entry_step ---
        migrations.AddField(
            model_name="campaign",
            name="entry_step",
            field=models.ForeignKey(
                blank=True,
                help_text=(
                    "First step shown at funnel entry. Required to activate a funnel "
                    "campaign. Must belong to this campaign. Never derived from position "
                    "(§XV-2 / ADR-009 §1)."
                ),
                null=True,
                on_delete=django.db.models.deletion.SET_NULL,
                related_name="+",
                to="campaigns.campaignstep",
            ),
        ),
        # --- 3. CampaignSession.campaign_type (blank; backfilled below) ---
        migrations.AddField(
            model_name="campaignsession",
            name="campaign_type",
            field=models.CharField(
                blank=True,
                default="",
                max_length=32,
                help_text=(
                    "Denormalized from campaign.campaign_type at session creation. "
                    "Enforces the (order, campaign_type) uniqueness invariant at DB level."
                ),
            ),
            preserve_default=False,
        ),
        # --- 4. CampaignIssuedCode ---
        migrations.CreateModel(
            name="CampaignIssuedCode",
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
                    "issued_at",
                    models.DateTimeField(auto_now_add=True),
                ),
                (
                    "campaign_session",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="issued_codes",
                        to="campaigns.campaignsession",
                    ),
                ),
                (
                    "discount_code",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.PROTECT,
                        related_name="+",
                        to="discounts.discountcode",
                    ),
                ),
                (
                    "step",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.PROTECT,
                        related_name="issued_codes",
                        to="campaigns.campaignstep",
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
                "verbose_name": "campaign issued code",
                "verbose_name_plural": "campaign issued codes",
            },
        ),
        migrations.AddConstraint(
            model_name="campaignissuedcode",
            constraint=models.UniqueConstraint(
                fields=["campaign_session", "step"],
                name="uniq_issued_code_per_session_step",
            ),
        ),
        # --- 5. CampaignStepTranslation ---
        migrations.CreateModel(
            name="CampaignStepTranslation",
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
                ("title", models.CharField(blank=True, max_length=255)),
                ("description", models.TextField(blank=True)),
                (
                    "cta_label",
                    models.CharField(
                        blank=True,
                        help_text="Translated call-to-action button label shown to the shopper.",
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
                ("created_at", models.DateTimeField(auto_now_add=True)),
                ("updated_at", models.DateTimeField(auto_now=True)),
                (
                    "ai_job",
                    models.ForeignKey(
                        blank=True,
                        null=True,
                        on_delete=django.db.models.deletion.SET_NULL,
                        related_name="campaign_step_translations",
                        to="aijobs.aijob",
                    ),
                ),
                (
                    "step",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="translations",
                        to="campaigns.campaignstep",
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
                "verbose_name": "campaign step translation",
                "verbose_name_plural": "campaign step translations",
            },
        ),
        migrations.AlterUniqueTogether(
            name="campaignsteptranslation",
            unique_together={("store", "step", "lang_code")},
        ),
        # --- 6. Backfill CampaignSession.campaign_type ---
        # Must run BEFORE the UniqueConstraint so that all rows have a non-empty
        # campaign_type value (blank string would violate the constraint for multiple
        # sessions on the same order).
        migrations.RunPython(
            backfill_campaign_session_campaign_type,
            reverse_code=noop_reverse,
        ),
        # --- 7. UniqueConstraint (order, campaign_type) — added AFTER backfill ---
        migrations.AddConstraint(
            model_name="campaignsession",
            constraint=models.UniqueConstraint(
                fields=["order", "campaign_type"],
                name="uniq_session_per_order_type",
            ),
        ),
    ]
