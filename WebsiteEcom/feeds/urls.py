"""
URL patterns for the feeds app (ADR-026 D2, TICKET-033).

Mounted in webecom/urls.py BEFORE the storefront catch-all (like /chat/,
/sitemap.xml) — 'feeds' is also added to
permalinks.models.RESERVED_TOP_LEVEL_SLUGS in the same PR so a product/page
can never be slugged 'feeds' and shadow this route.

Pattern mirrors sitemaps/urls.py's re_path style for embedding two literal-
separated segments (country + lang) inside one path component, e.g.
/feeds/google/us-en.xml, /feeds/facebook/fr-fr.xml.
"""

from django.urls import re_path

from feeds.views import feed_view

app_name = "feeds"

urlpatterns = [
    re_path(
        r"^feeds/(?P<provider_key>[a-z]+)/(?P<country_code>[a-z]{2})-(?P<lang_code>[a-z]{2}(?:-[a-z]{2})?)\.xml$",
        feed_view,
        name="feed_view",
    ),
]
