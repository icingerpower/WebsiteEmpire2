#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/PageRecord.h"
#include "model/PageVariantRecord.h"

/**
 * Read-only access to the pages and page_variants tables in content.db.
 *
 * Implementations:
 *   - PageRepositorySQLite  (StaticWebsiteServeLib)
 */
class IPageRepository
{
public:
    virtual ~IPageRepository() = default;

    /** Returns page metadata for the given URL path, or std::nullopt. */
    virtual std::optional<PageRecord> findByPath(const std::string &path) const = 0;

    /**
     * Returns the active variant with the given label for the given page,
     * or std::nullopt if the variant does not exist or is inactive.
     */
    virtual std::optional<PageVariantRecord> findVariant(int64_t            pageId,
                                                          const std::string &label) const = 0;

    /**
     * Returns the labels of all currently active variants for the given page
     * (e.g. {"control"} for a plain page, {"control", "a"} for an A/B test).
     */
    virtual std::vector<std::string> findActiveVariantLabels(int64_t pageId) const = 0;
};
