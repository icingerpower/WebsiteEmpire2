#include "PageController.h"

#include <random>

#include <drogon/HttpResponse.h>

IPageRepository    *PageController::s_pageRepo     = nullptr;
IMenuRepository    *PageController::s_menuRepo     = nullptr;
IRedirectRepository *PageController::s_redirectRepo = nullptr;

std::map<std::string, std::vector<uint8_t>> PageController::s_menuCache;

void PageController::setPageRepository(IPageRepository *repo)
{
    s_pageRepo = repo;
}

void PageController::setMenuRepository(IMenuRepository *repo)
{
    s_menuRepo = repo;
}

void PageController::setRedirectRepository(IRedirectRepository *repo)
{
    s_redirectRepo = repo;
}

void PageController::loadMenuCache(IMenuRepository *repo)
{
    s_menuCache.clear();
    for (auto &fragment : repo->findAll()) {
        const std::string key = fragment.domain + ":" + fragment.lang;
        s_menuCache[key] = std::move(fragment.htmlGz);
    }
}

// ---------------------------------------------------------------------------

std::string PageController::_selectVariant(const std::vector<std::string> &activeLabels,
                                            const drogon::HttpRequestPtr   &req)
{
    if (activeLabels.size() <= 1) {
        return "control";
    }

    const std::string &cookie = req->getCookie("ab_variant");
    if (!cookie.empty()) {
        for (const auto &label : activeLabels) {
            if (label == cookie) {
                return label;
            }
        }
    }

    // No valid cookie — assign randomly.
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<std::size_t> dist(0, activeLabels.size() - 1);
    return activeLabels[dist(rng)];
}

// ---------------------------------------------------------------------------

void PageController::servePage(const drogon::HttpRequestPtr                          &req,
                                std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                const std::string                                     &path)
{
    const std::string fullPath = "/" + path;

    // 1. Look up page metadata.
    const auto pageOpt = s_pageRepo->findByPath(fullPath);
    if (!pageOpt) {
        // Check redirects table before returning 404.
        const auto redirectOpt = s_redirectRepo->findByPath(fullPath);
        if (redirectOpt) {
            if (redirectOpt->statusCode == 410) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(static_cast<drogon::HttpStatusCode>(410));
                callback(resp);
            } else {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k301MovedPermanently);
                resp->addHeader("Location", redirectOpt->newPath);
                callback(resp);
            }
        } else {
            callback(drogon::HttpResponse::newNotFoundResponse());
        }
        return;
    }

    // 2. Select variant (A/B routing via cookie).
    const auto activeLabels  = s_pageRepo->findActiveVariantLabels(pageOpt->id);
    std::string variantLabel = _selectVariant(activeLabels, req);

    // 3. Load variant html_gz.
    auto variantOpt = s_pageRepo->findVariant(pageOpt->id, variantLabel);
    if (!variantOpt && variantLabel != "control") {
        variantOpt   = s_pageRepo->findVariant(pageOpt->id, "control");
        variantLabel = "control";
    }
    if (!variantOpt) {
        callback(drogon::HttpResponse::newNotFoundResponse());
        return;
    }

    // 4. Check ETag for 304.
    const std::string &etag = variantOpt->etag;
    if (req->getHeader("If-None-Match") == etag) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k304NotModified);
        callback(resp);
        return;
    }

    // 5. Build response body: menu fragment (gzip) + page body (gzip).
    //    Two concatenated gzip members decompress to the full HTML page.
    std::string body;
    const auto menuIt = s_menuCache.find(pageOpt->domain + ":" + pageOpt->lang);
    if (menuIt != s_menuCache.end()) {
        const auto &menuGz = menuIt->second;
        body.append(reinterpret_cast<const char *>(menuGz.data()), menuGz.size());
    }
    const auto &bodyGz = variantOpt->htmlGz;
    body.append(reinterpret_cast<const char *>(bodyGz.data()), bodyGz.size());

    // 6. Send response.
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k200OK);
    resp->addHeader("Content-Type", "text/html; charset=utf-8");
    resp->addHeader("Content-Encoding", "gzip");
    resp->addHeader("ETag", etag);
    resp->addHeader("Cache-Control", "no-cache");
    resp->setBody(std::move(body));

    // 7. Persist variant assignment in cookie for A/B tests.
    if (activeLabels.size() > 1) {
        drogon::Cookie abCookie("ab_variant", variantLabel);
        abCookie.setPath("/");
        abCookie.setMaxAge(86400 * 30);  // 30 days
        resp->addCookie(abCookie);
    }

    callback(resp);
}
