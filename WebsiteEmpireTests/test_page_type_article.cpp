#include <QtTest>

#include <QTemporaryDir>

#include "website/pages/PageTypeArticle.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocCategory.h"
#include "website/pages/blocs/PageBlocText.h"
#include "website/pages/blocs/AbstractPageBloc.h"
#include "website/EngineArticles.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

// Engine instance used in addCode calls (no init() — getLangCode returns "" for all indices).
EngineArticles engine;

// Owns the working directory and CategoryTable so PageTypeArticle stays valid.
struct ArticleFixture {
    QTemporaryDir   dir;
    CategoryTable   categoryTable;
    PageTypeArticle article;

    ArticleFixture()
        : categoryTable(QDir(dir.path()))
        , article(categoryTable)
    {}
};

// Builds the prefixed hash that PageTypeArticle::load() expects.
// PageTypeArticle: bloc 0 = PageBlocCategory, bloc 1 = PageBlocText.
QHash<QString, QString> makeHash(const QString &categories, const QString &text)
{
    return {
        {QStringLiteral("0_") + QLatin1String(PageBlocCategory::KEY_CATEGORIES), categories},
        {QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT),           text},
    };
}

// Loads text into the article then runs addCode(); returns the html output.
QString htmlFrom(ArticleFixture &f, const QString &text)
{
    f.article.load(makeHash(QString(), text));
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    f.article.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    return html;
}

// Returns the substring of html between <body> and </body>.
QString bodyContent(const QString &html)
{
    const int start = html.indexOf(QStringLiteral("<body>")) + 6;
    const int end   = html.indexOf(QStringLiteral("</body>"));
    if (start < 6 || end < 0 || end <= start) {
        return QString();
    }
    return html.mid(start, end - start);
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
    void test_pagetypearticle_get_page_blocs_returns_two_blocs();
    void test_pagetypearticle_get_page_blocs_all_non_null();
    void test_pagetypearticle_get_page_blocs_returns_same_ref_on_repeated_calls();

    // --- getAttributes ---
    void test_pagetypearticle_get_attributes_empty_by_default();
    void test_pagetypearticle_get_attributes_returns_same_ref_on_repeated_calls();

    // --- load / save ---
    void test_pagetypearticle_save_prefixes_text_bloc_key();
    void test_pagetypearticle_save_prefixes_category_bloc_key();
    void test_pagetypearticle_load_save_roundtrip_text();
    void test_pagetypearticle_load_ignores_unknown_prefix();
    void test_pagetypearticle_load_empty_hash_produces_empty_text_key();

    // --- addCode: HTML page structure ---
    void test_pagetypearticle_addcode_starts_with_doctype();
    void test_pagetypearticle_addcode_contains_html_head_body_tags();
    void test_pagetypearticle_addcode_ends_with_body_html_close();
    void test_pagetypearticle_addcode_head_before_body();
    void test_pagetypearticle_addcode_output_css_js_params_untouched();

    // --- addCode: CSS inlining ---
    void test_pagetypearticle_addcode_no_style_tag_when_no_css();
    void test_pagetypearticle_addcode_css_inlined_in_style_tag();
    void test_pagetypearticle_addcode_style_tag_in_head_before_body();

    // --- addCode: JS inlining ---
    void test_pagetypearticle_addcode_no_script_tag_when_no_js();

    // --- addCode: body content ---
    void test_pagetypearticle_addcode_empty_text_empty_body();
    void test_pagetypearticle_addcode_text_content_in_body();
    void test_pagetypearticle_addcode_single_para_wrapped_in_p_in_body();
    void test_pagetypearticle_addcode_two_paragraphs_produce_two_p_tags();
    void test_pagetypearticle_addcode_paragraphs_appear_in_order();
    void test_pagetypearticle_addcode_video_shortcode_expanded_in_body();
    void test_pagetypearticle_addcode_video_shortcode_tag_absent_from_output();
};

// =============================================================================
// getPageBlocs
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_get_page_blocs_returns_two_blocs()
{
    ArticleFixture f;
    QCOMPARE(f.article.getPageBlocs().size(), 2);   // 1
}

void Test_PageTypeArticle::test_pagetypearticle_get_page_blocs_all_non_null()
{
    ArticleFixture f;
    for (const AbstractPageBloc *bloc : f.article.getPageBlocs()) {
        QVERIFY(bloc != nullptr);   // 2
    }
}

