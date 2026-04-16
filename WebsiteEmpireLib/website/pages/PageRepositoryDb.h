#ifndef PAGEREPOSITORYDB_H
#define PAGEREPOSITORYDB_H

#include "website/pages/IPageRepository.h"
#include "website/pages/PageDb.h"

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

private:
    PageDb &m_db;

    static QString currentUtc();
};

#endif // PAGEREPOSITORYDB_H
