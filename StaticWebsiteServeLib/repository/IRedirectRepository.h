#pragma once

#include <optional>
#include <string>

#include "model/RedirectRecord.h"

/**
 * Read-only access to the redirects table in content.db.
 *
 * Implementations:
 *   - RedirectRepositorySQLite  (StaticWebsiteServeLib)
 */
class IRedirectRepository
{
public:
    virtual ~IRedirectRepository() = default;

    /**
     * Returns the redirect rule for the given URL path, or std::nullopt
     * if no redirect is defined for this path.
     */
    virtual std::optional<RedirectRecord> findByPath(const std::string &path) const = 0;
};
