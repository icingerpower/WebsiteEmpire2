#include "StatsDb.h"

StatsDb::StatsDb(const std::string &path)
    : m_db(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
{
    // WAL mode: allows the Qt app to read concurrently while Drogon writes.
    m_db.exec("PRAGMA journal_mode=WAL");
    createSchema();
}

SQLite::Database &StatsDb::database()
{
    return m_db;
}

void StatsDb::createSchema()
{
    m_db.exec(
        "CREATE TABLE IF NOT EXISTS displays_clicks ("
        "    id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    page_id    TEXT NOT NULL,"
        "    display_at TEXT NOT NULL,"
        "    clicked_at TEXT,"
        "    is_bot     INTEGER NOT NULL DEFAULT 0 CHECK(is_bot IN (0,1))"
        ")"
    );
    m_db.exec(
        "CREATE TABLE IF NOT EXISTS page_session ("
        "    id                   INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    page_id              TEXT    NOT NULL,"
        "    scrolling_percentage INTEGER NOT NULL CHECK(scrolling_percentage BETWEEN 0 AND 100),"
        "    time_on_page         INTEGER NOT NULL,"
        "    is_final_page        INTEGER NOT NULL CHECK(is_final_page IN (0,1)),"
        "    is_bot               INTEGER NOT NULL DEFAULT 0 CHECK(is_bot IN (0,1))"
        ")"
    );
    // Migration for stats.db files created before bot tracking: CREATE TABLE
    // IF NOT EXISTS never alters an existing table, so the column must be
    // added explicitly.  Pre-existing rows keep DEFAULT 0 (unclassified
    // traffic counts as human — the User-Agent was never stored).
    ensureIsBotColumn("displays_clicks");
    ensureIsBotColumn("page_session");
}

void StatsDb::ensureIsBotColumn(const std::string &table)
{
    SQLite::Statement info(m_db, "PRAGMA table_info(" + table + ")");
    while (info.executeStep()) {
        if (info.getColumn(1).getString() == "is_bot") {
            return;
        }
    }
    m_db.exec("ALTER TABLE " + table
              + " ADD COLUMN is_bot INTEGER NOT NULL DEFAULT 0 CHECK(is_bot IN (0,1))");
}
