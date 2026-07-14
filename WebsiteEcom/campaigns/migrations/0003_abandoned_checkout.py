# Generated for TICKET-021 (ADR-010): abandoned-checkout campaign.
#
# All operations are additive — no columns dropped.
#
# Operations:
#   1.  CampaignSession.order → nullable (abandoned-checkout has no order at detection)
#   2.  CampaignSession: add cart FK (nullable, SET_NULL)
#   3.  CampaignSession: add customer_email (EmailField, blank, default='')
#   4.  CampaignSession: add abandoned_at (nullable DateTimeField)
#   5.  CreateModel AbandonedCheckoutEmailStep
#   6.  CreateModel AbandonedCheckoutEmailSend
#   7.  AddConstraint AbandonedCheckoutEmailSend UniqueConstraint(session, email_step)
#   8.  CampaignIssuedCode.step → nullable
#   9.  CampaignIssuedCode: add email_step FK (nullable, PROTECT)
#   10. RunPython: backfill CampaignSession.customer_email from order.customer_email
#   11. AddConstraint CampaignSession: CheckConstraint(order OR cart OR email != '')
#   12. AddConstraint CampaignSession: UniqueConstraint(cart, campaign_type) WHERE cart IS NOT NULL
#   13. AddIndex CampaignSession: (store, campaign_type, state) for beat scan
#   14. AddConstraint CampaignIssuedCode: CheckConstraint exactly-one-of(step, email_step)
#   15. AddConstraint CampaignIssuedCode: UniqueConstraint(campaign_session, email_step)
#                                        WHERE email_step IS NOT NULL

import django.core.validators
import django.db.models.deletion
from django.db import migrations, models


def backfill_customer_email(apps, schema_editor):
    """
    Populate customer_email on existing CampaignSession rows from order.customer_email.

    All existing sessions have order IS NOT NULL (the field was required before this
    migration). The backfill enables cross-session suppression matching (AC-182) on
    historical sessions if the suppression service is ever called on old data.
    """
    CampaignSession = apps.get_model("campaigns", "CampaignSession")
    for session in CampaignSession.objects.filter(order__isnull=False).select_related("order"):
        if not session.customer_email:
            email = getattr(session.order, "customer_email", "") or ""
            if email:
                CampaignSession.objects.filter(pk=session.pk).update(customer_email=email)


