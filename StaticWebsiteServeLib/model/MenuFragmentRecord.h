#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * A compiled menu fragment stored in menu_fragments.
 * html_gz is the gzip-compressed menu HTML.
 *
 * Two-pass rendering: Drogon prepends this fragment to the page variant body
 * (page_variants.html_gz) to produce the full page response without
 * decompressing either stream.
 */
struct MenuFragmentRecord
{
    int64_t              id;
    std::string          domain;     // e.g. "example.com"
    std::string          lang;       // e.g. "en"
    std::vector<uint8_t> htmlGz;
    std::string          versionId;  // changes when menu content changes
    std::string          updatedAt;  // ISO 8601
};
