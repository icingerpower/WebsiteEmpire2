#ifndef HUBSEOTEMPLATEDB_H
#define HUBSEOTEMPLATEDB_H

#include <QDir>
#include <QHash>
#include <QList>
#include <QString>

/**
 * Persistent store for per-language hub-page SEO formula translations.
 *
 * Formula strings like "What Causes %1? %2 Conditions To Know" are stored
 * per (typeId, key, langCode).  Hub page types load language-specific
 * templates at render time via AbstractPageType::seoTemplate().
 *
 * Storage: hub_seo.db (SQLite) in the working-directory root.
 */
class HubSeoTemplateDb
{
public:
    explicit HubSeoTemplateDb(const QDir &workingDir);
    ~HubSeoTemplateDb();

    /** Look up a translated template. Returns empty when not found. */
    QString get(const QString &typeId,
                const QString &key,
                const QString &langCode) const;

    /** Store a translated template (INSERT OR REPLACE). */
    void set(const QString &typeId,
             const QString &key,
             const QString &langCode,
             const QString &templ);

    /**
     * Load all stored translations for one typeId.
     * Returns { key → { langCode → translatedTemplate } }.
     * Used by AbstractPageType::bindGenerationContext() to warm the render cache.
     */
    QHash<QString, QHash<QString, QString>> loadAll(const QString &typeId) const;

    /**
     * Returns every (typeId, key, langCode) triple stored in the DB.
     * Used by HubSeoTranslator::buildJobs() to skip already-translated entries.
     */
    struct StoredKey {
        QString typeId;
        QString key;
        QString langCode;
    };
    QList<StoredKey> allStoredKeys() const;

private:
    void _ensureSchema();

    QString m_connName;
};

#endif // HUBSEOTEMPLATEDB_H
