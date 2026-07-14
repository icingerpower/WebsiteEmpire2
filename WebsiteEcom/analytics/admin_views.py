"""
Analytics dashboard view — store-admin surface (TICKET-013).

Access rules:
  - Login required (login_required decorator).
  - Super-admins always pass.
  - Store admins must have an active StoreEmployee row for request.store with
    'analytics' permission at 'limited' or 'full' level (or full_access=True).
  - Any other user receives HTTP 403.

Store selection:
  - Normal path: request.store set by HostResolutionMiddleware.
  - Super-admin preview fallback: first active (non-deleted) store.

Period selection:
  - GET ?days=7|30|90|365 (default: ANALYTICS_REPORT_DAYS setting, fallback 30).
  - Clamped to [7, 365].

Data:
  - Reads AggregatedMetric rows for the past report_days days.
  - Scalar summaries: sessions, pageviews, purchases, revenue, conversion rate.
  - Dimension top-5: utm_sources, referrers, landing pages (aggregated over period).
  - Daily series: list of {date, sessions, purchases, revenue} dicts for charting.
  - Monthly series: monthly_revenue and monthly_purchases rows.
  - Hourly purchases: list of {hour, count} for all 24 hours.
  - Top countries: by revenue, top 10.

Revenue labeling:
  - revenue_label = 'Gross revenue' (Phase 1: no refund netting; label makes scope explicit).

Store isolation: all queries filter by store_id = store.pk.
No raw events are read here — only pre-aggregated AggregatedMetric rows.
"""

import json
from datetime import timedelta
from decimal import Decimal

from django.conf import settings
from django.contrib.auth.decorators import login_required
from django.db.models import Sum
from django.http import HttpResponseForbidden
from django.template.response import TemplateResponse
from django.utils import timezone
from django.utils.decorators import method_decorator
from django.views import View

from analytics.models import AggregatedMetric

AVAILABLE_PERIODS = [7, 30, 90, 365]


def _has_analytics_access(user, store) -> bool:
    """
    Return True if the user is allowed to view analytics for the given store.

    Rules (checked in order):
    1. Super-admins always pass (store may be None during super-admin preview).
    2. If store is None, deny — a store admin must have a concrete store.
    3. The user must have an active StoreEmployee row for (user, store).
    4. That row must grant 'analytics' access at 'limited' or 'full' level,
       OR have full_access=True (master override).

    This is the per-store check that prevents cross-tenant data leaks: a user
    who is is_store_admin=True for store A cannot access store B's analytics
    simply by hitting store B's admin host, because step 3 will find no row.

    NOTE (ADR-033 D3c / T054): "analytics" is a VIEW_ONLY_MODULE and this
    view is one of the call sites ADR-033 D3a names for migration onto
    stores.permissions.check_module_access. Left AS-IS in this pass —
    analytics/ has a parallel in-flight change (T044) and was explicitly
    out of scope for this ticket's admin sweep; migrating this function to
    the shared resolver is a follow-up (the semantics above are already
    identical to check_module_access's, so the swap is a pure refactor).
    """
    if user.is_super_admin:
        return True
    if store is None:
        return False
    from stores.models import StoreEmployee
    try:
        employee = StoreEmployee.objects.get(user=user, store=store, is_active=True)
    except StoreEmployee.DoesNotExist:
        return False
    if employee.full_access:
        return True
    level = employee.permissions_json.get('analytics', 'none')
    return level in ('limited', 'full')


