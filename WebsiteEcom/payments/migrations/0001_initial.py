# Stub migration — declares ProcessorAccount so that the lazy FK in
# orders.Order.processor_account ('payments.ProcessorAccount') resolves at
# system-check time. Full payments schema ships in a future ticket.

import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):

    initial = True

    dependencies = [
        ("stores", "0003_alter_field_metadata"),
    ]

    operations = [
        migrations.CreateModel(
            name="ProcessorAccount",
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
            ],
            options={
                "verbose_name": "processor account",
                "verbose_name_plural": "processor accounts",
                "abstract": False,
            },
        ),
    ]
