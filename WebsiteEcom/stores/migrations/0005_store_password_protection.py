"""
Migration 0005: Add Store.password_protection boolean field (T002 MINOR).

Spec §1 defines password_protection: boolean, default false on Store.
Used to gate storefront access behind a password during pre-launch or for
private storefronts.  Storefront enforcement ships in Phase 2; this migration
only adds the storage column.

Default=False means all existing stores remain publicly accessible after
applying this migration — no data migration required.
"""

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("stores", "0004_organization_routing_fields"),
    ]

    operations = [
        migrations.AddField(
            model_name="store",
            name="password_protection",
            field=models.BooleanField(
                default=False,
                help_text=(
                    "When True, the storefront requires a password before visitors "
                    "can browse.  Used for pre-launch or private storefronts."
                ),
            ),
        ),
    ]
