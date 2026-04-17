#include "TranslationSettings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

TranslationSettings::TranslationSettings(const QDir &workingDir)
{
    QFile f(workingDir.filePath(QLatin1String(FILENAME)));
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return;
    }

    const QJsonObject root = doc.object();

    for (const QJsonValue &v : root.value(QStringLiteral("targetLangs")).toArray()) {
        const QString lang = v.toString().trimmed();
        if (!lang.isEmpty()) {
            targetLangs.append(lang);
        }
    }

    limitPerRun = root.value(QStringLiteral("limitPerRun")).toInt(0);

    for (const QJsonValue &v : root.value(QStringLiteral("priorityPageTypes")).toArray()) {
        const QString t = v.toString().trimmed();
        if (!t.isEmpty()) {
            priorityPageTypes.append(t);
        }
    }
}

bool TranslationSettings::isConfigured() const
{
    return !targetLangs.isEmpty();
}
