#include "RedirectRepositorySQLite.h"

RedirectRepositorySQLite::RedirectRepositorySQLite(ContentDb &db)
    : m_db(db)
{
}

std::optional<RedirectRecord> RedirectRepositorySQLite::findByPath(const std::string &path) const
{
    SQLite::Statement q(m_db.database(),
        "SELECT old_path, new_path, status_code FROM redirects WHERE old_path = ?");
    q.bind(1, path);
    if (!q.executeStep()) {
        return std::nullopt;
    }
    RedirectRecord rec;
    rec.oldPath    = q.getColumn(0).getString();
    rec.newPath    = q.getColumn(1).getString();
    rec.statusCode = q.getColumn(2).getInt();
    return rec;
}
