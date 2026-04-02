#include "ImageController.h"

#include <drogon/HttpResponse.h>

IImageRepository *ImageController::s_imageRepo = nullptr;

void ImageController::setImageRepository(IImageRepository *repo)
{
    s_imageRepo = repo;
}

void ImageController::serveImage(const drogon::HttpRequestPtr                          &req,
                                  std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                  const std::string                                     &filename)
{
    // Extract bare hostname from Host header (strip port if present).
    std::string domain = req->getHeader("Host");
    const auto colonPos = domain.find(':');
    if (colonPos != std::string::npos) {
        domain = domain.substr(0, colonPos);
    }

    const auto imageOpt = s_imageRepo->findByDomainAndFilename(domain, filename);
    if (!imageOpt) {
        callback(drogon::HttpResponse::newNotFoundResponse());
        return;
    }

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k200OK);
    resp->addHeader("Content-Type", imageOpt->mimeType);
    resp->addHeader("Cache-Control", "public, max-age=31536000, immutable");
    resp->setBody(std::string(reinterpret_cast<const char *>(imageOpt->blob.data()),
                               imageOpt->blob.size()));
    callback(resp);
}
