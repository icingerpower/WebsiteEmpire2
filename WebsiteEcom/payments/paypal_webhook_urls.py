"""
URL patterns for the PayPal webhook endpoint.

Mounted at /webhooks/paypal/ in pradize/urls.py.
"""

from django.urls import path

from .paypal_webhook_views import paypal_webhook

urlpatterns = [
    path('', paypal_webhook, name='paypal-webhook'),
]
