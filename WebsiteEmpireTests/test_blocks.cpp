#include <QtTest>

#include "website/pages/PageBlockText.h"
#include "ExceptionWithTitleText.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

template<typename Fn>
bool throwsException(Fn &&fn)
{
    try {
        fn();
        return false;
    } catch (const ExceptionWithTitleText &) {
        return true;
    }
}

// Runs block.addCode() with the given text and returns the html output.
QString htmlFrom(PageBlockText &block, const QString &text)
{
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    block.addCode(text, html, css, js, cssDoneIds, jsDoneIds);
    return html;
}

} // namespace

// =============================================================================
// Test_Website_PageBlockText
// =============================================================================

class Test_Website_PageBlockText : public QObject
{
    Q_OBJECT

private slots:
    // --- createEditWidget ---
    void test_pageblocktext_create_edit_widget_returns_non_null();
    void test_pageblocktext_create_edit_widget_has_no_parent();

    // --- Empty / whitespace content ---
    void test_pageblocktext_empty_content_produces_no_html();
    void test_pageblocktext_empty_content_leaves_css_js_unchanged();
    void test_pageblocktext_whitespace_only_produces_no_html();

    // --- Single paragraph ---
    void test_pageblocktext_single_para_wrapped_in_p();
    void test_pageblocktext_single_para_text_preserved();
    void test_pageblocktext_single_para_css_untouched();
    void test_pageblocktext_single_para_js_untouched();
    void test_pageblocktext_single_para_appends_to_existing_html();

    // --- Multiple paragraphs ---
    void test_pageblocktext_two_paragraphs_produce_two_p_tags();
    void test_pageblocktext_two_paragraphs_content_preserved();
    void test_pageblocktext_paragraphs_appear_in_order();
    void test_pageblocktext_three_paragraphs_produce_three_p_tags();

    // --- Video shortcode ---
    void test_pageblocktext_video_shortcode_tag_absent_from_output();
    void test_pageblocktext_video_shortcode_url_in_output();
    void test_pageblocktext_video_shortcode_wrapped_in_p();
    void test_pageblocktext_text_before_shortcode_preserved();
    void test_pageblocktext_text_after_shortcode_preserved();
    void test_pageblocktext_two_shortcodes_in_one_paragraph();
    void test_pageblocktext_shortcode_in_second_of_two_paragraphs();

    // --- SPINNABLE ---
    void test_pageblocktext_spinnable_tag_absent_from_output();
    void test_pageblocktext_spinnable_with_nested_video_shortcode();

    // --- Syntax / validation errors ---
    void test_pageblocktext_missing_mandatory_arg_throws();
    void test_pageblocktext_duplicate_arg_throws();
    void test_pageblocktext_unknown_arg_throws();
};

// =============================================================================
// createEditWidget
// =============================================================================

void Test_Website_PageBlockText::test_pageblocktext_create_edit_widget_returns_non_null()
{
    PageBlockText block;
    QWidget *w = block.createEditWidget();
    QVERIFY(w != nullptr);   // 1
    delete w;
}

void Test_Website_PageBlockText::test_pageblocktext_create_edit_widget_has_no_parent()
{
    PageBlockText block;
    QWidget *w = block.createEditWidget();
    QVERIFY(w->parent() == nullptr);   // 2
    delete w;
}

// =============================================================================
// Empty / whitespace
// =============================================================================

void Test_Website_PageBlockText::test_pageblocktext_empty_content_produces_no_html()
{
    PageBlockText block;
    const auto &html = htmlFrom(block, QStringLiteral(""));
    QVERIFY(html.isEmpty());   // 3
}

