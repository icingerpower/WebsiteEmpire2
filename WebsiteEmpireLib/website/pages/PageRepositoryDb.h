#ifndef PAGEREPOSITORYDB_H
#define PAGEREPOSITORYDB_H

#include "website/pages/IPageRepository.h"
#include "website/pages/PageDb.h"

#include <QSet>

/**
 * IPageRepository backed by PageDb (QSqlDatabase / QSQLITE).
 *
 * All write operations execute inside a transaction for atomicity.
 * saveData() replaces the full page_data set in one DELETE + INSERT pass.
 * updatePermalink() records the old permalink in permalink_history before
 * updating the pages row.  The new history entry defaults to redirect_type
 * = 'permanent'; call setHistoryRedirectType() to change it afterwards.
 */
class PageRepositoryDb : public IPageRepository
{
public:
    explicit PageRepositoryDb(PageDb &db);

    int                              create(const QString &typeId,
                                            const QString &permalink,
                                            const QString &lang) override;
    int                              createTranslation(int           sourcePageId,
                                                       const QString &typeId,
                                                       const QString &permalink,
                                                       const QString &lang) override;
    std::optional<PageRecord>        findById(int id) const override;
    QList<PageRecord>                findAll() const override;
    QList<PageRecord>                findSourcePages(const QString &lang) const override;
    QList<PageRecord>                findTranslations(int sourcePageId) const override;
    void                             saveData(int id,
                                              const QHash<QString, QString> &data) override;
    QHash<QString, QString>          loadData(int id) const override;
    void                             remove(int id) override;
    void                             updatePermalink(int id,
                                                     const QString &newPermalink) override;
    QList<PermalinkHistoryEntry>     permalinkHistory(int id) const override;
    void                             setHistoryRedirectType(int historyEntryId,
                                                            const QString &type) override;
    QString                          translatedAt(int id) const override;
    void                             setTranslatedAt(int id,
                                                     const QString &utcIso) override;
    QList<PageRecord>                findPendingByTypeId(const QString &typeId) const override;
    int                              countByTypeId(const QString &typeId) const override;
    void                             setGeneratedAt(int id,
                                                    const QString &utcIso) override;
    void                             recordStrategyAttempt(int pageId,
                                                           const QString &strategyId) override;
    QStringList                      strategyAttempts(int pageId) const override;
    QList<PageRecord>                findGeneratedByTypeId(const QString &typeId) const override;
    void                             setLangCodesToTranslate(int id,
                                                             const QStringList &langs) override;

    // Returns the number of generated pages (generated_at IS NOT NULL) whose
    // permalink is in expectedPermalinks.  Used by the GUI to cross-reference
    // pages.db with an aspire DB's topic set so that manually-added pages
    // that have no matching topic are never counted as "done".
    int countGeneratedMatchingPermalinks(const QString      &typeId,
                                          const QSet<QString> &expectedPermalinks) const;

    static QString currentUtc();

private:
    PageDb &m_db;
};

#endif // PAGEREPOSITORYDB_H
