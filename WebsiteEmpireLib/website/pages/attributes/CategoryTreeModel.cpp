#include "CategoryTreeModel.h"
#include "CategoryTable.h"

// internalId == 0 is the virtual root sentinel.  Real category ids start at 1.
static constexpr quintptr VIRTUAL_ROOT_ID = 0;

CategoryTreeModel::CategoryTreeModel(CategoryTable &table, QObject *parent)
    : QAbstractItemModel(parent)
    , m_table(table)
{
    connect(&m_table, &CategoryTable::categoriesReset, this, &CategoryTreeModel::_onCategoriesReset);
    connect(&m_table, &CategoryTable::categoryAdded,   this, &CategoryTreeModel::_onCategoriesReset);
    connect(&m_table, &CategoryTable::categoryRemoved,
            this, [this](const QList<int> &) { _onCategoriesReset(); });
    connect(&m_table, &CategoryTable::categoryRenamed, this, &CategoryTreeModel::_onCategoryRenamed);
    _rebuild();
}

// ---- Selection support ------------------------------------------------------

void CategoryTreeModel::setSelectionEnabled(bool enabled)
{
    if (m_selectionEnabled == enabled) {
        return;
    }
    m_selectionEnabled = enabled;
    beginResetModel();
    endResetModel();
}

void CategoryTreeModel::setCheckedIds(const QSet<int> &ids)
{
    m_checkedIds = ids;
    const int roots = m_children.value(0).size();
    if (roots > 0) {
        const QModelIndex rootParent = _rootParentIndex();
        emit dataChanged(index(0, 0, rootParent),
                         index(roots - 1, 0, rootParent),
                         {Qt::CheckStateRole});
    }
}

QSet<int> CategoryTreeModel::checkedIds() const
{
    return m_checkedIds;
}

QModelIndex CategoryTreeModel::indexForId(int id) const
{
    const int parentId = m_parentOf.value(id, -1);
    if (parentId < 0) {
        return {};
    }
    const QList<int> &siblings = m_children.value(parentId);
    const int row = siblings.indexOf(id);
    if (row < 0) {
        return {};
    }
    return createIndex(row, 0, static_cast<quintptr>(id));
}

// ---- Virtual root -----------------------------------------------------------

void CategoryTreeModel::setVirtualRootEnabled(bool enabled)
{
    if (m_virtualRoot == enabled) {
        return;
    }
    beginResetModel();
    m_virtualRoot = enabled;
    endResetModel();
}

// ---- QAbstractItemModel -----------------------------------------------------

QModelIndex CategoryTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        if (m_virtualRoot) {
            // The only top-level item is the virtual root.
            if (row == 0 && column == 0) {
                return createIndex(0, 0, VIRTUAL_ROOT_ID);
            }
            return {};
        }
        // No virtual root: expose real root categories directly.
        const QList<int> &children = m_children.value(0);
        if (row < 0 || row >= children.size()) {
            return {};
        }
        return createIndex(row, column, static_cast<quintptr>(children.at(row)));
    }

    // parent is either the virtual root (id==0) or a real category (id>0).
    // In both cases, children live under that id in m_children.
    const int parentId = static_cast<int>(parent.internalId());
    const QList<int> &children = m_children.value(parentId);
    if (row < 0 || row >= children.size()) {
        return {};
    }
    return createIndex(row, column, static_cast<quintptr>(children.at(row)));
}

QModelIndex CategoryTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) {
        return {};
    }
    const int childId = static_cast<int>(child.internalId());

    // Virtual root has no parent.
    if (m_virtualRoot && static_cast<quintptr>(childId) == VIRTUAL_ROOT_ID) {
        return {};
    }

    const int parentId = m_parentOf.value(childId, -1);
    if (parentId < 0) {
        return {};
    }
    if (parentId == 0) {
        // Parent is the virtual root when enabled, otherwise top-level (no parent index).
        if (m_virtualRoot) {
            return createIndex(0, 0, VIRTUAL_ROOT_ID);
        }
        return {};
    }

    // parentId > 0: find the parent's row among its own siblings.
    const int grandParentId = m_parentOf.value(parentId, 0);
    const QList<int> &siblings = m_children.value(grandParentId);
    const int row = siblings.indexOf(parentId);
    if (row < 0) {
        return {};
    }
    return createIndex(row, 0, static_cast<quintptr>(parentId));
}

int CategoryTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return m_virtualRoot ? 1 : m_children.value(0).size();
    }
    const int parentId = static_cast<int>(parent.internalId());
    return m_children.value(parentId).size();
}

int CategoryTreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 1;
}

QVariant CategoryTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const int id = static_cast<int>(index.internalId());

    // Virtual root node.
    if (m_virtualRoot && static_cast<quintptr>(id) == VIRTUAL_ROOT_ID) {
        if (role == Qt::DisplayRole) {
            return tr("(root)");
        }
        if (role == Qt::UserRole) {
            return 0; // parentId for new top-level categories
        }
        return {};
    }

    // Real category.
    const CategoryTable::CategoryRow *row = m_table.categoryById(id);
    if (!row) {
        return {};
    }

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return row->name;
    case Qt::CheckStateRole:
        if (!m_selectionEnabled) {
            return {};
        }
        return m_checkedIds.contains(id) ? Qt::Checked : Qt::Unchecked;
    case Qt::UserRole:
        return id;
    default:
        return {};
    }
}

bool CategoryTreeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid()) {
        return false;
    }
    const int id = static_cast<int>(index.internalId());

    // Virtual root cannot be modified.
    if (m_virtualRoot && static_cast<quintptr>(id) == VIRTUAL_ROOT_ID) {
        return false;
    }

    if (role == Qt::CheckStateRole && m_selectionEnabled) {
        if (value.toInt() == Qt::Checked) {
            m_checkedIds.insert(id);
        } else {
            m_checkedIds.remove(id);
        }
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }

    if (role == Qt::EditRole) {
        const QString newName = value.toString().trimmed();
        if (newName.isEmpty()) {
            return false;
        }
        // Ensure the committed name is unique; append -2, -3, … if needed.
        const QString uniqueName = m_table.uniqueName(newName, id);
        m_table.renameCategory(id, uniqueName);
        // dataChanged is emitted by _onCategoryRenamed via the signal.
        return true;
    }

    return false;
}

Qt::ItemFlags CategoryTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    const int id = static_cast<int>(index.internalId());

    // Virtual root: selectable only — not editable, not checkable.
    if (m_virtualRoot && static_cast<quintptr>(id) == VIRTUAL_ROOT_ID) {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }

    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
    if (m_selectionEnabled) {
        f |= Qt::ItemIsUserCheckable;
    }
    return f;
}

QVariant CategoryTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal && section == 0) {
        return tr("Category");
    }
    return {};
}

// ---- Private slots ----------------------------------------------------------

void CategoryTreeModel::_onCategoriesReset()
{
    beginResetModel();
    _rebuild();
    endResetModel();
}

void CategoryTreeModel::_onCategoryRenamed(int id)
{
    const QModelIndex idx = indexForId(id);
    if (idx.isValid()) {
        emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
    }
}

// ---- Private helpers --------------------------------------------------------

void CategoryTreeModel::_rebuild()
{
    m_children.clear();
    m_parentOf.clear();
    for (const auto &row : std::as_const(m_table.categories())) {
        m_children[row.parentId].append(row.id);
        m_parentOf.insert(row.id, row.parentId);
    }
}

QModelIndex CategoryTreeModel::_rootParentIndex() const
{
    return m_virtualRoot ? createIndex(0, 0, VIRTUAL_ROOT_ID) : QModelIndex();
}
