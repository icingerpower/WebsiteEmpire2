#ifndef SITEMAPCONFIG_H
#define SITEMAPCONFIG_H

/**
 * Tuning parameters for SitemapOrchestrator.
 *
 * chunkSize:  maximum URLs per individual sitemap file (Google hard-limits at 50 000).
 * recentDays: pages updated within this window are also listed in sitemap-recent.xml
 *             so Googlebot discovers fresh content ahead of the regular crawl cycle.
 */
struct SitemapConfig
{
    static constexpr int DEFAULT_CHUNK_SIZE  = 50000;
    static constexpr int DEFAULT_RECENT_DAYS = 7;

    int chunkSize  = DEFAULT_CHUNK_SIZE;
    int recentDays = DEFAULT_RECENT_DAYS;
};

#endif // SITEMAPCONFIG_H
