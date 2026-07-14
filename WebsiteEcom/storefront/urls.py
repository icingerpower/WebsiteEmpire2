"""
Storefront URL configuration (ADR-012 D5, ADR-015 §6, TICKET-029 Phase 2).

This URL conf is mounted LAST in webecom/urls.py so it acts as a catch-all
for all buyer-facing paths.  It must not shadow admin, webhooks, sitemaps, or
any other non-storefront routes.

URL structure:
  /             → home_view
  /cart/        → cart_page
  /search/      → search_view

  Checkout (ADR-015 §6 — all registered BEFORE the <path:slug> catch-all):
  GET  /checkout/                          → checkout_view
  POST /checkout/contact/                  → checkout_contact_post
  POST /checkout/address/                  → checkout_address_post
  POST /checkout/shipping/                 → checkout_shipping_post
  POST /checkout/pay/                      → checkout_pay_post
  GET  /checkout/payment/return/           → checkout_payment_return
  GET  /checkout/paypal/return/            → checkout_paypal_return   (ADR-020)
  GET  /checkout/paypal/cancel/            → checkout_paypal_cancel   (ADR-020)
  GET  /checkout/retry/<signed_token>/     → checkout_retry
  POST /checkout/discount/apply/           → apply_discount_view
  POST /checkout/discount/remove/          → remove_discount_view
  GET  /orders/<public_order_id>/thank-you/ → order_thank_you

  Currency selection (ADR-023 §4) — registered BEFORE the <path:slug> catch-all:
  POST /currency/set/                      → currency.views.set_display_currency

  /<path:slug>  → page_view (permalink resolver catch-all; MUST be last)
"""

from django.urls import path

from currency.views import set_display_currency
from storefront.views import cart_page, home_view, page_view, search_view
from storefront import views_checkout
from storefront import views_product_forms

app_name = "storefront"

urlpatterns = [
    path("", home_view, name="home"),
    path("cart/", cart_page, name="cart"),
    path("search/", search_view, name="search"),

    # -----------------------------------------------------------------------
    # Product engagement forms (T029 TH-082) — registered BEFORE the catch-all.
    # -----------------------------------------------------------------------
    path(
        "products/<slug:product_slug>/notify-me/",
        views_product_forms.notify_me_view,
        name="product-notify-me",
    ),
    path(
        "products/<slug:product_slug>/quotation/",
        views_product_forms.quotation_request_view,
        name="product-quotation",
    ),

    # -----------------------------------------------------------------------
    # Checkout — all paths registered BEFORE the <path:slug> catch-all.
    # ADR-015 §6 URL map.
    # -----------------------------------------------------------------------
    path("checkout/", views_checkout.checkout_view, name="checkout"),
    path("checkout/contact/", views_checkout.checkout_contact_post, name="checkout-contact"),
    path("checkout/address/", views_checkout.checkout_address_post, name="checkout-address"),
    # ADR-017: country metadata endpoint — must be registered BEFORE the <path:slug>
    # catch-all and before any address POST so it is never shadowed.
    path(
        "checkout/address/country-meta/",
        views_checkout.country_meta_view,
        name="checkout-address-country-meta",
    ),
    path("checkout/shipping/", views_checkout.checkout_shipping_post, name="checkout-shipping"),
    path("checkout/pay/", views_checkout.checkout_pay_post, name="checkout-pay"),
    path("checkout/payment/return/", views_checkout.checkout_payment_return, name="checkout-payment-return"),
    path("checkout/paypal/return/", views_checkout.checkout_paypal_return, name="checkout-paypal-return"),
    path("checkout/paypal/cancel/", views_checkout.checkout_paypal_cancel, name="checkout-paypal-cancel"),
    path("checkout/retry/<str:signed_token>/", views_checkout.checkout_retry, name="checkout-retry"),
    path("checkout/discount/apply/", views_checkout.apply_discount_view, name="apply-discount"),
    path("checkout/discount/remove/", views_checkout.remove_discount_view, name="remove-discount"),
    path("orders/<str:public_order_id>/thank-you/", views_checkout.order_thank_you, name="order-thank-you"),

    # -----------------------------------------------------------------------
    # Currency selection (ADR-023 §4) — registered BEFORE the <path:slug>
    # catch-all like every other fixed storefront route. This is the ONLY
    # code path that writes session['display_currency'] — never middleware.
    # -----------------------------------------------------------------------
    path("currency/set/", set_display_currency, name="set-display-currency"),

    # -----------------------------------------------------------------------
    # Catch-all: MUST be last — shadows all unmatched paths via PermalinkResolver.
    # -----------------------------------------------------------------------
    path("<path:slug>", page_view, name="page"),
]
