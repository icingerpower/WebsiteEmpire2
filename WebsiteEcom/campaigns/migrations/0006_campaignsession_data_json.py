from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("campaigns", "0005_campaign_capture_window_minutes_and_more"),
    ]

    operations = [
        migrations.AddField(
            model_name="campaignsession",
            name="data_json",
            field=models.JSONField(
                blank=True,
                default=list,
                help_text=(
                    "Audit log of notable events for this session "
                    "(e.g. upsell_charge_failed entries). Each entry is a dict with "
                    "'event' and 'ts' keys appended by the service layer."
                ),
            ),
        ),
    ]
