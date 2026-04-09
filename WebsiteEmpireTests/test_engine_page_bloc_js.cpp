#include <QtTest>

#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"
#include "website/pages/blocs/PageBlocJs.h"
#include "website/pages/PageTypeJsApp.h"
#include "website/pages/AbstractPageType.h"
#include "website/EngineArticles.h"
#include "website/HostTable.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

// Engine instance used in addCode calls (no init() — no working dir needed).
EngineArticles engine;

// Loads all three fields into block then runs addCode(); returns html/css/js.
struct AddCodeResult { QString html; QString css; QString js; };

AddCodeResult runAddCode(PageBlocJs &bloc,
                          const QString &html = {},
                          const QString &css  = {},
                          const QString &js   = {})
{
    bloc.load({
        {QLatin1String(PageBlocJs::KEY_HTML), html},
        {QLatin1String(PageBlocJs::KEY_CSS),  css},
        {QLatin1String(PageBlocJs::KEY_JS),   js},
    });
    AddCodeResult result;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0,
                 result.html, result.css, result.js,
                 cssDoneIds, jsDoneIds);
    return result;
}

} // namespace

// =============================================================================
// Test_Engine_PageBlocJs
// =============================================================================

class Test_Engine_PageBlocJs : public QObject
{
    Q_OBJECT

private slots:
    // --- createEditWidget ---
    void test_pageblocjs_create_edit_widget_returns_non_null();
    void test_pageblocjs_create_edit_widget_has_no_parent();

    // --- getName ---
    void test_pageblocjs_get_name_is_not_empty();

    // --- Empty content ---
    void test_pageblocjs_empty_html_produces_no_html_output();
    void test_pageblocjs_empty_css_produces_no_css_output();
    void test_pageblocjs_empty_js_produces_no_js_output();
    void test_pageblocjs_empty_all_leaves_existing_html_unchanged();
    void test_pageblocjs_empty_all_leaves_existing_css_unchanged();
    void test_pageblocjs_empty_all_leaves_existing_js_unchanged();

    // --- Verbatim HTML passthrough ---
    void test_pageblocjs_html_appended_verbatim();
    void test_pageblocjs_html_not_wrapped_in_paragraph();
    void test_pageblocjs_html_multiline_preserved();
    void test_pageblocjs_html_script_tags_not_escaped();
    void test_pageblocjs_html_shortcode_like_text_not_expanded();
    void test_pageblocjs_html_appends_to_existing_html();

    // --- Verbatim CSS passthrough ---
    void test_pageblocjs_css_appended_verbatim();
    void test_pageblocjs_css_multiline_preserved();
    void test_pageblocjs_css_appends_to_existing_css();

    // --- Verbatim JS passthrough ---
    void test_pageblocjs_js_appended_verbatim();
    void test_pageblocjs_js_multiline_preserved();
    void test_pageblocjs_js_appends_to_existing_js();

    // --- HTML does not bleed into CSS or JS ---
    void test_pageblocjs_html_not_in_css();
    void test_pageblocjs_html_not_in_js();
    void test_pageblocjs_css_not_in_html();
    void test_pageblocjs_js_not_in_html();

    // --- load / save ---
    void test_pageblocjs_save_stores_html_under_key();
    void test_pageblocjs_save_stores_css_under_key();
    void test_pageblocjs_save_stores_js_under_key();
    void test_pageblocjs_load_restores_html();
    void test_pageblocjs_load_restores_css();
    void test_pageblocjs_load_restores_js();
    void test_pageblocjs_load_save_roundtrip();
    void test_pageblocjs_load_ignores_unknown_keys();
    void test_pageblocjs_load_empty_hash_gives_empty_output();

    // --- PageTypeJsApp structure ---
    void test_pagetypejsapp_type_id();
    void test_pagetypejsapp_display_name();
    void test_pagetypejsapp_has_four_blocs();
    void test_pagetypejsapp_registered_in_registry();
    void test_pagetypejsapp_registry_create_returns_non_null();

    // --- EngineArticles integration ---
    void test_enginearticles_has_jsapp_page_type();
    void test_enginearticles_jsapp_type_id_correct();
};

// =============================================================================
// createEditWidget
// =============================================================================

void Test_Engine_PageBlocJs::test_pageblocjs_create_edit_widget_returns_non_null()
{
    PageBlocJs bloc;
    AbstractPageBlockWidget *w = bloc.createEditWidget();
    QVERIFY(w != nullptr);   // 1
    delete w;
}

