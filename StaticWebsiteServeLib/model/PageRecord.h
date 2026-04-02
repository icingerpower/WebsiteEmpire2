#pragma once

#include <cstdint>
#include <string>

/**
 * Metadata for a page entry in content.db.
 * The actual HTML bodies are stored in page_variants, keyed by (page_id, label).
 * etag is an aggregate that changes whenever any active variant changes.
 */
struct PageRecord
{
    int64_t     id;
    std::string path;       // e.g. "/my-page.html"
    std::string domain;     // e.g. "example.com"
    std::string lang;       // e.g. "en"
    std::string etag;
    std::string updatedAt;  // ISO 8601, drives incremental rsync
};
