#include "FashionTaxonomyHubSyncer.h"

#include "website/pages/FashionHubDirtySet.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageTypeFashionTagHub.h"
#include "website/pages/blocs/PageBlocFashionHubGrid.h"
#include "website/pages/blocs/PageBlocFashionTaxonomyLinks.h"
#include "website/taxonomy/TaxonomyDb.h"

#include <QDateTime>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

#include <atomic>
#include <algorithm>

namespace {

// PageBlocFashionHubGrid is always bloc index 0 in PageTypeFashionTagHub
// (see that class's constructor), so its raw page_data keys are "0_dimension"
// / "0_tag_value" once AbstractPageType::save() prefixes them — mirrors
// CategoryHubSyncer.cpp's hardcoded "0_categories" for the same reason
// (PageBlocHubGrid is bloc index 0 in PageTypeCategory).
const QString kDimensionKey = QStringLiteral("0_") + QLatin1String(PageBlocFashionHubGrid::KEY_DIMENSION);
const QString kTagValueKey  = QStringLiteral("0_") + QLatin1String(PageBlocFashionHubGrid::KEY_TAG_VALUE);

// Returns a permalink for a new hub page that doesn't conflict with any
// existing page in the repo. Prefers "<hubPrefix><slug>"; falls back to
// "<hubPrefix><slug>-2", "-3", ... if taken. Mirrors
// CategoryHubSyncer's uniqueHubPermalink() shape.
QString uniqueFashionHubPermalink(const QString &hubPrefix, const QString &tagValue,
                                  const IPageRepository &repo)
{
    const QString slug = PageBlocFashionTaxonomyLinks::slugify(tagValue);
    const QString base = hubPrefix + (slug.isEmpty() ? QStringLiteral("tag") : slug);

    const QList<PageRecord> &all = repo.findAll();
    const auto taken = [&](const QString &candidate) {
        return std::any_of(all.constBegin(), all.constEnd(),
            [&](const PageRecord &r) { return r.permalink == candidate; });
    };

    if (!taken(base)) {
        return base;
    }
    for (int suffix = 2; suffix < 1000; ++suffix) {
        const QString candidate = base + QStringLiteral("-") + QString::number(suffix);
        if (!taken(candidate)) {
            return candidate;
        }
    }
    return base; // pathological case — give up de-duplicating past 999 collisions
}

} // namespace

// =============================================================================
// Constructor
// =============================================================================

FashionTaxonomyHubSyncer::FashionTaxonomyHubSyncer(IPageRepository     &repo,
                                                   const QDir          &workingDir,
                                                   FashionHubDirtySet  &dirtySet,
                                                   PageGenerator       &generator)
    : m_repo(repo)
    , m_workingDir(workingDir)
    , m_dirtySet(dirtySet)
    , m_generator(generator)
{
}

// =============================================================================
// extractFashionTags  (static)
// =============================================================================

QHash<QString, QStringList> FashionTaxonomyHubSyncer::extractFashionTags(
    const QHash<QString, QString> &pageData)
{
    // PageBlocFashionTaxonomyLinks' storage key for each dimension is
    // prefixed with its bloc index by AbstractPageType::save() (e.g.
    // "7_fashion_color") — match by suffix, merging across every matching
    // key, the same tolerant pattern CategoryHubSyncer::extractCategoryIds()
    // uses for "*_categories" so the bloc index never needs to be hardcoded.
    QHash<QString, QStringList> result;
    for (const auto &dim : PageBlocFashionTaxonomyLinks::dimensions()) {
        const QString suffix = QLatin1Char('_') + dim.taxonomyId;
        QStringList names;
        for (auto it = pageData.constBegin(); it != pageData.constEnd(); ++it) {
            if (!it.key().endsWith(suffix)) {
                continue;
            }
            const QStringList parts = it.value().split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &p : parts) {
                const QString name = p.trimmed();
                if (!name.isEmpty() && !names.contains(name)) {
                    names.append(name);
                }
            }
        }
        if (!names.isEmpty()) {
            result.insert(dim.taxonomyId, names);
        }
    }
    return result;
}

// =============================================================================
// hubPageIdFor
// =============================================================================

int FashionTaxonomyHubSyncer::hubPageIdFor(const QString &dimension, const QString &tagValue) const
{
    const QList<PageRecord> &all = m_repo.findAll();
    for (const PageRecord &record : std::as_const(all)) {
        if (record.typeId != QLatin1String(PageTypeFashionTagHub::TYPE_ID)) {
            continue;
        }
        const QHash<QString, QString> &data = m_repo.loadData(record.id);
        if (data.value(kDimensionKey) == dimension
                && data.value(kTagValueKey) == tagValue) {
            return record.id;
        }
    }
    return -1;
}

