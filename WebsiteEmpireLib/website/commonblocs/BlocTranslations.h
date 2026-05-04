#ifndef BLOCTRANSLATIONS_H
#define BLOCTRANSLATIONS_H

#include <QHash>
#include <QStringList>

class QSettings;

/**
 * Per-field, per-language translation store for a common bloc.
 *
 * Register fields via setSource(fieldId, sourceText).  Setting new source
 * text automatically purges any translation whose stored source-hash no
 * longer matches.  Translations for other fields are unaffected.
 *
 * Persistence uses QSettings sub-groups named "tr_<langCode>" under the
 * caller's current group:
 *
 *   [header]
 *   title=My Site
 *
 *   [header\tr_fr]
 *   title=Mon site
 *   title_hash=<sha1 of "My Site">
 */
class BlocTranslations
{
public:
    /**
     * Register or update source text for a field.
     * Existing translations are kept even if stale — they continue to be served
     * until the translation pipeline explicitly replaces them.
     */
    void setSource(const QString &fieldId, const QString &source);

    /**
     * Returns the current source text for fieldId, or a shared empty string
     * if fieldId is not registered.
     */
    const QString &source(const QString &fieldId) const;

    /**
     * Store a translation produced from the current source text.
     * Silently ignored if fieldId is not registered.
     */
    void setTranslation(const QString &fieldId,
                        const QString &langCode,
                        const QString &translatedText);

    /**
     * True iff every registered field that has non-empty source text has a
     * fresh (hash-matching) translation for langCode.  A stale translation
     * (source changed since last translation) counts as missing.
     */
    bool isComplete(const QString &langCode) const;

    /**
     * Field IDs that have non-empty source but either no translation for
     * langCode, or a stale one (hash mismatch).
     */
    QStringList missingFields(const QString &langCode) const;

    /**
     * All lang codes that have at least one translation (across any field).
     */
    QStringList knownLangCodes() const;

    /**
     * Returns the translated text, or a shared empty string if absent.
     */
    const QString &translation(const QString &fieldId,
                               const QString &langCode) const;

    /**
     * Persist to QSettings.  Caller must have called settings.beginGroup(blocId)
     * already.  Writes sub-groups named "tr_<langCode>".
     */
    void saveToSettings(QSettings &settings) const;

    /**
     * Restore from QSettings.  Caller must have called settings.beginGroup(blocId)
     * already.  Loads all translations regardless of hash — stale ones are
     * preserved for serving until re-translated.
     */
    void loadFromSettings(QSettings &settings);

    /**
     * Persist all translations as flat key→value pairs into map.
     * Key format: "tr:<langCode>:<fieldId>"  — value: translated text.
     *             "tr:<langCode>:<fieldId>:hash" — value: source SHA1.
     * Compatible with the page_data flat map (prefixed by AbstractPageType).
     * Caller must ensure setSource() has been called for each field first.
     */
    void saveToMap(QHash<QString, QString> &map) const;

    /**
     * Restore translations from a flat key→value map produced by saveToMap().
     * Loads all entries regardless of hash — stale ones are preserved for
     * serving until the translation pipeline replaces them.
     * Must be called after setSource() has been called for each known field.
     */
    void loadFromMap(const QHash<QString, QString> &map);

private:
    struct Entry {
        QString text;
        QString sourceHash;
    };

    QStringList                           m_fieldOrder;
    QHash<QString, QString>               m_sources;  // fieldId -> source text
    QHash<QString, QHash<QString, Entry>> m_entries;  // fieldId -> langCode -> Entry

    static QString _sha1(const QString &text);
    void           _purgeStaleFor(const QString &fieldId, const QString &newHash);
};

#endif // BLOCTRANSLATIONS_H
