#include "CategoryHubSyncer.h"

#include "website/pages/CategoryHubDirtySet.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageTypeCategory.h"
#include "website/pages/attributes/CategoryTable.h"

#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

#include <atomic>
#include <algorithm>

// =============================================================================
// Internal helpers
// =============================================================================

namespace {

QString categoryNameToSlug(const QString &name)
{
    QString slug = name.toLower();
    slug.replace(QLatin1Char(' '), QLatin1Char('-'));
    static const QRegularExpression nonSlugChars(QStringLiteral("[^a-z0-9-]"));
    slug.remove(nonSlugChars);
    static const QRegularExpression multiHyphen(QStringLiteral("-{2,}"));
    slug.replace(multiHyphen, QStringLiteral("-"));
    slug = slug.trimmed();
    if (slug.isEmpty()) {
        return QString();
    }
    return slug;
}

// Returns a permalink for a new hub page that doesn't conflict with any
// existing page in the repo.  Prefers "/<slug>"; falls back to
// "/<slug>-<id>" if the slug is already taken.
QString uniqueHubPermalink(int categoryId, const QString &name, const IPageRepository &repo)
{
    const QString slug = categoryNameToSlug(name);
    const QString preferred = QStringLiteral("/") + (slug.isEmpty()
        ? QStringLiteral("category-") + QString::number(categoryId)
        : slug);

    const QList<PageRecord> &all = repo.findAll();
    const bool taken = std::any_of(all.constBegin(), all.constEnd(),
        [&](const PageRecord &r) { return r.permalink == preferred; });

    if (!taken) {
        return preferred;
    }
    const QString base = slug.isEmpty()
        ? QStringLiteral("category")
        : slug;
    return QStringLiteral("/") + base + QStringLiteral("-")
           + QString::number(categoryId);
}

} // namespace

// =============================================================================
// Constructor
// =============================================================================

CategoryHubSyncer::CategoryHubSyncer(IPageRepository     &repo,
                                     const CategoryTable &categoryTable,
                                     CategoryHubDirtySet &dirtySet,
                                     PageGenerator       &generator)
    : m_repo(repo)
    , m_categoryTable(categoryTable)
    , m_dirtySet(dirtySet)
    , m_generator(generator)
{
}

// =============================================================================
// extractCategoryIds  (static)
// =============================================================================

QSet<int> CategoryHubSyncer::extractCategoryIds(const QHash<QString, QString> &pageData)
{
    QSet<int> result;
    for (auto it = pageData.constBegin(); it != pageData.constEnd(); ++it) {
        if (!it.key().endsWith(QLatin1String("_categories"))) {
            continue;
        }
        const QStringList parts = it.value().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &part : std::as_const(parts)) {
            bool ok = false;
            const int id = part.trimmed().toInt(&ok);
            if (ok && id > 0) {
                result.insert(id);
            }
        }
    }
    return result;
}

// =============================================================================
// hubPageIdsForCategories
// =============================================================================

QSet<int> CategoryHubSyncer::hubPageIdsForCategories(const QSet<int> &categoryIds) const
{
    if (categoryIds.isEmpty()) {
        return {};
    }
    QSet<int> result;
    const QList<PageRecord> &all = m_repo.findAll();
    for (const PageRecord &record : std::as_const(all)) {
        if (record.typeId != QLatin1String(PageTypeCategory::TYPE_ID)) {
            continue;
        }
        const QHash<QString, QString> &data = m_repo.loadData(record.id);
        const QSet<int> &hubCategories = extractCategoryIds(data);
        for (int catId : std::as_const(categoryIds)) {
            if (hubCategories.contains(catId)) {
                result.insert(record.id);
                break;
            }
        }
    }
    return result;
}

// =============================================================================
// onPageSaved
// =============================================================================

void CategoryHubSyncer::onPageSaved(const QHash<QString, QString> &pageData)
{
    const QSet<int> &categoryIds = extractCategoryIds(pageData);
    if (categoryIds.isEmpty()) {
        return;
    }
    const QSet<int> &hubIds = hubPageIdsForCategories(categoryIds);
    if (!hubIds.isEmpty()) {
        m_dirtySet.addAll(hubIds);
    }
}

// =============================================================================
// syncStubs
// =============================================================================

