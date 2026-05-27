#include "ImageRepositorySQLite.h"

ImageRepositorySQLite::ImageRepositorySQLite(ImageDb &db)
    : m_db(db)
{
}

std::optional<ImageRecord> ImageRepositorySQLite::findByDomainAndFilename(
    const std::string &domain,
    const std::string &filename) const
{
    // Priority: exact host domain > server lang code > domain="" fallback.
    // The lang fallback ensures translated images (stored as domain=lang by
    // PageTranslator) are served by the per-language server even when the Host
    // header doesn't match the stored domain key.
    SQLite::Statement q(m_db.database(),
        "SELECT i.id, i.blob, i.mime_type"
        " FROM images i"
        " JOIN image_names n ON n.image_id = i.id"
        " WHERE n.filename = ?"
        "   AND (n.domain = ? OR n.domain = ? OR n.domain = '')"
        " ORDER BY CASE WHEN n.domain = ? THEN 0"
        "               WHEN n.domain = ? THEN 1"
        "               ELSE 2 END"
        " LIMIT 1");
    q.bind(1, filename);
    q.bind(2, domain);
    q.bind(3, m_lang);
    q.bind(4, domain);
    q.bind(5, m_lang);
    if (!q.executeStep()) {
        return std::nullopt;
    }

    ImageRecord rec;
    rec.id = q.getColumn(0).getInt64();

    const SQLite::Column blobCol = q.getColumn(1);
    const auto *data = static_cast<const uint8_t *>(blobCol.getBlob());
    rec.blob.assign(data, data + blobCol.getBytes());

    rec.mimeType = q.getColumn(2).getString();
    return rec;
}
