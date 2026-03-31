#include "HostTable.h"

#include <QFile>
#include <QMap>
#include <QTextStream>
#include <QUuid>

static constexpr int COLUMN_COUNT = 5;
static const QString CSV_HEADER   = QStringLiteral("Id;Name;Url;Port;Username;Password");
static const QString ENCRYPT_KEY  = QStringLiteral("WebsiteEmpireSecretKey2026");

HostTable::HostTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
    , m_filePath(workingDir.absoluteFilePath(QStringLiteral("hosts.csv")))
{
    _load();
}

// ---- Public API -------------------------------------------------------------

QString HostTable::addRow()
{
    const int row = m_rows.size();
    beginInsertRows(QModelIndex(), row, row);
    HostRow newRow;
    newRow.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_rows.append(newRow);
    endInsertRows();
    _save();
    return newRow.id;
}

QString HostTable::idForRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    return m_rows.at(row).id;
}

int HostTable::rowForId(const QString &id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

// ---- QAbstractTableModel ----------------------------------------------------

int HostTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

int HostTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return COLUMN_COUNT;
}

QVariant HostTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }

    const auto &row = m_rows.at(index.row());

    if (role == Qt::UserRole) {
        return row.id;
    }

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case COL_NAME:     return row.name;
        case COL_URL:      return row.url;
        case COL_PORT:     return row.port;
        case COL_USERNAME: return row.username;
        case COL_PASSWORD: return row.password;
        default:           break;
        }
    }

    return {};
}

QVariant HostTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    switch (section) {
    case COL_NAME:     return tr("Name");
    case COL_URL:      return tr("URL");
    case COL_PORT:     return tr("Port");
    case COL_USERNAME: return tr("Username");
    case COL_PASSWORD: return tr("Password");
    default:           return {};
    }
}

bool HostTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }
    if (index.row() < 0 || index.row() >= m_rows.size()) {
        return false;
    }

    auto &row = m_rows[index.row()];
    const QString str = value.toString();

    switch (index.column()) {
    case COL_NAME:     if (row.name     == str) { return false; } row.name     = str; break;
    case COL_URL:      if (row.url      == str) { return false; } row.url      = str; break;
    case COL_PORT:     if (row.port     == str) { return false; } row.port     = str; break;
    case COL_USERNAME: if (row.username == str) { return false; } row.username = str; break;
    case COL_PASSWORD: if (row.password == str) { return false; } row.password = str; break;
    default:           return false;
    }

    _save();
    emit dataChanged(index, index, {role});
    return true;
}

Qt::ItemFlags HostTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}

bool HostTable::removeRows(int row, int count, const QModelIndex &parent)
{
    if (row < 0 || count <= 0 || row + count > m_rows.size()) {
        return false;
    }
    beginRemoveRows(parent, row, row + count - 1);
    m_rows.erase(m_rows.begin() + row, m_rows.begin() + row + count);
    endRemoveRows();
    _save();
    return true;
}

// ---- Persistence ------------------------------------------------------------

void HostTable::_load()
{
    beginResetModel();
    m_rows.clear();

    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        endResetModel();
        return;
    }

    QTextStream in(&file);
    const QString headerLine = in.readLine();
    const QStringList headers = headerLine.split(';');

    QMap<QString, int> col;
    for (int i = 0; i < headers.size(); ++i) {
        col.insert(headers.at(i).trimmed(), i);
    }

    const int idxId       = col.value(QStringLiteral("Id"),       -1);
    const int idxName     = col.value(QStringLiteral("Name"),     -1);
    const int idxUrl      = col.value(QStringLiteral("Url"),      -1);
    const int idxPort     = col.value(QStringLiteral("Port"),     -1);
    const int idxUsername = col.value(QStringLiteral("Username"), -1);
    const int idxPassword = col.value(QStringLiteral("Password"), -1);

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QStringList parts = line.split(';');

        HostRow row;
        if (idxId       >= 0 && idxId       < parts.size()) { row.id       = parts.at(idxId); }
        if (idxName     >= 0 && idxName     < parts.size()) { row.name     = parts.at(idxName); }
        if (idxUrl      >= 0 && idxUrl      < parts.size()) { row.url      = parts.at(idxUrl); }
        if (idxPort     >= 0 && idxPort     < parts.size()) { row.port     = parts.at(idxPort); }
        if (idxUsername >= 0 && idxUsername < parts.size()) { row.username = parts.at(idxUsername); }
        if (idxPassword >= 0 && idxPassword < parts.size()) {
            const auto &enc = parts.at(idxPassword);
            row.password = enc.isEmpty() ? QString() : _decrypt(enc);
        }

        // Assign a fresh ID if the CSV row had none (e.g. manually edited file).
        if (row.id.isEmpty()) {
            row.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        m_rows.append(row);
    }

    endResetModel();
}

void HostTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "HostTable: failed to save" << m_filePath << ":" << file.errorString();
        return;
    }

    QTextStream out(&file);
    out << CSV_HEADER << '\n';

    for (const auto &row : std::as_const(m_rows)) {
        const QString encPassword = row.password.isEmpty() ? QString() : _encrypt(row.password);
        QStringList fields;
        fields << row.id
               << row.name
               << row.url
               << row.port
               << row.username
               << encPassword;
        out << fields.join(';') << '\n';
    }
}

// ---- Encryption -------------------------------------------------------------

QString HostTable::_encrypt(const QString &value) const
{
    QString xored;
    xored.reserve(value.size());
    for (int i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        const QChar k = ENCRYPT_KEY.at(i % ENCRYPT_KEY.size());
        xored.append(QChar(c.unicode() ^ k.unicode()));
    }

    QString result;
    result.reserve(xored.size() * 4);
    for (const QChar &ch : std::as_const(xored)) {
        result.append(QString::number(ch.unicode(), 16).rightJustified(4, '0'));
    }
    return result;
}

QString HostTable::_decrypt(const QString &value) const
{
    QString xored;
    xored.reserve(value.size() / 4);
    for (int i = 0; i + 3 < value.size(); i += 4) {
        const QString hex = value.mid(i, 4);
        bool ok;
        const ushort unicode = hex.toUShort(&ok, 16);
        if (ok) {
            xored.append(QChar(unicode));
        }
    }

    QString result;
    result.reserve(xored.size());
    for (int i = 0; i < xored.size(); ++i) {
        const QChar c = xored.at(i);
        const QChar k = ENCRYPT_KEY.at(i % ENCRYPT_KEY.size());
        result.append(QChar(c.unicode() ^ k.unicode()));
    }
    return result;
}