void Test_Website_PageBlockText::test_pageblocktext_empty_content_leaves_css_js_unchanged()
{
    PageBlockText block;
    QString html;
    QString css = QStringLiteral("pre-existing-css");
    QString js  = QStringLiteral("pre-existing-js");
    QSet<QString> cssDoneIds, jsDoneIds;
    block.addCode(QStringLiteral(""), html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(css == QStringLiteral("pre-existing-css"));   // 4
    QVERIFY(js  == QStringLiteral("pre-existing-js"));    // 5
}

void Test_Website_PageBlockText::test_pageblocktext_whitespace_only_produces_no_html()
{
    PageBlockText block;
    const auto &html = htmlFrom(block, QStringLiteral("   \n\n   \n   "));
    QVERIFY(html.isEmpty());   // 6
}

// =============================================================================
// Single paragraph
// =============================================================================

void Test_Website_PageBlockText::test_pageblocktext_single_para_wrapped_in_p()
{
    PageBlockText block;
    const auto &html = htmlFrom(block, QStringLiteral("Hello world"));
    QVERIFY(html.startsWith(QStringLiteral("<p>")));    // 7
    QVERIFY(html.endsWith(QStringLiteral("</p>")));     // 8
}

void Test_Website_PageBlockText::test_pageblocktext_single_para_text_preserved()
{
    PageBlockText block;
    const auto &html = htmlFrom(block, QStringLiteral("Hello world"));
    QVERIFY(html.contains(QStringLiteral("Hello world")));   // 9
}

void Test_Website_PageBlockText::test_pageblocktext_single_para_css_untouched()
{
    PageBlockText block;
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    block.addCode(QStringLiteral("Some text"), html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(css.isEmpty());   // 10
}

void Test_Website_PageBlockText::test_pageblocktext_single_para_js_untouched()
{
    PageBlockText block;
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    block.addCode(QStringLiteral("Some text"), html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(js.isEmpty());   // 11
}

void Test_Website_PageBlockText::test_pageblocktext_single_para_appends_to_existing_html()
{
    PageBlockText block;
    QString html = QStringLiteral("existing");
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    block.addCode(QStringLiteral("new"), html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.startsWith(QStringLiteral("existing")));   // 12
    QVERIFY(html.contains(QStringLiteral("<p>new</p>")));   // 13
}

// =============================================================================
// Multiple paragraphs
// =============================================================================

void Test_Website_PageBlockText::test_pageblocktext_two_paragraphs_produce_two_p_tags()
{
    PageBlockText block;
    const auto &html = htmlFrom(block, QStringLiteral("first\n\nsecond"));
    QVERIFY(html.count(QStringLiteral("<p>"))  == 2);   // 14
    QVERIFY(html.count(QStringLiteral("</p>")) == 2);   // 15
}

void Test_Website_PageBlockText::test_pageblocktext_two_paragraphs_content_preserved()
{
    PageBlockText block;
    const auto &html = htmlFrom(block, QStringLiteral("first\n\nsecond"));
    QVERIFY(html.contains(QStringLiteral("first")));    // 16
    QVERIFY(html.contains(QStringLiteral("second")));   // 17
}

void Test_Website_PageBlockText::test_pageblocktext_paragraphs_appear_in_order()
{
    PageBlockText block;
    const auto &html = htmlFrom(block, QStringLiteral("alpha\n\nbeta"));
    QVERIFY(html.indexOf(QStringLiteral("alpha")) < html.indexOf(QStringLiteral("beta")));   // 18
}

void Test_Website_PageBlockText::test_pageblocktext_three_paragraphs_produce_three_p_tags()
{
    PageBlockText block;
    const auto &html = htmlFrom(block, QStringLiteral("x\n\ny\n\nz"));
    QVERIFY(html.count(QStringLiteral("<p>"))  == 3);   // 19
    QVERIFY(html.count(QStringLiteral("</p>")) == 3);   // 20
    QVERIFY(html == QStringLiteral("<p>x</p><p>y</p><p>z</p>"));   // 21
}

// =============================================================================
// Video shortcode
// =============================================================================

void Test_Website_PageBlockText::test_pageblocktext_video_shortcode_tag_absent_from_output()
{
    PageBlockText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[VIDEO url=\"https://example.com/video.mp4\"][/VIDEO]"));
    QVERIFY(!html.contains(QStringLiteral("[VIDEO")));    // 22
    QVERIFY( html.contains(QStringLiteral("<video")));    // 23
}

void Test_Website_PageBlockText::test_pageblocktext_video_shortcode_url_in_output()
{
    PageBlockText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[VIDEO url=\"https://example.com/video.mp4\"][/VIDEO]"));
    QVERIFY(html.contains(QStringLiteral("https://example.com/video.mp4")));   // 24
}

void Test_Website_PageBlockText::test_pageblocktext_video_shortcode_wrapped_in_p()
{
    PageBlockText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[VIDEO url=\"https://example.com/video.mp4\"][/VIDEO]"));
    QVERIFY(html.startsWith(QStringLiteral("<p>")));   // 25
    QVERIFY(html.endsWith(QStringLiteral("</p>")));    // 26
}

