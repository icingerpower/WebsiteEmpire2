# Generated manually for T007 Phase-1 remediation
# Adds: DiscountCode.provenance, .stackable, .free_product, .currency
# Adds: CampaignReward model (ADR-002 §5 idempotency guard)

import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("catalog", "0002_collection_collectionproduct_and_more"),
        ("discounts", "0002_initial"),
        ("stores", "0003_alter_field_metadata"),
    ]

    operations = [
        # --- DiscountCode new fields ---
        migrations.AddField(
            model_name="discountcode",
            name="provenance",
            field=models.CharField(
                choices=[
                    ("merchant", "Merchant"),
                    ("lead_capture", "Lead capture"),
                    ("campaign", "Campaign"),
                    ("sold", "Sold"),
                ],
                default="merchant",
                help_text=(
                    "Origin of this code: merchant (manually created), lead_capture "
                    "(sign-up flow), campaign (external campaign reward), sold (paid for). "
                    "Used for the \"765×$5 audit\" — how many codes of each origin "
                    "were redeemed."
                ),
                max_length=20,
            ),
        ),
        migrations.AddField(
            model_name="discountcode",
            name="stackable",
            field=models.BooleanField(
                default=False,
                help_text=(
                    "If True, this code may be combined with a gift card at checkout. "
                    "If False, DiscountService will reject it when another discount is "
                    "already present in the cart (coupon+gift-card stacking opt-out)."
                ),
            ),
        ),
        migrations.AddField(
            model_name="discountcode",
            name="free_product",
            field=models.ForeignKey(
                blank=True,
                help_text=(
                    "Only meaningful when value_type=FREE_PRODUCT. "
                    "The specific product the customer receives for free."
                ),
                null=True,
                on_delete=django.db.models.deletion.SET_NULL,
                related_name="free_product_coupons",
                to="catalog.product",
            ),
        ),
        migrations.AddField(
            model_name="discountcode",
            name="currency",
            field=models.CharField(
                blank=True,
                default="",
                help_text="ISO 4217 currency code (e.g. EUR, USD). Empty = all currencies.",
                max_length=3,
            ),
        ),
        # --- CampaignReward model ---
        migrations.CreateModel(
            name="CampaignReward",
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
                    "campaign_id",
                    models.CharField(
                        help_text="Opaque external campaign reference (e.g. marketing platform UUID).",
                        max_length=100,
                    ),
                ),
                (
                    "recipient_email",
                    models.EmailField(
                        help_text="Normalise to lowercase before create/lookup to avoid duplicates.",
                    ),
                ),
                (
                    "discount_code",
                    models.ForeignKey(
                        help_text="The code issued to this recipient for this campaign.",
                        on_delete=django.db.models.deletion.PROTECT,
                        related_name="campaign_rewards",
                        to="discounts.discountcode",
                    ),
                ),
                (
                    "issued_at",
                    models.DateTimeField(auto_now_add=True),
                ),
            ],
            options={
                "verbose_name": "campaign reward",
                "verbose_name_plural": "campaign rewards",
            },
        ),
        migrations.AddIndex(
            model_name="campaignreward",
            index=models.Index(
                fields=["store", "campaign_id"],
                name="disc_camp_reward_camp_idx",
            ),
        ),
        migrations.AddIndex(
            model_name="campaignreward",
            index=models.Index(
                fields=["store", "recipient_email"],
                name="disc_camp_reward_email_idx",
            ),
        ),
        migrations.AlterUniqueTogether(
            name="campaignreward",
            unique_together={("store", "campaign_id", "recipient_email")},
        ),
    ]
