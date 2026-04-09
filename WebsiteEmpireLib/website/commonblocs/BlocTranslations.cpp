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

    const QString newHash = _sha1(source);
    _purgeStaleFor(fieldId, newHash);
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
            continue; // empty source does not require translation
        }
        const auto fieldIt = m_entries.constFind(fieldId);
        if (fieldIt == m_entries.constEnd()) {
            return false;
        }
        if (!fieldIt.value().contains(langCode)) {
            return false;
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
        if (fieldIt == m_entries.constEnd() || !fieldIt.value().contains(langCode)) {
            result.append(fieldId);
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
            const QString storedHash = settings.value(fieldId + QStringLiteral("_hash")).toString();
            const QString currentHash = _sha1(m_sources.value(fieldId));

            if (storedHash == currentHash) {
                Entry entry;
                entry.text = settings.value(fieldId).toString();
                entry.sourceHash = storedHash;
                m_entries[fieldId][langCode] = entry;
            }
            // else: stale translation — do not restore
        }

        settings.endGroup();
    }
}

QString BlocTranslations::_sha1(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex());
}

void BlocTranslations::_purgeStaleFor(const QString &fieldId, const QString &newHash)
{
    auto fieldIt = m_entries.find(fieldId);
    if (fieldIt == m_entries.end()) {
        return;
    }

    auto &langMap = fieldIt.value();
    auto langIt = langMap.begin();
    while (langIt != langMap.end()) {
        if (langIt.value().sourceHash != newHash) {
            langIt = langMap.erase(langIt);
        } else {
            ++langIt;
        }
    }

    if (langMap.isEmpty()) {
        m_entries.erase(fieldIt);
    }
}
