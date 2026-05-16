#ifndef PERMALINKHISTORYMODEL_H
#define PERMALINKHISTORYMODEL_H

#include "website/pages/PermalinkHistoryEntry.h"

#include <QAbstractTableModel>
#include <QList>

class IPageRepository;

/**
 * Table model backing DialogPermalinkHistory.
 *
 * Columns:
 *   COL_PERMALINK   — old permalink (read-only)
 *   COL_CHANGED_AT  — ISO 8601 timestamp (read-only)
 *   COL_REDIRECT    — redirect type string key, shown as human-readable label (editable)
 *
 * Redirect type values and their display labels:
 *   "permanent"  → "Permanent 301"   (default)
 *   "parked"     → "Parked 302"
 *   "deleted"    → "Deleted 410"
 *   "none"       → "No redirect"
 *
 * setData() with Qt::EditRole on COL_REDIRECT accepts the raw type value string
 * ("permanent", "parked", "deleted", "none") and immediately persists via
 * IPageRepository::setHistoryRedirectType().
 *
 * The model holds a non-owning reference to IPageRepository; it must outlive
 * this model.
 */
class PermalinkHistoryModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    static constexpr int COL_PERMALINK  = 0;
    static constexpr int COL_CHANGED_AT = 1;
    static constexpr int COL_REDIRECT   = 2;
    static constexpr int COLUMN_COUNT   = 3;

    // Maps each raw redirect type value to its display label.
    static const QStringList &redirectTypeValues();
    static const QStringList &redirectTypeLabels();

    explicit PermalinkHistoryModel(IPageRepository &repo,
                                   int              pageId,
                                   QObject         *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

private:
    IPageRepository             &m_repo;
    int                          m_pageId;
    QList<PermalinkHistoryEntry> m_entries;
};

#endif // PERMALINKHISTORYMODEL_H
