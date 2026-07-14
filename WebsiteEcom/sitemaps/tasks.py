"""
Celery tasks for sitemap regeneration (TICKET-026, SM-020).

SM-020: sitemap updates are event-driven (debounced) plus a nightly full rebuild.

v1 implementation note:
  Sitemaps are currently generated dynamically from the Permalink table on every
  request.  No pre-generated file is produced.  The nightly rebuild task is a
  placeholder that will be upgraded to full pre-generation once request-time
  generation proves too slow (> 2 s on the largest stores).

  When pre-generation is added, this task will:
    1. Iterate all active Store rows.
    2. For each (store, domain, language), call PermalinkSitemap.get_urls().
    3. Render the XML templates to a file in MEDIA_ROOT/sitemaps/.
    4. Serve the files directly via NGINX (bypass Django for the hot path).

  Known limitation: adding a new store/language requires no action — the index
  and per-language sitemaps are built on demand.  Removing an enabled language
  is instant (LocaleMiddleware returns 404 for unknown lang_codes).
"""

from celery import shared_task


@shared_task(name="sitemaps.tasks.rebuild_sitemap_cache")
def rebuild_sitemap_cache():
    """
    Nightly sitemap cache rebuild (SM-020: daily full rebuild as safety net).

    In v1 (dynamic generation), this is a no-op.  It exists so the beat schedule
    is wired and the task autodiscovery works; the implementation is added here
    when pre-generation is introduced.
    """
    pass
