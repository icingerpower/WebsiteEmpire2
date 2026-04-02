#include "ImageRepositorySQLite.h"

ImageRepositorySQLite::ImageRepositorySQLite(ImageDb &db)
    : m_db(db)
{
}

std::optional<ImageRecord> ImageRepositorySQLite::findByDomainAndFilename(
    const std::string &domain,
    const std::string &filename) const
{
    SQLite::Statement q(m_db.database(),
        "SELECT i.id, i.blob, i.mime_type"
        " FROM images i"
        " JOIN image_names n ON n.image_id = i.id"
        " WHERE n.domain = ? AND n.filename = ?");
    q.bind(1, domain);
    q.bind(2, filename);
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
