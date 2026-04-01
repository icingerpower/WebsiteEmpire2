#include <QtTest>

#include "website/pages/PageTypeArticle.h"
#include "website/pages/blocs/AbstractPageBloc.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

// Runs article.addCode() and returns the html output.
QString htmlFrom(PageTypeArticle &article, const QString &text)
{
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    article.addCode(text, html, css, js, cssDoneIds, jsDoneIds);
    return html;
}

} // namespace

// =============================================================================
// Test_PageTypeArticle
// =============================================================================

class Test_PageTypeArticle : public QObject
{
    Q_OBJECT

private slots:
    // --- getPageBlocs ---
    void test_pagetypearticle_get_page_blocs_returns_one_bloc();
    void test_pagetypearticle_get_page_blocs_bloc_is_non_null();
    void test_pagetypearticle_get_page_blocs_returns_same_ref_on_repeated_calls();

    // --- getAttributes ---
    void test_pagetypearticle_get_attributes_empty_by_default();
    void test_pagetypearticle_get_attributes_returns_same_ref_on_repeated_calls();

    // --- addCode: empty / whitespace ---
    void test_pagetypearticle_addcode_empty_content_no_html();
    void test_pagetypearticle_addcode_whitespace_only_no_html();
    void test_pagetypearticle_addcode_empty_content_leaves_css_js_unchanged();

    // --- addCode: single paragraph ---
    void test_pagetypearticle_addcode_single_para_wrapped_in_p();
    void test_pagetypearticle_addcode_single_para_text_preserved();
    void test_pagetypearticle_addcode_single_para_css_untouched();
    void test_pagetypearticle_addcode_single_para_js_untouched();
    void test_pagetypearticle_addcode_appends_to_existing_html();

    // --- addCode: multiple paragraphs ---
    void test_pagetypearticle_addcode_two_paragraphs_produce_two_p_tags();
    void test_pagetypearticle_addcode_two_paragraphs_content_preserved();
    void test_pagetypearticle_addcode_paragraphs_appear_in_order();
    void test_pagetypearticle_addcode_three_paragraphs_produce_three_p_tags();

    // --- addCode: shortcode expansion ---
    void test_pagetypearticle_addcode_video_shortcode_expanded();
    void test_pagetypearticle_addcode_video_shortcode_tag_absent_from_output();
    void test_pagetypearticle_addcode_video_url_in_output();

    // --- addCode: repeated calls accumulate ---
    void test_pagetypearticle_addcode_two_calls_accumulate_html();
};

// =============================================================================
// getPageBlocs
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_get_page_blocs_returns_one_bloc()
{
    PageTypeArticle article;
    QVERIFY(article.getPageBlocs().size() == 1);   // 1
}

void Test_PageTypeArticle::test_pagetypearticle_get_page_blocs_bloc_is_non_null()
{
    PageTypeArticle article;
    QVERIFY(article.getPageBlocs().first() != nullptr);   // 2
}

void Test_PageTypeArticle::test_pagetypearticle_get_page_blocs_returns_same_ref_on_repeated_calls()
{
    PageTypeArticle article;
    const QList<const AbstractPageBloc *> *first  = &article.getPageBlocs();
    const QList<const AbstractPageBloc *> *second = &article.getPageBlocs();
    QVERIFY(first == second);   // 3
}

// =============================================================================
// getAttributes
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_get_attributes_empty_by_default()
{
    // PageBlocText exposes no attributes — the aggregated list must be empty.
    PageTypeArticle article;
    QVERIFY(article.getAttributes().isEmpty());   // 4
}

void Test_PageTypeArticle::test_pagetypearticle_get_attributes_returns_same_ref_on_repeated_calls()
{
    // The lazy cache must return the same list address every time.
    PageTypeArticle article;
    const QList<const AbstractAttribute *> *first  = &article.getAttributes();
    const QList<const AbstractAttribute *> *second = &article.getAttributes();
    QVERIFY(first == second);   // 5
}

