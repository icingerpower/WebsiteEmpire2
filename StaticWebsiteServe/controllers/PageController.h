#pragma once

#include <drogon/HttpController.h>

#include <map>
#include <string>
#include <vector>

#include "repository/IPageRepository.h"
#include "repository/IMenuRepository.h"
#include "repository/IRedirectRepository.h"

/**
 * Serves pre-compressed HTML pages via two-pass rendering, and serves
 * sitemap / robots files directly from the content DB.
 *
 * Routes:
 *   GET /robots.txt               → serveFile (no menu, text/plain)
 *   GET /sitemap*.xml             → serveFile (no menu, application/xml)
 *   GET /(<slug>.html | <slug>)   → servePage (menu + body, text/html)
 *
 * The servePage regex is ^/(.+\.html|[^/.]+)$ — it matches .html pages and
 * extension-less slugs only, intentionally leaving paths with other extensions
 * (e.g. .webp, .svg, .jpg) to ImageController.
 *
 * Serving flow:
 *  1. Look up the path in pages → PageRecord (metadata).
 *  2. If not found, check redirects → 301 / 410 / 404.
 *  3. Pick the active variant: session cookie "ab_variant" or random assignment.
 *  4. Load variant's html_gz (body without menu) from page_variants.
 *  5. Prepend the in-memory menu fragment for (domain, lang).
 *  6. Send the concatenated gzip stream with Content-Encoding: gzip.
 *     (Two gzip members concatenated decompress to the full HTML page.)
 *
 * Call loadMenuCache() once after setMenuRepository() and before app().run()
 * to populate the in-memory menu cache from the DB.
 *
 * Drogon auto-creates this controller; inject dependencies via static setters
 * before app().run().
 */
class PageController : public drogon::HttpController<PageController>
{
public:
    static void setPageRepository(IPageRepository *repo);
    static void setMenuRepository(IMenuRepository *repo);
    static void setRedirectRepository(IRedirectRepository *repo);

    /**
     * Reads all menu fragments from the repository into the in-memory cache.
     * Must be called after setMenuRepository() and before app().run().
     */
    static void loadMenuCache(IMenuRepository *repo);

    METHOD_LIST_BEGIN
    ADD_METHOD_VIA_REGEX(PageController::serveFile, "^/(robots\\.txt|sitemap[\\w-]*\\.xml)$", drogon::Get);
    ADD_METHOD_VIA_REGEX(PageController::servePage, "^/(.+\\.html|[^/.]+)$", drogon::Get);
    METHOD_LIST_END

    void serveFile(const drogon::HttpRequestPtr                          &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                   const std::string                                     &filename);

    void servePage(const drogon::HttpRequestPtr                          &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                   const std::string                                     &path);

private:
    static IPageRepository    *s_pageRepo;
    static IMenuRepository    *s_menuRepo;
    static IRedirectRepository *s_redirectRepo;

    /** key: "domain:lang" → gzip menu bytes */
    static std::map<std::string, std::vector<uint8_t>> s_menuCache;

    /**
     * Selects the variant label to serve based on active variants and the
     * request's "ab_variant" cookie.  Falls back to "control" for plain pages.
     */
    static std::string _selectVariant(const std::vector<std::string>    &activeLabels,
                                       const drogon::HttpRequestPtr      &req);
};
