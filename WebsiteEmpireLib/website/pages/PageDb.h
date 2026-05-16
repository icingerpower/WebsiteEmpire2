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
 *   id                  INTEGER PRIMARY KEY AUTOINCREMENT
 *   type_id             TEXT    NOT NULL
 *   permalink           TEXT    UNIQUE NOT NULL
 *   lang                TEXT    NOT NULL
 *   created_at          TEXT    NOT NULL   -- ISO 8601 UTC
 *   updated_at          TEXT    NOT NULL   -- ISO 8601 UTC
 *   translated_at       TEXT               -- ISO 8601 UTC; migration column
 *   source_page_id      INTEGER            -- NULL for root pages; migration column
 *   generated_at        TEXT               -- ISO 8601 UTC; migration column
 *   langs_to_translate  TEXT               -- comma-separated BCP-47; migration column
 *   generation_state    INTEGER DEFAULT 0  -- PageGenerationState enum; migration column
 *   flags               INTEGER DEFAULT 0  -- PageFlag bitmask; migration column
 *   end_permalink       TEXT    DEFAULT '' -- URL slug suffix from strategy (e.g. "genes-biomarkers"); migration column
 *   published_at        TEXT               -- ISO 8601 UTC; NULL until first deploy; migration column
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
 * page_translation_image_states     -- per-language social image generation state
 *   page_id     INTEGER NOT NULL REFERENCES pages(id) ON DELETE CASCADE
 *   lang        TEXT    NOT NULL   -- BCP-47 target language code
 *   state       INTEGER NOT NULL DEFAULT 0  -- PageGenerationState (Pending=0 / Complete=3)
 *   PRIMARY KEY (page_id, lang)
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
