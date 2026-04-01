#include "StatsController.h"

#include <drogon/HttpResponse.h>

IStatsWriter *StatsController::s_statsWriter = nullptr;

void StatsController::setStatsWriter(IStatsWriter *writer)
{
    s_statsWriter = writer;
}

void StatsController::recordDisplay(const drogon::HttpRequestPtr                          &req,
                                     std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    // TODO: parse JSON body, call s_statsWriter->recordDisplay(), return {"id": rowid}.
    callback(drogon::HttpResponse::newHttpResponse());
}

void StatsController::recordClick(const drogon::HttpRequestPtr                          &req,
                                   std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                   int64_t                                                 id)
{
    // TODO: parse JSON body, call s_statsWriter->recordClick(id, clickedAt).
    callback(drogon::HttpResponse::newHttpResponse());
}

void StatsController::recordSession(const drogon::HttpRequestPtr                          &req,
                                     std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    // TODO: parse JSON body, call s_statsWriter->recordSession(...).
    callback(drogon::HttpResponse::newHttpResponse());
}
