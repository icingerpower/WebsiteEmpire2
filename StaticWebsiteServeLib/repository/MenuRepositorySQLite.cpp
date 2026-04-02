#include "MenuRepositorySQLite.h"

MenuRepositorySQLite::MenuRepositorySQLite(ContentDb &db)
    : m_db(db)
{
}

std::optional<MenuFragmentRecord> MenuRepositorySQLite::findByDomainAndLang(
    const std::string &domain,
    const std::string &lang) const
{
    SQLite::Statement q(m_db.database(),
        "SELECT id, domain, lang, html_gz, version_id, updated_at"
        " FROM menu_fragments WHERE domain = ? AND lang = ?");
    q.bind(1, domain);
    q.bind(2, lang);
    if (!q.executeStep()) {
        return std::nullopt;
    }

    MenuFragmentRecord rec;
    rec.id     = q.getColumn(0).getInt64();
    rec.domain = q.getColumn(1).getString();
    rec.lang   = q.getColumn(2).getString();

    const SQLite::Column blobCol = q.getColumn(3);
    const auto *data = static_cast<const uint8_t *>(blobCol.getBlob());
    rec.htmlGz.assign(data, data + blobCol.getBytes());

    rec.versionId = q.getColumn(4).getString();
    rec.updatedAt = q.getColumn(5).getString();
    return rec;
}

std::vector<MenuFragmentRecord> MenuRepositorySQLite::findAll() const
{
    SQLite::Statement q(m_db.database(),
        "SELECT id, domain, lang, html_gz, version_id, updated_at FROM menu_fragments");
    std::vector<MenuFragmentRecord> result;
    while (q.executeStep()) {
        MenuFragmentRecord rec;
        rec.id     = q.getColumn(0).getInt64();
        rec.domain = q.getColumn(1).getString();
        rec.lang   = q.getColumn(2).getString();

        const SQLite::Column blobCol = q.getColumn(3);
        const auto *data = static_cast<const uint8_t *>(blobCol.getBlob());
        rec.htmlGz.assign(data, data + blobCol.getBytes());

        rec.versionId = q.getColumn(4).getString();
        rec.updatedAt = q.getColumn(5).getString();
        result.push_back(std::move(rec));
    }
    return result;
}
