import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("cart", "0003_alter_cart_discount_code"),
        ("orders", "0010_alter_ordercharge_status"),
        ("shipping", "0002_seed_carriers_and_catchall_zone"),
        ("stores", "0003_alter_field_metadata"),
    ]

    operations = [
        migrations.CreateModel(
            name="CheckoutState",
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
                    "step",
                    models.CharField(
                        choices=[
                            ("contact", "Contact"),
                            ("address", "Shipping address"),
                            ("shipping", "Shipping method"),
                            ("payment", "Payment"),
                            ("confirmed", "Confirmed"),
                        ],
                        default="contact",
                        max_length=20,
                    ),
                ),
                ("email", models.EmailField(blank=True, default="", max_length=254)),
                ("phone", models.CharField(blank=True, default="", max_length=50)),
                ("newsletter_opt_in", models.BooleanField(default=False)),
                ("shipping_address", models.JSONField(default=dict)),
                ("billing_address", models.JSONField(default=dict)),
                ("checkout_language", models.CharField(blank=True, default="", max_length=10)),
                (
                    "initiate_event_fired",
                    models.BooleanField(
                        default=False,
                        help_text="True after EVT_INITIATE_CHECKOUT has been fired once for this checkout session.",
                    ),
                ),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                ("updated_at", models.DateTimeField(auto_now=True)),
                (
                    "cart",
                    models.OneToOneField(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="checkout_state",
                        to="cart.cart",
                    ),
                ),
                (
                    "order",
                    models.ForeignKey(
                        blank=True,
                        null=True,
                        on_delete=django.db.models.deletion.SET_NULL,
                        related_name="+",
                        to="orders.order",
                    ),
                ),
                (
                    "shipping_rate",
                    models.ForeignKey(
                        blank=True,
                        null=True,
                        on_delete=django.db.models.deletion.SET_NULL,
                        related_name="+",
                        to="shipping.shippingrate",
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
                "verbose_name": "Checkout State",
                "verbose_name_plural": "Checkout States",
            },
        ),
    ]
