#include "LinksManager.h"

#include <QCoreApplication>

// =============================================================================
// Singleton
// =============================================================================

LinksManager &LinksManager::instance()
{
    static LinksManager s_instance;
    return s_instance;
}

// =============================================================================
// Mutation
// =============================================================================

void LinksManager::setKeywords(const QString &pageUrl, const QStringList &keywords)
{
    beginResetModel();

    const int idx = indexOfUrl(pageUrl);
    if (keywords.isEmpty()) {
        if (idx >= 0) {
            m_entries.removeAt(idx);
        }
    } else if (idx >= 0) {
        m_entries[idx].keywords = keywords;
    } else {
        Entry e;
        e.pageUrl  = pageUrl;
        e.keywords = keywords;
        m_entries.append(std::move(e));
    }

    recomputeMaxKeywords();
    endResetModel();
}

void LinksManager::setPageTitle(const QString &pageUrl, const QString &title)
{
    const int idx = indexOfUrl(pageUrl);
    if (idx < 0) {
        return;
    }
    m_entries[idx].pageTitle = title;
    const QModelIndex changed = index(idx, 1);
    emit dataChanged(changed, changed, {Qt::DisplayRole});
}

// =============================================================================
// QAbstractTableModel interface
// =============================================================================

int LinksManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_entries.size();
}

int LinksManager::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return FIXED_COLS + m_maxKeywords;
}

QVariant LinksManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || role != Qt::DisplayRole) {
        return {};
    }
    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row >= m_entries.size()) {
        return {};
    }
    const Entry &e = m_entries.at(row);
    switch (col) {
    case 0: return e.pageUrl;
    case 1: return e.pageTitle;
    case 2: return e.keywords.size();
    default: {
        const int kwIdx = col - FIXED_COLS;
        return kwIdx < e.keywords.size() ? QVariant(e.keywords.at(kwIdx)) : QVariant{};
    }
    }
}

QVariant LinksManager::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    switch (section) {
    case 0: return QCoreApplication::translate("LinksManager", "Page URL");
    case 1: return QCoreApplication::translate("LinksManager", "Title");
    case 2: return QCoreApplication::translate("LinksManager", "Count");
    default:
        return QCoreApplication::translate("LinksManager", "Keyword %1")
                   .arg(section - FIXED_COLS + 1);
    }
}

// =============================================================================
// Private helpers
// =============================================================================

int LinksManager::indexOfUrl(const QString &pageUrl) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).pageUrl == pageUrl) {
            return i;
        }
    }
    return -1;
}

void LinksManager::recomputeMaxKeywords()
{
    int mx = 0;
    for (const auto &e : std::as_const(m_entries)) {
        if (e.keywords.size() > mx) {
            mx = e.keywords.size();
        }
    }
    m_maxKeywords = mx;
}
