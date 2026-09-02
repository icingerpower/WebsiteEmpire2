#include <drogon/drogon_test.h>

#include "BotDetector.h"

// ---------------------------------------------------------------------------
// Crawlers must be detected
// ---------------------------------------------------------------------------

DROGON_TEST(test_botdetector_googlebot)
{
    CHECK(BotDetector::isBot(
        "Mozilla/5.0 AppleWebKit/537.36 (KHTML, like Gecko; compatible; "
        "Googlebot/2.1; +http://www.google.com/bot.html) Chrome/125.0 Safari/537.36") == true);
}

DROGON_TEST(test_botdetector_bingbot)
{
    CHECK(BotDetector::isBot(
        "Mozilla/5.0 (compatible; bingbot/2.0; +http://www.bing.com/bingbot.htm)") == true);
}

DROGON_TEST(test_botdetector_headless_chrome)
{
    CHECK(BotDetector::isBot(
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "HeadlessChrome/125.0.0.0 Safari/537.36") == true);
}

DROGON_TEST(test_botdetector_gptbot)
{
    CHECK(BotDetector::isBot(
        "Mozilla/5.0 AppleWebKit/537.36 (KHTML, like Gecko; compatible; "
        "GPTBot/1.0; +https://openai.com/gptbot)") == true);
}

DROGON_TEST(test_botdetector_case_insensitive)
{
    CHECK(BotDetector::isBot("SOMECRAWLER/1.0") == true);
}

DROGON_TEST(test_botdetector_python_requests)
{
    CHECK(BotDetector::isBot("python-requests/2.31.0") == true);
}

DROGON_TEST(test_botdetector_curl)
{
    CHECK(BotDetector::isBot("curl/8.5.0") == true);
}

DROGON_TEST(test_botdetector_empty_ua_is_bot)
{
    // Every real browser sends a User-Agent; only scripts omit it.
    CHECK(BotDetector::isBot("") == true);
}

// ---------------------------------------------------------------------------
// Real browsers must NOT be detected
// ---------------------------------------------------------------------------

DROGON_TEST(test_botdetector_desktop_chrome_is_human)
{
    CHECK(BotDetector::isBot(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36") == false);
}

DROGON_TEST(test_botdetector_desktop_firefox_is_human)
{
    CHECK(BotDetector::isBot(
        "Mozilla/5.0 (X11; Linux x86_64; rv:127.0) Gecko/20100101 Firefox/127.0") == false);
}

DROGON_TEST(test_botdetector_iphone_safari_is_human)
{
    CHECK(BotDetector::isBot(
        "Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 "
        "(KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1") == false);
}

DROGON_TEST(test_botdetector_android_chrome_is_human)
{
    CHECK(BotDetector::isBot(
        "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/126.0.0.0 Mobile Safari/537.36") == false);
}

int main(int argc, char *argv[])
{
    return drogon::test::run(argc, argv);
}
