#include "SymptomHubSyncer.h"

#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageTypeSymptomHub.h"
#include "website/pages/PageTypeSymptomIndex.h"
#include "website/pages/blocs/PageBlocSymptomLinks.h"  // SymptomNav::slugify
#include "website/taxonomy/TaxonomyDb.h"

#include <QSet>

// =============================================================================
// Constructor
// =============================================================================

SymptomHubSyncer::SymptomHubSyncer(IPageRepository &repo)
    : m_repo(repo)
{
}

// =============================================================================
// syncStubs
// =============================================================================

int SymptomHubSyncer::syncStubs(const QDir &workingDir, const QString &lang)
{
    // Use the taxonomy vocabulary (same source as PageBlocSymptomLinks).
    const QStringList symNames = TaxonomyDb(workingDir).load(QStringLiteral("symptoms"));
    if (symNames.isEmpty()) {
        return 0;
    }

    // Build set of permalinks already in pages.db to avoid duplicates.
    QSet<QString> existingPermalinks;
    const QList<PageRecord> &all = m_repo.findAll();
    existingPermalinks.reserve(all.size());
    for (const PageRecord &r : std::as_const(all)) {
        existingPermalinks.insert(r.permalink);
    }

    int created = 0;

    // Create /symptoms index page if absent.
    const QString indexPermalink = QStringLiteral("/symptoms");
    if (!existingPermalinks.contains(indexPermalink)) {
        m_repo.create(QLatin1String(PageTypeSymptomIndex::TYPE_ID), indexPermalink, lang);
        existingPermalinks.insert(indexPermalink);
        ++created;
    }

    // Create one hub stub per symptom in the taxonomy vocabulary.
    for (const QString &name : std::as_const(symNames)) {
        if (name.isEmpty()) {
            continue;
        }
        const QString slug      = SymptomNav::slugify(name);
        const QString permalink = QStringLiteral("/symptoms/") + slug;
        if (existingPermalinks.contains(permalink)) {
            continue;
        }
        m_repo.create(QLatin1String(PageTypeSymptomHub::TYPE_ID), permalink, lang);
        existingPermalinks.insert(permalink);
        ++created;
    }

    return created;
}
