#ifndef TRANSLATIONSTATUSTABLE_H
#define TRANSLATIONSTATUSTABLE_H

#include "website/pages/PageRecord.h"

#include <QAbstractTableModel>
#include <QList>
#include <QPair>
#include <QStringList>

class CategoryTable;
class IPageRepository;

/**
 * Read-only table model showing per-page translation completion status.
 *
 * Layout
 * ------
 * Rows    : one per source page (sourcePageId == 0) in the repository.
 * Columns : COL_PERMALINK, COL_TYPE, COL_LANG (fixed), then one column per
 *           target language supplied at construction.
 *
 * Each language column uses Qt::CheckStateRole: Qt::Checked when
 * isTranslationComplete() is true for that page × language, Qt::Unchecked
 * otherwise.
 *
 * Call reload() to refresh from the repository after external changes.
 * The model is read-only — setData() is not implemented.
 */
class TranslationStatusTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum FixedCol {
        COL_PERMALINK = 0,
        COL_TYPE,
        COL_LANG,
        COL_PROGRESS,   ///< "done/total" translation count for this page row
        COL_COUNT_FIXED
    };

    explicit TranslationStatusTable(IPageRepository   &repo,
                                    CategoryTable     &categoryTable,
                                    const QStringList &targetLangs,
                                    QObject           *parent = nullptr);

    /** Reload all rows from the repository (calls beginResetModel / endResetModel). */
    void reload();

    /**
     * Returns {done, total} where total = rows × target languages and
     * done = number of (page, lang) pairs where isTranslationComplete is true.
     * Both values are 0 before reload() is first called.
     */
    QPair<int, int> progress() const;

    int           rowCount(const QModelIndex &parent = {})                        const override;
    int           columnCount(const QModelIndex &parent = {})                     const override;
    QVariant      data(const QModelIndex &index, int role = Qt::DisplayRole)      const override;
    QVariant      headerData(int section, Qt::Orientation orientation,
                             int role = Qt::DisplayRole)                           const override;
    Qt::ItemFlags flags(const QModelIndex &index)                                 const override;

private:
    struct Row {
        PageRecord  record;
        QList<bool> isComplete; ///< one entry per target language
    };

    IPageRepository &m_repo;
    CategoryTable   &m_categoryTable;
    QStringList      m_targetLangs;
    QList<Row>       m_rows;
};

#endif // TRANSLATIONSTATUSTABLE_H
