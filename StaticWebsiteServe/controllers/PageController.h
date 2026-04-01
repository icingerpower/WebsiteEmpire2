#pragma once

#include <drogon/HttpController.h>

#include "db/ContentDb.h"

/**
 * Serves pre-compressed HTML pages stored in content.db.
 *
 * Route: GET /(.+\.html)$
 *
 * - Looks up the path in ContentDb.
 * - Returns 404 if not found.
 * - Returns 304 if the client's ETag matches.
 * - Returns 200 with Content-Encoding: gzip and the compressed blob.
 *
 * Drogon auto-creates this controller, so dependencies are injected via
 * setContentDb() before app().run().
 */
class PageController : public drogon::HttpController<PageController>
{
public:
    static void setContentDb(ContentDb *db);

    METHOD_LIST_BEGIN
    ADD_METHOD_VIA_REGEX(PageController::servePage, "^/(.+\\.html)$", drogon::Get);
    METHOD_LIST_END

    void servePage(const drogon::HttpRequestPtr                          &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                   const std::string                                     &path);

private:
    static ContentDb *s_contentDb;
};