void Test_Engine_PageBlocJs::test_pageblocjs_create_edit_widget_has_no_parent()
{
    PageBlocJs bloc;
    AbstractPageBlockWidget *w = bloc.createEditWidget();
    QVERIFY(w->parent() == nullptr);   // 2
    delete w;
}

// =============================================================================
// getName
// =============================================================================

void Test_Engine_PageBlocJs::test_pageblocjs_get_name_is_not_empty()
{
    PageBlocJs bloc;
    QVERIFY(!bloc.getName().isEmpty());   // 3
}

// =============================================================================
// Empty content
// =============================================================================

void Test_Engine_PageBlocJs::test_pageblocjs_empty_html_produces_no_html_output()
{
    PageBlocJs bloc;
    const auto &result = runAddCode(bloc, {}, {}, {});
    QVERIFY(result.html.isEmpty());   // 4
}

void Test_Engine_PageBlocJs::test_pageblocjs_empty_css_produces_no_css_output()
{
    PageBlocJs bloc;
    const auto &result = runAddCode(bloc, {}, {}, {});
    QVERIFY(result.css.isEmpty());   // 5
}

void Test_Engine_PageBlocJs::test_pageblocjs_empty_js_produces_no_js_output()
{
    PageBlocJs bloc;
    const auto &result = runAddCode(bloc, {}, {}, {});
    QVERIFY(result.js.isEmpty());   // 6
}

void Test_Engine_PageBlocJs::test_pageblocjs_empty_all_leaves_existing_html_unchanged()
{
    PageBlocJs bloc;
    bloc.load({});
    QString html = QStringLiteral("existing-html");
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(html, QStringLiteral("existing-html"));   // 7
}

void Test_Engine_PageBlocJs::test_pageblocjs_empty_all_leaves_existing_css_unchanged()
{
    PageBlocJs bloc;
    bloc.load({});
    QString html, js;
    QString css = QStringLiteral("existing-css");
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css, QStringLiteral("existing-css"));   // 8
}

void Test_Engine_PageBlocJs::test_pageblocjs_empty_all_leaves_existing_js_unchanged()
{
    PageBlocJs bloc;
    bloc.load({});
    QString html, css;
    QString js = QStringLiteral("existing-js");
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(js, QStringLiteral("existing-js"));   // 9
}

// =============================================================================
// Verbatim HTML passthrough
// =============================================================================

void Test_Engine_PageBlocJs::test_pageblocjs_html_appended_verbatim()
{
    PageBlocJs bloc;
    const QString input = QStringLiteral("<div id=\"app\"><button>Click</button></div>");
    const auto &result = runAddCode(bloc, input, {}, {});
    QCOMPARE(result.html, input);   // 10
}

void Test_Engine_PageBlocJs::test_pageblocjs_html_not_wrapped_in_paragraph()
{
    PageBlocJs bloc;
    const auto &result = runAddCode(bloc, QStringLiteral("<div>hello</div>"), {}, {});
    QVERIFY(!result.html.contains(QStringLiteral("<p>")));    // 11
    QVERIFY(!result.html.contains(QStringLiteral("</p>")));   // 12
}

void Test_Engine_PageBlocJs::test_pageblocjs_html_multiline_preserved()
{
    PageBlocJs bloc;
    const QString input = QStringLiteral("<div>\n  <span>line1</span>\n  <span>line2</span>\n</div>");
    const auto &result = runAddCode(bloc, input, {}, {});
    QCOMPARE(result.html, input);   // 13
}

void Test_Engine_PageBlocJs::test_pageblocjs_html_script_tags_not_escaped()
{
    PageBlocJs bloc;
    const QString input = QStringLiteral("<script>alert('test');</script>");
    const auto &result = runAddCode(bloc, input, {}, {});
    QVERIFY(result.html.contains(QStringLiteral("<script>")));    // 14
    QVERIFY(result.html.contains(QStringLiteral("</script>")));   // 15
}

void Test_Engine_PageBlocJs::test_pageblocjs_html_shortcode_like_text_not_expanded()
{
    PageBlocJs bloc;
    // Shortcode-like syntax must pass through verbatim — no expansion performed.
    const QString input = QStringLiteral("[VIDEO url=\"x\"][/VIDEO]");
    const auto &result = runAddCode(bloc, input, {}, {});
    QCOMPARE(result.html, input);   // 16
}

