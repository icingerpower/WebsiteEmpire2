from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("orders", "0008_order_processor_customer_id_and_more"),
    ]

    operations = [
        migrations.AddField(
            model_name="ordercharge",
            name="breach_alerted",
            field=models.BooleanField(
                default=False,
                help_text=(
                    "Set to True after the first uncaptured-breach alert is emitted "
                    "for this charge (ADR-011 Q5 pass 4). Prevents repeated alerts "
                    "per charge on subsequent watchdog runs."
                ),
            ),
        ),
    ]
