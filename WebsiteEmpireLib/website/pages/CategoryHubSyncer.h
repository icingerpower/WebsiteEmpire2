#ifndef CATEGORYHUBSYNCER_H
#define CATEGORYHUBSYNCER_H

#include <QDir>
#include <QHash>
#include <QSet>
#include <QString>

class AbstractEngine;
class CategoryHubDirtySet;
class CategoryTable;
class IPageRepository;
class PageGenerator;

/**
 * Orchestrates the three triggers that keep category-hub pages fresh.
 *
 * Trigger 1 — article edited / created (no AI, dirty-mark only):
 *   Call onPageSaved() after any article page_data is written.  The syncer
 *   reads the saved category IDs, finds every hub page that covers those
 *   categories, and adds those hub page IDs to the CategoryHubDirtySet.
 *   Call renderDirtyHubs() at the end of the edit session (or immediately
 *   after save) to re-render only the affected hubs.
 *   Only call onPageSaved() for article/content pages, not for hub pages
 *   themselves — a hub's "0_categories" key names the articles it displays,
 *   not the category the hub belongs to.
 *
 * Trigger 2 — new category / explicit re-generation (one-time AI metadata):
 *   syncStubs() creates a stub PageRecord (empty page_data, generated_at NULL)
 *   for every CategoryTable entry that lacks a corresponding hub page.  The
 *   stub appears in IPageRepository::findPendingByTypeId("category_hub") so
 *   the existing AI generation launcher picks it up automatically.
 *
 * Trigger 3 — "Generate and publish locally" stats freshness:
 *   markStaleByStats() opens stats.db and adds to the dirty set every hub
 *   page whose generated_at is older than the most recent displays_clicks
 *   entry.  Call this before renderDirtyHubs() on a publish run.
 *   Hub pages with an empty generated_at are skipped here — they are handled
 *   by Trigger 2 (AI generation).
 *
 * Crash safety
 * ------------
 * renderDirtyHubs() processes each hub one at a time:
 *   1. generateSubset() writes the hub HTML to content.db.
 *   2. setGeneratedAt() stamps the timestamp.
 *   3. dirtySet.remove() atomically updates dirty_hub_pages.json on disk.
 * A crash between steps 1–2 and step 3 leaves the ID in the file; the next
 * run re-renders the hub (idempotent UPSERT in content.db).
 */
class CategoryHubSyncer
{
public:
    explicit CategoryHubSyncer(IPageRepository     &repo,
                                const CategoryTable &categoryTable,
                                CategoryHubDirtySet &dirtySet,
                                PageGenerator       &generator);

    /**
     * Call after any article page_data is saved.  Extracts all category IDs
     * from pageData, finds hub pages that cover those categories, and adds
     * those hub page IDs to the dirty set.
     */
    void onPageSaved(const QHash<QString, QString> &pageData);

    /**
     * Creates a stub hub PageRecord (empty page_data) for every category in
     * categoryTable that has no corresponding hub page.  The stub is picked up
     * by IPageRepository::findPendingByTypeId("category_hub") so the AI
     * generation launcher can produce the social metadata on the next run.
     * Returns the number of stubs created.
     */
    int syncStubs(const QString &lang);

    /**
     * Re-renders (HTML only, no AI) all hub pages whose IDs are in dirtySet.
     * For each source hub, its translation pages are also re-rendered so every
     * language variant reflects the current article list.
     * After each source hub is successfully rendered, its ID is removed from
     * the dirty set (crash-safe write to disk before moving to the next hub).
     * Returns the total number of PageRecord variants written to content.db.
     */
    int renderDirtyHubs(const QDir     &workingDir,
                         const QString  &domain,
                         AbstractEngine &engine,
                         int             websiteIndex);

    /**
     * Opens stats.db from workingDir and adds to the dirty set any hub page
     * whose generated_at is older than the most recent display_at entry in
     * displays_clicks.  Hub pages with empty generated_at are skipped (they
     * are new stubs handled by Trigger 2).
     * No-op and returns 0 if stats.db does not exist.
     */
    int markStaleByStats(const QDir &workingDir);

    /**
     * Returns all category IDs found in any "*_categories" key of pageData.
     * Used by onPageSaved() to identify which hubs are affected by a save.
     */
    static QSet<int> extractCategoryIds(const QHash<QString, QString> &pageData);

    /**
     * Returns the IDs of hub pages (typeId == "category_hub") that cover at
     * least one of the given category IDs according to their stored page_data.
     */
    QSet<int> hubPageIdsForCategories(const QSet<int> &categoryIds) const;

private:
    IPageRepository     &m_repo;
    const CategoryTable &m_categoryTable;
    CategoryHubDirtySet &m_dirtySet;
    PageGenerator       &m_generator;
};

#endif // CATEGORYHUBSYNCER_H