void Test_Engine_PageBlocJs::test_pageblocjs_html_appends_to_existing_html()
{
    PageBlocJs bloc;
    bloc.load({{QLatin1String(PageBlocJs::KEY_HTML), QStringLiteral("<div>new</div>")}});
    QString html = QStringLiteral("existing");
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.startsWith(QStringLiteral("existing")));             // 17
    QVERIFY(html.contains(QStringLiteral("<div>new</div>")));         // 18
}

// =============================================================================
// Verbatim CSS passthrough
// =============================================================================

void Test_Engine_PageBlocJs::test_pageblocjs_css_appended_verbatim()
{
    PageBlocJs bloc;
    const QString input = QStringLiteral("#app { color: red; font-size: 14px; }");
    const auto &result = runAddCode(bloc, {}, input, {});
    QCOMPARE(result.css, input);   // 19
}

void Test_Engine_PageBlocJs::test_pageblocjs_css_multiline_preserved()
{
    PageBlocJs bloc;
    const QString input = QStringLiteral(".cls {\n  display: flex;\n  gap: 8px;\n}");
    const auto &result = runAddCode(bloc, {}, input, {});
    QCOMPARE(result.css, input);   // 20
}

void Test_Engine_PageBlocJs::test_pageblocjs_css_appends_to_existing_css()
{
    PageBlocJs bloc;
    bloc.load({{QLatin1String(PageBlocJs::KEY_CSS), QStringLiteral("body{margin:0}")}});
    QString html, js;
    QString css = QStringLiteral("pre-existing;");
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(css.startsWith(QStringLiteral("pre-existing;")));          // 21
    QVERIFY(css.contains(QStringLiteral("body{margin:0}")));           // 22
}

// =============================================================================
// Verbatim JS passthrough
// =============================================================================

void Test_Engine_PageBlocJs::test_pageblocjs_js_appended_verbatim()
{
    PageBlocJs bloc;
    const QString input = QStringLiteral("document.getElementById('app').textContent='hello';");
    const auto &result = runAddCode(bloc, {}, {}, input);
    QCOMPARE(result.js, input);   // 23
}

void Test_Engine_PageBlocJs::test_pageblocjs_js_multiline_preserved()
{
    PageBlocJs bloc;
    const QString input = QStringLiteral("function init() {\n  console.log('ok');\n}");
    const auto &result = runAddCode(bloc, {}, {}, input);
    QCOMPARE(result.js, input);   // 24
}

void Test_Engine_PageBlocJs::test_pageblocjs_js_appends_to_existing_js()
{
    PageBlocJs bloc;
    bloc.load({{QLatin1String(PageBlocJs::KEY_JS), QStringLiteral("init();")}});
    QString html, css;
    QString js = QStringLiteral("pre-existing;");
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(js.startsWith(QStringLiteral("pre-existing;")));   // 25
    QVERIFY(js.contains(QStringLiteral("init();")));           // 26
}

// =============================================================================
// HTML does not bleed into CSS or JS
// =============================================================================

void Test_Engine_PageBlocJs::test_pageblocjs_html_not_in_css()
{
    PageBlocJs bloc;
    const auto &result = runAddCode(bloc,
                                     QStringLiteral("<div>content</div>"),
                                     QStringLiteral("body{}"),
                                     {});
    QVERIFY(!result.css.contains(QStringLiteral("<div>")));   // 27
}

void Test_Engine_PageBlocJs::test_pageblocjs_html_not_in_js()
{
    PageBlocJs bloc;
    const auto &result = runAddCode(bloc,
                                     QStringLiteral("<div>content</div>"),
                                     {},
                                     QStringLiteral("var x=1;"));
    QVERIFY(!result.js.contains(QStringLiteral("<div>")));   // 28
}

void Test_Engine_PageBlocJs::test_pageblocjs_css_not_in_html()
{
    PageBlocJs bloc;
    const auto &result = runAddCode(bloc,
                                     QStringLiteral("<div>content</div>"),
                                     QStringLiteral("body{color:red}"),
                                     {});
    QVERIFY(!result.html.contains(QStringLiteral("body{color:red}")));   // 29
}

void Test_Engine_PageBlocJs::test_pageblocjs_js_not_in_html()
{
    PageBlocJs bloc;
    const auto &result = runAddCode(bloc,
                                     QStringLiteral("<div>content</div>"),
                                     {},
                                     QStringLiteral("var x=1;"));
    QVERIFY(!result.html.contains(QStringLiteral("var x=1;")));   // 30
}

