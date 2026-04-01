#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * A page fetched from content.db.
 * html_gz is the gzip-compressed HTML blob; serve it with Content-Encoding: gzip.
 * etag is used for HTTP caching (If-None-Match / 304 responses).
 */
struct PageRecord
{
    std::string          id;
    std::string          path;    // e.g. "/my-page.html"
    std::vector<uint8_t> htmlGz;
    std::string          etag;
};
