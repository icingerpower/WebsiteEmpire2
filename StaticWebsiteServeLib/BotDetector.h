#pragma once

#include <string>

/**
 * Classifies an HTTP User-Agent string as bot or human traffic.
 *
 * The stats beacons (POST /stats/display, /stats/session) fire from any
 * client that executes JavaScript — which includes Googlebot's renderer,
 * Bingbot, and every headless-browser crawler.  Without classification the
 * visit log is dominated by crawler renders (measured ~85% of healybio
 * sessions under 15 s during initial indexing).
 *
 * Detection is a case-insensitive substring match against a fixed token
 * list ("bot", "crawl", "spider", "headless", …).  An EMPTY User-Agent is
 * classified as a bot: every real browser sends one, only scripts omit it.
 *
 * Known accepted trade-off: a few exotic real-device UAs contain matched
 * substrings (e.g. Cubot phones contain "bot").  This under-counts humans
 * marginally; never the other way around for major browsers.
 */
class BotDetector
{
public:
    static bool isBot(const std::string &userAgent);
};