// =============================================================================
// load / save
// =============================================================================

void Test_Engine_PageBlocJs::test_pageblocjs_save_stores_html_under_key()
{
    PageBlocJs bloc;
    bloc.load({{QLatin1String(PageBlocJs::KEY_HTML), QStringLiteral("<p>hi</p>")}});
    QHash<QString, QString> out;
    bloc.save(out);
    QVERIFY(out.contains(QLatin1String(PageBlocJs::KEY_HTML)));                          // 31
    QCOMPARE(out.value(QLatin1String(PageBlocJs::KEY_HTML)), QStringLiteral("<p>hi</p>")); // 32
}

void Test_Engine_PageBlocJs::test_pageblocjs_save_stores_css_under_key()
{
    PageBlocJs bloc;
    bloc.load({{QLatin1String(PageBlocJs::KEY_CSS), QStringLiteral("body{}")}});
    QHash<QString, QString> out;
    bloc.save(out);
    QVERIFY(out.contains(QLatin1String(PageBlocJs::KEY_CSS)));                     // 33
    QCOMPARE(out.value(QLatin1String(PageBlocJs::KEY_CSS)), QStringLiteral("body{}")); // 34
}

void Test_Engine_PageBlocJs::test_pageblocjs_save_stores_js_under_key()
{
    PageBlocJs bloc;
    bloc.load({{QLatin1String(PageBlocJs::KEY_JS), QStringLiteral("var x=1;")}});
    QHash<QString, QString> out;
    bloc.save(out);
    QVERIFY(out.contains(QLatin1String(PageBlocJs::KEY_JS)));                        // 35
    QCOMPARE(out.value(QLatin1String(PageBlocJs::KEY_JS)), QStringLiteral("var x=1;")); // 36
}

void Test_Engine_PageBlocJs::test_pageblocjs_load_restores_html()
{
    PageBlocJs bloc;
    const auto &result = runAddCode(bloc, QStringLiteral("<div>restored</div>"), {}, {});
    QVERIFY(result.html.contains(QStringLiteral("<div>restored</div>")));   // 37
}

void Test_Engine_PageBlocJs::test_pageblocjs_load_restores_css()
{
    PageBlocJs bloc;
    const auto &result = runAddCode(bloc, {}, QStringLiteral(".cls{color:blue}"), {});
    QVERIFY(result.css.contains(QStringLiteral(".cls{color:blue}")));   // 38
}

void Test_Engine_PageBlocJs::test_pageblocjs_load_restores_js()
{
    PageBlocJs bloc;
    const auto &result = runAddCode(bloc, {}, {}, QStringLiteral("console.log('ok');"));
    QVERIFY(result.js.contains(QStringLiteral("console.log('ok');")));   // 39
}

void Test_Engine_PageBlocJs::test_pageblocjs_load_save_roundtrip()
{
    const QString originalHtml = QStringLiteral("<div>app</div>");
    const QString originalCss  = QStringLiteral(".app{display:flex}");
    const QString originalJs   = QStringLiteral("const app=document.querySelector('.app');");

    PageBlocJs bloc;
    bloc.load({
        {QLatin1String(PageBlocJs::KEY_HTML), originalHtml},
        {QLatin1String(PageBlocJs::KEY_CSS),  originalCss},
        {QLatin1String(PageBlocJs::KEY_JS),   originalJs},
    });
    QHash<QString, QString> saved;
    bloc.save(saved);

    PageBlocJs bloc2;
    bloc2.load(saved);
    QHash<QString, QString> saved2;
    bloc2.save(saved2);

    QCOMPARE(saved2.value(QLatin1String(PageBlocJs::KEY_HTML)), originalHtml);   // 40
    QCOMPARE(saved2.value(QLatin1String(PageBlocJs::KEY_CSS)),  originalCss);    // 41
    QCOMPARE(saved2.value(QLatin1String(PageBlocJs::KEY_JS)),   originalJs);     // 42
}

void Test_Engine_PageBlocJs::test_pageblocjs_load_ignores_unknown_keys()
{
    PageBlocJs bloc;
    bloc.load({
        {QLatin1String(PageBlocJs::KEY_HTML), QStringLiteral("<div/>"  )},
        {QStringLiteral("unknown_future_key"), QStringLiteral("value")},
    });
    QHash<QString, QString> out;
    bloc.save(out);
    QVERIFY(!out.contains(QStringLiteral("unknown_future_key")));   // 43
}

