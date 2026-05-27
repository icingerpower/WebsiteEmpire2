#include "SitemapOrchestrator.h"

#include "RobotsWriter.h"
#include "SitemapChunkWriter.h"
#include "SitemapIndexWriter.h"

#include <QDateTime>
#include <QMap>
#include <QSqlDatabase>
#include <QSqlQuery>

void SitemapOrchestrator::generate(const QString       &connName,
                                    const QString       &domain,
                                    const QString       &baseUrl,
                                    const SitemapConfig &config)
{
    if (baseUrl.isEmpty()) {
        return;
    }

    QSqlDatabase db = QSqlDatabase::database(connName);

    // Remove stale sitemap / robots entries before regenerating so chunks
    // from removed languages do not persist across generation runs.
    {
        QSqlQuery del(db);
        del.exec(QStringLiteral(
            "DELETE FROM page_variants WHERE page_id IN "
            "(SELECT id FROM pages WHERE path LIKE '/sitemap%' OR path = '/robots.txt')"));
        del.exec(QStringLiteral(
            "DELETE FROM pages WHERE path LIKE '/sitemap%' OR path = '/robots.txt'"));
    }

    // Read all non-sitemap pages, grouped by language.
    QMap<QString, QList<SitemapEntry>> byLang;
    QList<SitemapEntry>                recentEntries;
    const QString cutoffDate = QDateTime::currentDateTimeUtc()
                                .addDays(-config.recentDays)
                                .toString(QStringLiteral("yyyy-MM-dd"));

    {
        QSqlQuery q(db);
        q.exec(QStringLiteral(
            "SELECT path, lang, updated_at FROM pages"
            " WHERE lang != '_'"
            " ORDER BY lang ASC, updated_at DESC"));
        while (q.next()) {
            const QString &path      = q.value(0).toString();
            const QString &lang      = q.value(1).toString();
            const QString  updatedAt = q.value(2).toString().left(10);

            SitemapEntry e;
            e.loc     = baseUrl + path;
            e.lastmod = updatedAt;

            byLang[lang].append(e);

            if (updatedAt >= cutoffDate) {
                recentEntries.append(e);
            }
        }
    }

    QList<SitemapIndexEntry> indexEntries;

    // Recent chunk listed first so Googlebot prioritises fresh content.
    if (!recentEntries.isEmpty()) {
        const QString recentPath = QStringLiteral("/sitemap-recent.xml");
        const QString maxLastmod = SitemapChunkWriter::write(
            connName, domain, recentPath, recentEntries);
        SitemapIndexEntry ie;
        ie.loc     = baseUrl + recentPath;
        ie.lastmod = maxLastmod;
        indexEntries.append(ie);
    }

    // Per-language chunks.
    for (auto it = byLang.cbegin(); it != byLang.cend(); ++it) {
        const QString             &lang    = it.key();
        const QList<SitemapEntry> &entries = it.value();
        const int total     = entries.size();
        const int numChunks = (total + config.chunkSize - 1) / config.chunkSize;

        for (int i = 0; i < numChunks; ++i) {
            const int from  = i * config.chunkSize;
            const int count = qMin(config.chunkSize, total - from);
            const QList<SitemapEntry> chunk = entries.mid(from, count);

            const QString chunkPath = QStringLiteral("/sitemap-")
                                      + lang
                                      + QStringLiteral("-")
                                      + QString::number(i + 1)
                                      + QStringLiteral(".xml");

            const QString maxLastmod = SitemapChunkWriter::write(
                connName, domain, chunkPath, chunk);

            SitemapIndexEntry ie;
            ie.loc     = baseUrl + chunkPath;
            ie.lastmod = maxLastmod;
            indexEntries.append(ie);
        }
    }

    SitemapIndexWriter::write(connName, domain, baseUrl, indexEntries);
    RobotsWriter::write(connName, domain, baseUrl);
}
