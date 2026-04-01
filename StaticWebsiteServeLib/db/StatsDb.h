#pragma once

#include <string>

#include <SQLiteCpp/SQLiteCpp.h>

/**
 * Owns the connection to stats.db and ensures the schema exists.
 * Opened with WAL journal mode for safe concurrent reads from the Qt app.
 *
 * Schema
 * ------
 * displays_clicks
 *   id          INTEGER PRIMARY KEY AUTOINCREMENT
 *   page_id     TEXT    NOT NULL
 *   display_at  TEXT    NOT NULL   -- ISO 8601 UTC
 *   clicked_at  TEXT               -- NULL until a click is recorded
 *
 * page_session
 *   id                   INTEGER PRIMARY KEY AUTOINCREMENT
 *   page_id              TEXT    NOT NULL
 *   scrolling_percentage INTEGER NOT NULL CHECK(scrolling_percentage BETWEEN 0 AND 100)
 *   time_on_page         INTEGER NOT NULL   -- seconds
 *   is_final_page        INTEGER NOT NULL CHECK(is_final_page IN (0,1))
 */
class StatsDb
{
public:
    explicit StatsDb(const std::string &path);

    SQLite::Database &database();

private:
    SQLite::Database m_db;

    void createSchema();
};
