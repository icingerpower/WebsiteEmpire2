/**
 * Tests for PageController — covers the Content-Type header fix where
 * addHeader("Content-Type", …) was replaced with setContentTypeString()
 * so that Drogon's canonical content-type field is correctly overridden.
 *
 * With the old code (addHeader), resp->contentTypeString() returned an empty
 * string for sitemap XML and robots.txt, and "text/html" (the Drogon default)
 * for HTML pages.  With the fix (setContentTypeString), the correct MIME type
 * is returned.
 */

#include <drogon/drogon_test.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <optional>
#include <string>
#include <vector>

#include "model/MenuFragmentRecord.h"
#include "model/PageRecord.h"
#include "model/PageVariantRecord.h"
#include "model/RedirectRecord.h"
#include "repository/IMenuRepository.h"
#include "repository/IPageRepository.h"
#include "repository/IRedirectRepository.h"

#include "../StaticWebsiteServe/controllers/PageController.h"

// ---------------------------------------------------------------------------
// Minimal stub implementations — return only what each test sets up
// ---------------------------------------------------------------------------

struct StubPage {
    PageRecord       page;
    PageVariantRecord variant;
};

class StubPageRepository : public IPageRepository
{
public:
    std::optional<StubPage> entry;

    std::optional<PageRecord> findByPath(const std::string &path) const override
    {
        if (entry && entry->page.path == path) {
            return entry->page;
        }
        return std::nullopt;
    }

    std::optional<PageVariantRecord> findVariant(int64_t            pageId,
                                                 const std::string &label) const override
    {
        if (entry && entry->page.id == pageId && label == "control") {
            return entry->variant;
        }
        return std::nullopt;
    }

    std::vector<std::string> findActiveVariantLabels(int64_t pageId) const override
    {
        if (entry && entry->page.id == pageId) {
            return {"control"};
        }
        return {};
    }
};

class StubMenuRepository : public IMenuRepository
{
public:
    std::optional<MenuFragmentRecord> findByDomainAndLang(const std::string &,
                                                          const std::string &) const override
    {
        return std::nullopt;
    }

    std::vector<MenuFragmentRecord> findAll() const override
    {
        return {};
    }
};

class StubRedirectRepository : public IRedirectRepository
{
public:
    std::optional<RedirectRecord> findByPath(const std::string &) const override
    {
        return std::nullopt;
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Minimal gzip-valid bytes (just a header prefix — content not decompressed
 *  by the controller, only forwarded as-is). */
static const std::vector<uint8_t> kFakeGz = {0x1f, 0x8b, 0x08, 0x00};

/** Build a fake page + control variant with the given path and etag. */
static StubPage makePage(int64_t            id,
                         const std::string &path,
                         const std::string &etag)
{
    PageRecord page;
    page.id        = id;
    page.path      = path;
    page.domain    = "example.com";
    page.lang      = "en";
    page.etag      = etag;
    page.updatedAt = "2026-01-01";

    PageVariantRecord variant;
    variant.id       = 1;
    variant.pageId   = id;
    variant.label    = "control";
    variant.isActive = true;
    variant.htmlGz   = kFakeGz;
    variant.etag     = etag;

    return StubPage{page, variant};
}

/** Wire up PageController's static repositories and menu cache. */
static void setupController(StubPageRepository    &pageRepo,
                             StubMenuRepository    &menuRepo,
                             StubRedirectRepository &redirectRepo)
{
    PageController::setPageRepository(&pageRepo);
    PageController::setMenuRepository(&menuRepo);
    PageController::setRedirectRepository(&redirectRepo);
    PageController::loadMenuCache(&menuRepo);
}

// ---------------------------------------------------------------------------
// serveFile — sitemap XML content-type
// ---------------------------------------------------------------------------

DROGON_TEST(test_pagecontroller_servefile_sitemap_xml_content_type)
{
    StubPageRepository     pageRepo;
    StubMenuRepository     menuRepo;
    StubRedirectRepository redirectRepo;

    pageRepo.entry = makePage(1, "/sitemap.xml", "etag-xml");
    setupController(pageRepo, menuRepo, redirectRepo);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath("/sitemap.xml");

    drogon::HttpResponsePtr captured;
    PageController ctrl;
    ctrl.serveFile(req,
                   [&captured](const drogon::HttpResponsePtr &resp) {
                       captured = resp;
                   },
                   "sitemap.xml");

    REQUIRE(captured != nullptr);
    CHECK(captured->getStatusCode() == drogon::k200OK);

    // With the fix: setContentTypeString correctly sets the canonical field.
    // With the old bug: addHeader("Content-Type",...) left contentTypeString() empty.
    const std::string ct = captured->contentTypeString();
    CHECK(ct.find("application/xml") != std::string::npos);
}

// ---------------------------------------------------------------------------
// serveFile — robots.txt content-type
// ---------------------------------------------------------------------------

DROGON_TEST(test_pagecontroller_servefile_robots_txt_content_type)
{
    StubPageRepository     pageRepo;
    StubMenuRepository     menuRepo;
    StubRedirectRepository redirectRepo;

    pageRepo.entry = makePage(2, "/robots.txt", "etag-robots");
    setupController(pageRepo, menuRepo, redirectRepo);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath("/robots.txt");

    drogon::HttpResponsePtr captured;
    PageController ctrl;
    ctrl.serveFile(req,
                   [&captured](const drogon::HttpResponsePtr &resp) {
                       captured = resp;
                   },
                   "robots.txt");

    REQUIRE(captured != nullptr);
    CHECK(captured->getStatusCode() == drogon::k200OK);

    // With the fix: "text/plain; charset=utf-8" is set via setContentTypeString.
    // With the old bug: contentTypeString() would be empty (addHeader had no effect).
    const std::string ct = captured->contentTypeString();
    CHECK(ct.find("text/plain") != std::string::npos);
}

// ---------------------------------------------------------------------------
// servePage — HTML content-type
// ---------------------------------------------------------------------------

DROGON_TEST(test_pagecontroller_servepage_html_content_type)
{
    StubPageRepository     pageRepo;
    StubMenuRepository     menuRepo;
    StubRedirectRepository redirectRepo;

    pageRepo.entry = makePage(3, "/article.html", "etag-html");
    setupController(pageRepo, menuRepo, redirectRepo);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath("/article.html");

    drogon::HttpResponsePtr captured;
    PageController ctrl;
    ctrl.servePage(req,
                   [&captured](const drogon::HttpResponsePtr &resp) {
                       captured = resp;
                   },
                   "article.html");

    REQUIRE(captured != nullptr);
    CHECK(captured->getStatusCode() == drogon::k200OK);

    // With the fix: "text/html; charset=utf-8" is set via setContentTypeString.
    // With the old bug: addHeader had no effect on contentTypeString().
    const std::string ct = captured->contentTypeString();
    CHECK(ct.find("text/html") != std::string::npos);
}

// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    return drogon::test::run(argc, argv);
}
