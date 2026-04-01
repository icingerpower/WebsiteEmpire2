#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * An image fetched from images.db.
 * The name→id indirection lives in the image_names table (keyed by domain + filename),
 * allowing the same binary blob to be served under different filenames across domains.
 */
struct ImageRecord
{
    int64_t              id;
    std::vector<uint8_t> blob;
    std::string          mimeType;  // e.g. "image/webp"
};
