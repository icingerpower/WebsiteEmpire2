#include <QtTest>

#include "website/pages/PageFlag.h"
#include "website/pages/PageGenerationState.h"
#include "website/pages/PageStateFormatter.h"

class Test_Website_PageStateFormatter : public QObject
{
    Q_OBJECT

private slots:
    // --- formatGenerationState ---
    void test_formatter_state_pending();
    void test_formatter_state_content_ready();
    void test_formatter_state_main_image_ready();
    void test_formatter_state_complete();
    void test_formatter_state_social_complete();
    void test_formatter_state_unknown_returns_question_mark();

    // --- formatFlags ---
    void test_formatter_flags_none_returns_empty();
    void test_formatter_flags_social_media();
    void test_formatter_flags_needs_rewrite();
    void test_formatter_flags_needs_ads();
    void test_formatter_flags_social_and_rewrite();
    void test_formatter_flags_social_and_ads();
    void test_formatter_flags_rewrite_and_ads();
    void test_formatter_flags_all_three();
    void test_formatter_flags_order_is_s_r_a();
};

// ---------------------------------------------------------------------------
// formatGenerationState
// ---------------------------------------------------------------------------

void Test_Website_PageStateFormatter::test_formatter_state_pending()
{
    QCOMPARE(PageStateFormatter::formatGenerationState(
                 static_cast<int>(PageGenerationState::Pending)),
             QStringLiteral("Pending"));
}

void Test_Website_PageStateFormatter::test_formatter_state_content_ready()
{
    QCOMPARE(PageStateFormatter::formatGenerationState(
                 static_cast<int>(PageGenerationState::ContentReady)),
             QStringLiteral("Content"));
}

void Test_Website_PageStateFormatter::test_formatter_state_main_image_ready()
{
    QCOMPARE(PageStateFormatter::formatGenerationState(
                 static_cast<int>(PageGenerationState::MainImageReady)),
             QStringLiteral("Image"));
}

void Test_Website_PageStateFormatter::test_formatter_state_complete()
{
    QCOMPARE(PageStateFormatter::formatGenerationState(
                 static_cast<int>(PageGenerationState::Complete)),
             QStringLiteral("Complete"));
}

void Test_Website_PageStateFormatter::test_formatter_state_social_complete()
{
    QCOMPARE(PageStateFormatter::formatGenerationState(
                 static_cast<int>(PageGenerationState::SocialComplete)),
             QStringLiteral("CompleteWithSocial"));
}

void Test_Website_PageStateFormatter::test_formatter_state_unknown_returns_question_mark()
{
    QCOMPARE(PageStateFormatter::formatGenerationState(999), QStringLiteral("?"));
}

// ---------------------------------------------------------------------------
// formatFlags
// ---------------------------------------------------------------------------

void Test_Website_PageStateFormatter::test_formatter_flags_none_returns_empty()
{
    QCOMPARE(PageStateFormatter::formatFlags(0u), QStringLiteral(""));
}

void Test_Website_PageStateFormatter::test_formatter_flags_social_media()
{
    QCOMPARE(PageStateFormatter::formatFlags(static_cast<quint32>(PageFlag::SocialMedia)),
             QStringLiteral("S"));
}

void Test_Website_PageStateFormatter::test_formatter_flags_needs_rewrite()
{
    QCOMPARE(PageStateFormatter::formatFlags(static_cast<quint32>(PageFlag::NeedsRewrite)),
             QStringLiteral("R"));
}

void Test_Website_PageStateFormatter::test_formatter_flags_needs_ads()
{
    QCOMPARE(PageStateFormatter::formatFlags(static_cast<quint32>(PageFlag::NeedsAds)),
             QStringLiteral("A"));
}

void Test_Website_PageStateFormatter::test_formatter_flags_social_and_rewrite()
{
    const quint32 flags = static_cast<quint32>(PageFlag::SocialMedia)
                        | static_cast<quint32>(PageFlag::NeedsRewrite);
    QCOMPARE(PageStateFormatter::formatFlags(flags), QStringLiteral("S R"));
}

void Test_Website_PageStateFormatter::test_formatter_flags_social_and_ads()
{
    const quint32 flags = static_cast<quint32>(PageFlag::SocialMedia)
                        | static_cast<quint32>(PageFlag::NeedsAds);
    QCOMPARE(PageStateFormatter::formatFlags(flags), QStringLiteral("S A"));
}

void Test_Website_PageStateFormatter::test_formatter_flags_rewrite_and_ads()
{
    const quint32 flags = static_cast<quint32>(PageFlag::NeedsRewrite)
                        | static_cast<quint32>(PageFlag::NeedsAds);
    QCOMPARE(PageStateFormatter::formatFlags(flags), QStringLiteral("R A"));
}

void Test_Website_PageStateFormatter::test_formatter_flags_all_three()
{
    const quint32 flags = static_cast<quint32>(PageFlag::SocialMedia)
                        | static_cast<quint32>(PageFlag::NeedsRewrite)
                        | static_cast<quint32>(PageFlag::NeedsAds);
    QCOMPARE(PageStateFormatter::formatFlags(flags), QStringLiteral("S R A"));
}

void Test_Website_PageStateFormatter::test_formatter_flags_order_is_s_r_a()
{
    // Verify order is independent of which bits happen to be numerically lower.
    // Set all three and check the exact output string.
    const quint32 all = static_cast<quint32>(PageFlag::SocialMedia)
                      | static_cast<quint32>(PageFlag::NeedsRewrite)
                      | static_cast<quint32>(PageFlag::NeedsAds);
    const QString result = PageStateFormatter::formatFlags(all);
    QVERIFY2(result.startsWith(QStringLiteral("S")), qPrintable(result));
    QVERIFY2(result.contains(QStringLiteral("R")),   qPrintable(result));
    QVERIFY2(result.endsWith(QStringLiteral("A")),   qPrintable(result));
}

QTEST_MAIN(Test_Website_PageStateFormatter)
#include "test_website_page_state_formatter.moc"
