"""
URL patterns for the sitemaps app (TICKET-026).

Pattern notes:
- sitemap_index:    exact path match — /sitemap.xml
- sitemap_language: regex to capture the ISO 639-1 lang code before ".xml"
                    (e.g. /sitemap-en.xml, /sitemap-pt-br.xml)
- robots_txt:       exact path match — /robots.txt
"""

from django.urls import re_path, path

from sitemaps.views import robots_txt_view, sitemap_index_view, sitemap_language_view

app_name = "sitemaps"

urlpatterns = [
    path("sitemap.xml", sitemap_index_view, name="sitemap_index"),
    re_path(
        r"^sitemap-(?P<lang_code>[a-z]{2}(?:-[a-z]{2})?)\.xml$",
        sitemap_language_view,
        name="sitemap_language",
    ),
    path("robots.txt", robots_txt_view, name="robots_txt"),
]
