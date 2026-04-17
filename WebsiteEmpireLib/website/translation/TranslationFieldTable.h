#ifndef TRANSLATIONFIELDTABLE_H
#define TRANSLATIONFIELDTABLE_H

#include <QAbstractTableModel>
#include <QList>
#include <QPair>
#include <QStringList>

class AbstractCommonBloc;

/**
 * Table model showing field-level translation status for common blocs.
 *
 * Layout
 * ------
 * Rows    : one per (bloc × translatable field).  Blocs with no translatable
 *           fields produce no rows.
 * Columns : COL_BLOC_NAME, COL_FIELD_LABEL, COL_SOURCE (fixed), then one
 *           column per target language supplied at construction.
 *
 * Fixed columns are read-only.  Language columns are editable: setData()
 * calls AbstractCommonBloc::setTranslation() on the owning bloc so that the
 * change is reflected in memory immediately.  Callers should persist the
 * theme (AbstractTheme::saveBlocsData()) after receiving dataChanged().
 */
class TranslationFieldTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum FixedCol {
        COL_BLOC_NAME   = 0,
        COL_FIELD_LABEL,
        COL_SOURCE,
        COL_PROGRESS,   ///< "done/total" translation count for this field row
        COL_COUNT_FIXED
    };

    explicit TranslationFieldTable(const QList<AbstractCommonBloc *> &blocs,
                                   const QStringList                 &targetLangs,
                                   QObject                           *parent = nullptr);

    /**
     * Returns {done, total} where total = rows × target languages and
     * done = number of cells that have a non-empty translation.
     */
    QPair<int, int> progress() const;

    int           rowCount(const QModelIndex &parent = {})                        const override;
    int           columnCount(const QModelIndex &parent = {})                     const override;
    QVariant      data(const QModelIndex &index, int role = Qt::DisplayRole)      const override;
    bool          setData(const QModelIndex &index, const QVariant &value,
                          int role = Qt::EditRole)                                       override;
    QVariant      headerData(int section, Qt::Orientation orientation,
                             int role = Qt::DisplayRole)                           const override;
    Qt::ItemFlags flags(const QModelIndex &index)                                 const override;

private:
    struct Row {
        AbstractCommonBloc *bloc;        ///< owning bloc — valid for the model's lifetime
        QString             blocName;
        QString             fieldId;     ///< stable field key used by translatedText()
        QString             sourceText;
        QList<QString>      translations; ///< one per target lang; empty = not yet translated
    };

    QStringList m_targetLangs;
    QList<Row>  m_rows;
};

#endif // TRANSLATIONFIELDTABLE_H
