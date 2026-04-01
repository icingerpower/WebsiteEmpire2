#include "PageController.h"

#include <drogon/HttpResponse.h>

ContentDb *PageController::s_contentDb = nullptr;

void PageController::setContentDb(ContentDb *db)
{
    s_contentDb = db;
}

void PageController::servePage(const drogon::HttpRequestPtr                          &req,
                                std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                const std::string                                     &path)
{
    // TODO: query s_contentDb for path, handle ETag / 304, return gzip blob.
    callback(drogon::HttpResponse::newNotFoundResponse());
}
