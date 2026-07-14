"""
URL patterns for payment webhook endpoints.

Mounted at /webhooks/stripe/ in pradize/urls.py.
"""

from django.urls import path

from .webhook_views import stripe_webhook

urlpatterns = [
    path('', stripe_webhook, name='stripe-webhook'),
]
