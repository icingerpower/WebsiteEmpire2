#ifndef SITEMAPENTRY_H
#define SITEMAPENTRY_H

#include <QString>

/** One URL entry inside a <urlset> sitemap chunk. */
struct SitemapEntry
{
    QString loc;      ///< Full absolute URL (e.g. "https://example.com/article")
    QString lastmod;  ///< ISO 8601 date, YYYY-MM-DD
};

/** One child-sitemap pointer inside a <sitemapindex>. */
struct SitemapIndexEntry
{
    QString loc;      ///< Full URL to the child sitemap file
    QString lastmod;  ///< MAX(lastmod) of all URLs in that child — drives Googlebot re-fetch
};

#endif // SITEMAPENTRY_H