void Test_PageTypeArticle::test_pagetypearticle_get_page_blocs_returns_same_ref_on_repeated_calls()
{
    ArticleFixture f;
    QVERIFY(&f.article.getPageBlocs() == &f.article.getPageBlocs());   // 3
}

// =============================================================================
// getAttributes
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_get_attributes_empty_by_default()
{
    ArticleFixture f;
    QVERIFY(f.article.getAttributes().isEmpty());   // 4
}

void Test_PageTypeArticle::test_pagetypearticle_get_attributes_returns_same_ref_on_repeated_calls()
{
    ArticleFixture f;
    QVERIFY(&f.article.getAttributes() == &f.article.getAttributes());   // 5
}

// =============================================================================
// load / save
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_save_prefixes_text_bloc_key()
{
    ArticleFixture f;
    f.article.load(makeHash(QString(), QStringLiteral("hello")));
    QHash<QString, QString> saved;
    f.article.save(saved);

    const QString key = QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT);
    QVERIFY(saved.contains(key));                            // 6
    QCOMPARE(saved.value(key), QStringLiteral("hello"));    // 7
}

void Test_PageTypeArticle::test_pagetypearticle_save_prefixes_category_bloc_key()
{
    ArticleFixture f;
    f.article.load(makeHash(QString(), QString()));
    QHash<QString, QString> saved;
    f.article.save(saved);

    const QString key = QStringLiteral("0_") + QLatin1String(PageBlocCategory::KEY_CATEGORIES);
    QVERIFY(saved.contains(key));   // 8
}

void Test_PageTypeArticle::test_pagetypearticle_load_save_roundtrip_text()
{
    ArticleFixture f;
    const QString text = QStringLiteral("first para\n\nsecond para");
    f.article.load(makeHash(QString(), text));

    QHash<QString, QString> saved;
    f.article.save(saved);

    ArticleFixture f2;
    f2.article.load(saved);
    QHash<QString, QString> saved2;
    f2.article.save(saved2);

    const QString key = QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT);
    QCOMPARE(saved2.value(key), text);   // 9
}

void Test_PageTypeArticle::test_pagetypearticle_load_ignores_unknown_prefix()
{
    ArticleFixture f;
    QHash<QString, QString> input = makeHash(QString(), QStringLiteral("text"));
    input.insert(QStringLiteral("9_future_key"), QStringLiteral("value"));
    f.article.load(input);   // must not throw  // 10

    QHash<QString, QString> saved;
    f.article.save(saved);
    QVERIFY(!saved.contains(QStringLiteral("9_future_key")));   // 11
}

void Test_PageTypeArticle::test_pagetypearticle_load_empty_hash_produces_empty_text_key()
{
    ArticleFixture f;
    f.article.load({});
    QHash<QString, QString> saved;
    f.article.save(saved);

    const QString key = QStringLiteral("1_") + QLatin1String(PageBlocText::KEY_TEXT);
    QVERIFY(saved.value(key).isEmpty());   // 12
}

