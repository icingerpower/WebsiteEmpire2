#include "CategoryHubDirtySet.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

CategoryHubDirtySet::CategoryHubDirtySet(const QDir &workingDir)
    : m_filePath(workingDir.filePath(QLatin1String(FILENAME)))
{
    _load();
}

void CategoryHubDirtySet::add(int hubPageId)
{
    m_ids.insert(hubPageId);
    _save();
}

void CategoryHubDirtySet::addAll(const QSet<int> &hubPageIds)
{
    m_ids.unite(hubPageIds);
    _save();
}

void CategoryHubDirtySet::remove(int hubPageId)
{
    m_ids.remove(hubPageId);
    _save();
}

void CategoryHubDirtySet::clear()
{
    m_ids.clear();
    _save();
}

const QSet<int> &CategoryHubDirtySet::all() const
{
    return m_ids;
}

bool CategoryHubDirtySet::isEmpty() const
{
    return m_ids.isEmpty();
}

bool CategoryHubDirtySet::contains(int hubPageId) const
{
    return m_ids.contains(hubPageId);
}

void CategoryHubDirtySet::_load()
{
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return;
    }
    const QJsonArray arr = doc.object().value(QStringLiteral("hubPageIds")).toArray();
    for (const QJsonValue &v : std::as_const(arr)) {
        const int id = v.toInt(-1);
        if (id > 0) {
            m_ids.insert(id);
        }
    }
}

void CategoryHubDirtySet::_save() const
{
    QJsonArray arr;
    for (int id : std::as_const(m_ids)) {
        arr.append(id);
    }
    QJsonObject obj;
    obj[QStringLiteral("hubPageIds")] = arr;

    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly)) {
        return;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    f.commit();
}
