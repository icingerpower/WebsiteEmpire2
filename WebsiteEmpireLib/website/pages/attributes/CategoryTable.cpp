#include "CategoryTable.h"

#include <QFile>
#include <QMap>
#include <QTextStream>

CategoryTable::CategoryTable(const QDir &workingDir, QObject *parent)
    : QObject(parent)
    , m_filePath(workingDir.absoluteFilePath(QStringLiteral("categories.csv")))
{
    _load();
}

// ---- Queries ----------------------------------------------------------------

const QList<CategoryTable::CategoryRow> &CategoryTable::categories() const
{
    return m_rows;
}

const CategoryTable::CategoryRow *CategoryTable::categoryById(int id) const
{
    for (const auto &row : std::as_const(m_rows)) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
}

QString CategoryTable::translationFor(int id, const QString &langCode) const
{
    const CategoryRow *row = categoryById(id);
    if (!row) {
        return {};
    }
    const QString &translation = row->translations.value(langCode);
    return translation.isEmpty() ? row->name : translation;
}

// ---- Mutations --------------------------------------------------------------

int CategoryTable::addCategory(const QString &name, int parentId)
{
    int maxId = 0;
    for (const auto &row : std::as_const(m_rows)) {
        if (row.id > maxId) {
            maxId = row.id;
        }
    }
    const int newId = maxId + 1;
    CategoryRow row;
    row.id       = newId;
    row.parentId = parentId;
    row.name     = name;
    m_rows.append(row);
    _save();
    emit categoryAdded(newId);
    return newId;
}

void CategoryTable::removeCategory(int id)
{
    // Collect the target id and all its descendants via BFS.
    QList<int> toRemove;
    toRemove.append(id);
    for (int i = 0; i < toRemove.size(); ++i) {
        const int parentId = toRemove.at(i);
        for (const auto &row : std::as_const(m_rows)) {
            if (row.parentId == parentId) {
                toRemove.append(row.id);
            }
        }
    }

    auto isInToRemove = [&toRemove](const CategoryRow &r) {
        return toRemove.contains(r.id);
    };
    m_rows.erase(std::remove_if(m_rows.begin(), m_rows.end(), isInToRemove),
                 m_rows.end());

    _save();
    emit categoryRemoved(toRemove);
}

void CategoryTable::renameCategory(int id, const QString &newName)
{
    for (auto &row : m_rows) {
        if (row.id == id) {
            if (row.name == newName) {
                return;
            }
            row.name = newName;
            _save();
            emit categoryRenamed(id);
            return;
        }
    }
}

void CategoryTable::setTranslation(int id, const QString &langCode, const QString &value)
{
    for (auto &row : m_rows) {
        if (row.id == id) {
            row.translations.insert(langCode, value);
            _save();
            return;
        }
    }
}

// ---- Persistence ------------------------------------------------------------

void CategoryTable::_load()
{
    m_rows.clear();

    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit categoriesReset();
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
    const int idxParentId = col.value(QStringLiteral("ParentId"), -1);
    const int idxName     = col.value(QStringLiteral("Name"),     -1);

    // Any header that is not Id/ParentId/Name is a language-code column.
    QList<QPair<QString, int>> langCols;
    for (auto it = col.cbegin(); it != col.cend(); ++it) {
        const QString &header = it.key();
        if (header != QLatin1String("Id") &&
            header != QLatin1String("ParentId") &&
            header != QLatin1String("Name")) {
            langCols.append({header, it.value()});
        }
    }

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QStringList parts = line.split(';');

        CategoryRow row;
        if (idxId       >= 0 && idxId       < parts.size()) { row.id       = parts.at(idxId).toInt(); }
        if (idxParentId >= 0 && idxParentId < parts.size()) { row.parentId = parts.at(idxParentId).toInt(); }
        if (idxName     >= 0 && idxName     < parts.size()) { row.name     = parts.at(idxName); }

        for (const auto &langCol : std::as_const(langCols)) {
            const int colIdx = langCol.second;
            if (colIdx < parts.size()) {
                const QString &val = parts.at(colIdx);
                if (!val.isEmpty()) {
                    row.translations.insert(langCol.first, val);
                }
            }
        }

        if (row.id > 0) {
            m_rows.append(row);
        }
    }

    emit categoriesReset();
}

void CategoryTable::_save()
{
    // Collect all known language codes from all rows, sorted for stable output.
    QStringList langCodes;
    for (const auto &row : std::as_const(m_rows)) {
        for (const QString &langCode : row.translations.keys()) {
            if (!langCodes.contains(langCode)) {
                langCodes.append(langCode);
            }
        }
    }
    langCodes.sort();

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "CategoryTable: failed to save" << m_filePath << ":" << file.errorString();
        return;
    }

    QTextStream out(&file);

    // Header.
    QStringList headerParts;
    headerParts << QStringLiteral("Id")
                << QStringLiteral("ParentId")
                << QStringLiteral("Name");
    headerParts += langCodes;
    out << headerParts.join(';') << '\n';

    // Rows.
    for (const auto &row : std::as_const(m_rows)) {
        QStringList fields;
        fields << QString::number(row.id)
               << QString::number(row.parentId)
               << row.name;
        for (const auto &langCode : std::as_const(langCodes)) {
            fields << row.translations.value(langCode);
        }
        out << fields.join(';') << '\n';
    }
}
