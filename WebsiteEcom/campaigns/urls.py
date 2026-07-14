"""
URL patterns for the campaigns app (T028 — upsell accept/decline endpoints).
"""

from django.urls import path

from campaigns.views import upsell_accept_view, upsell_decline_view

urlpatterns = [
    path("upsell/accept/", upsell_accept_view, name="upsell_accept"),
    path("upsell/decline/", upsell_decline_view, name="upsell_decline"),
]
