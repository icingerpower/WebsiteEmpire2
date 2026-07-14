# ADR-029 Risks: the D5 frequency-cap check and the D9 refund-voiding lookup
# both run `campaign_id__startswith=...` / `campaign_id__endswith=...` queries
# against discounts_campaignreward.campaign_id. On PostgreSQL, a plain btree
# index on a text/varchar column can only serve a `LIKE 'prefix%'` query under
# the C locale — under any other (locale-aware) collation, which is the
# default for most deployments, Postgres cannot use a plain index for LIKE and
# falls back to a sequential scan. This migration adds a second, Postgres-only
# index using the `varchar_pattern_ops` operator class, which Postgres CAN use
# for `LIKE 'prefix%'` regardless of locale.
#
# Guarded by `schema_editor.connection.vendor` because:
# - sqlite (used by the test suite, webecom/settings/test.py) does not have a
#   pattern-ops concept at all and CREATE INDEX ... varchar_pattern_ops would
#   simply error.
# - Django's own `Index(opclasses=[...])` field is documented as
#   PostgreSQL-only and raises NotSupportedError on other backends' schema
#   editors — a plain migrations.RunPython + raw SQL, self-guarded by vendor,
#   is the safe cross-backend way to add this.
#
# Reversible: DROP INDEX IF EXISTS is a no-op on backends where the index was
# never created.

from django.db import migrations

_INDEX_NAME = "disc_camp_reward_campid_pattern_idx"


def create_pattern_index(apps, schema_editor):
    if schema_editor.connection.vendor != "postgresql":
        return
    schema_editor.execute(
        f"CREATE INDEX IF NOT EXISTS {_INDEX_NAME} "
        "ON discounts_campaignreward (campaign_id varchar_pattern_ops)"
    )


def drop_pattern_index(apps, schema_editor):
    if schema_editor.connection.vendor != "postgresql":
        return
    schema_editor.execute(f"DROP INDEX IF EXISTS {_INDEX_NAME}")


class Migration(migrations.Migration):

    dependencies = [
        ("discounts", "0005_gift_card_campaigns"),
    ]

    operations = [
        migrations.RunPython(create_pattern_index, drop_pattern_index),
    ]