// =============================================================================
// addCode: empty / whitespace
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_addcode_empty_content_no_html()
{
    PageTypeArticle article;
    const auto &html = htmlFrom(article, QStringLiteral(""));
    QVERIFY(html.isEmpty());   // 6
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_whitespace_only_no_html()
{
    PageTypeArticle article;
    const auto &html = htmlFrom(article, QStringLiteral("   \n\n   \n   "));
    QVERIFY(html.isEmpty());   // 7
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_empty_content_leaves_css_js_unchanged()
{
    PageTypeArticle article;
    QString html;
    QString css = QStringLiteral("existing-css");
    QString js  = QStringLiteral("existing-js");
    QSet<QString> cssDoneIds, jsDoneIds;
    article.addCode(QStringLiteral(""), html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(css == QStringLiteral("existing-css"));   // 8
    QVERIFY(js  == QStringLiteral("existing-js"));    // 9
}

// =============================================================================
// addCode: single paragraph
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_addcode_single_para_wrapped_in_p()
{
    PageTypeArticle article;
    const auto &html = htmlFrom(article, QStringLiteral("Hello world"));
    QVERIFY(html.startsWith(QStringLiteral("<p>")));   // 10
    QVERIFY(html.endsWith(QStringLiteral("</p>")));    // 11
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_single_para_text_preserved()
{
    PageTypeArticle article;
    const auto &html = htmlFrom(article, QStringLiteral("Hello world"));
    QVERIFY(html.contains(QStringLiteral("Hello world")));   // 12
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_single_para_css_untouched()
{
    PageTypeArticle article;
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    article.addCode(QStringLiteral("Some text"), html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(css.isEmpty());   // 13
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_single_para_js_untouched()
{
    PageTypeArticle article;
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    article.addCode(QStringLiteral("Some text"), html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(js.isEmpty());   // 14
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_appends_to_existing_html()
{
    PageTypeArticle article;
    QString html = QStringLiteral("prefix");
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    article.addCode(QStringLiteral("new"), html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.startsWith(QStringLiteral("prefix")));        // 15
    QVERIFY(html.contains(QStringLiteral("<p>new</p>")));      // 16
}

// =============================================================================
// addCode: multiple paragraphs
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_addcode_two_paragraphs_produce_two_p_tags()
{
    PageTypeArticle article;
    const auto &html = htmlFrom(article, QStringLiteral("first\n\nsecond"));
    QVERIFY(html.count(QStringLiteral("<p>"))  == 2);   // 17
    QVERIFY(html.count(QStringLiteral("</p>")) == 2);   // 18
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_two_paragraphs_content_preserved()
{
    PageTypeArticle article;
    const auto &html = htmlFrom(article, QStringLiteral("first\n\nsecond"));
    QVERIFY(html.contains(QStringLiteral("first")));    // 19
    QVERIFY(html.contains(QStringLiteral("second")));   // 20
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_paragraphs_appear_in_order()
{
    PageTypeArticle article;
    const auto &html = htmlFrom(article, QStringLiteral("alpha\n\nbeta"));
    QVERIFY(html.indexOf(QStringLiteral("alpha")) < html.indexOf(QStringLiteral("beta")));   // 21
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_three_paragraphs_produce_three_p_tags()
{
    PageTypeArticle article;
    const auto &html = htmlFrom(article, QStringLiteral("x\n\ny\n\nz"));
    QVERIFY(html.count(QStringLiteral("<p>"))  == 3);                          // 22
    QVERIFY(html.count(QStringLiteral("</p>")) == 3);                          // 23
    QVERIFY(html == QStringLiteral("<p>x</p><p>y</p><p>z</p>"));               // 24
}

// =============================================================================
// addCode: shortcode expansion
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_addcode_video_shortcode_expanded()
{
    PageTypeArticle article;
    const auto &html = htmlFrom(
        article,
        QStringLiteral("[VIDEO url=\"https://example.com/video.mp4\"][/VIDEO]"));
    QVERIFY(html.contains(QStringLiteral("<video")));   // 25
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_video_shortcode_tag_absent_from_output()
{
    PageTypeArticle article;
    const auto &html = htmlFrom(
        article,
        QStringLiteral("[VIDEO url=\"https://example.com/video.mp4\"][/VIDEO]"));
    QVERIFY(!html.contains(QStringLiteral("[VIDEO")));   // 26
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_video_url_in_output()
{
    PageTypeArticle article;
    const auto &html = htmlFrom(
        article,
        QStringLiteral("[VIDEO url=\"https://example.com/video.mp4\"][/VIDEO]"));
    QVERIFY(html.contains(QStringLiteral("https://example.com/video.mp4")));   // 27
}

// =============================================================================
// addCode: repeated calls accumulate
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_addcode_two_calls_accumulate_html()
{
    // Calling addCode twice on the same buffers must accumulate both outputs.
    PageTypeArticle article;
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    article.addCode(QStringLiteral("first"), html, css, js, cssDoneIds, jsDoneIds);
    article.addCode(QStringLiteral("second"), html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.contains(QStringLiteral("<p>first</p>")));    // 28
    QVERIFY(html.contains(QStringLiteral("<p>second</p>")));   // 29
    QVERIFY(html.indexOf(QStringLiteral("first")) < html.indexOf(QStringLiteral("second")));   // 30
}

QTEST_MAIN(Test_PageTypeArticle)
#include "test_page_type_article.moc"
