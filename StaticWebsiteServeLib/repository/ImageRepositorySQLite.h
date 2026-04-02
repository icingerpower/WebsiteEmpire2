#pragma once

#include "repository/IImageRepository.h"
#include "db/ImageDb.h"

class ImageRepositorySQLite : public IImageRepository
{
public:
    explicit ImageRepositorySQLite(ImageDb &db);

    std::optional<ImageRecord> findByDomainAndFilename(const std::string &domain,
                                                        const std::string &filename) const override;

private:
    ImageDb &m_db;
};
