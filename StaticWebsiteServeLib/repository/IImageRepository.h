#pragma once

#include <optional>
#include <string>

#include "model/ImageRecord.h"

/**
 * Read-only access to images.db.
 *
 * Implementations:
 *   - ImageRepositorySQLite  (StaticWebsiteServeLib)
 */
class IImageRepository
{
public:
    virtual ~IImageRepository() = default;

    /**
     * Resolves a domain + filename to an image blob.
     * The domain is the bare hostname (e.g. "example.com").
     * Returns std::nullopt if the name is not registered.
     */
    virtual std::optional<ImageRecord> findByDomainAndFilename(const std::string &domain,
                                                               const std::string &filename) const = 0;
};
