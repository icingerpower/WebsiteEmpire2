#ifndef SITEMAPORCHESTRATOR_H
#define SITEMAPORCHESTRATOR_H

#include "SitemapConfig.h"

#include <QString>
#include <QStringList>

/**
 * Drives the full sitemap + robots.txt generation pass.
 *
 * Called by PageGenerator::generateAll() after all HTML pages have been
 * written.  The orchestrator reads the content DB directly (same connName),
 * groups pages by language, chunks each language into ≤ chunkSize URLs,
 * and writes:
 *
 *   /sitemap-recent.xml   — pages updated within the last recentDays
 *                           (listed first in the index so Googlebot finds
 *                           fresh content sooner)
 *   /sitemap-{lang}-N.xml — per-language chunks, N = 1-indexed
 *   /sitemap.xml          — <sitemapindex> listing all of the above
 *   /robots.txt           — "Allow: /" + Sitemap: pointer
 *
 * Every call starts by deleting all existing sitemap / robots rows so stale
 * language chunks (from a language that was removed) do not persist.
 *
 * generate() is a no-op when baseUrl is empty (domain not configured).
 */
class SitemapOrchestrator
{
public:
    /**
     * additionalSitemapUrls is forwarded to RobotsWriter: the sitemap URLs of the
     * other languages in a multilingual deployment.  Crawlers only read
     * robots.txt from the domain root, so without them every non-root language
     * is undiscoverable via sitemaps.
     */
    static void generate(const QString      &connName,
                         const QString      &domain,
                         const QString      &baseUrl,
                         const SitemapConfig &config = {},
                         const QStringList  &additionalSitemapUrls = {});
};

#endif // SITEMAPORCHESTRATOR_H
