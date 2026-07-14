"""
Public (unauthenticated) URLs for the stores app — the employee invite
accept-link (ADR-033 D4b, TICKET-047). Mounted at /invite/ in webecom/urls.py.
"""

from django.urls import path

from stores.views import accept_invite_view

urlpatterns = [
    path(
        "accept/<str:uidb64>/<int:employee_pk>/<str:token>/",
        accept_invite_view,
        name="stores_accept_invite",
    ),
]
