#ifndef IPAGEREPOSITORY_H
#define IPAGEREPOSITORY_H

#include "website/pages/PageFlag.h"
#include "website/pages/PageGenerationState.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PermalinkHistoryEntry.h"
#include "website/pages/RasterImageStatus.h"

#include <QHash>
#include <QList>
#include <QString>
#include <optional>

/**
 * CRUD interface for page storage.
 *
 * Page content is a flat key→value map (see page_data table).  Bloc keys are
 * namespaced by position index "<i>_<key>" so multiple blocs of the same type
 * coexist without collision.
 *
 * Translation workflow
 * --------------------
 * 1. Author creates source pages (sourcePageId == 0, lang == editing lang).
 * 2. PageTranslator calls createTranslation() for each target language, then
 *    saves AI-generated data with saveData() and stamps the result with
 *    setTranslatedAt().
 * 3. PageEditorDialog disables editing of translated pages whose translatedAt
 *    is empty (not yet AI-translated).
 * 4. If the source page is later updated (updatedAt > translatedAt), the
 *    editor shows a staleness warning but allows the user to proceed.
 *
 * Permalink history
 * -----------------
 * updatePermalink() automatically records the old permalink in permalink_history
 * with a default redirectType of "permanent" (301).  The type can be changed
 * per-entry via setHistoryRedirectType().  PageGenerator reads this history to
 * emit the correct redirect rows into content.db (301 / 302 / 410).
 */
class IPageRepository
{
public:
    virtual ~IPageRepository() = default;

    // -------------------------------------------------------------------------
    // CRUD
    // -------------------------------------------------------------------------

    /** Creates a new root page and returns its id. */
    virtual int create(const QString &typeId,
                       const QString &permalink,
                       const QString &lang) = 0;

    /**
     * Creates a page that is an AI translation of sourcePageId.
     * The new page starts with translatedAt empty (locked for editing) until
     * setTranslatedAt() is called after the AI pass completes.
     * Returns the id of the newly created page.
     */
    virtual int createTranslation(int           sourcePageId,
                                  const QString &typeId,
                                  const QString &permalink,
                                  const QString &lang) = 0;

    /** Returns the page record for id, or std::nullopt if not found. */
    virtual std::optional<PageRecord> findById(int id) const = 0;

    /** Returns all page records ordered by id ASC. */
    virtual QList<PageRecord> findAll() const = 0;

    /**
     * Returns root pages (source_page_id IS NULL) for the given lang,
     * ordered by id ASC.
     */
    virtual QList<PageRecord> findSourcePages(const QString &lang) const = 0;

    /**
     * Returns all translations of sourcePageId (source_page_id == sourcePageId),
     * ordered by id ASC.
     */
    virtual QList<PageRecord> findTranslations(int sourcePageId) const = 0;

    /** Replaces the full page_data set for id in one transaction. */
    virtual void saveData(int id, const QHash<QString, QString> &data) = 0;

    virtual QHash<QString, QString> loadData(int id) const = 0;

    /** Deletes the page and cascades to page_data and permalink_history. */
    virtual void remove(int id) = 0;

    // -------------------------------------------------------------------------
    // Permalink management
    // -------------------------------------------------------------------------

    /**
     * Updates the permalink for id.  When the page is published (published_at IS NOT NULL)
     * and the permalink actually changes, records the old permalink in permalink_history
     * with redirect_type = 'permanent'.  No-op when the permalink has not changed.
     */
    virtual void updatePermalink(int id, const QString &newPermalink) = 0;

    /**
     * Returns the permalink history for id ordered by changedAt ASC.
     * Each entry carries the redirectType that PageGenerator uses.
     */
    virtual QList<PermalinkHistoryEntry> permalinkHistory(int id) const = 0;

    /**
     * Changes the redirect type for a single history entry.
     * type must be one of: "permanent" (301), "parked" (302), "deleted" (410), "none" (no redirect).
     */
    virtual void setHistoryRedirectType(int historyEntryId, const QString &type) = 0;

    // -------------------------------------------------------------------------
    // End-permalink / published state
    // -------------------------------------------------------------------------

    /**
     * Sets the end_permalink suffix for page id.
     * Call immediately after create() when the page was generated from a strategy
     * that has a non-empty endPermalink (e.g. "genes-biomarkers").
     */
    virtual void setEndPermalink(int id, const QString &value) = 0;

    /**
     * Stamps published_at to utcIso (ISO 8601 UTC) for page id.
     * Call this in PaneDomains::deployLocally() after a successful deploy so
     * that subsequent permalink changes trigger history entries.
     * No-op when published_at is already set (once published, stays published).
     */
    virtual void setPublishedAt(int id, const QString &utcIso) = 0;

