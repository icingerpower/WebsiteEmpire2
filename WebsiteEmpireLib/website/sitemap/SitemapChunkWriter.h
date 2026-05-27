#ifndef SITEMAPCHUNKWRITER_H
#define SITEMAPCHUNKWRITER_H

#include "SitemapEntry.h"

#include <QList>
#include <QString>

/**
 * Writes one <urlset> sitemap chunk to the content database.
 *
 * The XML is gzip-compressed and stored in page_variants under chunkPath
 * (e.g. "/sitemap-en-1.xml"), using the same pages + page_variants layout
 * as PageGenerator so PageController::serveFile can serve it transparently.
 *
 * Returns the MAX(lastmod) string across all entries so SitemapIndexWriter
 * can record an accurate <lastmod> for this chunk in the index — Googlebot
 * uses the index <lastmod> to decide whether to re-fetch a chunk at all.
 *
 * Pre-condition: the content DB connection connName must already be open and
 * schema-initialised (PageGenerator::ensureSchema must have been called).
 */
class SitemapChunkWriter
{
public:
    static QString write(const QString             &connName,
                         const QString             &domain,
                         const QString             &chunkPath,
                         const QList<SitemapEntry> &entries);
};

#endif // SITEMAPCHUNKWRITER_H
