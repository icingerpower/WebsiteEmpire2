#include "SitemapIndexWriter.h"

#include "SitemapChunkWriter.h"    // reuses upsertBlob via SitemapChunkWriter::write pattern
#include "website/pages/PageGenerator.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>

namespace {

QString buildSitemapIndex(const QString &baseUrl, const QList<SitemapIndexEntry> &entries)
{
    QString xml;
    xml += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<sitemapindex xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n");
    for (const SitemapIndexEntry &e : entries) {
        xml += QStringLiteral("<sitemap><loc>");
        xml += e.loc.toHtmlEscaped();
        xml += QStringLiteral("</loc><lastmod>");
        xml += e.lastmod;
        xml += QStringLiteral("</lastmod></sitemap>\n");
    }
    xml += QStringLiteral("</sitemapindex>\n");
    return xml;
}

} // namespace

void SitemapIndexWriter::write(const QString                  &connName,
                                const QString                  &domain,
                                const QString                  &baseUrl,
                                const QList<SitemapIndexEntry> &entries)
{
    const QString    xml  = buildSitemapIndex(baseUrl, entries);
    const QByteArray gz   = PageGenerator::gzipCompress(xml.toUtf8());
    const QString    etag = PageGenerator::computeEtag(gz);
    const QString    now  = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSqlDatabase db = QSqlDatabase::database(connName);
    db.transaction();

    QSqlQuery upsertPage(db);
    upsertPage.prepare(QStringLiteral(
        "INSERT INTO pages (path, domain, lang, etag, updated_at)"
        " VALUES ('/sitemap.xml', :domain, '_', :etag, :now)"
        " ON CONFLICT(path) DO UPDATE SET"
        "   domain=excluded.domain, etag=excluded.etag, updated_at=excluded.updated_at"));
    upsertPage.bindValue(QStringLiteral(":domain"), domain);
    upsertPage.bindValue(QStringLiteral(":etag"),   etag);
    upsertPage.bindValue(QStringLiteral(":now"),    now);
    upsertPage.exec();

    QSqlQuery idQ(db);
    idQ.exec(QStringLiteral("SELECT id FROM pages WHERE path = '/sitemap.xml'"));
    idQ.next();
    const int pageId = idQ.value(0).toInt();

    QSqlQuery upsertVariant(db);
    upsertVariant.prepare(QStringLiteral(
        "INSERT INTO page_variants (page_id, label, is_active, html_gz, etag)"
        " VALUES (:page_id, 'control', 1, :gz, :etag)"
        " ON CONFLICT(page_id, label) DO UPDATE SET"
        "   html_gz=excluded.html_gz, etag=excluded.etag, is_active=1"));
    upsertVariant.bindValue(QStringLiteral(":page_id"), pageId);
    upsertVariant.bindValue(QStringLiteral(":gz"),      gz);
    upsertVariant.bindValue(QStringLiteral(":etag"),    etag);
    upsertVariant.exec();

    db.commit();
}
