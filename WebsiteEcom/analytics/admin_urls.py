"""
Analytics admin URL patterns — mounted at admin/analytics/ in pradize/urls.py.

These URLs are separate from the public beacon endpoint (analytics/urls.py)
and require login + store_admin or super_admin access.
"""

from django.urls import path
from django.views.generic import RedirectView

from analytics.admin_views import AnalyticsDashboardView

urlpatterns = [
    path('', RedirectView.as_view(url='dashboard/', permanent=False)),
    path('dashboard/', AnalyticsDashboardView.as_view(), name='analytics-dashboard'),
]
