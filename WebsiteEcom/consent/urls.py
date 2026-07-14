"""
Consent endpoint URLs — mounted at /_consent/ in webecom/urls.py, BEFORE the
storefront catch-all (same underscore-prefixed internal-endpoint convention
as /_analytics/, ADR-025 D5).
"""

from django.urls import path

from consent import views

app_name = "consent"

urlpatterns = [
    path("", views.consent_post, name="consent_post"),
]