void Test_Website_PageBlockText::test_pageblocktext_text_before_shortcode_preserved()
{
    PageBlockText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("before [VIDEO url=\"https://example.com/v.mp4\"][/VIDEO]"));
    QVERIFY(html.contains(QStringLiteral("before")));   // 27
    QVERIFY(html.indexOf(QStringLiteral("before")) < html.indexOf(QStringLiteral("<video")));   // 28
}

void Test_Website_PageBlockText::test_pageblocktext_text_after_shortcode_preserved()
{
    PageBlockText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[VIDEO url=\"https://example.com/v.mp4\"][/VIDEO] after"));
    QVERIFY(html.contains(QStringLiteral("after")));   // 29
    QVERIFY(html.indexOf(QStringLiteral("<video")) < html.indexOf(QStringLiteral("after")));   // 30
}

void Test_Website_PageBlockText::test_pageblocktext_two_shortcodes_in_one_paragraph()
{
    PageBlockText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[VIDEO url=\"https://a.com/1.mp4\"][/VIDEO]"
                       "[VIDEO url=\"https://b.com/2.mp4\"][/VIDEO]"));
    QVERIFY(html.count(QStringLiteral("<video"))   == 2);   // 31
    QVERIFY(html.count(QStringLiteral("</video>")) == 2);   // 32
}

void Test_Website_PageBlockText::test_pageblocktext_shortcode_in_second_of_two_paragraphs()
{
    PageBlockText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("first para\n\n[VIDEO url=\"https://example.com/v.mp4\"][/VIDEO]"));
    QVERIFY(html.count(QStringLiteral("<p>")) == 2);               // 33
    QVERIFY(html.contains(QStringLiteral("first para")));          // 34
    QVERIFY(html.contains(QStringLiteral("<video")));              // 35
}

// =============================================================================
// SPINNABLE shortcode
// =============================================================================

void Test_Website_PageBlockText::test_pageblocktext_spinnable_tag_absent_from_output()
{
    PageBlockText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[SPINNABLE id=\"1\"]{hello|world}[/SPINNABLE]"));
    QVERIFY(!html.contains(QStringLiteral("[SPINNABLE")));   // 36
    QVERIFY( html.contains(QStringLiteral("<p>")));          // 37
}

void Test_Website_PageBlockText::test_pageblocktext_spinnable_with_nested_video_shortcode()
{
    // The SPINNABLE spins and emits either a [VIDEO][/VIDEO] shortcode or
    // plain text.  PageBlockText must recursively expand whatever the
    // SPINNABLE emitted, so no raw shortcode tags survive in the final HTML.
    PageBlockText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[SPINNABLE id=\"0\"]"
                       "{[VIDEO url=\"https://v.com/v.mp4\"][/VIDEO]|plaintext}"
                       "[/SPINNABLE]"));

    QVERIFY(!html.contains(QStringLiteral("[SPINNABLE")));    // 38
    QVERIFY(!html.contains(QStringLiteral("[VIDEO")));        // 39
    QVERIFY(!html.contains(QStringLiteral("[/VIDEO]")));      // 40
    QVERIFY(!html.contains(QStringLiteral("[/SPINNABLE]")));  // 41
    // Whichever option the RNG picked, the output must be one of the two
    // valid expansions (video element or the literal word "plaintext").
    QVERIFY(html.contains(QStringLiteral("<video")) ||
            html.contains(QStringLiteral("plaintext")));      // 42
}

// =============================================================================
// Syntax / validation errors
// =============================================================================

void Test_Website_PageBlockText::test_pageblocktext_missing_mandatory_arg_throws()
{
    // VIDEO requires the mandatory "url" argument.
    PageBlockText block;
    QVERIFY(throwsException([&] {   // 43
        htmlFrom(block, QStringLiteral("[VIDEO][/VIDEO]"));
    }));
}

void Test_Website_PageBlockText::test_pageblocktext_duplicate_arg_throws()
{
    // Duplicate argument key must be caught by parse().
    PageBlockText block;
    QVERIFY(throwsException([&] {   // 44
        htmlFrom(block, QStringLiteral("[VIDEO url=\"a\" url=\"b\"][/VIDEO]"));
    }));
}

void Test_Website_PageBlockText::test_pageblocktext_unknown_arg_throws()
{
    // Unknown argument must be caught by validate().
    PageBlockText block;
    QVERIFY(throwsException([&] {   // 45
        htmlFrom(block,
                 QStringLiteral("[VIDEO url=\"https://example.com/v.mp4\" badarg=\"x\"][/VIDEO]"));
    }));
}

QTEST_MAIN(Test_Website_PageBlockText)
#include "test_blocks.moc"
