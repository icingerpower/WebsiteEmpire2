#include "PageGenerator.h"

#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PermalinkHistoryEntry.h"
#include "website/sitemap/SitemapOrchestrator.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <atomic>
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
// _writePage  (private helper)
// =============================================================================

bool PageGenerator::_writePage(AbstractPageType &type,
                                const PageRecord &record,
                                const QString    &connName,
                                const QString    &domain,
                                AbstractEngine   &engine,
                                int               websiteIndex)
{
    type.setGenerationContext(record.permalink, record.lang, record.langCodesToTranslate);

    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    type.addCode(QStringView{}, engine, websiteIndex, html, css, js, cssDoneIds, jsDoneIds);

    const QByteArray &htmlGz = gzipCompress(html.toUtf8());
    const QString    &etag   = computeEtag(htmlGz);
    const QString    &now    = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSqlDatabase db = QSqlDatabase::database(connName);
    db.transaction();

    QSqlQuery upsertPage(db);
    upsertPage.prepare(QStringLiteral(
        "INSERT INTO pages (path, domain, lang, etag, updated_at)"
        " VALUES (:path, :domain, :lang, :etag, :now)"
        " ON CONFLICT(path) DO UPDATE SET"
        "   domain=excluded.domain, lang=excluded.lang,"
        "   etag=excluded.etag, updated_at=excluded.updated_at"));
    upsertPage.bindValue(QStringLiteral(":path"),   record.permalink);
    upsertPage.bindValue(QStringLiteral(":domain"), domain);
    upsertPage.bindValue(QStringLiteral(":lang"),   engine.getLangCode(websiteIndex));
    upsertPage.bindValue(QStringLiteral(":etag"),   etag);
    upsertPage.bindValue(QStringLiteral(":now"),    now);
    upsertPage.exec();

    QSqlQuery idQ(db);
    idQ.prepare(QStringLiteral("SELECT id FROM pages WHERE path = :path"));
    idQ.bindValue(QStringLiteral(":path"), record.permalink);
    idQ.exec();
    idQ.next();
    const int contentPageId = idQ.value(0).toInt();

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

    const QList<PermalinkHistoryEntry> &history = m_pageRepo.permalinkHistory(record.id);
    for (const PermalinkHistoryEntry &entry : std::as_const(history)) {
        if (entry.redirectType == QStringLiteral("none")) {
            continue; // no redirect row emitted; old URL yields 404
        }
        QSqlQuery redirect(db);
        if (entry.redirectType == QStringLiteral("deleted")) {
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
    return true;
}

// =============================================================================
// generateAll
// =============================================================================

int PageGenerator::generateAll(const QDir     &workingDir,
                               const QString  &domain,
                               AbstractEngine &engine,
                               int             websiteIndex)
{
    return generateAll(workingDir, workingDir, domain, engine, websiteIndex);
}

int PageGenerator::generateAll(const QDir     &workingDir,
                               const QDir     &outputDir,
                               const QString  &domain,
                               AbstractEngine &engine,
                               int             websiteIndex)
{
    static std::atomic<int> s_counter{0};
    const QString connName = QStringLiteral("page_generator_all_")
                             + QString::number(s_counter.fetch_add(1));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(outputDir.filePath(QLatin1StringView(FILENAME)));
        db.open();
    }
    ensureSchema(connName);

    int count = 0;
    const QList<PageRecord> &pages = m_pageRepo.findAll();
    const QString &currentLang = engine.getLangCode(websiteIndex);

    // Pre-pass: build available-pages index and translated-permalink map.
    {
        // Collect every category ID covered by at least one article so we can
        // exclude hub pages whose category has no articles from availablePages.
        // That prevents PageBlocCategoryLinks from linking to empty hub pages.
        QSet<int> articleCatIds;
        for (const PageRecord &r : std::as_const(pages)) {
            if (r.typeId != QStringLiteral("article")) {
                continue;
            }
            const QHash<QString, QString> &data = m_pageRepo.loadData(r.id);
            const auto &catStr = data.value(QStringLiteral("0_categories"));
            for (const QString &part : catStr.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                bool ok = false;
                const int id = part.trimmed().toInt(&ok);
                if (ok && id > 0) {
                    articleCatIds.insert(id);
                }
            }
        }

        QHash<QString, QSet<QString>>            availablePages;
        QHash<QString, QHash<QString, QString>>  translatedPermalinks;

        for (const PageRecord &r : std::as_const(pages)) {
            // Skip hub pages whose category has no articles — they would render
            // empty and should not appear as link targets.
            if (r.typeId == QStringLiteral("category_hub")) {
                const QHash<QString, QString> &data = m_pageRepo.loadData(r.id);
                const auto &catStr = data.value(QStringLiteral("0_categories"));
                bool hasArticles = false;
                for (const QString &part : catStr.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                    bool ok = false;
                    const int id = part.trimmed().toInt(&ok);
                    if (ok && articleCatIds.contains(id)) {
                        hasArticles = true;
                        break;
                    }
                }
                if (!hasArticles) {
                    continue;
                }
            }

            if (r.langCodesToTranslate.isEmpty()) {
                availablePages[currentLang].insert(r.permalink);
            } else {
                availablePages[r.lang].insert(r.permalink);
                for (const QString &lang : std::as_const(r.langCodesToTranslate)) {
                    availablePages[lang].insert(r.permalink);
                }
            }

            if (!r.endPermalink.isEmpty() && !r.langCodesToTranslate.isEmpty()) {
                const QHash<QString, QString> &data = m_pageRepo.loadData(r.id);
                for (const QString &lang : std::as_const(r.langCodesToTranslate)) {
                    const QString trSlugKey = QStringLiteral("tr:") + lang
                                             + QStringLiteral(":_permalink_slug");
                    const QString &trSlug = data.value(trSlugKey);
                    if (!trSlug.isEmpty()) {
                        translatedPermalinks[lang][r.permalink] =
                            QLatin1Char('/') + trSlug;
                    }
                }
            }
        }

        engine.setAvailablePages(availablePages);
        engine.setTranslatedPermalinks(translatedPermalinks);
    }

    for (const PageRecord &record : std::as_const(pages)) {
        const bool hasExplicitTargets = !record.langCodesToTranslate.isEmpty();
        const bool isSourceLang       = (record.lang == currentLang);
        const bool isTargetLang       = record.langCodesToTranslate.contains(currentLang);

        if (hasExplicitTargets && !isSourceLang && !isTargetLang) {
            continue;
        }

        // Skip pages excluded from availablePages (e.g. empty hub pages).
        if (!engine.isPageAvailable(record.permalink, websiteIndex)) {
            continue;
        }

        auto type = AbstractPageType::createForTypeId(record.typeId, m_categoryTable);
        if (!type) {
            continue;
        }

        const QHash<QString, QString> &data = m_pageRepo.loadData(record.id);
        type->load(data);
        type->setAuthorLang(record.lang);
        type->bindGenerationContext(m_pageRepo, workingDir);

        // Skip pages whose translation is incomplete for the current language.
        // Partial deploys are valid — only fully-translated pages are published.
        if (hasExplicitTargets && isTargetLang
                && !type->isTranslationComplete(QStringView{}, currentLang)) {
            continue;
        }

        // For translated pages of strategies that carry an endPermalink suffix,
        // use the AI-translated slug stored in page data as the output path.
        // Falls back to the source permalink when not set.
        PageRecord effectiveRecord = record;
        if (isTargetLang && !record.endPermalink.isEmpty()) {
            const QString trSlugKey = QStringLiteral("tr:")
                                      + currentLang
                                      + QStringLiteral(":_permalink_slug");
            const QString &trSlug = data.value(trSlugKey);
            if (!trSlug.isEmpty()) {
                effectiveRecord.permalink = QLatin1Char('/') + trSlug;
            }
        }

        if (_writePage(*type, effectiveRecord, connName, domain, engine, websiteIndex)) {
            ++count;
        }
    }

    if (!domain.isEmpty()) {
        SitemapOrchestrator::generate(
            connName, domain, QStringLiteral("https://") + domain);
    }

    {
        QSqlDatabase db = QSqlDatabase::database(connName);
        db.close();
    }
    QSqlDatabase::removeDatabase(connName);

    return count;
}

// =============================================================================
// generateSubset
// =============================================================================

int PageGenerator::generateSubset(const QList<int> &pageIds,
                                   const QDir       &workingDir,
                                   const QString    &domain,
                                   AbstractEngine   &engine,
                                   int               websiteIndex)
{
    if (pageIds.isEmpty()) {
        return 0;
    }

    static std::atomic<int> s_counter{0};
    const QString connName = QStringLiteral("page_generator_subset_")
                             + QString::number(s_counter.fetch_add(1));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(workingDir.filePath(QLatin1StringView(FILENAME)));
        db.open();
    }
    ensureSchema(connName);

    int count = 0;
    const QString &currentLang = engine.getLangCode(websiteIndex);

    for (int id : std::as_const(pageIds)) {
        const auto optRecord = m_pageRepo.findById(id);
        if (!optRecord) {
            continue;
        }
        const PageRecord &record = *optRecord;

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
        type->bindGenerationContext(m_pageRepo, workingDir);

        // Skip silently on incomplete translation — subset renders are best-effort
        // re-renders, not a CI gate.
        if (hasExplicitTargets && isTargetLang
                && !type->isTranslationComplete(QStringView{}, currentLang)) {
            continue;
        }

        PageRecord effectiveRecord = record;
        if (isTargetLang && !record.endPermalink.isEmpty()) {
            const QString trSlugKey = QStringLiteral("tr:")
                                      + currentLang
                                      + QStringLiteral(":_permalink_slug");
            const QString &trSlug = data.value(trSlugKey);
            if (!trSlug.isEmpty()) {
                effectiveRecord.permalink = QLatin1Char('/') + trSlug;
            }
        }

        if (_writePage(*type, effectiveRecord, connName, domain, engine, websiteIndex)) {
            ++count;
        }
    }

    {
        QSqlDatabase db = QSqlDatabase::database(connName);
        db.close();
    }
    QSqlDatabase::removeDatabase(connName);

    return count;
}
