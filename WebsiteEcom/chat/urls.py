"""
Chat app URLs (ADR-024 Decision 4).

Mounted at "chat/" in webecom/urls.py BEFORE `path("", include("storefront.urls"))`
— i.e. before the storefront `<path:slug>` catch-all — so these two fixed routes
can never be shadowed by a Product/StaticPage/Collection permalink (see also the
"chat" entry added to permalinks.models.RESERVED_TOP_LEVEL_SLUGS, which prevents
a store admin from ever creating a page/product slugged exactly "chat").
"""

from django.urls import path

from chat import views

app_name = "chat"

urlpatterns = [
    path("session/", views.chat_session_view, name="session"),
    path("message/", views.chat_message_view, name="message"),
]
