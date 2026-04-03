#ifndef WEBSITESETTINGSTABLE_H
#define WEBSITESETTINGSTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QString>

// Table model for global website settings, persisted to settings_global.csv.
//
// Visible columns: Parameter (read-only, translated), Value (editable).
// Each row carries a hidden stable ID accessible via Qt::UserRole.
//
// The CSV stores only Id;Value — the parameter label is derived from the ID
// at runtime via tr(), so renaming a parameter in the UI never breaks
// existing saved data.
//
// Row order, added rows, and deleted rows in the CSV are all handled by
// matching rows to their stable ID rather than by position.
class WebsiteSettingsTable : public QAbstractTableModel
{
    Q_OBJECT
public:
    static constexpr int COL_PARAMETER = 0;
    static constexpr int COL_VALUE     = 1;

    // Stable IDs — never change once data has been saved.
    static const QString ID_WEBSITE_NAME;
    static const QString ID_AUTHOR;
    static const QString ID_BASE_URL;

    explicit WebsiteSettingsTable(const QDir &workingDir, QObject *parent = nullptr);

    // Returns the current value for the given stable ID, or empty string if not found.
    QString valueForId(const QString &id) const;

    // Named getters — each returns valueForId() for the corresponding stable ID.
    QString websiteName() const;
    QString author()      const;
    QString baseUrl()     const;

    // QAbstractItemModel interface
    int           rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int           columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant      data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant      headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool          setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    struct SettingRow {
        QString id;    // stable machine ID — saved in CSV, used for reload matching
        QString label; // translated parameter name — NOT saved, derived from id at construction
        QString value; // user-set value — saved in CSV
    };

    void _load();
    void _save();

    QString           m_filePath;
    QList<SettingRow> m_rows;
};

#endif // WEBSITESETTINGSTABLE_H
