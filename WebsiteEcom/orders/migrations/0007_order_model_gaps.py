# Generated migration for T009/T010 Phase-1 remediation.
#
# Adds:
#   Order.customer         FK to customers.Customer (nullable)
#   Order.organization     FK to stores.Organization (nullable, PROTECT)
#   Order.authorized_at    DateTimeField (nullable)
#   Order.captured_at      DateTimeField (nullable)
#   Order.placed_at        DateTimeField (nullable)
#   Order.card_last4       CharField(4, blank)
#   Order.customer_local_hour  SmallIntegerField (nullable, 0-23)
#   OrderCharge.refunded_amount  DecimalField (default 0.00, ADR-007 §7)
#   OrderFulfillment.status      CharField (FulfillmentRecordStatus, default pending)
#   OrderFulfillment.line_items_json  JSONField (default list)
#
# All new fields are nullable or have defaults — no data loss, safe to reverse.

import decimal

import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("customers", "0001_initial"),
        ("orders", "0006_alter_order_discount_code"),
        ("stores", "0004_organization_routing_fields"),
    ]

    operations = [
        # ------------------------------------------------------------------
        # Order: customer FK
        # ------------------------------------------------------------------
        migrations.AddField(
            model_name="order",
            name="customer",
            field=models.ForeignKey(
                blank=True,
                help_text="Linked Customer record (null for guest checkout).",
                null=True,
                on_delete=django.db.models.deletion.SET_NULL,
                related_name="orders",
                to="customers.customer",
            ),
        ),
        # ------------------------------------------------------------------
        # Order: organization FK (merchant-of-record, ADR-006)
        # ------------------------------------------------------------------
        migrations.AddField(
            model_name="order",
            name="organization",
            field=models.ForeignKey(
                blank=True,
                help_text=(
                    "Merchant-of-record Organization for this order "
                    "(set at routing time, ADR-006)."
                ),
                null=True,
                on_delete=django.db.models.deletion.PROTECT,
                related_name="orders",
                to="stores.organization",
            ),
        ),
        # ------------------------------------------------------------------
        # Order: payment timestamp fields
        # ------------------------------------------------------------------
        migrations.AddField(
            model_name="order",
            name="authorized_at",
            field=models.DateTimeField(
                blank=True,
                null=True,
                help_text="Timestamp when payment was authorized by the processor.",
            ),
        ),
        migrations.AddField(
            model_name="order",
            name="captured_at",
            field=models.DateTimeField(
                blank=True,
                null=True,
                help_text="Timestamp when payment was captured by the processor.",
            ),
        ),
        migrations.AddField(
            model_name="order",
            name="placed_at",
            field=models.DateTimeField(
                blank=True,
                null=True,
                help_text=(
                    "Timestamp when the customer confirmed the order. May differ from "
                    "created_at for draft orders or abandoned-cart recoveries."
                ),
            ),
        ),
        # ------------------------------------------------------------------
        # Order: card_last4 and customer_local_hour
        # ------------------------------------------------------------------
        migrations.AddField(
            model_name="order",
            name="card_last4",
            field=models.CharField(
                blank=True,
                default="",
                max_length=4,
                help_text="Last 4 digits of the card used (display only — never store full PAN).",
            ),
        ),
        migrations.AddField(
            model_name="order",
            name="customer_local_hour",
            field=models.SmallIntegerField(
                blank=True,
                null=True,
                help_text=(
                    "Customer local hour (0–23) at order placement, derived from the "
                    "timezone header or IP geo. Used for time-of-day analytics segmentation."
                ),
            ),
        ),
        # ------------------------------------------------------------------
        # OrderCharge: refunded_amount (ADR-007 §7 refund cap)
        # ------------------------------------------------------------------
        migrations.AddField(
            model_name="ordercharge",
            name="refunded_amount",
            field=models.DecimalField(
                decimal_places=2,
                default=decimal.Decimal("0.00"),
                help_text=(
                    "Cumulative amount already refunded against this charge (ADR-007 §7). "
                    "Refund cap = (amount_cents / 100) − refunded_amount."
                ),
                max_digits=10,
            ),
        ),
        # ------------------------------------------------------------------
        # OrderFulfillment: status and line_items_json (T010, spec §4)
        # ------------------------------------------------------------------
        migrations.AddField(
            model_name="orderfulfillment",
            name="status",
            field=models.CharField(
                choices=[
                    ("pending", "Pending"),
                    ("partial", "Partial"),
                    ("shipped", "Shipped"),
                    ("delivered", "Delivered"),
                    ("returned", "Returned"),
                ],
                default="pending",
                max_length=20,
                help_text="Per-shipment lifecycle status (not the order-level fulfillment_status).",
            ),
        ),
        migrations.AddField(
            model_name="orderfulfillment",
            name="line_items_json",
            field=models.JSONField(
                default=list,
                help_text=(
                    "List of {variant_id, quantity_shipped} dicts identifying which items "
                    "and quantities this fulfillment covers. Used for partial fulfillment tracking."
                ),
            ),
        ),
    ]
