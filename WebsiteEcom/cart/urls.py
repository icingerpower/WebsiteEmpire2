"""
Cart app URL configuration.

/checkout/resume/<token>/  — abandoned-checkout resume link handler (ADR-010 Q3).
/cart/add/                 — add a product variant to the cart (POST-only).
/cart/update/              — update a cart line item quantity (POST-only).
/cart/remove/              — remove a cart line item (POST-only).
"""

from django.urls import path

from cart import views

urlpatterns = [
    path(
        "checkout/resume/<str:token>/",
        views.resume_abandoned_checkout,
        name="resume_abandoned_checkout",
    ),
    path("cart/add/", views.add_to_cart, name="cart-add"),
    path("cart/update/", views.update_cart_item, name="cart-update"),
    path("cart/remove/", views.remove_cart_item, name="cart-remove"),
]