    /**
     * Sets published_at = NOW() for every page whose generation_state is Complete
     * and whose published_at is still NULL.
     * Call once in PaneDomains::deployLocally() after a successful deploy.
     */
    virtual void markAllCompleteAsPublished() = 0;

    // -------------------------------------------------------------------------
    // Translation tracking
    // -------------------------------------------------------------------------

    /**
     * Returns the translatedAt timestamp for id, or an empty string if the
     * page has never been AI-translated (still locked for editing).
     */
    virtual QString translatedAt(int id) const = 0;

    /**
     * Sets translatedAt to utcIso (ISO 8601 UTC).
     * Call this immediately after saving AI-translated data for a page.
     */
    virtual void setTranslatedAt(int id, const QString &utcIso) = 0;

    // -------------------------------------------------------------------------
    // AI content generation tracking
    // -------------------------------------------------------------------------

    /**
     * Returns source pages (sourcePageId == 0) of typeId whose generated_at
     * IS NULL and whose page_data is empty — i.e. pages that need AI content
     * generation.  Ordered by id ASC.
     *
     * Excludes pages that already have manually-written content (page_data
     * non-empty) even when generated_at is still NULL, so that manual edits
     * are never silently overwritten by the generation launcher.
     */
    virtual QList<PageRecord> findPendingByTypeId(const QString &typeId) const = 0;

    /**
     * Total count of source pages (sourcePageId == 0) for typeId, regardless
     * of whether they have content.  Used together with findPendingByTypeId()
     * to compute nDone = countByTypeId - findPendingByTypeId().size().
     */
    virtual int countByTypeId(const QString &typeId) const = 0;

    /**
     * Stamps generatedAt to utcIso (ISO 8601 UTC).
     * Call this immediately after LauncherGeneration saves AI-generated content.
     */
    virtual void setGeneratedAt(int id, const QString &utcIso) = 0;

    /**
     * Records that strategyId was used to attempt content generation for page pageId.
     * Multiple calls append entries in chronological order.
     * Called by both LauncherGeneration (on success) and LauncherImprove.
     */
    virtual void recordStrategyAttempt(int pageId, const QString &strategyId) = 0;

    /**
     * Returns the strategy IDs attempted on pageId in attempt order (oldest first).
     * Returns an empty list if no attempts have been recorded (e.g. legacy pages).
     */
    virtual QStringList strategyAttempts(int pageId) const = 0;

    /**
     * Returns source pages (source_page_id IS NULL) of typeId that have already
     * been generated (generated_at IS NOT NULL), ordered by id ASC.
     * Used by LauncherImprove to find candidates for re-generation.
     */
    virtual QList<PageRecord> findGeneratedByTypeId(const QString &typeId) const = 0;

    // -------------------------------------------------------------------------
    // AI content update tracking
    // -------------------------------------------------------------------------

    /**
     * Records that promptId was used to update the content of page pageId.
     * Multiple calls append entries; order is chronological.
     */
    virtual void recordUpdateAttempt(int pageId, const QString &promptId) = 0;

    /**
     * Returns the most recent updated_at ISO timestamp for (pageId, promptId),
     * or an empty string if this prompt has never been applied to this page.
     */
    virtual QString lastUpdateAttemptAt(int pageId, const QString &promptId) const = 0;

    /**
     * Returns source pages of typeId that have been generated (generated_at IS NOT NULL),
     * ordered so that pages least recently updated by promptId come first
     * (pages never updated by this prompt come before all others).
     * limit <= 0 means unlimited.
     * When skipIfDataKey is non-empty, pages whose page_data already contains
     * that key with a non-empty value are excluded from the results entirely.
     */
    virtual QList<PageRecord> findPagesForUpdate(const QString &typeId,
                                                  const QString &promptId,
                                                  int            limit,
                                                  const QString &skipIfDataKey = {}) const = 0;

    /**
     * Returns all pages that have at least one update_attempt row for promptId,
     * ordered by permalink ascending.
     */
    virtual QList<PageRecord> findPagesWithUpdateAttempt(const QString &promptId) const = 0;

    /**
     * Returns source pages (source_page_id IS NULL) of typeId that have NO
     * update_attempt row for promptId, ordered by permalink ascending.
     * Used to display articles not yet processed by this prompt.
     */
    virtual QList<PageRecord> findPagesWithoutUpdateAttempt(const QString &typeId,
                                                             const QString &promptId) const = 0;

