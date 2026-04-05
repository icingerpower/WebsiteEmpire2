#include "PageGenerator.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <zlib.h>

// =============================================================================
// Constructor
// =============================================================================

PageGenerator::PageGenerator(IPageRepository &pageRepo, CategoryTable &categoryTable)
    : m_pageRepo(pageRepo)
    , m_categoryTable(categoryTable)
{
}

// =============================================================================
// gzipCompress
// =============================================================================

QByteArray PageGenerator::gzipCompress(const QByteArray &input)
{
    z_stream stream{};
    // windowBits = 15 + 16 → standard gzip format
    deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                 15 + 16, 8, Z_DEFAULT_STRATEGY);

    QByteArray output;
    output.resize(static_cast<int>(deflateBound(&stream,
                                                static_cast<uLong>(input.size()))));

    stream.next_in   = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in  = static_cast<uInt>(input.size());
    stream.next_out  = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());

    deflate(&stream, Z_FINISH);
    output.resize(static_cast<int>(stream.total_out));
    deflateEnd(&stream);

    return output;
}

// =============================================================================
// computeEtag
// =============================================================================

QString PageGenerator::computeEtag(const QByteArray &data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

// =============================================================================
// ensureSchema
// =============================================================================

void PageGenerator::ensureSchema(const QString &connectionName)
{
    QSqlQuery q(QSqlDatabase::database(connectionName));
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS pages ("
        "  id         INTEGER PRIMARY KEY,"
        "  path       TEXT    UNIQUE NOT NULL,"
        "  domain     TEXT    NOT NULL,"
        "  lang       TEXT    NOT NULL,"
        "  etag       TEXT    NOT NULL,"
        "  updated_at TEXT    NOT NULL"
        ")"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS page_variants ("
        "  id        INTEGER PRIMARY KEY,"
        "  page_id   INTEGER NOT NULL REFERENCES pages(id),"
        "  label     TEXT    NOT NULL,"
        "  is_active INTEGER NOT NULL,"
        "  html_gz   BLOB    NOT NULL,"
        "  etag      TEXT    NOT NULL,"
        "  UNIQUE(page_id, label)"
        ")"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS redirects ("
        "  old_path    TEXT    PRIMARY KEY,"
        "  new_path    TEXT,"
        "  status_code INTEGER NOT NULL"
        ")"));
}

// =============================================================================
// generateAll
// =============================================================================

int PageGenerator::generateAll(const QDir     &workingDir,
                               const QString  &domain,
                               AbstractEngine &engine,
                               int             websiteIndex)
{
    // Open content.db.
    const QString connName = QStringLiteral("page_generator_content_db");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(workingDir.filePath(QLatin1StringView(FILENAME)));
        db.open();
    }
    ensureSchema(connName);

    int count = 0;
    const QList<PageRecord> &pages = m_pageRepo.findAll();

    for (const PageRecord &record : std::as_const(pages)) {
        auto type = AbstractPageType::createForTypeId(record.typeId, m_categoryTable);
        if (!type) {
            continue;
        }

        const QHash<QString, QString> &data = m_pageRepo.loadData(record.id);
        type->load(data);

        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        type->addCode(QStringView{}, engine, websiteIndex, html, css, js, cssDoneIds, jsDoneIds);

        const QByteArray &htmlGz  = gzipCompress(html.toUtf8());
        const QString    &etag    = computeEtag(htmlGz);
        const QDateTime   utcNow  = QDateTime::currentDateTimeUtc();
        const QString    &now     = utcNow.toString(Qt::ISODate);

        QSqlDatabase db = QSqlDatabase::database(connName);
        db.transaction();

        // Upsert page row.
        QSqlQuery upsertPage(db);
        upsertPage.prepare(QStringLiteral(
            "INSERT INTO pages (path, domain, lang, etag, updated_at)"
            " VALUES (:path, :domain, :lang, :etag, :now)"
            " ON CONFLICT(path) DO UPDATE SET"
            "   domain=excluded.domain, lang=excluded.lang,"
            "   etag=excluded.etag, updated_at=excluded.updated_at"));
        upsertPage.bindValue(QStringLiteral(":path"),   record.permalink);
        upsertPage.bindValue(QStringLiteral(":domain"), domain);
        upsertPage.bindValue(QStringLiteral(":lang"),   record.lang);
        upsertPage.bindValue(QStringLiteral(":etag"),   etag);
        upsertPage.bindValue(QStringLiteral(":now"),    now);
        upsertPage.exec();

        // Fetch the page id (whether just inserted or pre-existing).
        QSqlQuery idQ(db);
        idQ.prepare(QStringLiteral("SELECT id FROM pages WHERE path = :path"));
        idQ.bindValue(QStringLiteral(":path"), record.permalink);
        idQ.exec();
        idQ.next();
        const int contentPageId = idQ.value(0).toInt();

        // Upsert control variant.
        QSqlQuery upsertVariant(db);
        upsertVariant.prepare(QStringLiteral(
            "INSERT INTO page_variants (page_id, label, is_active, html_gz, etag)"
            " VALUES (:page_id, 'control', 1, :html_gz, :etag)"
            " ON CONFLICT(page_id, label) DO UPDATE SET"
            "   is_active=1, html_gz=excluded.html_gz, etag=excluded.etag"));
        upsertVariant.bindValue(QStringLiteral(":page_id"), contentPageId);
        upsertVariant.bindValue(QStringLiteral(":html_gz"), htmlGz);
        upsertVariant.bindValue(QStringLiteral(":etag"),    etag);
        upsertVariant.exec();

        // Insert redirect rows for old permalinks (301 → current permalink).
        const QList<QString> &history = m_pageRepo.permalinkHistory(record.id);
        for (const QString &oldPath : std::as_const(history)) {
            QSqlQuery redirect(db);
            redirect.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO redirects (old_path, new_path, status_code)"
                " VALUES (:old_path, :new_path, 301)"));
            redirect.bindValue(QStringLiteral(":old_path"), oldPath);
            redirect.bindValue(QStringLiteral(":new_path"), record.permalink);
            redirect.exec();
        }

        db.commit();
        ++count;
    }

    {
        QSqlDatabase db = QSqlDatabase::database(connName);
        db.close();
    }
    QSqlDatabase::removeDatabase(connName);

    return count;
}
