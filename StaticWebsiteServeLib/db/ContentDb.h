#pragma once

#include <string>

#include <SQLiteCpp/SQLiteCpp.h>

/**
 * Owns the connection to content.db and ensures the schema exists.
 *
 * Schema
 * ------
 * pages
 *   id         INTEGER PRIMARY KEY
 *   path       TEXT UNIQUE NOT NULL   -- URL path, e.g. "/my-page.html"
 *   domain     TEXT NOT NULL          -- bare hostname, e.g. "example.com"
 *   lang       TEXT NOT NULL          -- BCP 47 language tag, e.g. "en"
 *   etag       TEXT NOT NULL          -- aggregate ETag across all active variants
 *   updated_at TEXT NOT NULL          -- ISO 8601 UTC; drives incremental rsync
 *
 * page_variants                        -- one row per (page, label); holds the HTML body
 *   id        INTEGER PRIMARY KEY
 *   page_id   INTEGER NOT NULL REFERENCES pages(id)
 *   label     TEXT NOT NULL           -- "control" | "a" | "b" | …
 *   is_active INTEGER NOT NULL        -- 1 = served; 0 = retired
 *   html_gz   BLOB NOT NULL           -- gzip body WITHOUT the menu fragment
 *   etag      TEXT NOT NULL
 *   UNIQUE(page_id, label)
 *
 * menu_fragments                       -- one row per (domain, lang)
 *   id         INTEGER PRIMARY KEY
 *   domain     TEXT NOT NULL
 *   lang       TEXT NOT NULL
 *   html_gz    BLOB NOT NULL           -- gzip menu HTML prepended at serve time
 *   version_id TEXT NOT NULL           -- changes when menu content changes
 *   updated_at TEXT NOT NULL           -- ISO 8601 UTC
 *   UNIQUE(domain, lang)
 *
 * redirects                            -- 301/410 rules checked on unknown paths
 *   old_path    TEXT PRIMARY KEY
 *   new_path    TEXT                   -- NULL for 410 Gone responses
 *   status_code INTEGER NOT NULL       -- 301 or 410
 */
class ContentDb
{
public:
    static constexpr const char *FILENAME = "content.db";

    explicit ContentDb(const std::string &path);

    SQLite::Database &database();

private:
    SQLite::Database m_db;

    void createSchema();
};
