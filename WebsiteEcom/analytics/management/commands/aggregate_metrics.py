"""
Management command: aggregate_metrics

Reads raw events from the analytics DB (via raw SQL on connections['analytics'])
and writes pre-aggregated AggregatedMetric rows (via Django ORM).

Idempotent: existing rows for (store_id, metric_type, period_start) are
deleted before fresh rows are inserted.  Running twice for the same day
produces identical results.

Daily metric types (one period_start row per store per day, unless noted):

  Scalar (dimension_key=''):
    sessions           — distinct session_ids per store per day
    pageviews          — total page_view events per store per day
    unique_visitors    — distinct session_ids with a page_view per store per day
    add_to_carts       — total add_to_cart events per store per day
    checkouts          — total begin_checkout / checkout_start events per store per day
    purchases          — total purchase events per store per day
    revenue            — sum of properties['total'] for purchase events
    conversion_rate    — purchases / sessions * 100 per store per day
    avg_order_value    — revenue / purchases (0 if no purchases)
    scroll_depth       — avg of properties['scroll_pct'] for scroll events (0 if none)

  Dimension (one row per store/day/dimension_key, top-20 per store):
    top_utm_source          — distinct session count per utm_source
    top_referrer            — distinct session count per referrer
    top_landing_page        — page_view count per landing_page
    purchases_by_hour       — purchase count per customer_local_hour (0-23)
    collection_impressions  — collection_impression event count per collection_id
    collection_scroll_depth_avg — avg scroll_pct for collection_scroll_depth per collection_id
    top_country_revenue     — sum of purchase event value per country code

Monthly metric types (period_start = first day of month, dimension_key=''):
    monthly_revenue    — sum of daily revenue for the calendar month
    monthly_purchases  — sum of daily purchase counts for the calendar month

Usage:
    python3 manage.py aggregate_metrics
    python3 manage.py aggregate_metrics --date 2026-07-01
    python3 manage.py aggregate_metrics --days 7

TICKET-044 / ADR-035 note: this command is untouched by the TimescaleDB
readiness guards -- it already reads raw events via SQL and writes
AggregatedMetric through the ORM, so it works identically whether
analytics_event is a plain table or a hypertable. Replacing this nightly
cron with a TimescaleDB continuous aggregate (an incrementally-maintained
materialized view) is a documented FUTURE path, explicitly out of scope for
v1 (ADR-035 point 4) -- not designed or scheduled here. See
docs/adr/ADR-035-timescaledb-migration.md.
"""

import logging
from datetime import date, datetime, timedelta, timezone as tz
from decimal import Decimal, InvalidOperation

from django.conf import settings as django_settings
from django.core.management.base import BaseCommand
from django.db import connections
from django.db.models import Sum
from django.utils import timezone

from analytics.models import AggregatedMetric

logger = logging.getLogger('analytics.aggregate')

SCALAR_METRIC_TYPES = [
    'sessions',
    'pageviews',
    'unique_visitors',
    'add_to_carts',
    'checkouts',
    'purchases',
    'revenue',
    'conversion_rate',
    'avg_order_value',
    'scroll_depth',
]
DIMENSION_METRIC_TYPES = [
    'top_utm_source',
    'top_referrer',
    'top_landing_page',
    'purchases_by_hour',
    'collection_impressions',
    'collection_scroll_depth_avg',
    'top_country_revenue',
    # TICKET-042 / ADR-028 §6: raw per-page-version counts, dimension_key =
    # f"{product_id}:{page_version_id}" (":0" = the primary page).
    #
    # W3-3 (WAVE3_AUDIT.md): this metric was ORIGINALLY not top-N-capped like
    # the other dimension metrics below, on the assumption it was bounded by
    # the 10-version-per-product cap (ADR-028 P1). That assumption was wrong:
    # both product_id and page_version_id are unvalidated public beacon input
    # (only shape-validated as plain integers by W3-2's ingest coercion, never
    # checked against the store's actual catalog) — a single client can send
    # 50 events/request with fabricated distinct (product_id, page_version_id)
    # pairs and mint unlimited daily AggregatedMetric rows in the main DB. Now
    # capped at PAGE_VERSION_TOP_N per store/day, same _top_n() mechanism as
    # every other dimension metric.
    'page_version_views',
    'page_version_atc',
]
MONTHLY_METRIC_TYPES = ['monthly_revenue', 'monthly_purchases']

