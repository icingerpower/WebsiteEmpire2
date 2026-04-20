#ifndef GENSTRATEGYTABLE_H
#define GENSTRATEGYTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QString>

// Table model for website generation strategies, persisted to strategies.json.
//
// Visible columns: Name, Page type, Theme, Non-svg images, N done, N total.
// Each row carries a hidden UUID id accessible via idForRow() / Qt::UserRole.
// The id never changes after a row is added, so external references remain
// valid across renames or row reordering.
//
// themeId is the stable id of an AbstractTheme, or empty meaning "all themes".
// The JSON file uses stable ids (pageTypeId, themeId) rather than display
// names so that renaming a type or theme never corrupts saved data.
class GenStrategyTable : public QAbstractTableModel
{
    Q_OBJECT
public:
    static constexpr int COL_NAME             = 0;
    static constexpr int COL_PAGE_TYPE        = 1;
    static constexpr int COL_THEME            = 2;
    static constexpr int COL_NON_SVG_IMAGES   = 3;
    static constexpr int COL_PRIMARY_ATTR_ID  = 4; // AbstractPageAttributes::getId() of the aspire primary table
    static constexpr int COL_PRIORITY         = 5; // generation priority: 1 = first (normal), 2+ = improvement passes
    static constexpr int COL_N_DONE           = 6;
    static constexpr int COL_N_TOTAL          = 7;

    explicit GenStrategyTable(const QDir &workingDir, QObject *parent = nullptr);

    // Appends a new fully-populated row, saves, and returns its stable id.
    // themeId may be empty to mean "all themes".
    // customInstructions may be empty to use the generic prompt.
    // primaryAttrId may be empty to mean "no source table linked".
    // priority 1 = normal generation pass; 2+ = improvement passes used when
    //   a page generated with a lower priority number performs poorly.
    QString addRow(const QString &name,
                   const QString &pageTypeId,
                   const QString &themeId,
                   const QString &customInstructions,
                   bool           nonSvgImages,
                   const QString &primaryAttrId = QString{},
                   int            priority = 1);

    // Updates the progress counters for a row and saves.  Used by
    // PaneGeneration::computeRemainingToDo() — never written by the launcher
    // to avoid coupling between headless and GUI code.
    void setNDone(int row, int value);
    void setNTotal(int row, int value);

    // Returns the stable hidden id for visual row, or empty string if out of range.
    QString idForRow(int row) const;

    // Returns the themeId for visual row (empty = all themes), or empty if out of range.
    QString themeIdForRow(int row) const;

    // Returns the custom instructions for visual row, or empty if out of range or not set.
    QString customInstructionsForRow(int row) const;

    // Returns the primaryAttrId for visual row (empty = no source table linked).
    QString primaryAttrIdForRow(int row) const;

    // Returns the resolved path to the aspire DB for visual row.
    // Set automatically when the user picks the file via the Import dialog.
    // Empty = use the standard results_db/<primaryAttrId>.db convention.
    QString primaryDbPathForRow(int row) const;

    // Persists the resolved aspire DB path for visual row and saves strategies.json.
    void setPrimaryDbPath(int row, const QString &path);

    // Returns the priority for visual row (1 = normal, 2+ = improvement passes).
    int priorityForRow(int row) const;

    // Returns the visual row index for id, or -1 if not found.
    int rowForId(const QString &id) const;

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

private:
    struct StrategyRow {
        QString id;
        QString name;
        QString pageTypeId;
        QString themeId;             // empty = all themes
        QString customInstructions;  // empty = use generic prompt
        QString primaryAttrId;       // AbstractPageAttributes::getId() of the aspire primary table; empty = none
        QString primaryDbPath;       // absolute path to the aspire DB file; empty = use results_db/ convention
        bool    nonSvgImages = false;
        int     priority = 1;
        int     nDone  = 0;
        int     nTotal = 0;
    };

    void _load();
    void _save() const;

    QString            m_filePath;
    QList<StrategyRow> m_rows;
};

#endif // GENSTRATEGYTABLE_H
