#include "WebsiteSettingsTable.h"

#include "CountryLangManager.h"

#include <QFile>
#include <QMap>
#include <QTextStream>

#include <algorithm>

static constexpr int COLUMN_COUNT = 2;
static const QString CSV_HEADER   = QStringLiteral("Id;Value");

const QString WebsiteSettingsTable::ID_WEBSITE_NAME      = QStringLiteral("website_name");
const QString WebsiteSettingsTable::ID_AUTHOR            = QStringLiteral("author");
const QString WebsiteSettingsTable::ID_EDITING_LANG_CODE = QStringLiteral("editing_lang_code");

WebsiteSettingsTable::WebsiteSettingsTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
    , m_filePath(workingDir.absoluteFilePath(QStringLiteral("settings_global.csv")))
{
    QStringList langCodes = CountryLangManager::instance()->defaultLangCodes();
    langCodes.prepend(QStringLiteral("en"));
    std::sort(langCodes.begin(), langCodes.end());

    m_rows = {
        { ID_WEBSITE_NAME,      tr("Website name"),    {},           {} },
        { ID_AUTHOR,            tr("Author"),           {},           {} },
        { ID_EDITING_LANG_CODE, tr("Editing language"), QStringLiteral("en"), langCodes },
    };
    _load();
}

// ---- Named getters ----------------------------------------------------------

QString WebsiteSettingsTable::websiteName()     const { return valueForId(ID_WEBSITE_NAME); }
QString WebsiteSettingsTable::author()          const { return valueForId(ID_AUTHOR); }
QString WebsiteSettingsTable::editingLangCode() const { return valueForId(ID_EDITING_LANG_CODE); }

QString WebsiteSettingsTable::valueForId(const QString &id) const
{
    for (const auto &row : std::as_const(m_rows)) {
        if (row.id == id) {
            return row.value;
        }
    }
    return {};
}

// ---- QAbstractTableModel ----------------------------------------------------

int WebsiteSettingsTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

int WebsiteSettingsTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return COLUMN_COUNT;
}

QVariant WebsiteSettingsTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }

    const auto &row = m_rows.at(index.row());

    if (role == Qt::UserRole) {
        return row.id;
    }

    if (role == AllowedValuesRole) {
        return row.allowedValues;
    }

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case COL_PARAMETER: return row.label;
        case COL_VALUE:     return row.value;
        default:            break;
        }
    }

    return {};
}

QVariant WebsiteSettingsTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    switch (section) {
    case COL_PARAMETER: return tr("Parameter");
    case COL_VALUE:     return tr("Value");
    default:            return {};
    }
}

bool WebsiteSettingsTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }
    if (index.row() < 0 || index.row() >= m_rows.size()) {
        return false;
    }
    if (index.column() != COL_VALUE) {
        return false;
    }

    auto &row = m_rows[index.row()];
    const QString str = value.toString();
    if (row.value == str) {
        return false;
    }
    row.value = str;
    _save();
    emit dataChanged(index, index, {role});
    return true;
}

Qt::ItemFlags WebsiteSettingsTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags f = QAbstractItemModel::flags(index);
    if (index.column() == COL_VALUE) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

// ---- Persistence ------------------------------------------------------------

void WebsiteSettingsTable::_load()
{
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&file);
    const QString headerLine = in.readLine();
    const QStringList headers = headerLine.split(';');

    QMap<QString, int> col;
    for (int i = 0; i < headers.size(); ++i) {
        col.insert(headers.at(i).trimmed(), i);
    }

    const int idxId    = col.value(QStringLiteral("Id"),    -1);
    const int idxValue = col.value(QStringLiteral("Value"), -1);

    QMap<QString, QString> savedValues;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QStringList parts = line.split(';');
        if (idxId >= 0 && idxId < parts.size()) {
            const QString &id    = parts.at(idxId);
            const QString  value = (idxValue >= 0 && idxValue < parts.size())
                                       ? parts.at(idxValue)
                                       : QString();
            savedValues.insert(id, value);
        }
    }

    for (auto &row : m_rows) {
        if (savedValues.contains(row.id)) {
            row.value = savedValues.value(row.id);
        }
    }
}

void WebsiteSettingsTable::_save()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "WebsiteSettingsTable: failed to save" << m_filePath << ":" << file.errorString();
        return;
    }

    QTextStream out(&file);
    out << CSV_HEADER << '\n';

    for (const auto &row : std::as_const(m_rows)) {
        out << row.id << ';' << row.value << '\n';
    }
}