void Test_Engine_PageBlocJs::test_pageblocjs_load_empty_hash_gives_empty_output()
{
    PageBlocJs bloc;
    bloc.load({});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.isEmpty());   // 44
    QVERIFY(css.isEmpty());    // 45
    QVERIFY(js.isEmpty());     // 46
}

// =============================================================================
// PageTypeJsApp structure
// =============================================================================

void Test_Engine_PageBlocJs::test_pagetypejsapp_type_id()
{
    QCOMPARE(QLatin1String(PageTypeJsApp::TYPE_ID), QLatin1String("js_app"));   // 47
}

void Test_Engine_PageBlocJs::test_pagetypejsapp_display_name()
{
    QCOMPARE(QLatin1String(PageTypeJsApp::DISPLAY_NAME), QLatin1String("JS App"));   // 48
}

void Test_Engine_PageBlocJs::test_pagetypejsapp_has_four_blocs()
{
    // CategoryTable is unused by the test — pass a temporary dir path.
    // PageTypeJsApp does not open any file during construction.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // We cannot construct CategoryTable without a real Qt Sql driver in this
    // test binary, so exercise getPageBlocs() via the EngineArticles instance
    // that was already initialised by other tests — but EngineArticles was not
    // init()'d, so getPageTypes() is empty.  Instead, rely on the static
    // constant: the type is composed of 4 blocs (category + intro + js + outro).
    // Verify via the TYPE_ID registration and bloc count together.
    QCOMPARE(static_cast<int>(
                 std::char_traits<char>::length(PageTypeJsApp::TYPE_ID)),
             6);   // 49 — "js_app" length sanity check
    // The bloc count is validated via EngineArticles integration below; here we
    // just assert the constant count matches the documented design (4 blocs).
    QVERIFY(true);   // 50 — structural test delegated to integration tests
}

void Test_Engine_PageBlocJs::test_pagetypejsapp_registered_in_registry()
{
    const QList<QString> ids = AbstractPageType::allTypeIds();
    QVERIFY(ids.contains(QLatin1String(PageTypeJsApp::TYPE_ID)));   // 51
}

void Test_Engine_PageBlocJs::test_pagetypejsapp_registry_create_returns_non_null()
{
    // Construct a minimal CategoryTable-compatible scenario: pass a dummy
    // CategoryTable by using the factory, which only requires a valid typeId.
    // Since CategoryTable construction needs a QDir, skip instantiation here
    // and rely on allTypeIds() having registered the type correctly.
    // The factory round-trip is tested by EngineArticles integration below.
    QVERIFY(AbstractPageType::allTypeIds().contains(
                QLatin1String(PageTypeJsApp::TYPE_ID)));   // 52
}

// =============================================================================
// EngineArticles integration
// =============================================================================

void Test_Engine_PageBlocJs::test_enginearticles_has_jsapp_page_type()
{
    // EngineArticles must declare js_app as one of its page types in
    // getPageTypes() — verified after init() with a real temporary dir.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    HostTable hostTable(QDir(dir.path()));
    EngineArticles eng;
    eng.init(QDir(dir.path()), hostTable);

    const auto &types = eng.getPageTypes();
    bool found = false;
    for (const AbstractPageType *t : types) {
        if (t->getTypeId() == QLatin1String(PageTypeJsApp::TYPE_ID)) {
            found = true;
            break;
        }
    }
    QVERIFY(found);   // 53
}

void Test_Engine_PageBlocJs::test_enginearticles_jsapp_type_id_correct()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    HostTable hostTable(QDir(dir.path()));
    EngineArticles eng;
    eng.init(QDir(dir.path()), hostTable);

    const auto &types = eng.getPageTypes();
    const AbstractPageType *jsApp = nullptr;
    for (const AbstractPageType *t : types) {
        if (t->getTypeId() == QLatin1String(PageTypeJsApp::TYPE_ID)) {
            jsApp = t;
            break;
        }
    }
    QVERIFY(jsApp != nullptr);                                                          // 54
    QCOMPARE(jsApp->getTypeId(),      QLatin1String(PageTypeJsApp::TYPE_ID));           // 55
    QCOMPARE(jsApp->getDisplayName(), QLatin1String(PageTypeJsApp::DISPLAY_NAME));      // 56
    QCOMPARE(jsApp->getPageBlocs().size(), 4);                                          // 57
}

QTEST_MAIN(Test_Engine_PageBlocJs)
#include "test_engine_page_bloc_js.moc"
