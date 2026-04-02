#include "ContentDb.h"

ContentDb::ContentDb(const std::string &path)
    : m_db(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
{
    createSchema();
}

SQLite::Database &ContentDb::database()
{
    return m_db;
}

void ContentDb::createSchema()
{
    m_db.exec(
        "CREATE TABLE IF NOT EXISTS pages ("
        "    id         INTEGER PRIMARY KEY,"
        "    path       TEXT UNIQUE NOT NULL,"
        "    domain     TEXT NOT NULL,"
        "    lang       TEXT NOT NULL,"
        "    etag       TEXT NOT NULL,"
        "    updated_at TEXT NOT NULL"
        ")"
    );

    m_db.exec(
        "CREATE TABLE IF NOT EXISTS page_variants ("
        "    id        INTEGER PRIMARY KEY,"
        "    page_id   INTEGER NOT NULL REFERENCES pages(id),"
        "    label     TEXT NOT NULL,"
        "    is_active INTEGER NOT NULL DEFAULT 1,"
        "    html_gz   BLOB NOT NULL,"
        "    etag      TEXT NOT NULL,"
        "    UNIQUE(page_id, label)"
        ")"
    );

    m_db.exec(
        "CREATE TABLE IF NOT EXISTS menu_fragments ("
        "    id         INTEGER PRIMARY KEY,"
        "    domain     TEXT NOT NULL,"
        "    lang       TEXT NOT NULL,"
        "    html_gz    BLOB NOT NULL,"
        "    version_id TEXT NOT NULL,"
        "    updated_at TEXT NOT NULL,"
        "    UNIQUE(domain, lang)"
        ")"
    );

    m_db.exec(
        "CREATE TABLE IF NOT EXISTS redirects ("
        "    old_path    TEXT PRIMARY KEY,"
        "    new_path    TEXT,"
        "    status_code INTEGER NOT NULL"
        ")"
    );
}