int CategoryHubSyncer::syncStubs(const QString &lang)
{
    // Build the set of category IDs already covered by existing hub pages.
    QSet<int> coveredCategoryIds;
    const QList<PageRecord> &all = m_repo.findAll();
    for (const PageRecord &record : std::as_const(all)) {
        if (record.typeId != QLatin1String(PageTypeCategory::TYPE_ID)) {
            continue;
        }
        const QHash<QString, QString> &data = m_repo.loadData(record.id);
        const QSet<int> &ids = extractCategoryIds(data);
        coveredCategoryIds.unite(ids);
    }

    int created = 0;
    const QList<CategoryTable::CategoryRow> &categories = m_categoryTable.categories();
    for (const CategoryTable::CategoryRow &row : std::as_const(categories)) {
        if (coveredCategoryIds.contains(row.id)) {
            continue;
        }
        const QString &permalink = uniqueHubPermalink(row.id, row.name, m_repo);
        const int id = m_repo.create(QLatin1String(PageTypeCategory::TYPE_ID), permalink, lang);
        // Pre-fill the hub-grid category selection so subsequent syncStubs() calls
        // recognise this stub as covering row.id (empty page_data would look uncovered).
        // Social metadata remains empty — the AI generation launcher fills it in.
        m_repo.saveData(id, {{QStringLiteral("0_categories"), QString::number(row.id)}});
        ++created;
    }
    return created;
}

// =============================================================================
// renderDirtyHubs
// =============================================================================

int CategoryHubSyncer::renderDirtyHubs(const QDir     &workingDir,
                                        const QString  &domain,
                                        AbstractEngine &engine,
                                        int             websiteIndex)
{
    // Snapshot the current dirty set before iterating.
    // We modify the set inside the loop (remove after each successful render).
    const QSet<int> toRender = m_dirtySet.all();
    int count = 0;

    for (int hubId : std::as_const(toRender)) {
        const auto optRecord = m_repo.findById(hubId);
        if (!optRecord) {
            // Page no longer exists — clean up without rendering.
            m_dirtySet.remove(hubId);
            continue;
        }

        // Collect source hub + all translation pages into one batch.
        QList<int> ids = {hubId};
        const QList<PageRecord> &translations = m_repo.findTranslations(hubId);
        for (const PageRecord &tr : std::as_const(translations)) {
            ids.append(tr.id);
        }

        const int rendered = m_generator.generateSubset(ids, workingDir, domain,
                                                        engine, websiteIndex);
        if (rendered > 0) {
            const QString &now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            m_repo.setGeneratedAt(hubId, now);
        }
        count += rendered;

        // Remove from dirty set AFTER render + stamp (crash-safe: file is
        // updated on disk before we move to the next hub).
        m_dirtySet.remove(hubId);
    }
    return count;
}

// =============================================================================
// markStaleByStats
// =============================================================================

int CategoryHubSyncer::markStaleByStats(const QDir &workingDir)
{
    const QString statsPath = workingDir.filePath(QStringLiteral("stats.db"));
    if (!QFile::exists(statsPath)) {
        return 0;
    }

    static std::atomic<int> s_counter{0};
    const QString connName = QStringLiteral("hub_syncer_stats_")
                             + QString::number(s_counter.fetch_add(1));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(statsPath);
        if (!db.open()) {
            QSqlDatabase::removeDatabase(connName);
            return 0;
        }
    }

    QString latestDisplayAt;
    {
        QSqlQuery q(QSqlDatabase::database(connName));
        q.exec(QStringLiteral("SELECT MAX(display_at) FROM displays_clicks"));
        if (q.next() && !q.value(0).isNull()) {
            latestDisplayAt = q.value(0).toString();
        }
    }
    {
        QSqlDatabase::database(connName).close();
    }
    QSqlDatabase::removeDatabase(connName);

    if (latestDisplayAt.isEmpty()) {
        return 0;
    }

    int count = 0;
    const QList<PageRecord> &pages = m_repo.findAll();
    for (const PageRecord &record : std::as_const(pages)) {
        if (record.typeId != QLatin1String(PageTypeCategory::TYPE_ID)) {
            continue;
        }
        if (record.generatedAt.isEmpty()) {
            continue;
        }
        if (record.generatedAt < latestDisplayAt) {
            m_dirtySet.add(record.id);
            ++count;
        }
    }
    return count;
}
