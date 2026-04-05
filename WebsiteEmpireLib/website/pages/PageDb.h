#ifndef PAGEDB_H
#define PAGEDB_H

#include <QDir>
#include <QSqlDatabase>
#include <QString>

/**
 * Owns the QSqlDatabase connection to pages.db and ensures the schema exists.
 *
 * Schema
 * ------
 * pages
 *   id          INTEGER PRIMARY KEY AUTOINCREMENT
 *   type_id     TEXT    NOT NULL
 *   permalink   TEXT    UNIQUE NOT NULL
 *   lang        TEXT    NOT NULL
 *   created_at  TEXT    NOT NULL   -- ISO 8601 UTC
 *   updated_at  TEXT    NOT NULL   -- ISO 8601 UTC
 *
 * page_data                         -- flat key/value store for bloc content
 *   page_id     INTEGER NOT NULL REFERENCES pages(id) ON DELETE CASCADE
 *   key         TEXT    NOT NULL
 *   value       TEXT    NOT NULL
 *   PRIMARY KEY (page_id, key)
 *
 * permalink_history                 -- audit trail for redirect rule generation
 *   id          INTEGER PRIMARY KEY AUTOINCREMENT
 *   page_id     INTEGER NOT NULL REFERENCES pages(id) ON DELETE CASCADE
 *   old_permalink TEXT  NOT NULL
 *   changed_at  TEXT    NOT NULL   -- ISO 8601 UTC
 *
 * Multiple PageDb instances in the same process each get a unique connection
 * name to satisfy Qt's "one connection per database name" requirement.
 *
 * Usage: pass workingDir to the constructor; the database file is created at
 * workingDir/pages.db.
 */
class PageDb
{
public:
    static constexpr const char *FILENAME = "pages.db";

    explicit PageDb(const QDir &workingDir);
    ~PageDb();

    // Returns a handle to the underlying connection (by value — Qt semantics).
    QSqlDatabase database() const;

    // Exposed for logging / diagnostics.
    const QString &connectionName() const;

private:
    QString m_connectionName;

    void createSchema();
};

#endif // PAGEDB_H
