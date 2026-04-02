#ifndef CATEGORYTREEMODEL_H
#define CATEGORYTREEMODEL_H

#include <QAbstractItemModel>
#include <QMap>
#include <QSet>

class CategoryTable;

/**
 * Tree model for a CategoryTable.
 *
 * Presents categories in a parent/child hierarchy driven by each row's
 * parentId field.  Root categories (parentId == 0) appear at the top level.
 *
 * Column 0: the category's English name (editable inline via Qt::EditRole —
 * calls CategoryTable::renameCategory, which persists immediately).
 *
 * The internal id of every QModelIndex is the category's int id cast to
 * quintptr, so QModelIndex::internalId() always gives the category id.
 *
 * Check-state support:
 *   Call setSelectionEnabled(true) to make items user-checkable (default).
 *   Use setCheckedIds() / checkedIds() to persist and restore selections.
 *   When selection is not needed (e.g. a vocabulary-management view), call
 *   setSelectionEnabled(false) to hide checkboxes.
 */
class CategoryTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit CategoryTreeModel(CategoryTable &table, QObject *parent = nullptr);

    // --- Selection support -----------------------------------------------------

    void      setSelectionEnabled(bool enabled);
    void      setCheckedIds(const QSet<int> &ids);
    QSet<int> checkedIds() const;

    // Returns the model index for the given category id, or an invalid index.
    QModelIndex indexForId(int id) const;

    // --- QAbstractItemModel ----------------------------------------------------

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex())    const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant    data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData(const QModelIndex &index, const QVariant &value,
                     int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant  headerData(int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;

private slots:
    void _onCategoriesReset();
    void _onCategoryRenamed(int id);

private:
    void _rebuild();

    CategoryTable &m_table;

    // parentId → ordered list of child ids (preserves CSV order)
    QMap<int, QList<int>> m_children;
    // id → parentId, for O(1) parent() lookup
    QMap<int, int>        m_parentOf;

    QSet<int> m_checkedIds;
    bool      m_selectionEnabled = true;
};

#endif // CATEGORYTREEMODEL_H
