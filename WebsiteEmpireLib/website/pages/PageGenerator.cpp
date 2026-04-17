#include "PageGenerator.h"

#include "ExceptionWithTitleText.h"
#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PermalinkHistoryEntry.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
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
    const QString &currentLang = engine.getLangCode(websiteIndex);

    // Build available-pages index: lang code → set of permalinks that will be
    // generated.  A page is available in language L when:
    //   • langCodesToTranslate is non-empty (assessed) AND L equals record.lang
    //     or L is in langCodesToTranslate, OR
    //   • langCodesToTranslate is empty (not yet assessed) — the page is treated
    //     as available in the current generation language for backward compat.
    // This map is used by blocs (e.g. PageBlocCategoryArticles) to filter links
    // so only reachable pages are listed.
    {
        QHash<QString, QSet<QString>> availablePages;
        for (const PageRecord &r : std::as_const(pages)) {
            if (r.langCodesToTranslate.isEmpty()) {
                // Not yet assessed: treated as available in the current language.
                availablePages[currentLang].insert(r.permalink);
            } else {
                availablePages[r.lang].insert(r.permalink);
                for (const QString &lang : std::as_const(r.langCodesToTranslate)) {
                    availablePages[lang].insert(r.permalink);
                }
            }
        }
        engine.setAvailablePages(availablePages);
    }

    for (const PageRecord &record : std::as_const(pages)) {
        // Skip pages not intended for this language.
        // Only apply the language filter when langCodesToTranslate is explicitly set.
        // Pages with an empty list have not been assessed — generate them for any
        // language (backward-compatible behaviour preserving pre-assessment state).
        const bool hasExplicitTargets = !record.langCodesToTranslate.isEmpty();
        const bool isSourceLang       = (record.lang == currentLang);
        const bool isTargetLang       = record.langCodesToTranslate.contains(currentLang);

        if (hasExplicitTargets && !isSourceLang && !isTargetLang) {
            continue;
        }

        auto type = AbstractPageType::createForTypeId(record.typeId, m_categoryTable);
        if (!type) {
            continue;
        }

        const QHash<QString, QString> &data = m_pageRepo.loadData(record.id);
        type->load(data);
        type->setAuthorLang(record.lang);

        // Guard: only when translation was explicitly requested for this language
        // and is incomplete.  An incomplete translation is a workflow bug — raise
        // so it is caught during CI rather than silently generating source text.
        if (hasExplicitTargets && isTargetLang
                && !type->isTranslationComplete(QStringView{}, currentLang)) {
            ExceptionWithTitleText ex(
                QCoreApplication::translate("PageGenerator", "Incomplete Translation"),
                QCoreApplication::translate("PageGenerator",
                    "Page '%1' has not been fully translated into '%2'. "
                    "Run the translator before generating.")
                    .arg(record.permalink, currentLang));
            ex.raise();
        }

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

        // Insert redirect rows for old permalinks — status depends on redirectType.
        const QList<PermalinkHistoryEntry> &history = m_pageRepo.permalinkHistory(record.id);
        for (const PermalinkHistoryEntry &entry : std::as_const(history)) {
            QSqlQuery redirect(db);
            if (entry.redirectType == QStringLiteral("deleted")) {
                // 410 Gone — no forwarding destination
                redirect.prepare(QStringLiteral(
                    "INSERT OR IGNORE INTO redirects (old_path, new_path, status_code)"
                    " VALUES (:old_path, NULL, 410)"));
                redirect.bindValue(QStringLiteral(":old_path"), entry.permalink);
            } else {
                const int code = (entry.redirectType == QStringLiteral("parked")) ? 302 : 301;
                redirect.prepare(QStringLiteral(
                    "INSERT OR IGNORE INTO redirects (old_path, new_path, status_code)"
                    " VALUES (:old_path, :new_path, :code)"));
                redirect.bindValue(QStringLiteral(":old_path"), entry.permalink);
                redirect.bindValue(QStringLiteral(":new_path"), record.permalink);
                redirect.bindValue(QStringLiteral(":code"),     code);
            }
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
