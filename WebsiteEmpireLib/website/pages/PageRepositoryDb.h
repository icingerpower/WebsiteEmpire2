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
 * updating the pages row.
 */
class PageRepositoryDb : public IPageRepository
{
public:
    explicit PageRepositoryDb(PageDb &db);

    int                          create(const QString &typeId,
                                        const QString &permalink,
                                        const QString &lang) override;
    std::optional<PageRecord>    findById(int id) const override;
    QList<PageRecord>            findAll() const override;
    void                         saveData(int id,
                                          const QHash<QString, QString> &data) override;
    QHash<QString, QString>      loadData(int id) const override;
    void                         remove(int id) override;
    void                         updatePermalink(int id,
                                                 const QString &newPermalink) override;
    QList<QString>               permalinkHistory(int id) const override;

private:
    PageDb &m_db;

    static QString currentUtc();
};

#endif // PAGEREPOSITORYDB_H
