#include <QtTest>

#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"
#include "website/pages/blocs/PageBlocText.h"
#include "website/EngineArticles.h"
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

// Engine instance used in addCode calls (no init() — getLangCode returns "" for all indices).
EngineArticles engine;

// Loads text into block then runs addCode(); returns the html output.
QString htmlFrom(PageBlocText &block, const QString &text)
{
    block.load({{QLatin1String(PageBlocText::KEY_TEXT), text}});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    block.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    return html;
}

} // namespace

// =============================================================================
// Test_Website_PageBlocText
// =============================================================================

class Test_Website_PageBlocText : public QObject
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

    // --- load / save ---
    void test_pagebloctext_save_stores_text_under_key();
    void test_pagebloctext_load_restores_text();
    void test_pagebloctext_load_save_roundtrip();
    void test_pagebloctext_load_ignores_unknown_keys();
    void test_pagebloctext_load_empty_hash_gives_empty_output();

    // --- Syntax / validation errors ---
    void test_pageblocktext_missing_mandatory_arg_throws();
    void test_pageblocktext_duplicate_arg_throws();
    void test_pageblocktext_unknown_arg_throws();
};

// =============================================================================
// createEditWidget
// =============================================================================

void Test_Website_PageBlocText::test_pageblocktext_create_edit_widget_returns_non_null()
{
    PageBlocText block;
    AbstractPageBlockWidget *w = block.createEditWidget();
    QVERIFY(w != nullptr);   // 1
    delete w;
}

void Test_Website_PageBlocText::test_pageblocktext_create_edit_widget_has_no_parent()
{
    PageBlocText block;
    AbstractPageBlockWidget *w = block.createEditWidget();
    QVERIFY(w->parent() == nullptr);   // 2
    delete w;
}

// =============================================================================
// Empty / whitespace
// =============================================================================

void Test_Website_PageBlocText::test_pageblocktext_empty_content_produces_no_html()
{
    PageBlocText block;
    const auto &html = htmlFrom(block, QStringLiteral(""));
    QVERIFY(html.isEmpty());   // 3
}

void Test_Website_PageBlocText::test_pageblocktext_empty_content_leaves_css_js_unchanged()
{
    PageBlocText block;
    QString html;
    QString css = QStringLiteral("pre-existing-css");
    QString js  = QStringLiteral("pre-existing-js");
    QSet<QString> cssDoneIds, jsDoneIds;
    block.addCode(QStringLiteral(""), engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(css == QStringLiteral("pre-existing-css"));   // 4
    QVERIFY(js  == QStringLiteral("pre-existing-js"));    // 5
}

void Test_Website_PageBlocText::test_pageblocktext_whitespace_only_produces_no_html()
{
    PageBlocText block;
    const auto &html = htmlFrom(block, QStringLiteral("   \n\n   \n   "));
    QVERIFY(html.isEmpty());   // 6
}

// =============================================================================
// Single paragraph
// =============================================================================

void Test_Website_PageBlocText::test_pageblocktext_single_para_wrapped_in_p()
{
    PageBlocText block;
    const auto &html = htmlFrom(block, QStringLiteral("Hello world"));
    QVERIFY(html.startsWith(QStringLiteral("<p>")));    // 7
    QVERIFY(html.endsWith(QStringLiteral("</p>")));     // 8
}

void Test_Website_PageBlocText::test_pageblocktext_single_para_text_preserved()
{
    PageBlocText block;
    const auto &html = htmlFrom(block, QStringLiteral("Hello world"));
    QVERIFY(html.contains(QStringLiteral("Hello world")));   // 9
}

void Test_Website_PageBlocText::test_pageblocktext_single_para_css_untouched()
{
    PageBlocText block;
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    block.addCode(QStringLiteral("Some text"), engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(css.isEmpty());   // 10
}

void Test_Website_PageBlocText::test_pageblocktext_single_para_js_untouched()
{
    PageBlocText block;
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    block.addCode(QStringLiteral("Some text"), engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(js.isEmpty());   // 11
}

void Test_Website_PageBlocText::test_pageblocktext_single_para_appends_to_existing_html()
{
    PageBlocText block;
    block.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("new")}});
    QString html = QStringLiteral("existing");
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    block.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.startsWith(QStringLiteral("existing")));   // 12
    QVERIFY(html.contains(QStringLiteral("<p>new</p>")));   // 13
}

