#pragma once

#include <string>

/**
 * A redirect rule stored in redirects.
 * statusCode:
 *   301 — permanent redirect; newPath holds the target.
 *   302 — temporary/parked redirect; newPath holds the target.
 *   410 — content deleted (Gone); newPath is empty.
 */
struct RedirectRecord
{
    std::string oldPath;
    std::string newPath;    // empty for 410 responses
    int         statusCode; // 301, 302, or 410
};
