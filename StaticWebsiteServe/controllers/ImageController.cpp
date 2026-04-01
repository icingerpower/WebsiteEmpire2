#include "ImageController.h"

#include <drogon/HttpResponse.h>

ImageDb *ImageController::s_imageDb = nullptr;

void ImageController::setImageDb(ImageDb *db)
{
    s_imageDb = db;
}

void ImageController::serveImage(const drogon::HttpRequestPtr                          &req,
                                  std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                  const std::string                                     &filename)
{
    // TODO: extract domain from req->getHeader("Host"), resolve name→id, stream blob.
    callback(drogon::HttpResponse::newNotFoundResponse());
}
