#pragma once

#include <optional>
#include <string>

#include "model/PageRecord.h"

/**
 * Read-only access to the pages table in content.db.
 *
 * Implementations:
 *   - PageRepositorySQLite  (StaticWebsiteServeLib, used by Drogon + Qt write path)
 */
class IPageRepository
{
public:
    virtual ~IPageRepository() = default;

    /** Returns the page for the given URL path (e.g. "/my-page.html"), or std::nullopt. */
    virtual std::optional<PageRecord> findByPath(const std::string &path) const = 0;
};