    /**
     * Deletes all update_attempt rows for the given page IDs and promptId.
     * No-op for any page ID that has no such row.
     */
    virtual void clearUpdateAttempts(const QList<int> &pageIds, const QString &promptId) = 0;

    // -------------------------------------------------------------------------
    // Translation scope management
    // -------------------------------------------------------------------------

    /**
     * Persists the langCodesToTranslate list for page id.
     * The list is stored as a comma-separated string in langs_to_translate.
     * Pass an empty list to clear (author language only).
     * Called exclusively by the assessment step.
     */
    virtual void setLangCodesToTranslate(int id, const QStringList &langs) = 0;

    // -------------------------------------------------------------------------
    // Translation data removal
    // -------------------------------------------------------------------------

    /**
     * Removes all translation data for page id and the given language code.
     * Deletes every page_data row whose key matches the pattern
     * "%_tr:<lang>:%" (e.g. "1_tr:fr:text", "1_tr:fr:text:hash").
     * No-op when no such rows exist.
     */
    virtual void clearTranslationData(int pageId, const QString &lang) = 0;

    /**
     * Removes all translation data for page id across every language.
     * Deletes every page_data row whose key contains "_tr:" (all languages).
     * No-op when no translation data exists for the page.
     */
    virtual void clearAllTranslationData(int pageId) = 0;

    // -------------------------------------------------------------------------
    // Generation state
    // -------------------------------------------------------------------------

    /**
     * Updates the generation_state column for a single page.
     * Call after each pipeline step completes to enable crash recovery.
     */
    virtual void setGenerationState(int id, PageGenerationState state) = 0;

    /**
     * Returns all pages that have a "policy_blocked_cli" entry in page_data.
     * Key   = permalink
     * Value = {page_id, comma-separated list of CLI names that failed with a
     *          Usage Policy error and must not be retried for this page}.
     *
     * Used by LauncherGeneration to skip policy-blocked pages for the current
     * CLI while still allowing other CLIs to retry them.
     */
    virtual QHash<QString, QPair<int, QString>> findPolicyBlockedPages() const = 0;

    /**
     * Returns all source pages (source_page_id IS NULL) of typeId whose
     * generation_state equals state, ordered by id ASC.
     * Used by LauncherGeneration to resume mid-pipeline work.
     */
    virtual QList<PageRecord> findByGenerationState(const QString        &typeId,
                                                     PageGenerationState   state) const = 0;

    // -------------------------------------------------------------------------
    // Translation image state
    // -------------------------------------------------------------------------

    /**
     * Returns the translation image state for (pageId, lang), or Pending if
     * no row exists yet.
     */
    virtual PageGenerationState translationImageState(int            pageId,
                                                       const QString &lang) const = 0;

    /**
     * Upserts the translation image state for (pageId, lang).
     * Call with Complete after social SVGs + WebPs are written for a language.
     */
    virtual void setTranslationImageState(int                 pageId,
                                           const QString      &lang,
                                           PageGenerationState state) = 0;

    /**
     * Resets all translation image states for pageId back to Pending.
     * Called by LauncherUpdate when the source SVG changes, so outdated
     * translated blobs are silently overwritten on the next translation run.
     * Does NOT delete any image blobs — only the state rows are updated.
     */
    virtual void invalidateTranslationImages(int pageId) = 0;

    /**
     * Returns the BCP-47 language codes for which pageId still has
     * translation image state Pending (i.e. social images need (re-)generation).
     * Only languages listed in the page's langCodesToTranslate are considered.
     */
    virtual QStringList pendingTranslationImageLangs(int pageId) const = 0;

    // -------------------------------------------------------------------------
    // Page flags
    // -------------------------------------------------------------------------

    /**
     * Sets or clears a single PageFlag bit for page id.
     *
     * When flag == PageFlag::SocialMedia and on == true, and the page's current
     * generation_state is Complete, the state is also reset to MainImageReady so
     * that the next --generation run picks up the page for the social-media
     * second pass.
     */
    virtual void setFlag(int id, PageFlag flag, bool on) = 0;

    /**
     * Returns all source pages (source_page_id IS NULL) that have the given
     * flag bit set, ordered by id ASC.
     * Used by LauncherReview and LauncherGeneration to locate pages that need
     * the social-media second pass or other review-driven actions.
     */
    virtual QList<PageRecord> findByFlag(PageFlag flag) const = 0;

    // -------------------------------------------------------------------------
    // Raster (non-SVG) image generation tracking
    // -------------------------------------------------------------------------
    //
    // A page with N raster [IMGFIX] refs is only safe to mark Complete (see
    // allRasterImagesTerminal()) or to publish (see allRasterImagesSuccess())
    // once every ref has a row here.  This is what lets generation resume
    // cleanly after a crash or a CLI usage-limit pause instead of silently
    // shipping a page with some images still missing — see LauncherGeneration
    // and PageGenerator's publish-time gate.

