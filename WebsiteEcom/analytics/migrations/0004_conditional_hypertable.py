# TICKET-044 / ADR-035: vendor+extension-gated hypertable conversion.
#
# This migration NEVER unconditionally converts analytics_event to a
# TimescaleDB hypertable. It only does so when the connection is PostgreSQL
# AND the timescaledb extension is already installed on the target database
# (an ops/release-checklist step, per ADR-035 point 5 — never an automatic
# side effect of `migrate`). On every other backend/environment this is a
# documented no-op that always logs at INFO which mode it detected (§XV-1) —
# it must never merely skip silently.
#
# See docs/adr/ADR-035-timescaledb-migration.md for the full decision record.

import logging

from django.db import migrations

from analytics.timescale import is_timescale_active

logger = logging.getLogger('analytics.timescale')


def create_hypertable_if_active(apps, schema_editor):
    connection = schema_editor.connection

    # use_cache=False: a migration run must never trust a cache warmed by
    # unrelated earlier code in the same process — always check live.
    if not is_timescale_active(connection, use_cache=False):
        logger.info(
            "TICKET-044/ADR-035: TimescaleDB not detected on connection '%s' "
            "(vendor=%s) -- analytics_event stays a plain table. This is a "
            "documented no-op, not a skipped step: the hypertable conversion "
            "is deferred until the timescaledb extension is installed for "
            "this environment (ops/release-checklist step, never automatic). "
            "See docs/adr/ADR-035-timescaledb-migration.md.",
            connection.alias, connection.vendor,
        )
        return

    logger.info(
        "TICKET-044/ADR-035: TimescaleDB extension detected on connection "
        "'%s' -- converting analytics_event to a hypertable "
        "(create_hypertable, if_not_exists=True, migrate_data=True).",
        connection.alias,
    )
    with connection.cursor() as cursor:
        cursor.execute(
            "SELECT create_hypertable('analytics_event', 'created_at', "
            "if_not_exists => TRUE, migrate_data => TRUE)"
        )


def noop_reverse(apps, schema_editor):
    """
    Reversal is intentionally a no-op.

    Converting a hypertable back to a plain table is not designed by
    ADR-035 (out of scope until the conversion is actually exercised in
    production), and the schema is byte-identical on both backends
    (ADR-004) -- there is nothing to roll back at the Django
    migration-state level either way.
    """


class Migration(migrations.Migration):

    dependencies = [
        ('analytics', '0003_add_beacon_dlq'),
    ]

    operations = [
        migrations.RunPython(create_hypertable_if_active, noop_reverse),
    ]