@method_decorator(login_required, name='dispatch')
class AnalyticsDashboardView(View):
    """Analytics dashboard for store admins and super-admins."""

    template_name = 'analytics/dashboard.html'

    def get(self, request, *args, **kwargs):
        user = request.user
        store = getattr(request, 'store', None)

        if not _has_analytics_access(user, store):
            return HttpResponseForbidden('Analytics access requires store membership.')

        # Super-admin fallback: preview the first active store.
        if store is None and user.is_super_admin:
            from stores.models import Store
            store = (
                Store.objects.active()
                .order_by('pk')
                .first()
            )

        if store is None:
            return HttpResponseForbidden('No accessible store found.')

        # Period selector — clamp to [7, 365].
        default_days = getattr(settings, 'ANALYTICS_REPORT_DAYS', 30)
        try:
            report_days = int(request.GET.get('days', default_days))
        except (TypeError, ValueError):
            report_days = default_days
        report_days = max(7, min(365, report_days))

        today = timezone.now().date()
        report_from = today - timedelta(days=report_days)

        context = self._build_context(store, report_from, report_days)
        context['store'] = store
        context['report_days'] = report_days
        context['available_periods'] = AVAILABLE_PERIODS
        return TemplateResponse(request, self.template_name, context)

    # ------------------------------------------------------------------
    # Context construction
    # ------------------------------------------------------------------

    def _build_context(self, store, report_from, report_days=30):
        """Query AggregatedMetric and build the template context."""
        store_id = store.pk

        # ---- Scalar metrics for the period --------------------------------
        # One ORM query for all scalar metric types with dimension_key=''.
        scalar_qs = (
            AggregatedMetric.objects
            .filter(
                store_id=store_id,
                metric_type__in=[
                    'sessions', 'pageviews', 'purchases',
                    'revenue', 'conversion_rate',
                ],
                period_start__gte=report_from,
                dimension_key='',
            )
            .values('metric_type', 'value', 'period_start')
            .order_by('period_start')
        )

        # Accumulate in Python — avoids one GROUP BY per metric type.
        sessions_list: list[Decimal] = []
        pageviews_list: list[Decimal] = []
        purchases_list: list[Decimal] = []
        revenue_list: list[Decimal] = []
        conversion_list: list[Decimal] = []

        # daily_map: date_str → {date, sessions, purchases, revenue (float)}
        daily_map: dict[str, dict] = {}

        for row in scalar_qs:
            mtype = row['metric_type']
            val = row['value']
            date_str = row['period_start'].isoformat()

            entry = daily_map.setdefault(
                date_str,
                {'date': date_str, 'sessions': 0, 'purchases': 0, 'revenue': 0.0},
            )

            if mtype == 'sessions':
                sessions_list.append(val)
                entry['sessions'] = int(val)
            elif mtype == 'pageviews':
                pageviews_list.append(val)
            elif mtype == 'purchases':
                purchases_list.append(val)
                entry['purchases'] = int(val)
            elif mtype == 'revenue':
                revenue_list.append(val)
                entry['revenue'] = float(val)
            elif mtype == 'conversion_rate':
                conversion_list.append(val)

        total_sessions = sum(sessions_list, Decimal('0'))
        total_pageviews = sum(pageviews_list, Decimal('0'))
        total_purchases = sum(purchases_list, Decimal('0'))
        total_revenue = sum(revenue_list, Decimal('0'))
        avg_conversion_rate = (
            sum(conversion_list, Decimal('0')) / len(conversion_list)
            if conversion_list
            else Decimal('0')
        )

        # ---- Dimension top-5 (aggregated over the full period) ------------
        # Sum counts per dimension_key across all days, then take top 5.
        top_utm_sources = list(
            AggregatedMetric.objects
            .filter(
                store_id=store_id,
                metric_type='top_utm_source',
                period_start__gte=report_from,
            )
            .values('dimension_key')
            .annotate(value=Sum('value'))
            .order_by('-value')[:5]
        )
        top_referrers = list(
            AggregatedMetric.objects
            .filter(
                store_id=store_id,
                metric_type='top_referrer',
                period_start__gte=report_from,
            )
            .values('dimension_key')
            .annotate(value=Sum('value'))
            .order_by('-value')[:5]
        )
        top_landing_pages = list(
            AggregatedMetric.objects
            .filter(
                store_id=store_id,
                metric_type='top_landing_page',
                period_start__gte=report_from,
            )
            .values('dimension_key')
            .annotate(value=Sum('value'))
            .order_by('-value')[:5]
        )

        # ---- Daily series for chart rendering -----------------------------
        daily_series = sorted(daily_map.values(), key=lambda x: x['date'])

        # ---- Monthly rollups ----------------------------------------------
        monthly_data = list(
            AggregatedMetric.objects
            .filter(
                store_id=store_id,
                metric_type__in=['monthly_revenue', 'monthly_purchases'],
                period_start__gte=report_from,
            )
            .values('metric_type', 'value', 'period_start')
            .order_by('period_start')
        )

        # ---- Hourly purchases (all 24 hours, default 0) -------------------
        hourly_qs = (
            AggregatedMetric.objects
            .filter(
                store_id=store_id,
                metric_type='purchases_by_hour',
                period_start__gte=report_from,
            )
            .values('dimension_key')
            .annotate(value=Sum('value'))
        )
        hourly_raw = {
            int(row['dimension_key']): int(row['value'])
            for row in hourly_qs
            if row['dimension_key'].isdigit()
        }
        hourly_purchases_list = [
            {'hour': h, 'count': hourly_raw.get(h, 0)}
            for h in range(24)
        ]

        # ---- Top countries by revenue (top 10) ----------------------------
        top_countries = list(
            AggregatedMetric.objects
            .filter(
                store_id=store_id,
                metric_type='top_country_revenue',
                period_start__gte=report_from,
            )
            .values('dimension_key')
            .annotate(value=Sum('value'))
            .order_by('-value')[:10]
        )

        return {
            'total_sessions': total_sessions,
            'total_pageviews': total_pageviews,
            'total_purchases': total_purchases,
            'total_revenue': total_revenue,
            'avg_conversion_rate': avg_conversion_rate,
            'top_utm_sources': top_utm_sources,
            'top_referrers': top_referrers,
            'top_landing_pages': top_landing_pages,
            'daily_series': daily_series,
            'daily_series_json': json.dumps(daily_series),
            'monthly_data': monthly_data,
            'hourly_purchases_list': hourly_purchases_list,
            'top_countries': top_countries,
            # Phase 1: no refund netting; label makes scope explicit.
            'revenue_label': 'Gross revenue',
        }
