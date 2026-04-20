#include "GenStrategyTable.h"

#include "aspire/generator/AbstractGenerator.h"
#include "website/theme/AbstractTheme.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

static constexpr int COLUMN_COUNT = 8;

static const QString JSON_KEY_ID                   = QStringLiteral("id");
static const QString JSON_KEY_NAME                 = QStringLiteral("name");
static const QString JSON_KEY_PAGE_TYPE_ID         = QStringLiteral("pageTypeId");
static const QString JSON_KEY_THEME_ID             = QStringLiteral("themeId");
static const QString JSON_KEY_CUSTOM_INSTRUCTIONS  = QStringLiteral("customInstructions");
static const QString JSON_KEY_PRIMARY_ATTR_ID      = QStringLiteral("primaryAttrId");
static const QString JSON_KEY_PRIMARY_DB_PATH      = QStringLiteral("primaryDbPath");
static const QString JSON_KEY_NON_SVG_IMAGES       = QStringLiteral("nonSvgImages");
static const QString JSON_KEY_PRIORITY             = QStringLiteral("priority");
static const QString JSON_KEY_N_DONE               = QStringLiteral("nDone");
static const QString JSON_KEY_N_TOTAL              = QStringLiteral("nTotal");

GenStrategyTable::GenStrategyTable(const QDir &workingDir, QObject *parent)
    : QAbstractTableModel(parent)
    , m_filePath(workingDir.absoluteFilePath(QStringLiteral("strategies.json")))
{
    _load();
}

// ---- Public API -------------------------------------------------------------

QString GenStrategyTable::addRow(const QString &name,
                                  const QString &pageTypeId,
                                  const QString &themeId,
                                  const QString &customInstructions,
                                  bool           nonSvgImages,
                                  const QString &primaryAttrId,
                                  int            priority)
{
    const int row = m_rows.size();
    beginInsertRows(QModelIndex(), row, row);
    StrategyRow newRow;
    newRow.id                 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    newRow.name               = name;
    newRow.pageTypeId         = pageTypeId;
    newRow.themeId            = themeId;
    newRow.customInstructions = customInstructions;
    newRow.primaryAttrId      = primaryAttrId;
    newRow.nonSvgImages       = nonSvgImages;
    newRow.priority           = qMax(1, priority);
    m_rows.append(newRow);
    endInsertRows();
    _save();
    return newRow.id;
}

QString GenStrategyTable::idForRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    return m_rows.at(row).id;
}

QString GenStrategyTable::themeIdForRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    return m_rows.at(row).themeId;
}

QString GenStrategyTable::customInstructionsForRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    return m_rows.at(row).customInstructions;
}

QString GenStrategyTable::primaryAttrIdForRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    return m_rows.at(row).primaryAttrId;
}

QString GenStrategyTable::primaryDbPathForRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    return m_rows.at(row).primaryDbPath;
}

void GenStrategyTable::setPrimaryDbPath(int row, const QString &path)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    m_rows[row].primaryDbPath = path;
    _save();
}

int GenStrategyTable::priorityForRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return 1;
    }
    return m_rows.at(row).priority;
}

void GenStrategyTable::setNDone(int row, int value)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    auto &r = m_rows[row];
    if (r.nDone == value) {
        return;
    }
    r.nDone = value;
    const QModelIndex idx = index(row, COL_N_DONE);
    emit dataChanged(idx, idx, {Qt::DisplayRole});
    _save();
}

void GenStrategyTable::setNTotal(int row, int value)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    auto &r = m_rows[row];
    if (r.nTotal == value) {
        return;
    }
    r.nTotal = value;
    const QModelIndex idx = index(row, COL_N_TOTAL);
    emit dataChanged(idx, idx, {Qt::DisplayRole});
    _save();
}

int GenStrategyTable::rowForId(const QString &id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

// ---- QAbstractTableModel ----------------------------------------------------

int GenStrategyTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

int GenStrategyTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return COLUMN_COUNT;
}

