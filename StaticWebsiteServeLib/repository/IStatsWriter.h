#pragma once

#include <cstdint>
#include <string>

/**
 * Write-only access to stats.db.
 *
 * Implementations:
 *   - StatsWriterSQLite  (StaticWebsiteServeLib, used by Drogon)
 *
 * All datetime strings are ISO 8601 UTC (e.g. "2026-04-01T14:32:00Z").
 */
class IStatsWriter
{
public:
    virtual ~IStatsWriter() = default;

    /**
     * Inserts a new row in displays_clicks for a page view.
     * Returns the rowid so the caller can later record a click against it.
     */
    virtual int64_t recordDisplay(const std::string &pageId,
                                  const std::string &displayAt) = 0;

    /**
     * Sets clicked_at on the displays_clicks row identified by displayRowId.
     * No-op if the id does not exist.
     */
    virtual void recordClick(int64_t            displayRowId,
                             const std::string &clickedAt) = 0;

    /**
     * Inserts a row in page_session.
     * scrollingPercentage must be 0–100; timeOnPage is in seconds.
     * isFinalPage is true when the user left the site (browser unload without internal navigation).
     */
    virtual void recordSession(const std::string &pageId,
                               int                scrollingPercentage,
                               int                timeOnPage,
                               bool               isFinalPage) = 0;
};
