#include "BlocTranslations.h"

#include <QCryptographicHash>
#include <QSettings>

static const QString s_emptyString;

void BlocTranslations::setSource(const QString &fieldId, const QString &source)
{
    if (!m_sources.contains(fieldId)) {
        m_fieldOrder.append(fieldId);
    }
    m_sources.insert(fieldId, source);
}

const QString &BlocTranslations::source(const QString &fieldId) const
{
    auto it = m_sources.constFind(fieldId);
    if (it != m_sources.constEnd()) {
        return it.value();
    }
    return s_emptyString;
}

void BlocTranslations::setTranslation(const QString &fieldId,
                                      const QString &langCode,
                                      const QString &translatedText)
{
    if (!m_sources.contains(fieldId)) {
        return; // silently ignore unregistered fields
    }

    Entry entry;
    entry.text = translatedText;
    entry.sourceHash = _sha1(m_sources.value(fieldId));
    m_entries[fieldId][langCode] = entry;
}

bool BlocTranslations::isComplete(const QString &langCode) const
{
    for (const auto &fieldId : std::as_const(m_fieldOrder)) {
        const auto &src = m_sources.value(fieldId);
        if (src.isEmpty()) {
            continue;
        }
        const auto fieldIt = m_entries.constFind(fieldId);
        if (fieldIt == m_entries.constEnd()) {
            return false;
        }
        const auto langIt = fieldIt.value().constFind(langCode);
        if (langIt == fieldIt.value().constEnd()) {
            return false;
        }
        if (langIt.value().sourceHash != _sha1(src)) {
            return false; // stale — needs retranslation
        }
    }
    return true;
}

QStringList BlocTranslations::missingFields(const QString &langCode) const
{
    QStringList result;
    for (const auto &fieldId : std::as_const(m_fieldOrder)) {
        const auto &src = m_sources.value(fieldId);
        if (src.isEmpty()) {
            continue;
        }
        const auto fieldIt = m_entries.constFind(fieldId);
        if (fieldIt == m_entries.constEnd()) {
            result.append(fieldId);
            continue;
        }
        const auto langIt = fieldIt.value().constFind(langCode);
        if (langIt == fieldIt.value().constEnd()) {
            result.append(fieldId);
            continue;
        }
        if (langIt.value().sourceHash != _sha1(src)) {
            result.append(fieldId); // stale — needs retranslation
        }
    }
    return result;
}

QStringList BlocTranslations::knownLangCodes() const
{
    QSet<QString> langs;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        const auto &langMap = it.value();
        for (auto langIt = langMap.cbegin(); langIt != langMap.cend(); ++langIt) {
            langs.insert(langIt.key());
        }
    }
    return langs.values();
}

const QString &BlocTranslations::translation(const QString &fieldId,
                                             const QString &langCode) const
{
    const auto fieldIt = m_entries.constFind(fieldId);
    if (fieldIt == m_entries.constEnd()) {
        return s_emptyString;
    }
    const auto langIt = fieldIt.value().constFind(langCode);
    if (langIt == fieldIt.value().constEnd()) {
        return s_emptyString;
    }
    const auto srcIt = m_sources.constFind(fieldId);
    if (srcIt == m_sources.constEnd()) {
        return s_emptyString;
    }
    if (langIt.value().sourceHash != _sha1(srcIt.value())) {
        return s_emptyString; // stale — source changed since this translation was produced
    }
    return langIt.value().text;
}

void BlocTranslations::saveToSettings(QSettings &settings) const
{
    // Collect all lang codes
    const QStringList langs = knownLangCodes();

    for (const auto &langCode : std::as_const(langs)) {
        settings.beginGroup(QStringLiteral("tr_") + langCode);
        for (const auto &fieldId : std::as_const(m_fieldOrder)) {
            const auto fieldIt = m_entries.constFind(fieldId);
            if (fieldIt == m_entries.constEnd()) {
                continue;
            }
            const auto langIt = fieldIt.value().constFind(langCode);
            if (langIt == fieldIt.value().constEnd()) {
                continue;
            }
            settings.setValue(fieldId, langIt.value().text);
            settings.setValue(fieldId + QStringLiteral("_hash"), langIt.value().sourceHash);
        }
        settings.endGroup();
    }
}

void BlocTranslations::loadFromSettings(QSettings &settings)
{
    const auto &groups = settings.childGroups();
    for (const auto &group : groups) {
        if (!group.startsWith(QStringLiteral("tr_"))) {
            continue;
        }
        const QString langCode = group.mid(3); // strip "tr_"
        settings.beginGroup(group);

        for (const auto &fieldId : std::as_const(m_fieldOrder)) {
            if (!settings.contains(fieldId)) {
                continue;
            }
            Entry entry;
            entry.text       = settings.value(fieldId).toString();
            entry.sourceHash = settings.value(fieldId + QStringLiteral("_hash")).toString();
            m_entries[fieldId][langCode] = entry;
        }

        settings.endGroup();
    }
}

QString BlocTranslations::_sha1(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex());
}

// =============================================================================
// saveToMap / loadFromMap
// =============================================================================

void BlocTranslations::saveToMap(QHash<QString, QString> &map) const
{
    for (auto fieldIt = m_entries.cbegin(); fieldIt != m_entries.cend(); ++fieldIt) {
        const QString &fieldId = fieldIt.key();
        for (auto langIt = fieldIt.value().cbegin(); langIt != fieldIt.value().cend(); ++langIt) {
            const QString &lang        = langIt.key();
            const Entry   &entry       = langIt.value();
            if (entry.text.isEmpty()) {
                continue;
            }
            const QString prefix = QStringLiteral("tr:") + lang + QStringLiteral(":") + fieldId;
            map.insert(prefix,                              entry.text);
            map.insert(prefix + QStringLiteral(":hash"),   entry.sourceHash);
        }
    }
}

void BlocTranslations::loadFromMap(const QHash<QString, QString> &map)
{
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        const QString &key = it.key();
        // Keys of interest: "tr:<lang>:<fieldId>" (no trailing ":hash")
        if (!key.startsWith(QStringLiteral("tr:"))) {
            continue;
        }
        if (key.endsWith(QStringLiteral(":hash"))) {
            continue;
        }

        // Split "tr:<lang>:<fieldId>" — exactly 3 colon-separated parts.
        const int firstColon  = key.indexOf(QLatin1Char(':'));           // after "tr"
        const int secondColon = key.indexOf(QLatin1Char(':'), firstColon + 1);
        if (firstColon < 0 || secondColon < 0) {
            continue;
        }
        const QString lang    = key.mid(firstColon + 1, secondColon - firstColon - 1);
        const QString fieldId = key.mid(secondColon + 1);

        if (!m_sources.contains(fieldId)) {
            continue; // unknown field — skip
        }

        Entry entry;
        entry.text       = it.value();
        entry.sourceHash = map.value(key + QStringLiteral(":hash"));
        m_entries[fieldId][lang] = entry;
    }
}