# ALL_METRIC_TYPES covers all daily granularity types used in the idempotency delete.
# Monthly types are managed separately by _run_month_rollup.
ALL_METRIC_TYPES = SCALAR_METRIC_TYPES + DIMENSION_METRIC_TYPES

# Maximum dimension rows kept per (store, day, metric_type).
TOP_N = 20

# W3-3 (WAVE3_AUDIT.md): separate, larger cap for page_version_views/atc.
# Legitimate cardinality here (products x up to 10 versions each, ADR-028 P1)
# is naturally larger than a UTM-source or referrer list, so reusing TOP_N=20
# would clip real data for any catalog with more than two versioned products.
# Configurable via settings for stores with unusually large catalogs.
PAGE_VERSION_TOP_N = getattr(django_settings, 'ANALYTICS_PAGE_VERSION_TOP_N', 500)


def _ph(conn) -> str:
    """Return the SQL parameter placeholder for the connection's vendor."""
    return '%s' if conn.vendor == 'postgresql' else '?'


def _json_num_expr(vendor: str, col: str, field: str) -> str:
    """
    SQL expression that extracts a numeric value from a JSON column.

    Returns NULL when the key is absent; callers wrap with COALESCE as needed.

    PostgreSQL: the JSONField stores as JSONB; use the ->> text extraction
                operator and cast the result to DECIMAL.
    SQLite:     the JSONField stores as TEXT; use json_extract() and cast to REAL.
                (json_extract is available since SQLite 3.9, released 2015.)
    """
    if vendor == 'postgresql':
        return f"CAST(({col}->>'{field}') AS DECIMAL)"
    return f"CAST(json_extract({col}, '$.{field}') AS REAL)"


def _json_int_expr(vendor: str, col: str, field: str) -> str:
    """
    SQL expression that extracts an integer value from a JSON column, wrapped
    in COALESCE(..., 0) by callers so a missing key maps to page_version_id=0
    (the primary page, ADR-028 §6 ":0" convention).

    W3-2 (WAVE3_AUDIT.md): `properties` is public beacon input. Ingest-time
    coercion (analytics/ingest.py::_coerce_int_or_none) now drops non-integer
    values before they are ever stored, but this expression must independently
    be non-throwing (defense in depth for pre-fix rows still in the retention
    window, and for any future ingest bug): a bare `CAST(x AS INTEGER)` raises
    "invalid input syntax for type integer" on PostgreSQL for any non-numeric
    string, `true`/`false`, or a fractional value — one such row previously
    killed `_run_day` for every store, repeatably, for the whole 90-day
    retention of the row. Guard the cast with a regex that only matches a
    plain 1-9-digit unsigned integer string (page_version_id is never
    negative) and short-circuit to NULL otherwise — a value this shape always
    fits a 32-bit INTEGER, so the CAST itself can never overflow either.

    SQLite's CAST(... AS INTEGER) is lenient (non-numeric text casts to 0)
    and was never the throwing side of this bug — see the ingest tests for
    why SQLite green was not sufficient evidence.
    """
    if vendor == 'postgresql':
        return (
            f"CASE WHEN ({col}->>'{field}') ~ '^[0-9]{{1,9}}$' "
            f"THEN CAST(({col}->>'{field}') AS INTEGER) ELSE NULL END"
        )
    return f"CAST(json_extract({col}, '$.{field}') AS INTEGER)"


