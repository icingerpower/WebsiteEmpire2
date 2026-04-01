#include "StatsWriterSQLite.h"

StatsWriterSQLite::StatsWriterSQLite(StatsDb &statsDb)
    : m_statsDb(statsDb)
{
}

int64_t StatsWriterSQLite::recordDisplay(const std::string &pageId,
                                          const std::string &displayAt)
{
    SQLite::Statement stmt(m_statsDb.database(),
        "INSERT INTO displays_clicks (page_id, display_at) VALUES (?, ?)");
    stmt.bind(1, pageId);
    stmt.bind(2, displayAt);
    stmt.exec();
    return m_statsDb.database().getLastInsertRowid();
}

void StatsWriterSQLite::recordClick(int64_t            displayRowId,
                                     const std::string &clickedAt)
{
    SQLite::Statement stmt(m_statsDb.database(),
        "UPDATE displays_clicks SET clicked_at = ? WHERE id = ?");
    stmt.bind(1, clickedAt);
    stmt.bind(2, displayRowId);
    stmt.exec();
}

void StatsWriterSQLite::recordSession(const std::string &pageId,
                                       int                scrollingPercentage,
                                       int                timeOnPage,
                                       bool               isFinalPage)
{
    SQLite::Statement stmt(m_statsDb.database(),
        "INSERT INTO page_session (page_id, scrolling_percentage, time_on_page, is_final_page)"
        " VALUES (?, ?, ?, ?)");
    stmt.bind(1, pageId);
    stmt.bind(2, scrollingPercentage);
    stmt.bind(3, timeOnPage);
    stmt.bind(4, static_cast<int>(isFinalPage));
    stmt.exec();
}
