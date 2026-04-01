#pragma once

#include <drogon/HttpController.h>

#include "repository/IStatsWriter.h"

/**
 * Receives browser-posted statistics and persists them to stats.db.
 *
 * POST  /stats/display         {"page_id":"..."}
 *   → inserts a displays_clicks row, returns {"id": <rowid>}
 *     so JS can later report a click.
 *
 * PATCH /stats/click/{id}      {"clicked_at":"<ISO8601>"}
 *   → updates the displays_clicks row to set clicked_at.
 *
 * POST  /stats/session         {"page_id":"...","scrolling_percentage":75,
 *                               "time_on_page":120,"is_final_page":true}
 *   → inserts a page_session row.
 *
 * Drogon auto-creates this controller, so dependencies are injected via
 * setStatsWriter() before app().run().
 */
class StatsController : public drogon::HttpController<StatsController>
{
public:
    static void setStatsWriter(IStatsWriter *writer);

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(StatsController::recordDisplay, "/stats/display",    drogon::Post);
    ADD_METHOD_TO(StatsController::recordClick,   "/stats/click/{id}", drogon::Patch);
    ADD_METHOD_TO(StatsController::recordSession, "/stats/session",    drogon::Post);
    METHOD_LIST_END

    void recordDisplay(const drogon::HttpRequestPtr                          &req,
                       std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void recordClick(const drogon::HttpRequestPtr                          &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                     int64_t                                                 id);

    void recordSession(const drogon::HttpRequestPtr                          &req,
                       std::function<void(const drogon::HttpResponsePtr &)> &&callback);

private:
    static IStatsWriter *s_statsWriter;
};
