#ifndef ROBOTSWRITER_H
#define ROBOTSWRITER_H

#include <QString>

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
    static void write(const QString &connName,
                      const QString &domain,
                      const QString &baseUrl);
};

#endif // ROBOTSWRITER_H
