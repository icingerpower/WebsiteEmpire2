#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/MenuFragmentRecord.h"

/**
 * Read-only access to the menu_fragments table in content.db.
 *
 * Implementations:
 *   - MenuRepositorySQLite  (StaticWebsiteServeLib)
 */
class IMenuRepository
{
public:
    virtual ~IMenuRepository() = default;

    /**
     * Returns the menu fragment for the given domain and language,
     * or std::nullopt if none has been stored.
     */
    virtual std::optional<MenuFragmentRecord> findByDomainAndLang(
        const std::string &domain,
        const std::string &lang) const = 0;

    /**
     * Returns all menu fragments.
     * Used by PageController at startup to populate its in-memory cache.
     */
    virtual std::vector<MenuFragmentRecord> findAll() const = 0;
};
