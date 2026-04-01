#pragma once

#include <string>

#include <SQLiteCpp/SQLiteCpp.h>

/**
 * Owns the connection to images.db and ensures the schema exists.
 *
 * Schema
 * ------
 * images
 *   id        INTEGER PRIMARY KEY
 *   blob      BLOB NOT NULL
 *   mime_type TEXT NOT NULL        -- e.g. "image/webp"
 *
 * image_names
 *   domain    TEXT NOT NULL        -- bare hostname, e.g. "example.com"
 *   filename  TEXT NOT NULL        -- URL filename, e.g. "hero-image.webp"
 *   image_id  INTEGER NOT NULL REFERENCES images(id)
 *   PRIMARY KEY (domain, filename)
 *
 * The indirection allows the same binary blob to be served under different
 * filenames on different domains (useful for per-language SEO).
 */
class ImageDb
{
public:
    explicit ImageDb(const std::string &path);

    SQLite::Database &database();

private:
    SQLite::Database m_db;

    void createSchema();
};
