#include "ImageDb.h"

ImageDb::ImageDb(const std::string &path)
    : m_db(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
{
    createSchema();
}

SQLite::Database &ImageDb::database()
{
    return m_db;
}

void ImageDb::createSchema()
{
    m_db.exec(
        "CREATE TABLE IF NOT EXISTS images ("
        "    id        INTEGER PRIMARY KEY,"
        "    blob      BLOB NOT NULL,"
        "    mime_type TEXT NOT NULL"
        ")"
    );
    m_db.exec(
        "CREATE TABLE IF NOT EXISTS image_names ("
        "    domain   TEXT    NOT NULL,"
        "    filename TEXT    NOT NULL,"
        "    image_id INTEGER NOT NULL REFERENCES images(id),"
        "    PRIMARY KEY (domain, filename)"
        ")"
    );
}
