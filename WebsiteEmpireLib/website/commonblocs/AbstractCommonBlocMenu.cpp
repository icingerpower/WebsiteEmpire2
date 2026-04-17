#include "AbstractCommonBlocMenu.h"
#include "website/commonblocs/widgets/WidgetCommonBlocMenu.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

void AbstractCommonBlocMenu::setItems(const QList<MenuItem> &items)
{
    m_items = items;
}

const QList<MenuItem> &AbstractCommonBlocMenu::items() const
{
    return m_items;
}

AbstractCommonBlocWidget *AbstractCommonBlocMenu::createEditWidget()
{
    return new WidgetCommonBlocMenu();
}

QVariantMap AbstractCommonBlocMenu::toMap() const
{
    QJsonArray arr;
    for (const auto &item : std::as_const(m_items)) {
        QJsonObject obj;
        obj[QStringLiteral("label")]  = item.label;
        obj[QStringLiteral("url")]    = item.url;
        obj[QStringLiteral("rel")]    = item.rel;
        obj[QStringLiteral("newTab")] = item.newTab;
        QJsonArray children;
        for (const auto &sub : std::as_const(item.children)) {
            QJsonObject subObj;
            subObj[QStringLiteral("label")]  = sub.label;
            subObj[QStringLiteral("url")]    = sub.url;
            subObj[QStringLiteral("rel")]    = sub.rel;
            subObj[QStringLiteral("newTab")] = sub.newTab;
            children.append(subObj);
        }
        obj[QStringLiteral("children")] = children;
        arr.append(obj);
    }
    return {{QStringLiteral("items"),
             QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))}};
}

void AbstractCommonBlocMenu::fromMap(const QVariantMap &map)
{
    const QByteArray json =
        map.value(QStringLiteral("items")).toString().toUtf8();
    m_items.clear();
    if (json.isEmpty()) {
        return;
    }
    const QJsonArray arr = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &val : arr) {
        const QJsonObject obj = val.toObject();
        MenuItem item;
        item.label  = obj[QStringLiteral("label")].toString();
        item.url    = obj[QStringLiteral("url")].toString();
        item.rel    = obj[QStringLiteral("rel")].toString();
        item.newTab = obj[QStringLiteral("newTab")].toBool();
        const QJsonArray children = obj[QStringLiteral("children")].toArray();
        for (const QJsonValue &cv : children) {
            const QJsonObject cObj = cv.toObject();
            MenuSubItem sub;
            sub.label  = cObj[QStringLiteral("label")].toString();
            sub.url    = cObj[QStringLiteral("url")].toString();
            sub.rel    = cObj[QStringLiteral("rel")].toString();
            sub.newTab = cObj[QStringLiteral("newTab")].toBool();
            item.children.append(sub);
        }
        m_items.append(item);
    }
}

QString AbstractCommonBlocMenu::buildRel(const QString &rel, bool newTab)
{
    if (!newTab) {
        return rel;
    }
    QString r = rel.trimmed();
    if (!r.contains(QStringLiteral("noopener"))) {
        if (!r.isEmpty()) {
            r += QLatin1Char(' ');
        }
        r += QStringLiteral("noopener noreferrer");
    }
    return r;
}

// ---- Translation overrides ----

static QSet<QString> collectAllLabels(const QList<MenuItem> &items)
{
    QSet<QString> labels;
    for (const auto &item : std::as_const(items)) {
        if (!item.label.isEmpty()) {
            labels.insert(item.label);
        }
        for (const auto &sub : std::as_const(item.children)) {
            if (!sub.label.isEmpty()) {
                labels.insert(sub.label);
            }
        }
    }
    return labels;
}

QHash<QString, QString> AbstractCommonBlocMenu::sourceTexts() const
{
    // For menus, we expose a single "items" field whose value is a JSON
    // array of all unique labels.
    const QSet<QString> labels = collectAllLabels(m_items);
    QJsonArray arr;
    for (const auto &lbl : labels) {
        arr.append(lbl);
    }
    QHash<QString, QString> result;
    if (!arr.isEmpty()) {
        result.insert(QStringLiteral("items"),
                      QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    }
    return result;
}

void AbstractCommonBlocMenu::setTranslation(const QString &fieldId,
                                            const QString &langCode,
                                            const QString &translatedText)
{
    if (fieldId != QStringLiteral("items")) {
        return;
    }
    // translatedText is a JSON object: {sourceLabel: translatedLabel, ...}
    const QJsonObject obj = QJsonDocument::fromJson(translatedText.toUtf8()).object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        m_labelTr[it.key()][langCode] = it.value().toString();
    }
}

QStringList AbstractCommonBlocMenu::missingTranslations(const QString &langCode,
                                                        const QString &sourceLangCode) const
{
    if (langCode.isEmpty() || langCode == sourceLangCode) {
        return {};
    }
    const QSet<QString> labels = collectAllLabels(m_items);
    QStringList missing;
    for (const auto &label : labels) {
        const auto srcIt = m_labelTr.constFind(label);
        if (srcIt == m_labelTr.constEnd() || !srcIt.value().contains(langCode)) {
            missing.append(label);
        }
    }
    return missing;
}

const QString &AbstractCommonBlocMenu::resolveLabel(const QString &sourceLabel,
                                                    const QString &lang,
                                                    const QString &sourceLang) const
{
    if (lang.isEmpty() || lang == sourceLang) {
        return sourceLabel;
    }
    const auto srcIt = m_labelTr.constFind(sourceLabel);
    if (srcIt != m_labelTr.constEnd()) {
        const auto langIt = srcIt.value().constFind(lang);
        if (langIt != srcIt.value().constEnd()) {
            return langIt.value();
        }
    }
    return sourceLabel;
}

void AbstractCommonBlocMenu::saveTranslations(QSettings &settings)
{
    if (m_labelTr.isEmpty()) {
        return;
    }

    // Invert m_labelTr (sourceLabel -> langCode -> text) to
    // langCode -> {sourceLabel: text} for compact storage
    QHash<QString, QJsonObject> perLang;
    for (auto srcIt = m_labelTr.cbegin(); srcIt != m_labelTr.cend(); ++srcIt) {
        const auto &langMap = srcIt.value();
        for (auto langIt = langMap.cbegin(); langIt != langMap.cend(); ++langIt) {
            perLang[langIt.key()][srcIt.key()] = langIt.value();
        }
    }

    settings.beginGroup(QStringLiteral("tr_items"));
    for (auto it = perLang.cbegin(); it != perLang.cend(); ++it) {
        settings.setValue(it.key(),
                          QString::fromUtf8(QJsonDocument(it.value()).toJson(QJsonDocument::Compact)));
    }
    settings.endGroup();
}

void AbstractCommonBlocMenu::loadTranslations(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("tr_items"));
    const auto &keys = settings.childKeys();
    for (const auto &langCode : keys) {
        const QByteArray json = settings.value(langCode).toString().toUtf8();
        const QJsonObject obj = QJsonDocument::fromJson(json).object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            m_labelTr[it.key()][langCode] = it.value().toString();
        }
    }
    settings.endGroup();
}

QString AbstractCommonBlocMenu::translatedText(const QString &fieldId,
                                               const QString &langCode) const
{
    return m_labelTr.value(fieldId).value(langCode);
}