    /**
     * Ensures a row exists for (pageId, refId) with status Pending, attempts 0.
     * No-op if a row already exists — never resets progress made in a prior run.
     * Call once per expected raster image ref before attempting generation.
     */
    virtual void ensureRasterImagePending(int pageId, const QString &refId,
                                          const QString &fileName) = 0;

    /**
     * Returns the current status for (pageId, refId), or RasterImageStatus::Pending
     * if no row exists (should not normally happen once ensureRasterImagePending()
     * has been called for every expected ref).
     */
    virtual RasterImageStatus rasterImageStatus(int pageId, const QString &refId) const = 0;

    /** Returns the number of attempts already recorded for (pageId, refId), or 0. */
    virtual int rasterImageAttempts(int pageId, const QString &refId) const = 0;

    /**
     * Records the outcome of one generation/review attempt for (pageId, refId):
     * increments attempts, sets status, and stores lastError (empty clears it).
     * Requires a row to already exist (call ensureRasterImagePending() first).
     */
    virtual void recordRasterImageAttempt(int                pageId,
                                          const QString      &refId,
                                          RasterImageStatus   status,
                                          const QString      &lastError) = 0;

    /**
     * Returns true if pageId has zero page_raster_images rows with status
     * Pending — i.e. every raster ref attempted so far has reached Success or
     * FailedFinal.  Vacuously true when pageId has no raster image rows at all
     * (nothing to wait for).  Used by LauncherGeneration to decide whether a
     * page can leave the auto-retry loop and reach PageGenerationState::Complete.
     */
    virtual bool allRasterImagesTerminal(int pageId) const = 0;

    /**
     * Returns true if pageId has at least one page_raster_images row.  Used by
     * the publish-time gate to distinguish "no raster images expected" (the
     * overwhelmingly common case, unaffected by this feature) from "raster
     * images expected" (which then must also pass allRasterImagesSuccess()).
     */
    virtual bool hasRasterImages(int pageId) const = 0;

    /**
     * Returns true only if EVERY page_raster_images row for pageId has status
     * Success.  Unlike allRasterImagesTerminal(), a FailedFinal row also makes
     * this false — a permanently-failed image must never go live silently.
     * Vacuously true when pageId has no raster image rows at all.
     *
     * This — NOT allRasterImagesTerminal() — is the completeness criterion for
     * a page: an article with a failed image is not a finished article.  It
     * governs the publish-time gate (PageGenerator), whether LauncherGeneration
     * may promote the page to PageGenerationState::Complete, and whether
     * PaneGeneration counts the page as "done".
     */
    virtual bool allRasterImagesSuccess(int pageId) const = 0;

    /**
     * Resets every FailedFinal raster row for pageId back to Pending with
     * attempts = 0 and last_error cleared.  Returns the number of rows reset.
     * Rows already Pending or Success are left untouched.
     *
     * This is the requeue primitive that makes a permanently-failed image
     * recoverable.  Without it a FailedFinal row is a dead end:
     * LauncherGeneration's per-image driver returns early for any row whose
     * status is not Pending, and the attempt loop starts at attempts + 1, so
     * clearing BOTH fields is what actually grants a fresh attempt budget.
     *
     * Called at the start of a retry pass, not on the first-pass path — a reset
     * inside the generate loop would spin forever on an image the AI genuinely
     * cannot produce.  One fresh budget per explicit retry run is the contract.
     */
    virtual int resetFailedRasterImages(int pageId) = 0;

    /**
     * Returns the source pages of typeId that have at least one raster row not
     * in Success — i.e. articles that are not truly finished because an image
     * is missing or permanently failed.  Ordered by id ascending.
     *
     * Used by PaneGeneration so the "done" count reflects publishable articles
     * rather than merely content-filled ones; a page listed here is one the
     * retry queue still has work to do on.
     */
    virtual QList<PageRecord> findPagesWithUnresolvedRasterImages(
        const QString &typeId) const = 0;

    /**
     * Returns how many of pageId's raster rows are not in Success — i.e. how
     * many images a repair run would have to (re)generate for this page.
     * 0 when the page has no raster rows or all of them succeeded.
     *
     * Used to tell the user the size of a repair run before starting it: each
     * image costs one generation AND one review CLI call.
     */
    virtual int countUnresolvedRasterImages(int pageId) const = 0;
};

#endif // IPAGEREPOSITORY_H
