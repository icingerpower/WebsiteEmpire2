#pragma once

#include "IStatsWriter.h"
#include "db/StatsDb.h"

/**
 * IStatsWriter implementation backed by StatsDb (SQLiteCpp).
 * Used by the Drogon server and the Qt app's write path.
 */
class StatsWriterSQLite : public IStatsWriter
{
public:
    explicit StatsWriterSQLite(StatsDb &statsDb);

    int64_t recordDisplay(const std::string &pageId,
                          const std::string &displayAt) override;

    void recordClick(int64_t            displayRowId,
                     const std::string &clickedAt) override;

    void recordSession(const std::string &pageId,
                       int                scrollingPercentage,
                       int                timeOnPage,
                       bool               isFinalPage) override;

private:
    StatsDb &m_statsDb;
};
