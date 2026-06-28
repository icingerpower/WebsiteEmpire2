#ifndef TAXONOMYDB_H
#define TAXONOMYDB_H
#include <QDir>
#include <QList>
#include <QPair>
#include <QStringList>

/**
 * Local vocabulary store for taxonomy data synced from aspire databases.
 * Stored in <workingDir>/taxonomy/taxonomy.db (SQLite).
 *
 * Tables:
 *   items(type, name PRIMARY KEY(type, name))
 *   translations(type, name, lang_code, translated_name, PRIMARY KEY(type, name, lang_code))
 *
 * The taxonomy/ directory is created on first write if absent.
 */
class TaxonomyDb
{
public:
    explicit TaxonomyDb(const QDir &workingDir);

    /** Replaces all items for type with names (DELETE + INSERT). */
    void sync(const QString &type, const QStringList &names);

    /** Returns all names for type, alphabetically ordered. */
    QStringList load(const QString &type) const;

    /** Returns the count of items for type. */
    int count(const QString &type) const;

    /** Returns all distinct types present in the items table. */
    QStringList allTypes() const;

    // ---- Translation API -------------------------------------------------------

    /** Stores or overwrites a single translation for an item. */
    void setTranslation(const QString &type, const QString &name,
                        const QString &langCode, const QString &translatedName);

    /** Returns the translation for (type, name) in langCode, falling back to name. */
    QString translationFor(const QString &type, const QString &name,
                           const QString &langCode) const;

    /**
     * Returns English names for type that have no translation for langCode,
     * alphabetically ordered.
     */
    QStringList loadUntranslated(const QString &type, const QString &langCode) const;

    /**
     * Returns (englishName, displayName) pairs ordered alphabetically by displayName.
     * displayName is the translated name or the English name when no translation exists.
     * Use englishName for slug/permalink generation; displayName for rendered HTML.
     */
    QList<QPair<QString, QString>> loadTranslated(const QString &type,
                                                   const QString &langCode) const;

private:
    void _ensureSchema() const;
    QString m_dbPath;
};
#endif