// =============================================================================
// onPageSaved
// =============================================================================

void FashionTaxonomyHubSyncer::onPageSaved(const QHash<QString, QString> &pageData)
{
    const QHash<QString, QStringList> &tagsByDimension = extractFashionTags(pageData);
    if (tagsByDimension.isEmpty()) {
        return;
    }

    QSet<int> hubIds;
    for (auto it = tagsByDimension.constBegin(); it != tagsByDimension.constEnd(); ++it) {
        const QString &dimension = it.key();
        for (const QString &tagValue : it.value()) {
            int hubId = hubPageIdFor(dimension, tagValue);
            if (hubId < 0) {
                // Brand-new tag value — create its hub stub on the fly, same
                // as CategoryHubSyncer does for a brand-new category.
                const QList<PageBlocFashionTaxonomyLinks::Dimension> &dims =
                    PageBlocFashionTaxonomyLinks::dimensions();
                const auto dimIt = std::find_if(dims.constBegin(), dims.constEnd(),
                    [&](const auto &d) { return d.taxonomyId == dimension; });
                if (dimIt == dims.constEnd()) {
                    continue; // unknown dimension id — ignore defensively
                }
                const QString &permalink = uniqueFashionHubPermalink(dimIt->hubPrefix, tagValue, m_repo);
                hubId = m_repo.create(QLatin1String(PageTypeFashionTagHub::TYPE_ID),
                                      permalink, QStringLiteral("en"));
                m_repo.saveData(hubId, {
                    {kDimensionKey, dimension},
                    {kTagValueKey, tagValue},
                });
            }
            hubIds.insert(hubId);
        }
    }
    if (!hubIds.isEmpty()) {
        m_dirtySet.addAll(hubIds);
    }
}

// =============================================================================
// syncStubs
// =============================================================================

int FashionTaxonomyHubSyncer::syncStubs(const QString &lang)
{
    TaxonomyDb taxDb(m_workingDir);

    // Build the set of (dimension, tagValue) pairs already covered by existing hub pages.
    QSet<QString> covered; // "dimension\x1f tagValue"
    const QList<PageRecord> &all = m_repo.findAll();
    for (const PageRecord &record : std::as_const(all)) {
        if (record.typeId != QLatin1String(PageTypeFashionTagHub::TYPE_ID)) {
            continue;
        }
        const QHash<QString, QString> &data = m_repo.loadData(record.id);
        const QString &dimension = data.value(kDimensionKey);
        const QString &tagValue  = data.value(kTagValueKey);
        if (!dimension.isEmpty() && !tagValue.isEmpty()) {
            covered.insert(dimension + QChar(0x1f) + tagValue);
        }
    }

    int created = 0;
    for (const auto &dim : PageBlocFashionTaxonomyLinks::dimensions()) {
        const QStringList &vocabulary = taxDb.load(dim.taxonomyId);
        for (const QString &tagValue : vocabulary) {
            if (covered.contains(dim.taxonomyId + QChar(0x1f) + tagValue)) {
                continue;
            }
            const QString &permalink = uniqueFashionHubPermalink(dim.hubPrefix, tagValue, m_repo);
            const int id = m_repo.create(QLatin1String(PageTypeFashionTagHub::TYPE_ID),
                                         permalink, lang);
            // Pre-fill dimension/tag_value so subsequent syncStubs() calls
            // recognise this stub as covering the pair (empty page_data
            // would look uncovered). Social metadata remains empty — the AI
            // generation launcher fills it in.
            m_repo.saveData(id, {
                {kDimensionKey, dim.taxonomyId},
                {kTagValueKey, tagValue},
            });
            ++created;
        }
    }
    return created;
}

// =============================================================================
// renderDirtyHubs
// =============================================================================

int FashionTaxonomyHubSyncer::renderDirtyHubs(const QDir     &workingDir,
                                              const QString  &domain,
                                              AbstractEngine &engine,
                                              int             websiteIndex)
{
    const QSet<int> toRender = m_dirtySet.all();
    int count = 0;

    for (int hubId : std::as_const(toRender)) {
        const auto optRecord = m_repo.findById(hubId);
        if (!optRecord) {
            m_dirtySet.remove(hubId);
            continue;
        }

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

int FashionTaxonomyHubSyncer::markStaleByStats(const QDir &workingDir)
{
    const QString statsPath = workingDir.filePath(QStringLiteral("stats.db"));
    if (!QFile::exists(statsPath)) {
        return 0;
    }

    static std::atomic<int> s_counter{0};
    const QString connName = QStringLiteral("fashion_hub_syncer_stats_")
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
        if (record.typeId != QLatin1String(PageTypeFashionTagHub::TYPE_ID)) {
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
