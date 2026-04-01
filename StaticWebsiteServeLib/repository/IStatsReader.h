#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Read access to stats.db — used by the Qt app to display statistics.
 *
 * Implementations:
 *   - StatsReaderSQLite   (StaticWebsiteServeLib, used by Qt logic layer)
 *   - StatsReaderQSql     (WebsiteEmpireLib, exposes QSqlQueryModel for Qt views — read-only display)
 *
 * All datetime strings are ISO 8601 UTC.
 */

struct DisplayClickRecord
{
    int64_t     id;
    std::string pageId;
    std::string displayAt;
    std::string clickedAt;   // empty string if no click was recorded
};

struct PageSessionRecord
{
    std::string pageId;
    int         scrollingPercentage;
    int         timeOnPage;   // seconds
    bool        isFinalPage;
};

class IStatsReader
{
public:
    virtual ~IStatsReader() = default;

    virtual std::vector<DisplayClickRecord> getDisplays(const std::string &pageId) const = 0;
    virtual std::vector<PageSessionRecord>  getSessions(const std::string &pageId) const = 0;
};
