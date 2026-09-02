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
 *   is_bot      INTEGER NOT NULL DEFAULT 0  -- 1 = crawler User-Agent (BotDetector)
 *
 * page_session
 *   id                   INTEGER PRIMARY KEY AUTOINCREMENT
 *   page_id              TEXT    NOT NULL
 *   scrolling_percentage INTEGER NOT NULL CHECK(scrolling_percentage BETWEEN 0 AND 100)
 *   time_on_page         INTEGER NOT NULL   -- seconds
 *   is_final_page        INTEGER NOT NULL CHECK(is_final_page IN (0,1))
 *   is_bot               INTEGER NOT NULL DEFAULT 0  -- 1 = crawler User-Agent (BotDetector)
 *
 * is_bot is added by ALTER TABLE migration on databases created before bot
 * tracking existed; their old rows keep 0 (unclassified — the User-Agent was
 * never stored, so they count as human until they age out).
 */
class StatsDb
{
public:
    static constexpr const char *FILENAME = "stats.db";

    explicit StatsDb(const std::string &path);

    SQLite::Database &database();

private:
    SQLite::Database m_db;

    void createSchema();

    // Adds the is_bot column to table when missing (pre-bot-tracking files).
    void ensureIsBotColumn(const std::string &table);
};