// =============================================================================
// Multiple paragraphs
// =============================================================================

void Test_Website_PageBlocText::test_pageblocktext_two_paragraphs_produce_two_p_tags()
{
    PageBlocText block;
    const auto &html = htmlFrom(block, QStringLiteral("first\n\nsecond"));
    QVERIFY(html.count(QStringLiteral("<p>"))  == 2);   // 14
    QVERIFY(html.count(QStringLiteral("</p>")) == 2);   // 15
}

void Test_Website_PageBlocText::test_pageblocktext_two_paragraphs_content_preserved()
{
    PageBlocText block;
    const auto &html = htmlFrom(block, QStringLiteral("first\n\nsecond"));
    QVERIFY(html.contains(QStringLiteral("first")));    // 16
    QVERIFY(html.contains(QStringLiteral("second")));   // 17
}

void Test_Website_PageBlocText::test_pageblocktext_paragraphs_appear_in_order()
{
    PageBlocText block;
    const auto &html = htmlFrom(block, QStringLiteral("alpha\n\nbeta"));
    QVERIFY(html.indexOf(QStringLiteral("alpha")) < html.indexOf(QStringLiteral("beta")));   // 18
}

void Test_Website_PageBlocText::test_pageblocktext_three_paragraphs_produce_three_p_tags()
{
    PageBlocText block;
    const auto &html = htmlFrom(block, QStringLiteral("x\n\ny\n\nz"));
    QVERIFY(html.count(QStringLiteral("<p>"))  == 3);   // 19
    QVERIFY(html.count(QStringLiteral("</p>")) == 3);   // 20
    QVERIFY(html == QStringLiteral("<p>x</p><p>y</p><p>z</p>"));   // 21
}

// =============================================================================
// Video shortcode
// =============================================================================

void Test_Website_PageBlocText::test_pageblocktext_video_shortcode_tag_absent_from_output()
{
    PageBlocText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[VIDEO url=\"https://example.com/video.mp4\"][/VIDEO]"));
    QVERIFY(!html.contains(QStringLiteral("[VIDEO")));    // 22
    QVERIFY( html.contains(QStringLiteral("<video")));    // 23
}

void Test_Website_PageBlocText::test_pageblocktext_video_shortcode_url_in_output()
{
    PageBlocText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[VIDEO url=\"https://example.com/video.mp4\"][/VIDEO]"));
    QVERIFY(html.contains(QStringLiteral("https://example.com/video.mp4")));   // 24
}

void Test_Website_PageBlocText::test_pageblocktext_video_shortcode_wrapped_in_p()
{
    PageBlocText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[VIDEO url=\"https://example.com/video.mp4\"][/VIDEO]"));
    QVERIFY(html.startsWith(QStringLiteral("<p>")));   // 25
    QVERIFY(html.endsWith(QStringLiteral("</p>")));    // 26
}

void Test_Website_PageBlocText::test_pageblocktext_text_before_shortcode_preserved()
{
    PageBlocText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("before [VIDEO url=\"https://example.com/v.mp4\"][/VIDEO]"));
    QVERIFY(html.contains(QStringLiteral("before")));   // 27
    QVERIFY(html.indexOf(QStringLiteral("before")) < html.indexOf(QStringLiteral("<video")));   // 28
}

void Test_Website_PageBlocText::test_pageblocktext_text_after_shortcode_preserved()
{
    PageBlocText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[VIDEO url=\"https://example.com/v.mp4\"][/VIDEO] after"));
    QVERIFY(html.contains(QStringLiteral("after")));   // 29
    QVERIFY(html.indexOf(QStringLiteral("<video")) < html.indexOf(QStringLiteral("after")));   // 30
}

void Test_Website_PageBlocText::test_pageblocktext_two_shortcodes_in_one_paragraph()
{
    PageBlocText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[VIDEO url=\"https://a.com/1.mp4\"][/VIDEO]"
                       "[VIDEO url=\"https://b.com/2.mp4\"][/VIDEO]"));
    QVERIFY(html.count(QStringLiteral("<video"))   == 2);   // 31
    QVERIFY(html.count(QStringLiteral("</video>")) == 2);   // 32
}

