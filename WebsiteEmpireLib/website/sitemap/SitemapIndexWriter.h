#ifndef SITEMAPINDEXWRITER_H
#define SITEMAPINDEXWRITER_H

#include "SitemapEntry.h"

#include <QList>
#include <QString>

/**
 * Writes the sitemap index file (/sitemap.xml) to the content database.
 *
 * The index is a <sitemapindex> XML listing every child sitemap with its
 * last-modified date.  It is stored gzip-compressed in page_variants at
 * path "/sitemap.xml", served by PageController::serveFile.
 *
 * Pre-condition: same as SitemapChunkWriter — connection must be open and
 * schema-ready.
 */
class SitemapIndexWriter
{
public:
    static void write(const QString                  &connName,
                      const QString                  &domain,
                      const QString                  &baseUrl,
                      const QList<SitemapIndexEntry> &entries);
};

#endif // SITEMAPINDEXWRITER_H
