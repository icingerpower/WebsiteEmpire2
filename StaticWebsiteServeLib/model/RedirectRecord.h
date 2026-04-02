#pragma once

#include <string>

/**
 * A redirect rule stored in redirects.
 * statusCode is 301 (permanent redirect) or 410 (gone).
 * newPath is empty when statusCode is 410.
 */
struct RedirectRecord
{
    std::string oldPath;
    std::string newPath;    // empty for 410 responses
    int         statusCode; // 301 or 410
};
