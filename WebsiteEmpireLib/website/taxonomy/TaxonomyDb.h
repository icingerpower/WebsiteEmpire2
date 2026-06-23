#ifndef TAXONOMYDB_H
#define TAXONOMYDB_H
#include <QDir>
#include <QStringList>

/**
 * Local vocabulary store for taxonomy data synced from aspire databases.
 * Stored in <workingDir>/taxonomy/taxonomy.db (SQLite).
 * Table: items(type TEXT NOT NULL, name TEXT NOT NULL, PRIMARY KEY(type, name))
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

private:
    void _ensureSchema() const;
    QString m_dbPath;
};
#endif
