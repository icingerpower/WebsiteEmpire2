#include "PageGenerator.h"

#include "ExceptionWithTitleText.h"
#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PermalinkHistoryEntry.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/sitemap/SitemapOrchestrator.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <atomic>
#include <zlib.h>

// =============================================================================
// Static helpers
// =============================================================================

// Derives a hub page permalink from a category name — same logic as
// _categoryPermalink() in the link-builder blocs so both sides agree.
// NFD decomposition maps accented letters to their ASCII base (é→e, ü→u, etc.)
// so translated names like "Santé mentale" slug correctly to "sante-mentale".
QString PageGenerator::categoryHubSlug(const QString &name)
{
    static const QRegularExpression s_nonAlnum(QStringLiteral("[^a-z0-9]+"));
    static const QRegularExpression s_combining(QStringLiteral("[\\x{0300}-\\x{036F}]"));
    QString slug = name.toLower().normalized(QString::NormalizationForm_D);
    slug.remove(s_combining);
    slug.replace(s_nonAlnum, QStringLiteral("-"));
    while (slug.startsWith(QLatin1Char('-'))) { slug.remove(0, 1); }
    while (slug.endsWith(QLatin1Char('-'))) { slug.chop(1); }
    if (slug.isEmpty()) { return {}; }
    return QStringLiteral("/") + slug + QStringLiteral(".html");
}

// =============================================================================
// Constructor
// =============================================================================

PageGenerator::PageGenerator(IPageRepository &pageRepo, CategoryTable &categoryTable)
    : m_pageRepo(pageRepo)
    , m_categoryTable(categoryTable)
{
}

