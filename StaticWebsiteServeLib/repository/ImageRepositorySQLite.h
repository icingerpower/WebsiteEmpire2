#pragma once

#include "repository/IImageRepository.h"
#include "db/ImageDb.h"

class ImageRepositorySQLite : public IImageRepository
{
public:
    explicit ImageRepositorySQLite(ImageDb &db);

    // Sets the server's own language code (e.g. "fr").  When set, image lookups
    // treat domain=lang as a higher-priority fallback than domain="" so that
    // translated images are served without requiring database migration at deploy time.
    void setLang(const std::string &lang) { m_lang = lang; }

    std::optional<ImageRecord> findByDomainAndFilename(const std::string &domain,
                                                        const std::string &filename) const override;

private:
    ImageDb    &m_db;
    std::string m_lang; // empty when no lang override is configured
};
