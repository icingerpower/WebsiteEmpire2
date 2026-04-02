#pragma once

#include <drogon/HttpController.h>

#include "repository/IImageRepository.h"

/**
 * Serves image blobs stored in images.db.
 *
 * Route: GET /(.+\.(webp|jpg|jpeg|png|gif|svg|ico))$
 *
 * Resolves filename → image_id via the image_names table (keyed by domain + filename),
 * then streams the blob with the correct Content-Type.
 * Returns 404 if the name is not registered for this domain.
 *
 * The domain is derived from the Host request header (port stripped).
 *
 * Drogon auto-creates this controller; inject dependencies via setImageRepository()
 * before app().run().
 */
class ImageController : public drogon::HttpController<ImageController>
{
public:
    static void setImageRepository(IImageRepository *repo);

    METHOD_LIST_BEGIN
    ADD_METHOD_VIA_REGEX(ImageController::serveImage,
                         "^/(.+\\.(webp|jpg|jpeg|png|gif|svg|ico))$",
                         drogon::Get);
    METHOD_LIST_END

    void serveImage(const drogon::HttpRequestPtr                          &req,
                    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                    const std::string                                     &filename);

private:
    static IImageRepository *s_imageRepo;
};
