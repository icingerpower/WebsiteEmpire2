#include "BotDetector.h"

#include <algorithm>
#include <array>
#include <cctype>

bool BotDetector::isBot(const std::string &userAgent)
{
    if (userAgent.empty()) {
        return true;
    }

    std::string lowered(userAgent.size(), '\0');
    std::transform(userAgent.begin(), userAgent.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // "bot" alone covers Googlebot, Bingbot, DuckDuckBot, YandexBot, GPTBot,
    // AhrefsBot, SemrushBot, PetalBot, TelegramBot, Applebot, …
    static constexpr std::array tokens = {
        "bot",
        "crawl",
        "spider",
        "slurp",
        "headless",
        "lighthouse",
        "phantom",
        "puppeteer",
        "playwright",
        "selenium",
        "python",
        "curl/",
        "wget/",
        "go-http-client",
        "okhttp",
        "java/",
        "libwww",
        "httpclient",
        "scrapy",
        "facebookexternalhit",
        "bingpreview",
        "feedfetcher",
        "mediapartners",
        "pingdom",
        "gtmetrix",
    };

    return std::any_of(tokens.begin(), tokens.end(),
                       [&lowered](const char *token) {
                           return lowered.find(token) != std::string::npos;
                       });
}
