#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * One compiled variant of a page body stored in page_variants.
 * html_gz is the gzip-compressed body WITHOUT the menu fragment.
 * Drogon prepends the menu fragment for (domain, lang) at serve time.
 * label is "control" for the default variant, "a"/"b"/… for A/B test arms.
 */
struct PageVariantRecord
{
    int64_t              id;
    int64_t              pageId;
    std::string          label;    // "control" | "a" | "b" | …
    bool                 isActive;
    std::vector<uint8_t> htmlGz;
    std::string          etag;
};