def _safe_dec(value, default: str = '0') -> Decimal:
    """Convert a raw DB value (int, float, str, or None) to Decimal."""
    if value is None:
        return Decimal(default)
    try:
        return Decimal(str(value))
    except InvalidOperation:
        return Decimal(default)


def _top_n(rows, n: int = TOP_N) -> dict:
    """
    Group dimension rows by store_id, keeping the top-N per store.

    Expects rows already ordered by (store_id, count DESC).
    Returns {store_id: [(dimension_key, count), ...]}.
    """
    result: dict[int, list] = {}
    tallies: dict[int, int] = {}
    for store_id, dim_key, cnt in rows:
        bucket = result.setdefault(store_id, [])
        if tallies.get(store_id, 0) < n:
            bucket.append((dim_key or '', cnt))
            tallies[store_id] = tallies.get(store_id, 0) + 1
    return result


class Command(BaseCommand):
    help = 'Aggregate raw analytics events into AggregatedMetric rows (idempotent).'

    def add_arguments(self, parser):
        parser.add_argument(
            '--date',
            type=str,
            metavar='YYYY-MM-DD',
            help='Aggregate for this date only (YYYY-MM-DD). Default: yesterday.',
        )
        parser.add_argument(
            '--days',
            type=int,
            default=1,
            help=(
                'Aggregate the last N days counting back from yesterday. '
                'Ignored when --date is given. Default: 1.'
            ),
        )

    def handle(self, *args, **options):
        if options['date']:
            try:
                target = date.fromisoformat(options['date'])
            except ValueError as exc:
                self.stderr.write(self.style.ERROR(f'Invalid --date value: {exc}'))
                return
            days_to_run = [target]
        else:
            yesterday = timezone.now().date() - timedelta(days=1)
            n = max(1, options['days'])
            days_to_run = [yesterday - timedelta(days=i) for i in range(n)]

        self.stdout.write(
            f'aggregate_metrics: {len(days_to_run)} day(s) to process.'
        )
        failed_days = []
        for target_date in days_to_run:
            # W3-2 (WAVE3_AUDIT.md): per-day containment. _json_int_expr is now
            # non-throwing on PostgreSQL, but this try/except is deliberate
            # defense in depth — a single day's aggregation must never be able
            # to abort the whole --days N run (or a future, still-unknown
            # poisoning vector must never take down every other day/store).
            try:
                self._run_day(target_date)
            except Exception:
                failed_days.append(target_date)
                logger.warning(
                    'aggregate_metrics: _run_day failed for %s — skipping this day, '
                    'continuing with the remaining days.',
                    target_date,
                    exc_info=True,
                )
                self.stderr.write(
                    self.style.WARNING(f'  {target_date}: FAILED — see log for traceback.')
                )

        if failed_days:
            self.stdout.write(
                self.style.WARNING(
                    f'aggregate_metrics: done — {len(days_to_run)} day(s) attempted, '
                    f'{len(failed_days)} failed: {failed_days}.'
                )
            )
        else:
            self.stdout.write(
                self.style.SUCCESS(
                    f'aggregate_metrics: done — {len(days_to_run)} day(s) processed.'
                )
            )

    # ------------------------------------------------------------------
    # Per-day aggregation
    # ------------------------------------------------------------------

    def _run_day(self, target_date: date) -> None:
        """Aggregate all metric types for a single calendar day (UTC boundaries)."""
        conn = connections['analytics']
        ph = _ph(conn)
        vendor = conn.vendor

        period_start = target_date
        period_end = target_date + timedelta(days=1)

        # UTC midnight boundaries as ISO strings for raw SQL comparisons.
        # Using ISO format ensures correct lexicographic ordering in SQLite and
        # proper timestamptz parsing in PostgreSQL.
        start_str = datetime(
            target_date.year, target_date.month, target_date.day,
            tzinfo=tz.utc,
        ).isoformat()
        end_str = datetime(
            period_end.year, period_end.month, period_end.day,
            tzinfo=tz.utc,
        ).isoformat()

        (scalar_rows, utm_rows, ref_rows, lp_rows,
         hour_rows, coll_imp_rows, coll_scroll_rows, country_rev_rows) = (
            self._query_analytics(conn, ph, vendor, start_str, end_str)
        )
        pv_view_rows, pv_atc_rows = self._query_page_version_metrics(
            conn, ph, vendor, start_str, end_str,
        )

        # Union of all store IDs that had any event today.
        store_ids = (
            {r[0] for r in scalar_rows}
            | {r[0] for r in utm_rows}
            | {r[0] for r in ref_rows}
            | {r[0] for r in lp_rows}
            | {r[0] for r in hour_rows}
            | {r[0] for r in coll_imp_rows}
            | {r[0] for r in coll_scroll_rows}
            | {r[0] for r in country_rev_rows}
            | {r[0] for r in pv_view_rows}
            | {r[0] for r in pv_atc_rows}
        )

        if not store_ids:
            self.stdout.write(f'  {target_date}: no events — skipping.')
            return

        utm_by_store = _top_n(utm_rows)
        ref_by_store = _top_n(ref_rows)
        lp_by_store = _top_n(lp_rows)
        hour_by_store = _top_n(hour_rows)
        coll_imp_by_store = _top_n(coll_imp_rows)
        coll_scroll_by_store = _top_n(coll_scroll_rows)
        country_rev_by_store = _top_n(country_rev_rows)
        scalar_by_store = {r[0]: r for r in scalar_rows}
        # W3-3 (WAVE3_AUDIT.md): page_version_views/atc are now top-N capped
        # (PAGE_VERSION_TOP_N, see DIMENSION_METRIC_TYPES comment) — neither
        # product_id nor page_version_id is validated against the store's
        # real catalog, so an uncapped group-by would let unlimited distinct
        # dimension_key rows accumulate in the main DB.
        pv_views_by_store = _top_n(pv_view_rows, n=PAGE_VERSION_TOP_N)
        pv_atc_by_store = _top_n(pv_atc_rows, n=PAGE_VERSION_TOP_N)

        # Idempotency: remove stale daily rows before re-inserting.
        deleted_count, _ = AggregatedMetric.objects.filter(
            store_id__in=list(store_ids),
            metric_type__in=ALL_METRIC_TYPES,
            period_start=period_start,
        ).delete()

        new_rows = self._build_metric_rows(
            store_ids, scalar_by_store,
            utm_by_store, ref_by_store, lp_by_store,
            hour_by_store, coll_imp_by_store, coll_scroll_by_store,
            country_rev_by_store,
            pv_views_by_store, pv_atc_by_store,
            period_start, period_end,
        )

        AggregatedMetric.objects.bulk_create(new_rows)

        self.stdout.write(
            f'  {target_date}: wrote {len(new_rows)} row(s) '
            f'for {len(store_ids)} store(s) '
            f'(deleted {deleted_count} stale).'
        )

        # Update the monthly rollup for this month now that daily rows are written.
        self._run_month_rollup(target_date, store_ids)

    # ------------------------------------------------------------------
    # Monthly rollup — reads from already-written daily AggregatedMetric rows
    # ------------------------------------------------------------------

    def _run_month_rollup(self, target_date: date, store_ids: set) -> None:
        """
        Compute month-level rollups by summing the daily revenue and purchase
        rows already written for this calendar month.

        Called from _run_day after bulk_create completes so all daily rows for
        target_date are in place.

        period_start for monthly rows = first day of the calendar month.
        period_end = first day of the next calendar month.
        """
        month_start = target_date.replace(day=1)
        # First day of next month (handles Dec → Jan year rollover).
        if target_date.month == 12:
            month_end = date(target_date.year + 1, 1, 1)
        else:
            month_end = date(target_date.year, target_date.month + 1, 1)

        # Idempotency: delete existing monthly rows for these stores/month.
        AggregatedMetric.objects.filter(
            store_id__in=list(store_ids),
            metric_type__in=MONTHLY_METRIC_TYPES,
            period_start=month_start,
        ).delete()

        new_monthly_rows = []
        for store_id in store_ids:
            daily_qs = AggregatedMetric.objects.filter(
                store_id=store_id,
                period_start__gte=month_start,
                period_start__lte=target_date,
                dimension_key='',
            )
            monthly_revenue = (
                daily_qs.filter(metric_type='revenue')
                .aggregate(total=Sum('value'))['total']
                or Decimal('0')
            )
            monthly_purchases = (
                daily_qs.filter(metric_type='purchases')
                .aggregate(total=Sum('value'))['total']
                or Decimal('0')
            )
            new_monthly_rows.extend([
                AggregatedMetric(
                    metric_type='monthly_revenue',
                    store_id=store_id,
                    period_start=month_start,
                    period_end=month_end,
                    dimension_key='',
                    value=monthly_revenue,
                ),
                AggregatedMetric(
                    metric_type='monthly_purchases',
                    store_id=store_id,
                    period_start=month_start,
                    period_end=month_end,
                    dimension_key='',
                    value=monthly_purchases,
                ),
            ])

        AggregatedMetric.objects.bulk_create(new_monthly_rows)

    # ------------------------------------------------------------------
    # Raw SQL queries against the analytics DB
    # ------------------------------------------------------------------

    def _query_analytics(self, conn, ph, vendor, start_str, end_str):
        """
        Run queries against the analytics DB for the given time window.

        Returns a tuple of eight row-lists:
        (scalar_rows, utm_rows, ref_rows, lp_rows,
         hour_rows, coll_imp_rows, coll_scroll_rows, country_rev_rows)
        """
        total_expr = _json_num_expr(vendor, 'properties', 'total')
        scroll_expr = _json_num_expr(vendor, 'properties', 'scroll_pct')
        coll_scroll_expr = _json_num_expr(vendor, 'properties', 'scroll_pct')

        # Single-pass query for all scalar metrics.
        # add_to_cart and begin_checkout/checkout_start are kept for backward compat
        # with historical data (not in the current EventType enum but may exist in DB).
        scalar_sql = f"""
            SELECT
                store_id,
                COUNT(DISTINCT session_id)                                          AS sessions,
                SUM(CASE WHEN event_type = 'page_view' THEN 1 ELSE 0 END)          AS pageviews,
                COUNT(DISTINCT CASE WHEN event_type = 'page_view'
                      THEN session_id ELSE NULL END)                                AS unique_visitors,
                SUM(CASE WHEN event_type = 'add_to_cart' THEN 1 ELSE 0 END)        AS add_to_carts,
                SUM(CASE WHEN event_type IN ('begin_checkout', 'checkout_start')
                         THEN 1 ELSE 0 END)                                        AS checkouts,
                SUM(CASE WHEN event_type = 'purchase' THEN 1 ELSE 0 END)           AS purchases,
                SUM(CASE WHEN event_type = 'purchase'
                    THEN COALESCE({total_expr}, 0) ELSE 0 END)                     AS revenue,
                COALESCE(AVG(CASE WHEN event_type IN ('scroll', 'collection_scroll_depth')
                    THEN {scroll_expr} ELSE NULL END), 0)                          AS scroll_depth
            FROM analytics_event
            WHERE created_at >= {ph} AND created_at < {ph}
            GROUP BY store_id
        """

        # Distinct sessions per (store, utm_source).
        utm_sql = f"""
            SELECT store_id, utm_source, COUNT(DISTINCT session_id) AS cnt
            FROM analytics_event
            WHERE created_at >= {ph} AND created_at < {ph}
            GROUP BY store_id, utm_source
            ORDER BY store_id, cnt DESC
        """

        # Distinct sessions per (store, referrer).
        ref_sql = f"""
            SELECT store_id, referrer, COUNT(DISTINCT session_id) AS cnt
            FROM analytics_event
            WHERE created_at >= {ph} AND created_at < {ph}
            GROUP BY store_id, referrer
            ORDER BY store_id, cnt DESC
        """

        # Page views per (store, landing_page).
        lp_sql = f"""
            SELECT store_id, landing_page, COUNT(*) AS cnt
            FROM analytics_event
            WHERE event_type = 'page_view'
              AND created_at >= {ph} AND created_at < {ph}
            GROUP BY store_id, landing_page
            ORDER BY store_id, cnt DESC
        """

        # Purchase count per (store, customer_local_hour).
        hour_sql = f"""
            SELECT store_id, CAST(customer_local_hour AS TEXT), COUNT(*) AS cnt
            FROM analytics_event
            WHERE event_type = 'purchase'
              AND customer_local_hour IS NOT NULL
              AND created_at >= {ph} AND created_at < {ph}
            GROUP BY store_id, customer_local_hour
            ORDER BY store_id, cnt DESC
        """

        # Collection impression count per (store, collection_id).
        coll_imp_sql = f"""
            SELECT store_id, CAST(collection_id AS TEXT), COUNT(*) AS cnt
            FROM analytics_event
            WHERE event_type = 'collection_impression'
              AND collection_id IS NOT NULL
              AND created_at >= {ph} AND created_at < {ph}
            GROUP BY store_id, collection_id
            ORDER BY store_id, cnt DESC
        """

        # Average scroll depth per (store, collection_id) for collection_scroll_depth events.
        coll_scroll_sql = f"""
            SELECT store_id, CAST(collection_id AS TEXT),
                   COALESCE(AVG({coll_scroll_expr}), 0) AS avg_depth
            FROM analytics_event
            WHERE event_type = 'collection_scroll_depth'
              AND collection_id IS NOT NULL
              AND created_at >= {ph} AND created_at < {ph}
            GROUP BY store_id, collection_id
            ORDER BY store_id, avg_depth DESC
        """

        # Purchase revenue per (store, country) using the denormalised value column.
        country_rev_sql = f"""
            SELECT store_id, country, SUM(COALESCE(value, 0)) AS revenue_sum
            FROM analytics_event
            WHERE event_type = 'purchase'
              AND country != ''
              AND created_at >= {ph} AND created_at < {ph}
            GROUP BY store_id, country
            ORDER BY store_id, revenue_sum DESC
        """

        with conn.cursor() as cursor:
            cursor.execute(scalar_sql, [start_str, end_str])
            scalar_rows = cursor.fetchall()

            cursor.execute(utm_sql, [start_str, end_str])
            utm_rows = cursor.fetchall()

            cursor.execute(ref_sql, [start_str, end_str])
            ref_rows = cursor.fetchall()

            cursor.execute(lp_sql, [start_str, end_str])
            lp_rows = cursor.fetchall()

            cursor.execute(hour_sql, [start_str, end_str])
            hour_rows = cursor.fetchall()

            cursor.execute(coll_imp_sql, [start_str, end_str])
            coll_imp_rows = cursor.fetchall()

            cursor.execute(coll_scroll_sql, [start_str, end_str])
            coll_scroll_rows = cursor.fetchall()

            cursor.execute(country_rev_sql, [start_str, end_str])
            country_rev_rows = cursor.fetchall()

        return (
            scalar_rows, utm_rows, ref_rows, lp_rows,
            hour_rows, coll_imp_rows, coll_scroll_rows, country_rev_rows,
        )

    # ------------------------------------------------------------------
    # Page-version metrics (TICKET-042 / ADR-028 §6)
    # ------------------------------------------------------------------

    def _query_page_version_metrics(self, conn, ph, vendor, start_str, end_str):
        """
        Return (pv_view_rows, pv_atc_rows) — each a list of
        (store_id, dimension_key, count) rows, dimension_key = f"{product_id}:{pv_id}"
        (":0" = the primary page's own baseline, ADR-028 §6).

        Only events carrying a non-NULL top-level product_id are counted (i.e.
        product-page page_view / add_to_cart events — see storefront_tags.py
        analytics_beacon). page_version_id defaults to 0 via COALESCE when the
        properties key is absent (the primary page).

        W3-3 (WAVE3_AUDIT.md): ORDER BY store_id, cnt DESC so the caller can
        apply the same _top_n() cardinality bound used by every other
        dimension metric — neither product_id nor page_version_id is
        validated against the store's actual catalog, so without this cap a
        client could otherwise mint unlimited distinct dimension_key rows.
        """
        pv_expr = _json_int_expr(vendor, 'properties', 'page_version_id')

        pv_views_sql = f"""
            SELECT store_id, product_id, COALESCE({pv_expr}, 0) AS pv_id, COUNT(*) AS cnt
            FROM analytics_event
            WHERE event_type = 'page_view' AND product_id IS NOT NULL
              AND created_at >= {ph} AND created_at < {ph}
            GROUP BY store_id, product_id, COALESCE({pv_expr}, 0)
            ORDER BY store_id, cnt DESC
        """
        pv_atc_sql = f"""
            SELECT store_id, product_id, COALESCE({pv_expr}, 0) AS pv_id, COUNT(*) AS cnt
            FROM analytics_event
            WHERE event_type = 'add_to_cart' AND product_id IS NOT NULL
              AND created_at >= {ph} AND created_at < {ph}
            GROUP BY store_id, product_id, COALESCE({pv_expr}, 0)
            ORDER BY store_id, cnt DESC
        """

        with conn.cursor() as cursor:
            cursor.execute(pv_views_sql, [start_str, end_str])
            raw_views = cursor.fetchall()
            cursor.execute(pv_atc_sql, [start_str, end_str])
            raw_atc = cursor.fetchall()

        pv_view_rows = [
            (store_id, f"{product_id}:{pv_id}", cnt)
            for store_id, product_id, pv_id, cnt in raw_views
        ]
        pv_atc_rows = [
            (store_id, f"{product_id}:{pv_id}", cnt)
            for store_id, product_id, pv_id, cnt in raw_atc
        ]
        return pv_view_rows, pv_atc_rows

    # ------------------------------------------------------------------
    # Build AggregatedMetric instances from query results
    # ------------------------------------------------------------------

    def _build_metric_rows(
        self,
        store_ids,
        scalar_by_store,
        utm_by_store,
        ref_by_store,
        lp_by_store,
        hour_by_store,
        coll_imp_by_store,
        coll_scroll_by_store,
        country_rev_by_store,
        pv_views_by_store,
        pv_atc_by_store,
        period_start,
        period_end,
    ):
        """Return a list of unsaved AggregatedMetric instances for all stores."""
        new_rows = []

        for store_id in store_ids:
            row = scalar_by_store.get(store_id)
            if row:
                (_, sessions_raw, pv_raw, uv_raw, cart_raw,
                 co_raw, pur_raw, rev_raw, scroll_raw) = row
            else:
                sessions_raw = pv_raw = uv_raw = cart_raw = 0
                co_raw = pur_raw = rev_raw = scroll_raw = 0

            sessions = _safe_dec(sessions_raw)
            pageviews = _safe_dec(pv_raw)
            unique_visitors = _safe_dec(uv_raw)
            add_to_carts = _safe_dec(cart_raw)
            checkouts = _safe_dec(co_raw)
            purchases = _safe_dec(pur_raw)
            revenue = _safe_dec(rev_raw)
            scroll_depth = _safe_dec(scroll_raw)

            conversion_rate = (
                (purchases / sessions * Decimal('100'))
                if sessions > 0
                else Decimal('0')
            )
            avg_order_value = (
                revenue / purchases
                if purchases > 0
                else Decimal('0')
            )

            # Scalar metrics — all share dimension_key=''.
            scalars = [
                ('sessions', sessions),
                ('pageviews', pageviews),
                ('unique_visitors', unique_visitors),
                ('add_to_carts', add_to_carts),
                ('checkouts', checkouts),
                ('purchases', purchases),
                ('revenue', revenue),
                ('conversion_rate', conversion_rate),
                ('avg_order_value', avg_order_value),
                ('scroll_depth', scroll_depth),
            ]
            new_rows.extend(
                AggregatedMetric(
                    metric_type=mtype,
                    store_id=store_id,
                    period_start=period_start,
                    period_end=period_end,
                    dimension_key='',
                    value=val,
                )
                for mtype, val in scalars
            )

            # Dimension metrics — one row per dimension_key.
            for dim_key, cnt in utm_by_store.get(store_id, []):
                new_rows.append(AggregatedMetric(
                    metric_type='top_utm_source',
                    store_id=store_id,
                    period_start=period_start,
                    period_end=period_end,
                    dimension_key=dim_key,
                    value=_safe_dec(cnt),
                ))
            for dim_key, cnt in ref_by_store.get(store_id, []):
                new_rows.append(AggregatedMetric(
                    metric_type='top_referrer',
                    store_id=store_id,
                    period_start=period_start,
                    period_end=period_end,
                    dimension_key=dim_key,
                    value=_safe_dec(cnt),
                ))
            for dim_key, cnt in lp_by_store.get(store_id, []):
                new_rows.append(AggregatedMetric(
                    metric_type='top_landing_page',
                    store_id=store_id,
                    period_start=period_start,
                    period_end=period_end,
                    dimension_key=dim_key,
                    value=_safe_dec(cnt),
                ))
            for dim_key, cnt in hour_by_store.get(store_id, []):
                new_rows.append(AggregatedMetric(
                    metric_type='purchases_by_hour',
                    store_id=store_id,
                    period_start=period_start,
                    period_end=period_end,
                    dimension_key=dim_key,
                    value=_safe_dec(cnt),
                ))
            for dim_key, cnt in coll_imp_by_store.get(store_id, []):
                new_rows.append(AggregatedMetric(
                    metric_type='collection_impressions',
                    store_id=store_id,
                    period_start=period_start,
                    period_end=period_end,
                    dimension_key=dim_key,
                    value=_safe_dec(cnt),
                ))
            for dim_key, avg_depth in coll_scroll_by_store.get(store_id, []):
                new_rows.append(AggregatedMetric(
                    metric_type='collection_scroll_depth_avg',
                    store_id=store_id,
                    period_start=period_start,
                    period_end=period_end,
                    dimension_key=dim_key,
                    value=_safe_dec(avg_depth),
                ))
            for dim_key, rev_sum in country_rev_by_store.get(store_id, []):
                new_rows.append(AggregatedMetric(
                    metric_type='top_country_revenue',
                    store_id=store_id,
                    period_start=period_start,
                    period_end=period_end,
                    dimension_key=dim_key,
                    value=_safe_dec(rev_sum),
                ))
            # TICKET-042 / ADR-028 §6: dimension_key = "product_id:page_version_id"
            # (":0" = primary). Not top-N capped (see DIMENSION_METRIC_TYPES comment).
            for dim_key, cnt in pv_views_by_store.get(store_id, []):
                new_rows.append(AggregatedMetric(
                    metric_type='page_version_views',
                    store_id=store_id,
                    period_start=period_start,
                    period_end=period_end,
                    dimension_key=dim_key,
                    value=_safe_dec(cnt),
                ))
            for dim_key, cnt in pv_atc_by_store.get(store_id, []):
                new_rows.append(AggregatedMetric(
                    metric_type='page_version_atc',
                    store_id=store_id,
                    period_start=period_start,
                    period_end=period_end,
                    dimension_key=dim_key,
                    value=_safe_dec(cnt),
                ))

        return new_rows
