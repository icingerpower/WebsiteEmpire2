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
        "    id      TEXT PRIMARY KEY,"
        "    path    TEXT UNIQUE NOT NULL,"
        "    html_gz BLOB NOT NULL,"
        "    etag    TEXT NOT NULL"
        ")"
    );
}