void PageGenerator::setWebsiteContext(const QString &websiteName, const QString &author)
{
    m_websiteName   = websiteName;
    m_websiteAuthor = author;
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
    QHash<QString, QString> publishedByLang;
    QHash<QString, QString> updatedByLang;
    publishedByLang[record.lang] = record.createdAt;
    updatedByLang[record.lang]   = record.updatedAt;
    if (record.id > 0) {
        for (const PageRecord &tr : m_pageRepo.findTranslations(record.id)) {
            publishedByLang[tr.lang] = tr.createdAt;
            updatedByLang[tr.lang]   = tr.updatedAt;
        }
    }
    type.setGenerationContext(record.permalink, record.lang, record.langCodesToTranslate,
                              publishedByLang, updatedByLang);
    type.setWebsiteContext(m_websiteName, m_websiteAuthor);
    type.prepareJsonLdImage(m_workingDir, domain);

    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    try {
        type.addCode(QStringView{}, engine, websiteIndex, html, css, js, cssDoneIds, jsDoneIds);
    } catch (ExceptionWithTitleText &ex) {
        const QString lang = engine.getLangCode(websiteIndex);
        ExceptionWithTitleText enriched(
            ex.errorTitle(),
            record.permalink + QStringLiteral(" [") + lang + QStringLiteral("]\n\n") + ex.errorText());
        enriched.raise();
    }

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
                               int             websiteIndex,
                               const QString  &sitemapBaseUrl)
{
    m_workingDir = workingDir;
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
        // Scan all *_categories keys (breadcrumb + cross-reference blocs).
        //
        // Also build translatedCatIds[lang]: category IDs that have at least one
        // article translated to lang. Used to gate category hub availability per lang.
        QSet<int> articleCatIds;
        QHash<QString, QSet<int>> translatedCatIds;

        for (const PageRecord &r : std::as_const(pages)) {
            if (r.typeId != QStringLiteral("article")) {
                continue;
            }
            const QHash<QString, QString> &data = m_pageRepo.loadData(r.id);

            QSet<int> artCatIds;
            for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
                if (!it.key().endsWith(QStringLiteral("_categories"))) {
                    continue;
                }
                for (const QString &part : it.value().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                    bool ok = false;
                    const int id = part.trimmed().toInt(&ok);
                    if (ok && id > 0) {
                        artCatIds.insert(id);
                        articleCatIds.insert(id);
                    }
                }
            }

            // For each target language that has translation data for this article,
            // record its categories as "translated for that language".
            if (!r.langCodesToTranslate.isEmpty() && !artCatIds.isEmpty()) {
                for (const QString &lang : std::as_const(r.langCodesToTranslate)) {
                    const QString marker = QStringLiteral("_tr:") + lang + QLatin1Char(':');
                    bool hasData = false;
                    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
                        if (it.key().contains(marker)) {
                            hasData = true;
                            break;
                        }
                    }
                    if (hasData) {
                        for (const int catId : std::as_const(artCatIds)) {
                            translatedCatIds[lang].insert(catId);
                        }
                    }
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

            availablePages[r.lang].insert(r.permalink);
            if (!r.langCodesToTranslate.isEmpty()) {
                if (r.typeId == QStringLiteral("category_hub")) {
                    // Hub pages render in any language via addCode — but only publish
                    // for a language when at least one article in this hub's category
                    // has been translated to that language.
                    const QHash<QString, QString> &hData = m_pageRepo.loadData(r.id);
                    const auto &hCatStr = hData.value(QStringLiteral("0_categories"));
                    for (const QString &lang : std::as_const(r.langCodesToTranslate)) {
                        const QSet<int> &tCats = translatedCatIds.value(lang);
                        if (tCats.isEmpty()) {
                            continue;
                        }
                        for (const QString &part : hCatStr.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                            bool ok = false;
                            const int id = part.trimmed().toInt(&ok);
                            if (ok && id > 0 && tCats.contains(id)) {
                                availablePages[lang].insert(r.permalink);
                                break;
                            }
                        }
                    }
                } else {
                    // Non-hub pages: only mark a target language available when inline
                    // translation data actually exists. Pages assessed for translation but
                    // not yet translated are excluded so hreflang never points to missing pages.
                    const QHash<QString, QString> &data = m_pageRepo.loadData(r.id);
                    for (const QString &lang : std::as_const(r.langCodesToTranslate)) {
                        const QString marker = QStringLiteral("_tr:") + lang + QLatin1Char(':');
                        for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
                            if (it.key().contains(marker)) {
                                availablePages[lang].insert(r.permalink);
                                break;
                            }
                        }
                    }
                }
            }

            // Hub pages: add translated permalink for EVERY language that translates
            // this hub (not just currentLang), so hreflang alternate links are correct
            // regardless of which language is being generated in this run.
            // Legacy hubs with an empty langCodesToTranslate fall back to currentLang only.
            if (r.typeId == QStringLiteral("category_hub")) {
                const QStringList &hubLangs = r.langCodesToTranslate.isEmpty()
                    ? QStringList{currentLang}
                    : r.langCodesToTranslate;
                const QHash<QString, QString> &hubData = m_pageRepo.loadData(r.id);
                const QString &catStr = hubData.value(QStringLiteral("0_categories"));
                for (const QString &part : catStr.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                    bool ok = false;
                    const int catId = part.trimmed().toInt(&ok);
                    if (!ok || catId <= 0) { continue; }
                    const CategoryTable::CategoryRow *catRow = m_categoryTable.categoryById(catId);
                    if (!catRow) { continue; }
                    for (const QString &lang : std::as_const(hubLangs)) {
                        const QString translatedName = m_categoryTable.translationFor(catId, lang);
                        if (translatedName != catRow->name) {
                            const QString trPermalink = categoryHubSlug(translatedName);
                            if (!trPermalink.isEmpty() && trPermalink != r.permalink) {
                                translatedPermalinks[lang][r.permalink] = trPermalink;
                                availablePages[lang].insert(trPermalink);
                            }
                        }
                    }
                    break; // use first category only
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
        // Assessed target-lang pages bypass this so the translation-completeness
        // check below can throw explicitly rather than silently skip.
        if (!isTargetLang && !engine.isPageAvailable(record.permalink, websiteIndex)) {
            continue;
        }
        // Category hubs bypass the target-lang check above (they have no inline
        // translation data), but must still be gated by isPageAvailable: the
        // pre-pass only marks a hub available for a language when at least one
        // article in its category has been translated to that language.
        if (record.typeId == QStringLiteral("category_hub")
                && !engine.isPageAvailable(record.permalink, websiteIndex)) {
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

        // Skip pages whose translation is incomplete — only publish fully-translated
        // pages; they are excluded from hreflang and links by the availablePages map.
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
        // Hub pages: generate at translated URL when the category has a
        // translation that differs from its English name.
        if (record.typeId == QStringLiteral("category_hub")) {
            const QString &catStr = data.value(QStringLiteral("0_categories"));
            for (const QString &part : catStr.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                bool ok = false;
                const int catId = part.trimmed().toInt(&ok);
                if (!ok || catId <= 0) { continue; }
                const CategoryTable::CategoryRow *catRow = m_categoryTable.categoryById(catId);
                if (!catRow) { continue; }
                const QString translatedName = m_categoryTable.translationFor(catId, currentLang);
                if (translatedName != catRow->name) {
                    const QString trPermalink = categoryHubSlug(translatedName);
                    if (!trPermalink.isEmpty()) {
                        effectiveRecord.permalink = trPermalink;
                    }
                }
                break;
            }
        }

        if (_writePage(*type, effectiveRecord, connName, domain, engine, websiteIndex)) {
            ++count;
            // Stamp generated_at so findGeneratedByTypeId() can see this page
            // as "content-filled" (used by category/symptom index blocs).
            // Only stamp source pages (translations have sourcePageId > 0).
            if (record.id > 0 && record.sourcePageId == 0) {
                m_pageRepo.setGeneratedAt(record.id,
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            }
        }
    }

    if (!domain.isEmpty()) {
        const QString &effectiveSitemapBase = sitemapBaseUrl.isEmpty()
            ? QStringLiteral("https://") + domain
            : sitemapBaseUrl;
        SitemapOrchestrator::generate(connName, domain, effectiveSitemapBase);
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

    m_workingDir = workingDir;
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
        // Hub pages: generate at translated URL when the category has a
        // translation that differs from its English name.
        if (record.typeId == QStringLiteral("category_hub")) {
            const QString &catStr = data.value(QStringLiteral("0_categories"));
            for (const QString &part : catStr.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                bool ok = false;
                const int catId = part.trimmed().toInt(&ok);
                if (!ok || catId <= 0) { continue; }
                const CategoryTable::CategoryRow *catRow = m_categoryTable.categoryById(catId);
                if (!catRow) { continue; }
                const QString translatedName = m_categoryTable.translationFor(catId, currentLang);
                if (translatedName != catRow->name) {
                    const QString trPermalink = categoryHubSlug(translatedName);
                    if (!trPermalink.isEmpty()) {
                        effectiveRecord.permalink = trPermalink;
                    }
                }
                break;
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
