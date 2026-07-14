"""
URL configuration for webecom project.

/admin/       — per-store StoreAdminSite (access gated by StoreEmployee, TICKET-002)
/superadmin/  — cross-org SuperAdminSite (access gated by User.is_super_admin)

Storefront and API URLs are added in later tickets.
"""

# Custom 404 handler (TH-087) — registered globally so Django uses it for all Http404
# exceptions, including those raised by the permalink resolver catch-all.
# Never returns a soft-200; always HTTP 404 status.
handler404 = "storefront.views.custom_404_view"

from django.conf import settings
from django.conf.urls.static import static
from django.urls import include, path

from webecom.admin import store_admin_site, super_admin_site

urlpatterns = [
    # Abandoned-checkout resume link (TICKET-021, ADR-010 Q3).
    # Must be registered early (no store-middleware interference needed for the bare path).
    path("", include("cart.urls")),
    # Analytics admin dashboard must come BEFORE the store_admin_site catch-all
    # because `path("admin/", store_admin_site.urls)` is a prefix match — it would
    # swallow any /admin/analytics/... request before this pattern is reached.
    path("admin/analytics/", include("analytics.admin_urls")),
    # Employee invite accept-link (ADR-033 D4b, TICKET-047) — unauthenticated,
    # registered before the store_admin_site catch-all for the same reason as
    # admin/analytics/ above. See stores.middleware.LocaleMiddleware
    # .EXCLUDED_PREFIXES for why "/invite/" also skips locale/store resolution.
    path("invite/", include("stores.urls")),
    path("admin/", store_admin_site.urls),
    path("superadmin/", super_admin_site.urls),
    # Analytics beacon — used by the frontend JS snippet (TICKET-012).
    # Mounted under a leading underscore so it is clearly internal.
    path("_analytics/", include("analytics.urls")),
    # Consent decision endpoint (ADR-025 D5, TICKET-048). Underscore-prefixed,
    # same convention as /_analytics/ — registered before the storefront
    # catch-all so it can never be shadowed by a permalink. No reserved-slug
    # entry needed: NFD slug normalization (core/slugs.py make_slug ->
    # Django's slugify) always strips leading underscores, so a permalink
    # slug literally equal to "_consent" can never be produced.
    path("_consent/", include("consent.urls")),
    # Payment processor webhooks (TICKET-018, TICKET-019).
    path("webhooks/stripe/", include("payments.webhook_urls")),
    path("webhooks/paypal/", include("payments.paypal_webhook_urls")),
    # Upsell accept/decline endpoints (TICKET-028, ADR-011).
    path("campaigns/", include("campaigns.urls")),
    # Sitemap index, per-language sitemaps, and robots.txt (TICKET-026, SM-001, ROB-001).
    # Scoped to the store/domain resolved by LocaleMiddleware from the Host header.
    # Known limitation: adding stores/languages takes effect immediately (dynamic
    # generation — no restart required).
    path("", include("sitemaps.urls")),
    # AI sales-assistant chat endpoints (ADR-024, TICKET-038). Registered BEFORE
    # the storefront catch-all so /chat/session/ and /chat/message/ can never be
    # shadowed by a Product/StaticPage/Collection permalink (see also "chat" in
    # permalinks.models.RESERVED_TOP_LEVEL_SLUGS).
    path("chat/", include("chat.urls")),
    # Catalog feed files (ADR-026, TICKET-033). Registered BEFORE the storefront
    # catch-all so /feeds/<provider>/<country>-<lang>.xml can never be shadowed
    # by a Product/StaticPage/Collection permalink (see also "feeds" in
    # permalinks.models.RESERVED_TOP_LEVEL_SLUGS). robots.txt already emits
    # "Disallow: /feeds/" (sitemaps/views.py, ROB-001).
    path("", include("feeds.urls")),
    # Lead capture overlay endpoints (ADR-027 D1/D3, TICKET-034). Registered
    # BEFORE the storefront catch-all so /overlay/signup/, /overlay/event/,
    # and /overlay/confirm/<token>/ can never be shadowed by a Product/
    # StaticPage/Collection permalink (see also "overlay" in
    # permalinks.models.RESERVED_TOP_LEVEL_SLUGS). Mounted here rather than
    # nested inside storefront/urls.py so reverse("engagement:...") resolves
    # against a top-level namespace, not "storefront:engagement:..." (same
    # reasoning as "chat" and "feeds" above).
    path("overlay/", include("engagement.urls")),
    # Storefront catch-all — MUST be last; it shadows all unmatched paths and
    # dispatches them through the PermalinkResolver (Phase 2).
    # WARNING: any app mounted AFTER this line is unreachable.
    path("", include("storefront.urls")),
] + (static(settings.MEDIA_URL, document_root=settings.MEDIA_ROOT) if settings.DEBUG else [])
