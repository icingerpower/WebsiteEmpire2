"""
Engagement app URLs (ADR-027 D1/D3, TICKET-034).

Mounted at "overlay/" in webecom/urls.py, registered BEFORE the storefront
catch-all (same top-level-namespace convention as "chat"/"feeds" — nesting
this include() inside storefront/urls.py instead would make
reverse("engagement:...") resolve as "storefront:engagement:...", which is
not what engagement/slot_providers.py and engagement/views.py call).
"overlay" is in permalinks.models.RESERVED_TOP_LEVEL_SLUGS so a Product/
StaticPage/Collection permalink can never shadow these routes.
"""

from django.urls import path

from engagement import views

app_name = "engagement"

urlpatterns = [
    path("signup/", views.overlay_signup, name="overlay-signup"),
    path("event/", views.overlay_event, name="overlay-event"),
    path("confirm/<str:token>/", views.overlay_confirm, name="overlay-confirm"),
]
