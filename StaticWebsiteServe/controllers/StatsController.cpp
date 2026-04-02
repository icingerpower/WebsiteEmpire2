#include "StatsController.h"

#include <drogon/HttpResponse.h>
#include <trantor/utils/Date.h>

IStatsWriter *StatsController::s_statsWriter = nullptr;

void StatsController::setStatsWriter(IStatsWriter *writer)
{
    s_statsWriter = writer;
}

void StatsController::recordDisplay(const drogon::HttpRequestPtr                          &req,
                                     std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    const auto &json = req->getJsonObject();
    if (!json || !json->isMember("page_id")) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    const std::string pageId    = (*json)["page_id"].asString();
    const std::string displayAt = trantor::Date::now().toFormattedString(false);
    const int64_t     rowId     = s_statsWriter->recordDisplay(pageId, displayAt);

    Json::Value result;
    result["id"] = rowId;
    callback(drogon::HttpResponse::newHttpJsonResponse(result));
}

void StatsController::recordClick(const drogon::HttpRequestPtr                          &req,
                                   std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                   int64_t                                                 id)
{
    const auto &json = req->getJsonObject();
    std::string clickedAt;
    if (json && json->isMember("clicked_at")) {
        clickedAt = (*json)["clicked_at"].asString();
    } else {
        clickedAt = trantor::Date::now().toFormattedString(false);
    }

    s_statsWriter->recordClick(id, clickedAt);

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k204NoContent);
    callback(resp);
}

void StatsController::recordSession(const drogon::HttpRequestPtr                          &req,
                                     std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    const auto &json = req->getJsonObject();
    if (!json || !json->isMember("page_id")) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    const std::string pageId              = (*json)["page_id"].asString();
    const int         scrollingPercentage = (*json)["scrolling_percentage"].asInt();
    const int         timeOnPage          = (*json)["time_on_page"].asInt();
    const bool        isFinalPage         = (*json)["is_final_page"].asBool();

    s_statsWriter->recordSession(pageId, scrollingPercentage, timeOnPage, isFinalPage);

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k204NoContent);
    callback(resp);
}
