#include "PermalinkHistoryModel.h"

#include "website/pages/IPageRepository.h"

const QStringList &PermalinkHistoryModel::redirectTypeValues()
{
    static const QStringList values = {
        QStringLiteral("permanent"),
        QStringLiteral("parked"),
        QStringLiteral("deleted"),
        QStringLiteral("none"),
    };
    return values;
}

const QStringList &PermalinkHistoryModel::redirectTypeLabels()
{
    static const QStringList labels = {
        QStringLiteral("Permanent 301"),
        QStringLiteral("Parked 302"),
        QStringLiteral("Deleted 410"),
        QStringLiteral("No redirect"),
    };
    return labels;
}

PermalinkHistoryModel::PermalinkHistoryModel(IPageRepository &repo,
                                             int              pageId,
                                             QObject         *parent)
    : QAbstractTableModel(parent)
    , m_repo(repo)
    , m_pageId(pageId)
    , m_entries(repo.permalinkHistory(pageId))
{
}

int PermalinkHistoryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_entries.size();
}

int PermalinkHistoryModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return COLUMN_COUNT;
}

QVariant PermalinkHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }

    const PermalinkHistoryEntry &e = m_entries.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case COL_PERMALINK:  return e.permalink;
        case COL_CHANGED_AT: return e.changedAt;
        case COL_REDIRECT: {
            const int idx = redirectTypeValues().indexOf(e.redirectType);
            return (idx >= 0) ? redirectTypeLabels().at(idx)
                              : e.redirectType;
        }
        default: break;
        }
    }

    if (role == Qt::EditRole && index.column() == COL_REDIRECT) {
        return e.redirectType;
    }

    return {};
}

QVariant PermalinkHistoryModel::headerData(int section,
                                            Qt::Orientation orientation,
                                            int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    switch (section) {
    case COL_PERMALINK:  return tr("Old Permalink");
    case COL_CHANGED_AT: return tr("Changed At");
    case COL_REDIRECT:   return tr("Redirect Type");
    default:             return {};
    }
}

Qt::ItemFlags PermalinkHistoryModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags f = QAbstractItemModel::flags(index);
    if (index.column() == COL_REDIRECT) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

bool PermalinkHistoryModel::setData(const QModelIndex &index,
                                     const QVariant    &value,
                                     int                role)
{
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }
    if (index.column() != COL_REDIRECT) {
        return false;
    }
    if (index.row() < 0 || index.row() >= m_entries.size()) {
        return false;
    }

    const QString &newType = value.toString();
    if (!redirectTypeValues().contains(newType)) {
        return false;
    }

    PermalinkHistoryEntry &e = m_entries[index.row()];
    if (e.redirectType == newType) {
        return true;
    }
    e.redirectType = newType;
    m_repo.setHistoryRedirectType(e.id, newType);
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}
