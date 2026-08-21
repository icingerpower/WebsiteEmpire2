#ifndef ROBOTSWRITER_H
#define ROBOTSWRITER_H

#include <QString>
#include <QStringList>

/**
 * Writes /robots.txt to the content database.
 *
 * Generated format:
 *   User-agent: *
 *   Disallow: /privacy-policy.html   ← only if the page exists in the DB
 *   Disallow: /terms-of-service.html
 *   Disallow: /cookie-policy.html
 *   Disallow: /legal-notice.html
 *   Disallow: /contact-us.html
 *   Disallow: /about-us.html
 *   Sitemap: <baseUrl>/sitemap.xml
 *
 * Legal/utility pages are disallowed to preserve crawl budget for content
 * pages.  Only paths that actually exist in the pages table are emitted,
 * so a site that has not yet created some of those pages won't have stale
 * Disallow lines.
 *
 * Pre-condition: same as SitemapChunkWriter — connection must be open and
 * schema-ready.
 */
class RobotsWriter
{
public:
    /**
     * additionalSitemapUrls: absolute sitemap URLs for the OTHER languages of a
     * multilingual deployment, e.g. "https://example.com/fr/sitemap.xml".  One
     * "Sitemap:" line is emitted per entry, after this deployment's own.
     *
     * Crawlers only read robots.txt at the domain root, so /fr/robots.txt is
     * never fetched and a language whose sitemap is listed nowhere else is
     * undiscoverable — the root file has to advertise every language.
     */
    static void write(const QString     &connName,
                      const QString     &domain,
                      const QString     &baseUrl,
                      const QStringList &additionalSitemapUrls = {});
};

#endif // ROBOTSWRITER_H