// =============================================================================
// addCode — HTML page structure
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_addcode_starts_with_doctype()
{
    ArticleFixture f;
    const auto &html = htmlFrom(f, QString());
    QVERIFY(html.startsWith(QStringLiteral("<!DOCTYPE html>")));   // 13
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_contains_html_head_body_tags()
{
    ArticleFixture f;
    const auto &html = htmlFrom(f, QString());
    QVERIFY(html.contains(QStringLiteral("<html>")));   // 14
    QVERIFY(html.contains(QStringLiteral("<head>")));   // 15
    QVERIFY(html.contains(QStringLiteral("<body>")));   // 16
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_ends_with_body_html_close()
{
    ArticleFixture f;
    const auto &html = htmlFrom(f, QString());
    QVERIFY(html.endsWith(QStringLiteral("</body></html>")));   // 17
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_head_before_body()
{
    ArticleFixture f;
    const auto &html = htmlFrom(f, QString());
    QVERIFY(html.indexOf(QStringLiteral("<head>")) < html.indexOf(QStringLiteral("<body>")));   // 18
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_output_css_js_params_untouched()
{
    ArticleFixture f;
    f.article.load(makeHash(QString(), QStringLiteral("text")));
    QString html;
    QString css = QStringLiteral("pre-css");
    QString js  = QStringLiteral("pre-js");
    QSet<QString> cssDoneIds, jsDoneIds;
    f.article.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css, QStringLiteral("pre-css"));   // 19
    QCOMPARE(js,  QStringLiteral("pre-js"));    // 20
}

// =============================================================================
// addCode — CSS inlining
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_addcode_no_style_tag_when_no_css()
{
    // No categories → PageBlocCategory emits no CSS.
    ArticleFixture f;
    const auto &html = htmlFrom(f, QStringLiteral("some text"));
    QVERIFY(!html.contains(QStringLiteral("<style>")));   // 21
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_css_inlined_in_style_tag()
{
    ArticleFixture f;
    const int catId = f.categoryTable.addCategory(QStringLiteral("Tech"));
    f.article.load(makeHash(QString::number(catId), QString()));
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    f.article.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);

    QVERIFY(html.contains(QStringLiteral("<style>")));        // 22
    QVERIFY(html.contains(QStringLiteral(".categories")));    // 23
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_style_tag_in_head_before_body()
{
    ArticleFixture f;
    const int catId = f.categoryTable.addCategory(QStringLiteral("Tech"));
    f.article.load(makeHash(QString::number(catId), QString()));
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    f.article.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);

    QVERIFY(html.indexOf(QStringLiteral("<style>")) < html.indexOf(QStringLiteral("<body>")));   // 24
}

// =============================================================================
// addCode — JS inlining
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_addcode_no_script_tag_when_no_js()
{
    // Neither PageBlocText nor PageBlocCategory emits JS.
    ArticleFixture f;
    const auto &html = htmlFrom(f, QStringLiteral("text"));
    QVERIFY(!html.contains(QStringLiteral("<script>")));   // 25
}

// =============================================================================
// addCode — body content
// =============================================================================

void Test_PageTypeArticle::test_pagetypearticle_addcode_empty_text_empty_body()
{
    ArticleFixture f;
    const auto &html = htmlFrom(f, QString());
    QVERIFY(bodyContent(html).isEmpty());   // 26
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_text_content_in_body()
{
    ArticleFixture f;
    const auto &html = htmlFrom(f, QStringLiteral("hello world"));
    QVERIFY(bodyContent(html).contains(QStringLiteral("hello world")));   // 27
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_single_para_wrapped_in_p_in_body()
{
    ArticleFixture f;
    const auto &body = bodyContent(htmlFrom(f, QStringLiteral("Hello")));
    QVERIFY(body.startsWith(QStringLiteral("<p>")));   // 28
    QVERIFY(body.endsWith(QStringLiteral("</p>")));    // 29
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_two_paragraphs_produce_two_p_tags()
{
    ArticleFixture f;
    const auto &body = bodyContent(htmlFrom(f, QStringLiteral("first\n\nsecond")));
    QCOMPARE(body.count(QStringLiteral("<p>")),  2);   // 30
    QCOMPARE(body.count(QStringLiteral("</p>")), 2);   // 31
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_paragraphs_appear_in_order()
{
    ArticleFixture f;
    const auto &body = bodyContent(htmlFrom(f, QStringLiteral("alpha\n\nbeta")));
    QVERIFY(body.indexOf(QStringLiteral("alpha")) < body.indexOf(QStringLiteral("beta")));   // 32
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_video_shortcode_expanded_in_body()
{
    ArticleFixture f;
    const auto &body = bodyContent(
        htmlFrom(f, QStringLiteral("[VIDEO url=\"https://example.com/v.mp4\"][/VIDEO]")));
    QVERIFY(body.contains(QStringLiteral("<video")));      // 33
    QVERIFY(body.contains(QStringLiteral("example.com"))); // 34
}

void Test_PageTypeArticle::test_pagetypearticle_addcode_video_shortcode_tag_absent_from_output()
{
    ArticleFixture f;
    const auto &html = htmlFrom(
        f, QStringLiteral("[VIDEO url=\"https://example.com/v.mp4\"][/VIDEO]"));
    QVERIFY(!html.contains(QStringLiteral("[VIDEO")));   // 35
}

QTEST_MAIN(Test_PageTypeArticle)
#include "test_page_type_article.moc"
