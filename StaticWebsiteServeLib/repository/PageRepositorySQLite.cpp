#include "PageRepositorySQLite.h"

namespace {

/**
 * Removes a leading "/<lang>" segment from path when it is exactly that
 * segment, mirroring what the nginx path-prefix proxy does in production:
 *   ("/es/categories", "es") → "/categories"
 *   ("/es/",           "es") → "/"
 *   ("/es",            "es") → "/"
 *   ("/espanol",       "es") → "/espanol"   (segment boundary respected)
 *   ("/fr/categories", "es") → "/fr/categories"
 * Returns path unchanged when lang is empty or does not match.
 */
std::string stripLangPrefix(const std::string &path, const std::string &lang)
{
    if (lang.empty()) {
        return path;
    }
    const std::string prefix = "/" + lang;
    if (path == prefix || path == prefix + "/") {
        return "/";
    }
    // The trailing '/' check is what keeps "/espanol" from matching lang "es".
    if (path.size() > prefix.size()
        && path.compare(0, prefix.size(), prefix) == 0
        && path[prefix.size()] == '/') {
        return path.substr(prefix.size());
    }
    return path;
}

} // namespace

PageRepositorySQLite::PageRepositorySQLite(ContentDb &db)
    : m_db(db)
{
}

const std::string &PageRepositorySQLite::_servedLang() const
{
    if (m_servedLangResolved) {
        return m_servedLang;
    }
    m_servedLangResolved = true;
    // Ignore the "_" placeholder and empty langs: keying the fallback off "_"
    // would mean it never fires for the real content.
    SQLite::Statement q(m_db.database(),
        "SELECT lang FROM pages WHERE lang != '_' AND lang != ''"
        " GROUP BY lang ORDER BY COUNT(*) DESC LIMIT 1");
    if (q.executeStep()) {
        m_servedLang = q.getColumn(0).getString();
    }
    return m_servedLang;
}

std::optional<PageRecord> PageRepositorySQLite::findByPath(const std::string &path) const
{
    if (auto exact = _selectByPath(path)) {
        return exact;
    }
    // Only now consider the request a proxy-style prefixed path — an existing
    // page always wins over the stripped interpretation.
    const std::string stripped = stripLangPrefix(path, _servedLang());
    if (stripped == path) {
        return std::nullopt;
    }
    return _selectByPath(stripped);
}

std::optional<PageRecord> PageRepositorySQLite::_selectByPath(const std::string &path) const
{
    SQLite::Statement q(m_db.database(),
        "SELECT id, path, domain, lang, etag, updated_at FROM pages WHERE path = ?");
    q.bind(1, path);
    if (!q.executeStep()) {
        return std::nullopt;
    }
    PageRecord rec;
    rec.id        = q.getColumn(0).getInt64();
    rec.path      = q.getColumn(1).getString();
    rec.domain    = q.getColumn(2).getString();
    rec.lang      = q.getColumn(3).getString();
    rec.etag      = q.getColumn(4).getString();
    rec.updatedAt = q.getColumn(5).getString();
    return rec;
}

std::optional<PageVariantRecord> PageRepositorySQLite::findVariant(int64_t           pageId,
                                                                     const std::string &label) const
{
    SQLite::Statement q(m_db.database(),
        "SELECT id, page_id, label, is_active, html_gz, etag"
        " FROM page_variants WHERE page_id = ? AND label = ? AND is_active = 1");
    q.bind(1, pageId);
    q.bind(2, label);
    if (!q.executeStep()) {
        return std::nullopt;
    }
    PageVariantRecord rec;
    rec.id       = q.getColumn(0).getInt64();
    rec.pageId   = q.getColumn(1).getInt64();
    rec.label    = q.getColumn(2).getString();
    rec.isActive = q.getColumn(3).getInt() != 0;

    const SQLite::Column blobCol = q.getColumn(4);
    const auto *data = static_cast<const uint8_t *>(blobCol.getBlob());
    rec.htmlGz.assign(data, data + blobCol.getBytes());

    rec.etag = q.getColumn(5).getString();
    return rec;
}

std::vector<std::string> PageRepositorySQLite::findActiveVariantLabels(int64_t pageId) const
{
    SQLite::Statement q(m_db.database(),
        "SELECT label FROM page_variants WHERE page_id = ? AND is_active = 1");
    q.bind(1, pageId);
    std::vector<std::string> labels;
    while (q.executeStep()) {
        labels.push_back(q.getColumn(0).getString());
    }
    return labels;
}
