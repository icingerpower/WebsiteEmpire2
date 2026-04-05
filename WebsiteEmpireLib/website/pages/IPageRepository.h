#ifndef IPAGEREPOSITORY_H
#define IPAGEREPOSITORY_H

#include "website/pages/PageRecord.h"
#include "website/pages/PermalinkHistoryEntry.h"

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
     * Updates the permalink for id and records the old one in permalink_history
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
     * type must be one of: "permanent" (301), "parked" (302), "deleted" (410).
     */
    virtual void setHistoryRedirectType(int historyEntryId, const QString &type) = 0;

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
};

#endif // IPAGEREPOSITORY_H
