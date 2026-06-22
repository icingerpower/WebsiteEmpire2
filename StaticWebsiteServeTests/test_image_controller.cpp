/**
 * Tests for ImageController — covers the Content-Type header fix where
 * addHeader("Content-Type", …) was replaced with setContentTypeString()
 * so that Drogon's canonical content-type field is correctly overridden.
 *
 * With the old code (addHeader), resp->contentTypeString() returned an empty
 * string for SVG and WebP images.  With the fix (setContentTypeString), the
 * correct MIME type is returned, making images visible in browsers.
 */

#include <drogon/drogon_test.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <optional>
#include <string>
#include <vector>

#include "model/ImageRecord.h"
#include "repository/IImageRepository.h"

#include "../StaticWebsiteServe/controllers/ImageController.h"

// ---------------------------------------------------------------------------
// Minimal stub implementation
// ---------------------------------------------------------------------------

class StubImageRepository : public IImageRepository
{
public:
    std::optional<ImageRecord> entry;

    std::optional<ImageRecord> findByDomainAndFilename(const std::string &domain,
                                                       const std::string &filename) const override
    {
        if (entry) {
            return entry;
        }
        return std::nullopt;
    }
};

// ---------------------------------------------------------------------------
// Helper: build a request with the given Host header and path
// ---------------------------------------------------------------------------

static drogon::HttpRequestPtr makeRequest(const std::string &host,
                                          const std::string &path)
{
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(path);
    req->addHeader("Host", host);
    return req;
}

// ---------------------------------------------------------------------------
// serveImage — SVG content-type is correctly set
// ---------------------------------------------------------------------------

DROGON_TEST(test_imagecontroller_serveimage_svg_content_type)
{
    StubImageRepository repo;
    ImageRecord rec;
    rec.id       = 1;
    rec.mimeType = "image/svg+xml";
    rec.blob     = {0x3c, 0x73, 0x76, 0x67, 0x3e};  // "<svg>"
    repo.entry   = rec;

    ImageController::setImageRepository(&repo);

    auto req = makeRequest("example.com", "/logo.svg");

    drogon::HttpResponsePtr captured;
    ImageController ctrl;
    ctrl.serveImage(req,
                    [&captured](const drogon::HttpResponsePtr &resp) {
                        captured = resp;
                    },
                    "logo.svg");

    REQUIRE(captured != nullptr);
    CHECK(captured->getStatusCode() == drogon::k200OK);

    // With the fix (setContentTypeString): contentTypeString() contains the
    // correct MIME type set by the controller.
    // With the old bug (addHeader): contentTypeString() would be empty because
    // addHeader does not override Drogon's canonical content-type field.
    const std::string ct = captured->contentTypeString();
    CHECK(ct.find("image/svg+xml") != std::string::npos);
}

// ---------------------------------------------------------------------------
// serveImage — WebP content-type is correctly set
// ---------------------------------------------------------------------------

DROGON_TEST(test_imagecontroller_serveimage_webp_content_type)
{
    StubImageRepository repo;
    ImageRecord rec;
    rec.id       = 2;
    rec.mimeType = "image/webp";
    rec.blob     = {0x52, 0x49, 0x46, 0x46};  // "RIFF" (WebP magic bytes prefix)
    repo.entry   = rec;

    ImageController::setImageRepository(&repo);

    auto req = makeRequest("example.com", "/hero.webp");

    drogon::HttpResponsePtr captured;
    ImageController ctrl;
    ctrl.serveImage(req,
                    [&captured](const drogon::HttpResponsePtr &resp) {
                        captured = resp;
                    },
                    "hero.webp");

    REQUIRE(captured != nullptr);
    CHECK(captured->getStatusCode() == drogon::k200OK);

    // With the fix (setContentTypeString): contentTypeString() returns "image/webp".
    // With the old bug (addHeader): contentTypeString() would be empty.
    const std::string ct = captured->contentTypeString();
    CHECK(ct.find("image/webp") != std::string::npos);
}

// ---------------------------------------------------------------------------
// serveImage — 404 when image not found
// ---------------------------------------------------------------------------

DROGON_TEST(test_imagecontroller_serveimage_returns_404_when_not_found)
{
    StubImageRepository repo;
    // entry is left empty — simulates missing image

    ImageController::setImageRepository(&repo);

    auto req = makeRequest("example.com", "/missing.svg");

    drogon::HttpResponsePtr captured;
    ImageController ctrl;
    ctrl.serveImage(req,
                    [&captured](const drogon::HttpResponsePtr &resp) {
                        captured = resp;
                    },
                    "missing.svg");

    REQUIRE(captured != nullptr);
    CHECK(captured->getStatusCode() == drogon::k404NotFound);
}

// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    return drogon::test::run(argc, argv);
}
