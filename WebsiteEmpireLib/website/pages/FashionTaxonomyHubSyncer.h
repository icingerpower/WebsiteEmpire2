#ifndef FASHIONTAXONOMYHUBSYNCER_H
#define FASHIONTAXONOMYHUBSYNCER_H

#include <QDir>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

class AbstractEngine;
class FashionHubDirtySet;
class IPageRepository;
class PageGenerator;

/**
 * Orchestrates the three triggers that keep fashion-tag-hub pages fresh —
 * one syncer for all dimensions (Color, Season, Occasion, Material, Style
 * Aesthetic, Product Type, Demographic, Fit/Silhouette, Pattern, Culture),
 * parameterized via PageBlocFashionTaxonomyLinks::dimensions(), mirroring
 * CategoryHubSyncer's three-trigger design (see that class for the pattern
 * this one follows exactly).
 *
 * Trigger 1 — article edited / created (no AI, dirty-mark only):
 *   Call onPageSaved() after any Fashion article page_data is written. The
 *   syncer reads the saved tags per dimension, finds every hub page that
 *   covers those (dimension, tag) pairs — creating a stub on the fly for a
 *   brand-new tag not yet backed by a hub — and adds those hub page IDs to
 *   the FashionHubDirtySet. Call renderDirtyHubs() to re-render only the
 *   affected hubs.
 *   NOTE: as with CategoryHubSyncer::onPageSaved(), this is not currently
 *   wired to any live "article saved" event in the app — it exists, is
 *   tested, and is ready to be called once such a hook is added for hub
 *   types in general.
 *
 * Trigger 2 — new tag value / explicit re-generation (one-time AI metadata):
 *   syncStubs() reads each dimension's already-synced vocabulary from
 *   TaxonomyDb (the local cache PaneTaxonomies maintains — not a second
 *   direct read of the aspire source DB) and creates a stub PageRecord for
 *   every value that has no corresponding hub page yet. The stub appears in
 *   IPageRepository::findPendingByTypeId("fashion_tag_hub") so the existing
 *   AI generation launcher picks it up automatically.
 *
 * Trigger 3 — "Generate and publish locally" stats freshness:
 *   markStaleByStats() opens stats.db and adds to the dirty set every hub
 *   page whose generated_at is older than the most recent displays_clicks
 *   entry. Call this before renderDirtyHubs() on a publish run.
 *
 * Crash safety — identical to CategoryHubSyncer::renderDirtyHubs():
 * generate -> stamp generated_at -> remove from dirty set, in that order, so
 * a crash mid-loop just re-renders the hub on the next run (idempotent).
 */
class FashionTaxonomyHubSyncer
{
public:
    explicit FashionTaxonomyHubSyncer(IPageRepository     &repo,
                                       const QDir          &workingDir,
                                       FashionHubDirtySet  &dirtySet,
                                       PageGenerator       &generator);

    /**
     * Call after any Fashion article page_data is saved. Extracts selected
     * tags per dimension from pageData, finds (or creates a stub for) the
     * hub page for each (dimension, tag) pair, and adds those hub page IDs
     * to the dirty set.
     */
    void onPageSaved(const QHash<QString, QString> &pageData);

    /**
     * For each dimension, reads its synced vocabulary from
     * TaxonomyDb and creates a stub hub PageRecord (empty page_data beyond
     * dimension/tag_value) for every value that has no corresponding hub
     * page yet. Returns the number of stubs created.
     */
    int syncStubs(const QString &lang);

    /**
     * Re-renders (HTML only, no AI) all hub pages whose IDs are in dirtySet.
     * For each source hub, its translation pages are also re-rendered so every
     * language variant reflects the current article list.
     * Returns the total number of PageRecord variants written to content.db.
     */
    int renderDirtyHubs(const QDir     &workingDir,
                         const QString  &domain,
                         AbstractEngine &engine,
                         int             websiteIndex);

    /**
     * Opens stats.db from workingDir and adds to the dirty set any hub page
     * whose generated_at is older than the most recent display_at entry in
     * displays_clicks. Hub pages with empty generated_at are skipped (they
     * are new stubs handled by Trigger 2).
     * No-op and returns 0 if stats.db does not exist.
     */
    int markStaleByStats(const QDir &workingDir);

    /**
     * Returns dimensionId -> selected tag names, extracted from every
     * PageBlocFashionTaxonomyLinks dimension key present in pageData.
     * Used by onPageSaved() to identify which hubs are affected by a save.
     */
    static QHash<QString, QStringList> extractFashionTags(const QHash<QString, QString> &pageData);

    /**
     * Returns the ID of the hub page (typeId == "fashion_tag_hub") whose
     * stored dimension/tag_value match the given pair, or -1 if none exists.
     */
    int hubPageIdFor(const QString &dimension, const QString &tagValue) const;

private:
    IPageRepository     &m_repo;
    QDir                  m_workingDir;
    FashionHubDirtySet   &m_dirtySet;
    PageGenerator        &m_generator;
};

#endif // FASHIONTAXONOMYHUBSYNCER_H
