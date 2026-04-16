#ifndef LINKSMANAGER_H
#define LINKSMANAGER_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QStringList>

/**
 * Central registry of auto-link keyword entries across every article page.
 *
 * Each row represents one article that has a PageBlocAutoLink bloc configured.
 * Columns 0–2 carry fixed page metadata; columns 3+ list the page's keywords,
 * one keyword per column.  Column headers for columns 3+ are translated via
 * tr() ("Keyword 1", "Keyword 2", …).
 *
 * Column layout:
 *   0  — Page URL (stable identifier)
 *   1  — Page title (filled externally via setPageTitle(); blank until set)
 *   2  — Keyword count
 *   3+ — keyword[col − 3]
 *
 * The column count is dynamic: FIXED_COLS + max(entry.keywords.size() over all
 * entries).  Rows whose list is shorter than the widest entry have empty cells
 * for the missing columns.
 *
 * Use instance() to obtain the application-wide singleton.
 * PageBlocAutoLink::save() calls setKeywords() so the registry always reflects
 * the latest saved state without requiring a full site re-scan.
 */
class LinksManager : public QAbstractTableModel
{
    Q_OBJECT

public:
    struct Entry {
        QString     pageUrl;
        QString     pageTitle;
        QStringList keywords;
    };

    static constexpr int FIXED_COLS = 3;

    /** Returns the application-wide singleton instance. */
    static LinksManager &instance();

    /**
     * Inserts or replaces the keyword list for pageUrl.
     * Passing an empty list removes the entry for that URL.
     * Emits beginResetModel/endResetModel so attached views update immediately.
     */
    void setKeywords(const QString &pageUrl, const QStringList &keywords);

    /**
     * Sets the display title for pageUrl.
     * A no-op when no entry exists for that URL.
     * Emits dataChanged on column 1 of the affected row.
     */
    void setPageTitle(const QString &pageUrl, const QString &title);

    // -------------------------------------------------------------------------
    // QAbstractTableModel interface
    // -------------------------------------------------------------------------
    int      rowCount   (const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data       (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData (int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;

private:
    LinksManager() = default;

    QList<Entry> m_entries;
    int          m_maxKeywords = 0;   // cached; recomputed in setKeywords()

    /** Returns the index of the entry whose pageUrl matches, or -1. */
    int indexOfUrl(const QString &pageUrl) const;

    /** Recomputes m_maxKeywords from m_entries after any change. */
    void recomputeMaxKeywords();
};

#endif // LINKSMANAGER_H
