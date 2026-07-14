"""
A/B page-version comparison report (TICKET-042, ADR-028 §6).

build_page_version_report() is the single place that combines:
- Behavioral counts (views, add-to-carts) from the analytics DB's
  AggregatedMetric rows (page_version_views / page_version_atc,
  dimension_key = f"{product_id}:{page_version_id}") — survives the 90-day
  raw-event purge.
- Order/revenue truth from the MAIN DB's OrderItem.page_version_id stamps
  (never from beacon events — ADR-004 §3b: purchase truth is server-side).

v1 boundary (ADR-028 §6 / P4, human-approved 2026-07-11): raw counts + a
plain conversion rate only. No statistical-significance testing — revisit
with AF-109 (order-bump split-test reporting) so A/B reporting lands once,
consistently.

Known limitation (documented, not fixed here): OrderItem.product_variant is
nullable (SET_NULL) — an order whose variant was later deleted from the
catalog cannot be attributed to this product by this report. Accepted as a
v1 edge case (ASSUMPTION, LOW); the primary attribution path (variant still
exists) is unaffected.
"""

from collections import defaultdict
from decimal import Decimal

from django.db.models import Sum


def build_page_version_report(store, product, days: int = 30) -> list[dict]:
    """
    Return one row per page-version-id (0 = primary + one per existing/deleted
    ProductPageVersion referenced by any stamp), each a dict with:
        page_version_id, label, views, add_to_carts, orders, units, revenue,
        conversion_rate.

    Rows for a page_version_id with no stamps/metrics at all are omitted,
    EXCEPT the primary (0) and every currently-existing ProductPageVersion for
    this product, which always appear (even at zero) so the report always
    shows the full current lineup.
    """
    from analytics.models import AggregatedMetric
    from catalog.models import ProductPageVersion
    from django.utils import timezone
    from datetime import timedelta
    from orders.models import OrderItem

    period_from = timezone.now().date() - timedelta(days=days)

    versions = list(
        ProductPageVersion.objects.for_store(store).filter(product=product).order_by("position", "pk")
    )
    version_by_id = {v.pk: v for v in versions}

    prefix = f"{product.pk}:"

    views_by_pv: dict[int, int] = {}
    for row in (
        AggregatedMetric.objects.filter(
            store_id=store.pk, metric_type="page_version_views",
            period_start__gte=period_from, dimension_key__startswith=prefix,
        ).values("dimension_key").annotate(total=Sum("value"))
    ):
        pv_id = int(row["dimension_key"].split(":", 1)[1])
        views_by_pv[pv_id] = int(row["total"] or 0)

    atc_by_pv: dict[int, int] = {}
    for row in (
        AggregatedMetric.objects.filter(
            store_id=store.pk, metric_type="page_version_atc",
            period_start__gte=period_from, dimension_key__startswith=prefix,
        ).values("dimension_key").annotate(total=Sum("value"))
    ):
        pv_id = int(row["dimension_key"].split(":", 1)[1])
        atc_by_pv[pv_id] = int(row["total"] or 0)

    # Order/revenue truth — main DB, OrderItem stamps (never beacon events).
    stats_by_pv: dict[int, dict] = defaultdict(
        lambda: {"orders": set(), "units": 0, "revenue": Decimal("0")}
    )
    order_item_rows = (
        OrderItem.objects.cross_store_unsafe()
        .filter(store=store, product_variant__product=product)
        .values("page_version_id", "order_id", "quantity", "line_total")
    )
    for row in order_item_rows:
        pv_id = row["page_version_id"] or 0
        bucket = stats_by_pv[pv_id]
        bucket["orders"].add(row["order_id"])
        bucket["units"] += row["quantity"]
        bucket["revenue"] += row["line_total"]

    all_pv_ids = {0} | set(version_by_id) | set(views_by_pv) | set(atc_by_pv) | set(stats_by_pv)

    rows = []
    for pv_id in sorted(all_pv_ids):
        if pv_id == 0:
            label = "Primary"
        elif pv_id in version_by_id:
            label = version_by_id[pv_id].name
        else:
            # Soft-ID convention (ADR-004): a deleted version's stamps remain
            # attributable without a live row to join to.
            label = f"(deleted #{pv_id})"

        views = views_by_pv.get(pv_id, 0)
        add_to_carts = atc_by_pv.get(pv_id, 0)
        stat = stats_by_pv.get(pv_id, {"orders": set(), "units": 0, "revenue": Decimal("0")})
        orders_count = len(stat["orders"])
        conversion_rate = (
            (Decimal(orders_count) / Decimal(views) * Decimal("100"))
            if views
            else Decimal("0")
        )

        rows.append({
            "page_version_id": pv_id,
            "label": label,
            "views": views,
            "add_to_carts": add_to_carts,
            "orders": orders_count,
            "units": stat["units"],
            "revenue": stat["revenue"],
            "conversion_rate": conversion_rate,
        })

    return rows