void Test_Website_PageBlocText::test_pageblocktext_shortcode_in_second_of_two_paragraphs()
{
    PageBlocText block;
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

void Test_Website_PageBlocText::test_pageblocktext_spinnable_tag_absent_from_output()
{
    PageBlocText block;
    const auto &html = htmlFrom(
        block,
        QStringLiteral("[SPINNABLE id=\"1\"]{hello|world}[/SPINNABLE]"));
    QVERIFY(!html.contains(QStringLiteral("[SPINNABLE")));   // 36
    QVERIFY( html.contains(QStringLiteral("<p>")));          // 37
}

void Test_Website_PageBlocText::test_pageblocktext_spinnable_with_nested_video_shortcode()
{
    // The SPINNABLE spins and emits either a [VIDEO][/VIDEO] shortcode or
    // plain text.  PageBlocText must recursively expand whatever the
    // SPINNABLE emitted, so no raw shortcode tags survive in the final HTML.
    PageBlocText block;
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
// load / save
// =============================================================================

void Test_Website_PageBlocText::test_pagebloctext_save_stores_text_under_key()
{
    PageBlocText block;
    block.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("hello")}});
    QHash<QString, QString> out;
    block.save(out);
    QVERIFY(out.contains(QLatin1String(PageBlocText::KEY_TEXT)));               // 46
    QCOMPARE(out.value(QLatin1String(PageBlocText::KEY_TEXT)), QStringLiteral("hello")); // 47
}

void Test_Website_PageBlocText::test_pagebloctext_load_restores_text()
{
    PageBlocText block;
    block.load({{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("world")}});
    const auto &html = htmlFrom(block, QStringLiteral("world"));
    QVERIFY(html.contains(QStringLiteral("world")));   // 48
}

void Test_Website_PageBlocText::test_pagebloctext_load_save_roundtrip()
{
    const QString original = QStringLiteral("round\n\ntrip");
    PageBlocText block;
    block.load({{QLatin1String(PageBlocText::KEY_TEXT), original}});
    QHash<QString, QString> saved;
    block.save(saved);

    PageBlocText block2;
    block2.load(saved);
    QHash<QString, QString> saved2;
    block2.save(saved2);

    QCOMPARE(saved2.value(QLatin1String(PageBlocText::KEY_TEXT)), original);   // 49
}

void Test_Website_PageBlocText::test_pagebloctext_load_ignores_unknown_keys()
{
    PageBlocText block;
    // Must not throw even with unrecognised keys (forward compatibility).
    block.load({
        {QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("text")},
        {QStringLiteral("unknown_future_key"), QStringLiteral("value")},
    });
    QHash<QString, QString> out;
    block.save(out);
    QVERIFY(!out.contains(QStringLiteral("unknown_future_key")));   // 50
}

void Test_Website_PageBlocText::test_pagebloctext_load_empty_hash_gives_empty_output()
{
    PageBlocText block;
    block.load({});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    block.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.isEmpty());   // 51
}

// =============================================================================
// Syntax / validation errors
// =============================================================================

void Test_Website_PageBlocText::test_pageblocktext_missing_mandatory_arg_throws()
{
    // VIDEO requires the mandatory "url" argument.
    PageBlocText block;
    QVERIFY(throwsException([&] {   // 43
        htmlFrom(block, QStringLiteral("[VIDEO][/VIDEO]"));
    }));
}

void Test_Website_PageBlocText::test_pageblocktext_duplicate_arg_throws()
{
    // Duplicate argument key must be caught by parse().
    PageBlocText block;
    QVERIFY(throwsException([&] {   // 44
        htmlFrom(block, QStringLiteral("[VIDEO url=\"a\" url=\"b\"][/VIDEO]"));
    }));
}

void Test_Website_PageBlocText::test_pageblocktext_unknown_arg_throws()
{
    // Unknown argument must be caught by validate().
    PageBlocText block;
    QVERIFY(throwsException([&] {   // 45
        htmlFrom(block,
                 QStringLiteral("[VIDEO url=\"https://example.com/v.mp4\" badarg=\"x\"][/VIDEO]"));
    }));
}

QTEST_MAIN(Test_Website_PageBlocText)
#include "test_website_page_bloc_text.moc"