class Migration(migrations.Migration):

    dependencies = [
        ("campaigns", "0002_campaign_model_delta"),
        ("cart", "0003_alter_cart_discount_code"),
    ]

    operations = [
        # --- 1. CampaignSession.order → nullable ---
        migrations.AlterField(
            model_name="campaignsession",
            name="order",
            field=models.ForeignKey(
                null=True,
                blank=True,
                on_delete=django.db.models.deletion.CASCADE,
                related_name="campaign_sessions",
                to="orders.order",
                help_text=(
                    "Order anchor for funnel-type sessions. Null for abandoned-checkout "
                    "sessions which use the cart FK instead (ADR-010 Q1)."
                ),
            ),
        ),
        # --- 2. CampaignSession.cart FK (nullable, SET_NULL) ---
        migrations.AddField(
            model_name="campaignsession",
            name="cart",
            field=models.ForeignKey(
                null=True,
                blank=True,
                on_delete=django.db.models.deletion.SET_NULL,
                related_name="campaign_sessions",
                to="cart.cart",
                help_text=(
                    "Cart anchor for abandoned-checkout sessions. SET_NULL on cart "
                    "deletion so the campaign audit trail survives cart purge (ADR-010 Q1)."
                ),
            ),
        ),
        # --- 3. CampaignSession.customer_email ---
        migrations.AddField(
            model_name="campaignsession",
            name="customer_email",
            field=models.EmailField(
                blank=True,
                default="",
                help_text=(
                    "Denormalized from cart.customer_email at session creation. "
                    "Survives cart deletion; used for cross-session suppression (AC-182)."
                ),
            ),
        ),
        # --- 4. CampaignSession.abandoned_at ---
        migrations.AddField(
            model_name="campaignsession",
            name="abandoned_at",
            field=models.DateTimeField(
                null=True,
                blank=True,
                help_text=(
                    "Frozen reference time for all send-delay calculations. "
                    "Set to cart.updated_at at detection time (ADR-010 Q4)."
                ),
            ),
        ),
        # --- 5. CreateModel AbandonedCheckoutEmailStep ---
        migrations.CreateModel(
            name="AbandonedCheckoutEmailStep",
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
                    "position",
                    models.PositiveIntegerField(
                        help_text=(
                            "Display order within the campaign. Gaps allowed; never renumbered (§VII). "
                            "Sequence order is defined by send_delay_hours, not position."
                        ),
                    ),
                ),
                (
                    "send_delay_hours",
                    models.PositiveIntegerField(
                        default=1,
                        validators=[django.core.validators.MinValueValidator(1)],
                        help_text=(
                            "Hours from session.abandoned_at before this email is sent. "
                            "Must be >= 1. Days-unit form input is stored as hours × 24. "
                            "Measured from abandoned_at for all steps (ADR-010 ASSUMPTION)."
                        ),
                    ),
                ),
                (
                    "email_type",
                    models.CharField(
                        max_length=20,
                        choices=[
                            ("warning", "Warning"),
                            ("reminder", "Reminder"),
                            ("incentive", "Incentive"),
                        ],
                        help_text="Semantic category of this email step.",
                    ),
                ),
                (
                    "email_style",
                    models.CharField(
                        max_length=30,
                        help_text="Wizard style key for the rendering wrapper (template wrapper choice).",
                    ),
                ),
                (
                    "subject",
                    models.CharField(
                        max_length=255,
                        help_text="Django template string for the email subject (may include {{ store.name }}, etc.).",
                    ),
                ),
                (
                    "body_template",
                    models.TextField(
                        help_text=(
                            "Django template string for the email body HTML. "
                            "Context: cart_items, resume_url, coupon_code, store."
                        ),
                    ),
                ),
                (
                    "coupon_config_json",
                    models.JSONField(
                        default=dict,
                        help_text=(
                            "Coupon to issue with this email. {} = no coupon. "
                            "Schema: {value_type: 'percent'|'fixed', value: number > 0, "
                            "expires_in_days: int >= 1 (default 30)}."
                        ),
                    ),
                ),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                ("updated_at", models.DateTimeField(auto_now=True)),
                (
                    "campaign",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="email_steps",
                        to="campaigns.campaign",
                        limit_choices_to={"campaign_type": "abandoned_checkout"},
                        help_text="Parent campaign — must be of type 'abandoned_checkout'.",
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
                "verbose_name": "abandoned checkout email step",
                "verbose_name_plural": "abandoned checkout email steps",
                "ordering": ["send_delay_hours", "position"],
            },
        ),
        migrations.AddIndex(
            model_name="abandonedcheckoutemailstep",
            index=models.Index(
                fields=["campaign", "send_delay_hours"],
                name="idx_email_step_campaign_delay",
            ),
        ),
        # --- 6. CreateModel AbandonedCheckoutEmailSend ---
        migrations.CreateModel(
            name="AbandonedCheckoutEmailSend",
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
                    "status",
                    models.CharField(
                        max_length=20,
                        choices=[
                            ("scheduled", "Scheduled"),
                            ("sent", "Sent"),
                            ("cancelled", "Cancelled"),
                            ("failed", "Failed"),
                        ],
                        default="scheduled",
                        help_text="Lifecycle state of this send attempt (§XV-3 — always written explicitly).",
                    ),
                ),
                (
                    "scheduled_at",
                    models.DateTimeField(
                        help_text="Due time: session.abandoned_at + step.send_delay_hours.",
                    ),
                ),
                (
                    "sent_at",
                    models.DateTimeField(
                        null=True,
                        blank=True,
                        help_text="Set when the email was accepted by the email backend.",
                    ),
                ),
                (
                    "cancelled_at",
                    models.DateTimeField(
                        null=True,
                        blank=True,
                        help_text="Set when suppression or a race guard cancelled this send.",
                    ),
                ),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                (
                    "email_step",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.PROTECT,
                        related_name="sends",
                        to="campaigns.abandonedcheckoutemailstep",
                    ),
                ),
                (
                    "session",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="email_sends",
                        to="campaigns.campaignsession",
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
                "verbose_name": "abandoned checkout email send",
                "verbose_name_plural": "abandoned checkout email sends",
            },
        ),
        migrations.AddIndex(
            model_name="abandonedcheckoutemailsend",
            index=models.Index(
                fields=["session", "status"],
                name="idx_email_send_session_status",
            ),
        ),
        # --- 7. UniqueConstraint AbandonedCheckoutEmailSend(session, email_step) ---
        migrations.AddConstraint(
            model_name="abandonedcheckoutemailsend",
            constraint=models.UniqueConstraint(
                fields=["session", "email_step"],
                name="uniq_send_per_session_step",
            ),
        ),
        # --- 8. CampaignIssuedCode.step → nullable ---
        migrations.AlterField(
            model_name="campaignissuedcode",
            name="step",
            field=models.ForeignKey(
                null=True,
                blank=True,
                on_delete=django.db.models.deletion.PROTECT,
                related_name="issued_codes",
                to="campaigns.campaignstep",
                help_text="Funnel step that issued this code. Null for abandoned-checkout coupons.",
            ),
        ),
        # --- 9. CampaignIssuedCode.email_step FK ---
        migrations.AddField(
            model_name="campaignissuedcode",
            name="email_step",
            field=models.ForeignKey(
                null=True,
                blank=True,
                on_delete=django.db.models.deletion.PROTECT,
                related_name="issued_codes",
                to="campaigns.abandonedcheckoutemailstep",
                help_text="Abandoned-checkout email step that issued this code. Null for funnel coupons.",
            ),
        ),
        # --- 10. Backfill customer_email from order.customer_email ---
        # Must run BEFORE the CheckConstraint so all existing rows are in a valid state.
        # All existing rows have order IS NOT NULL → constraint satisfied regardless,
        # but backfill ensures customer_email is set for suppression matching.
        migrations.RunPython(
            backfill_customer_email,
            reverse_code=migrations.RunPython.noop,
        ),
        # --- 11. CampaignSession CheckConstraint: anchor invariant ---
        migrations.AddConstraint(
            model_name="campaignsession",
            constraint=models.CheckConstraint(
                condition=(
                    models.Q(order__isnull=False)
                    | models.Q(cart__isnull=False)
                    | ~models.Q(customer_email="")
                ),
                name="session_has_anchor",
            ),
        ),
        # --- 12. CampaignSession UniqueConstraint: (cart, campaign_type) WHERE cart IS NOT NULL ---
        migrations.AddConstraint(
            model_name="campaignsession",
            constraint=models.UniqueConstraint(
                fields=["cart", "campaign_type"],
                condition=models.Q(cart__isnull=False),
                name="uniq_session_per_cart_type",
            ),
        ),
        # --- 13. CampaignSession Index: (store, campaign_type, state) ---
        migrations.AddIndex(
            model_name="campaignsession",
            index=models.Index(
                fields=["store", "campaign_type", "state"],
                name="idx_session_store_type_state",
            ),
        ),
        # --- 14. CampaignIssuedCode CheckConstraint: exactly-one-of(step, email_step) ---
        # Existing rows all have step IS NOT NULL, email_step IS NULL → satisfies constraint.
        migrations.AddConstraint(
            model_name="campaignissuedcode",
            constraint=models.CheckConstraint(
                condition=(
                    (
                        models.Q(step__isnull=False)
                        & models.Q(email_step__isnull=True)
                    )
                    | (
                        models.Q(step__isnull=True)
                        & models.Q(email_step__isnull=False)
                    )
                ),
                name="issued_code_exactly_one_step",
            ),
        ),
        # --- 15. CampaignIssuedCode UniqueConstraint: (campaign_session, email_step) ---
        migrations.AddConstraint(
            model_name="campaignissuedcode",
            constraint=models.UniqueConstraint(
                fields=["campaign_session", "email_step"],
                condition=models.Q(email_step__isnull=False),
                name="uniq_issued_code_per_session_email_step",
            ),
        ),
    ]
