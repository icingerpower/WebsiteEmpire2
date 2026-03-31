#ifndef HOSTTABLE_H
#define HOSTTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QString>

// Table model for host credentials, persisted to hosts.csv.
//
// Visible columns: Name, Url, Port, Username, Password.
// Password is obfuscated in the CSV (XOR + hex encoding) but held as plain
// text in memory.
//
// Every row carries a hidden UUID ID that is NOT shown in the model but is
// accessible via idForRow() or data(..., Qt::UserRole).  This lets callers
// record a selection by ID and restore it reliably even if rows are
// reordered or removed.
class HostTable : public QAbstractTableModel
{
    Q_OBJECT
public:
    static constexpr int COL_NAME     = 0;
    static constexpr int COL_URL      = 1;
    static constexpr int COL_PORT     = 2;
    static constexpr int COL_USERNAME = 3;
    static constexpr int COL_PASSWORD = 4;

    explicit HostTable(const QDir &workingDir, QObject *parent = nullptr);

    // Appends a blank row, saves, and returns its stable UUID.
    QString addRow();

    // Returns the stable hidden ID for row, or an empty string if out of range.
    QString idForRow(int row) const;

    // Returns the row index for id, or -1 if not found.
    int rowForId(const QString &id) const;

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

private:
    struct HostRow {
        QString id;
        QString name;
        QString url;
        QString port;
        QString username;
        QString password; // plain text in memory; obfuscated on disk
    };

    void _load();
    void _save();
    QString _encrypt(const QString &value) const;
    QString _decrypt(const QString &value) const;

    QString m_filePath;
    QList<HostRow> m_rows;
};

#endif // HOSTTABLE_H
