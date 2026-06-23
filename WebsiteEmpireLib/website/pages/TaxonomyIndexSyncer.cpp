#include "TaxonomyIndexSyncer.h"

#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageTypeTaxonomyIndex.h"

#include <QChar>
#include <QSet>

// =============================================================================
// Constructor
// =============================================================================

TaxonomyIndexSyncer::TaxonomyIndexSyncer(IPageRepository &repo)
    : m_repo(repo)
{
}

// =============================================================================
// syncStubs
// =============================================================================

int TaxonomyIndexSyncer::syncStubs(const QString &sourceLang)
{
    const QStringList sourceTypes = PageTypeTaxonomyIndex::aggregatedTypeIds();

    // -----------------------------------------------------------------------
    // 1. Collect source pages (pending + generated) to compute count and letters
    // -----------------------------------------------------------------------
    QList<PageRecord> sourcePages;
    for (const QString &typeId : std::as_const(sourceTypes)) {
        const QList<PageRecord> generated = m_repo.findGeneratedByTypeId(typeId);
        const QList<PageRecord> pending   = m_repo.findPendingByTypeId(typeId);
        sourcePages.reserve(sourcePages.size() + generated.size() + pending.size());
        sourcePages += generated;
        sourcePages += pending;
    }
    const int total = sourcePages.size();

    // -----------------------------------------------------------------------
    // 2. Build the set of all existing permalinks to avoid duplicates
    // -----------------------------------------------------------------------
    QSet<QString> existing;
    const QList<PageRecord> all = m_repo.findAll();
    existing.reserve(all.size());
    for (const PageRecord &r : std::as_const(all)) {
        existing.insert(r.permalink);
    }

    int created = 0;
    const QString base   = QLatin1String(PageTypeTaxonomyIndex::BASE_PERMALINK);
    const QString typeId = QLatin1String(PageTypeTaxonomyIndex::TYPE_ID);

    // -----------------------------------------------------------------------
    // 3. Always ensure the base stub (/categories)
    // -----------------------------------------------------------------------
    if (!existing.contains(base)) {
        m_repo.create(typeId, base, sourceLang);
        existing.insert(base);
        ++created;
    }

    // -----------------------------------------------------------------------
    // 4. If paginated, ensure one stub per letter B–Z that has entries.
    //    Letter A is served by the base stub at /categories.
    // -----------------------------------------------------------------------
    if (total > PAGINATION_THRESHOLD) {
        QSet<QChar> lettersPresent;
        for (const PageRecord &p : std::as_const(sourcePages)) {
            // Extract the last path segment and strip any .html extension
            QString seg = p.permalink.section(QLatin1Char('/'), -1, -1);
            if (seg.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)) {
                seg.chop(5);
            }
            if (!seg.isEmpty()) {
                const QChar first = seg.at(0).toLower();
                // 'a' is handled by the base stub; skip it here
                if (first.isLetter() && first != QLatin1Char('a')) {
                    lettersPresent.insert(first);
                }
            }
        }

        for (const QChar &letter : std::as_const(lettersPresent)) {
            const QString permalink = base + QLatin1Char('/') + letter;
            if (!existing.contains(permalink)) {
                m_repo.create(typeId, permalink, sourceLang);
                existing.insert(permalink);
                ++created;
            }
        }
    }

    return created;
}
