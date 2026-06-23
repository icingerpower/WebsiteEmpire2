#ifndef TAXONOMYINDEXSYNCER_H
#define TAXONOMYINDEXSYNCER_H

#include <QString>

class IPageRepository;

/**
 * Creates and maintains stub pages for the taxonomy index
 * (type_id "taxonomy_index").
 *
 * syncStubs() counts all pages (pending + generated) from the types listed in
 * PageTypeTaxonomyIndex::aggregatedTypeIds() and decides the layout:
 *
 *   ≤ PAGINATION_THRESHOLD total entries:
 *     — ensures a single stub at /categories (renders all entries)
 *
 *   > PAGINATION_THRESHOLD total entries:
 *     — ensures the default stub at /categories (renders letter A)
 *     — ensures one stub per letter B–Z that has at least one entry
 *       (e.g. /categories/b, /categories/c, …)
 *     — letter A is always served by the base /categories stub
 *
 * Always idempotent: existing permalinks are never duplicated.
 * Does not remove stubs; obsolete letter pages render an empty grid harmlessly.
 *
 * Stubs are created with empty page_data so the AI generation launcher fills
 * in the SEO and social metadata.  The grid content is rendered at generation
 * time from the live repository — no stub data needed.
 *
 * Call sites: PaneGeneratedPages::syncStubs(), LauncherPublish.
 */
class TaxonomyIndexSyncer
{
public:
    static constexpr int PAGINATION_THRESHOLD = 100;

    explicit TaxonomyIndexSyncer(IPageRepository &repo);

    /**
     * Creates any missing taxonomy_index stubs for the current layout.
     * Returns the number of stubs created (0 when everything is up to date).
     */
    int syncStubs(const QString &sourceLang);

private:
    IPageRepository &m_repo;
};

#endif // TAXONOMYINDEXSYNCER_H
