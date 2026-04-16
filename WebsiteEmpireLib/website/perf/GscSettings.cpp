#include "GscSettings.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

GscSettings::GscSettings(const QDir &workingDir)
{
    QFile file(workingDir.filePath(QLatin1StringView(FILENAME)));
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return;
    }

    const QJsonObject root = doc.object();
    m_serviceAccountKeyPath = root.value(QStringLiteral("serviceAccountKeyPath")).toString();

    const QJsonObject props = root.value(QStringLiteral("properties")).toObject();
    for (auto it = props.begin(); it != props.end(); ++it) {
        m_properties.insert(it.key(), it.value().toString());
    }
}

bool GscSettings::isConfigured() const
{
    return !m_serviceAccountKeyPath.isEmpty();
}

const QString &GscSettings::serviceAccountKeyPath() const
{
    return m_serviceAccountKeyPath;
}

QString GscSettings::propertyForDomain(const QString &domain) const
{
    return m_properties.value(domain);
}
