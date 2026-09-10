#include "PageRepositorySQLite.h"


PageRepositorySQLite::PageRepositorySQLite(ContentDb &db)
    : m_db(db)
{
}

std::optional<PageRecord> PageRepositorySQLite::findByPath(const std::string &path) const
{
    if (auto exact = _selectByPath(path)) {
        return exact;
    }
    return _selectDirectoryIndex(path);
}

std::optional<PageRecord> PageRepositorySQLite::_selectDirectoryIndex(
    const std::string &path) const
{
    // PageGenerator writes the home page as "/index.html" and never as "/", so
    // the bare host and any "/<lang>/" root 404'd without this. nginx supplies
    // the index document in production, which is why only local deploys were
    // affected; resolving it here makes the two behave the same.
    if (path.empty() || path.back() != '/') {
        return std::nullopt;
    }
    return _selectByPath(path + "index.html");
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