QVariant GenStrategyTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }

    const auto &row = m_rows.at(index.row());

    if (role == Qt::UserRole) {
        return row.id;
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case COL_NAME:      return row.name;
        case COL_PAGE_TYPE: return row.pageTypeId;
        case COL_THEME: {
            if (row.themeId.isEmpty()) {
                return tr("All");
            }
            const AbstractTheme *proto =
                AbstractTheme::ALL_THEMES().value(row.themeId, nullptr);
            return proto ? proto->getName() : row.themeId;
        }
        case COL_NON_SVG_IMAGES: return row.nonSvgImages ? tr("Yes") : tr("No");
        case COL_PRIORITY:       return row.priority;
        case COL_PRIMARY_ATTR_ID: {
            if (row.primaryAttrId.isEmpty()) {
                return tr("(None)");
            }
            for (auto it = AbstractGenerator::ALL_GENERATORS().constBegin();
                 it != AbstractGenerator::ALL_GENERATORS().constEnd(); ++it) {
                const AbstractGenerator::GeneratorTables tables = it.value()->getTables();
                if (tables.primary.contains(row.primaryAttrId)) {
                    return tables.primary.value(row.primaryAttrId).name;
                }
            }
            return row.primaryAttrId; // fallback: raw id
        }
        case COL_N_DONE:  return row.nDone;
        case COL_N_TOTAL: return row.nTotal;
        default:          break;
        }
    }

    return {};
}

QVariant GenStrategyTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    switch (section) {
    case COL_NAME:            return tr("Name");
    case COL_PAGE_TYPE:       return tr("Page type");
    case COL_THEME:           return tr("Theme");
    case COL_NON_SVG_IMAGES:  return tr("Non-svg images");
    case COL_PRIMARY_ATTR_ID: return tr("Source table");
    case COL_PRIORITY:        return tr("Priority");
    case COL_N_DONE:          return tr("N done");
    case COL_N_TOTAL:         return tr("N total");
    default:                  return {};
    }
}

Qt::ItemFlags GenStrategyTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return QAbstractItemModel::flags(index);
}

bool GenStrategyTable::removeRows(int row, int count, const QModelIndex &parent)
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

void GenStrategyTable::_load()
{
    beginResetModel();
    m_rows.clear();

    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        endResetModel();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        endResetModel();
        return;
    }

    for (const QJsonValue &val : doc.array()) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject obj = val.toObject();

        StrategyRow row;
        row.id                 = obj.value(JSON_KEY_ID).toString();
        row.name               = obj.value(JSON_KEY_NAME).toString();
        row.pageTypeId         = obj.value(JSON_KEY_PAGE_TYPE_ID).toString();
        row.themeId            = obj.value(JSON_KEY_THEME_ID).toString();
        row.customInstructions = obj.value(JSON_KEY_CUSTOM_INSTRUCTIONS).toString();
        row.primaryAttrId      = obj.value(JSON_KEY_PRIMARY_ATTR_ID).toString();
        row.primaryDbPath      = obj.value(JSON_KEY_PRIMARY_DB_PATH).toString();
        row.nonSvgImages       = obj.value(JSON_KEY_NON_SVG_IMAGES).toBool(false);
        row.priority           = obj.value(JSON_KEY_PRIORITY).toInt(1);
        row.nDone              = obj.value(JSON_KEY_N_DONE).toInt(0);
        row.nTotal             = obj.value(JSON_KEY_N_TOTAL).toInt(0);

        if (row.id.isEmpty()) {
            row.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        m_rows.append(row);
    }

    endResetModel();
}

void GenStrategyTable::_save() const
{
    QJsonArray arr;
    for (const auto &row : std::as_const(m_rows)) {
        QJsonObject obj;
        obj.insert(JSON_KEY_ID,                  row.id);
        obj.insert(JSON_KEY_NAME,                row.name);
        obj.insert(JSON_KEY_PAGE_TYPE_ID,        row.pageTypeId);
        obj.insert(JSON_KEY_THEME_ID,            row.themeId);
        obj.insert(JSON_KEY_CUSTOM_INSTRUCTIONS, row.customInstructions);
        obj.insert(JSON_KEY_PRIMARY_ATTR_ID,     row.primaryAttrId);
        obj.insert(JSON_KEY_PRIMARY_DB_PATH,     row.primaryDbPath);
        obj.insert(JSON_KEY_NON_SVG_IMAGES,      row.nonSvgImages);
        obj.insert(JSON_KEY_PRIORITY,            row.priority);
        obj.insert(JSON_KEY_N_DONE,              row.nDone);
        obj.insert(JSON_KEY_N_TOTAL,             row.nTotal);
        arr.append(obj);
    }

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "GenStrategyTable: failed to save" << m_filePath << ":" << file.errorString();
        return;
    }
    file.write(QJsonDocument(arr).toJson());
}
