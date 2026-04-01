#pragma once

#include <string>

#include <SQLiteCpp/SQLiteCpp.h>

/**
 * Owns the connection to content.db and ensures the schema exists.
 *
 * Schema
 * ------
 * pages
 *   id      TEXT PRIMARY KEY
 *   path    TEXT UNIQUE NOT NULL   -- URL path, e.g. "/my-page.html"
 *   html_gz BLOB NOT NULL          -- gzip-compressed HTML
 *   etag    TEXT NOT NULL          -- used for HTTP 304 caching
 */
class ContentDb
{
public:
    explicit ContentDb(const std::string &path);

    SQLite::Database &database();

private:
    SQLite::Database m_db;

    void createSchema();
};
