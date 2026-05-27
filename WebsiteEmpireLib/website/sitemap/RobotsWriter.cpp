#include "RobotsWriter.h"

#include "website/pages/PageGenerator.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>

void RobotsWriter::write(const QString &connName,
                          const QString &domain,
                          const QString &baseUrl)
{
    // Paths that waste crawl budget — disallowed only when they exist in the DB.
    static const QStringList kLegalPaths = {
        QStringLiteral("/privacy-policy.html"),
        QStringLiteral("/terms-of-service.html"),
        QStringLiteral("/cookie-policy.html"),
        QStringLiteral("/legal-notice.html"),
        QStringLiteral("/contact-us.html"),
        QStringLiteral("/about-us.html"),
    };

    QSqlDatabase db = QSqlDatabase::database(connName);

    // Find which legal paths actually exist so we don't emit stale Disallow lines.
    QStringList existingLegal;
    for (const QString &path : std::as_const(kLegalPaths)) {
        QSqlQuery chk(db);
        chk.prepare(QStringLiteral("SELECT 1 FROM pages WHERE path = :p AND lang != '_' LIMIT 1"));
        chk.bindValue(QStringLiteral(":p"), path);
        chk.exec();
        if (chk.next()) {
            existingLegal.append(path);
        }
    }

    QString txt;
    txt += QStringLiteral("User-agent: *\n");
    for (const QString &path : std::as_const(existingLegal)) {
        txt += QStringLiteral("Disallow: ");
        txt += path;
        txt += QLatin1Char('\n');
    }
    txt += QStringLiteral("Sitemap: ");
    txt += baseUrl;
    txt += QStringLiteral("/sitemap.xml\n");

    const QByteArray gz   = PageGenerator::gzipCompress(txt.toUtf8());
    const QString    etag = PageGenerator::computeEtag(gz);
    const QString    now  = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    db.transaction();

    QSqlQuery upsertPage(db);
    upsertPage.prepare(QStringLiteral(
        "INSERT INTO pages (path, domain, lang, etag, updated_at)"
        " VALUES ('/robots.txt', :domain, '_', :etag, :now)"
        " ON CONFLICT(path) DO UPDATE SET"
        "   domain=excluded.domain, etag=excluded.etag, updated_at=excluded.updated_at"));
    upsertPage.bindValue(QStringLiteral(":domain"), domain);
    upsertPage.bindValue(QStringLiteral(":etag"),   etag);
    upsertPage.bindValue(QStringLiteral(":now"),    now);
    upsertPage.exec();

    QSqlQuery idQ(db);
    idQ.exec(QStringLiteral("SELECT id FROM pages WHERE path = '/robots.txt'"));
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
