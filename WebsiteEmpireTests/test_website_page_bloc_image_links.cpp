#include <QtTest>

#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"
#include "website/pages/blocs/PageBlocImageLinks.h"
#include "website/EngineArticles.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

// Engine instance used in addCode calls.
EngineArticles engine;

// Builds a single-item hash with the given fields.
QHash<QString, QString> oneItemHash(const QString &imgUrl,
                                    const QString &linkType,
                                    const QString &target,
                                    const QString &alt)
{
    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocImageLinks::KEY_ITEM_COUNT), QStringLiteral("1"));
    h.insert(QStringLiteral("item_0_img"),    imgUrl);
    h.insert(QStringLiteral("item_0_type"),   linkType);
    h.insert(QStringLiteral("item_0_target"), target);
    h.insert(QStringLiteral("item_0_alt"),    alt);
    return h;
}

// Runs addCode on the bloc and returns the html output.
QString htmlFrom(PageBlocImageLinks &bloc, const QHash<QString, QString> &values = {})
{
    bloc.load(values);
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    return html;
}

// Runs addCode and returns the css output.
QString cssFrom(PageBlocImageLinks &bloc, const QHash<QString, QString> &values = {})
{
    bloc.load(values);
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    return css;
}

// Runs addCode and returns the js output.
QString jsFrom(PageBlocImageLinks &bloc, const QHash<QString, QString> &values = {})
{
    bloc.load(values);
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    return js;
}

} // namespace

// =============================================================================
// Test_Website_PageBlocImageLinks
// =============================================================================

class Test_Website_PageBlocImageLinks : public QObject
{
    Q_OBJECT

private slots:
    // --- createEditWidget ---
    void test_imagelinks_create_edit_widget_returns_non_null();
    void test_imagelinks_create_edit_widget_has_no_parent();

    // --- getName ---
    void test_imagelinks_get_name_is_not_empty();

    // --- Empty bloc (no items) ---
    void test_imagelinks_empty_bloc_produces_no_html();
    void test_imagelinks_empty_bloc_produces_no_css();
    void test_imagelinks_empty_bloc_produces_no_js();
    void test_imagelinks_empty_bloc_leaves_existing_html_unchanged();

    // --- Items with empty imageUrl ---
    void test_imagelinks_item_with_empty_image_url_produces_no_html();
    void test_imagelinks_item_with_empty_image_url_produces_no_css();
    void test_imagelinks_item_with_empty_image_url_produces_no_js();
    void test_imagelinks_all_items_empty_image_url_no_section_tag();
    void test_imagelinks_empty_image_url_items_preserved_in_save();

    // --- Single valid item: HTML structure ---
    void test_imagelinks_single_item_produces_section_tag();
    void test_imagelinks_single_item_produces_a_tag();
    void test_imagelinks_single_item_produces_img_tag();
    void test_imagelinks_single_item_image_url_in_output();
    void test_imagelinks_single_item_alt_text_in_output();
    void test_imagelinks_single_item_lazy_loading();
    void test_imagelinks_single_item_link_id_zero();

    // --- Multiple items ---
    void test_imagelinks_two_items_both_appear_in_html();
    void test_imagelinks_two_items_link_ids_sequential();
    void test_imagelinks_mixed_valid_and_empty_items_only_valid_appear();
    void test_imagelinks_mixed_valid_and_empty_items_valid_link_ids_sequential();

    // --- Link type resolution ---
    void test_imagelinks_category_link_type_resolves_to_category_path();
    void test_imagelinks_page_link_type_resolves_to_root_path();
    void test_imagelinks_url_link_type_uses_target_as_is();

    // --- Grid settings in HTML ---
    void test_imagelinks_desktop_cols_in_style();
    void test_imagelinks_tablet_cols_in_style();
    void test_imagelinks_mobile_cols_in_style();
    void test_imagelinks_custom_cols_appear_in_style();

    // --- CSS ---
    void test_imagelinks_css_emitted_on_first_call();
    void test_imagelinks_css_emitted_only_once();
    void test_imagelinks_css_contains_image_links_grid_class();
    void test_imagelinks_css_contains_media_queries();
    void test_imagelinks_css_contains_cols_d_variable();

    // --- JS ---
    void test_imagelinks_js_emitted_on_first_call();
    void test_imagelinks_js_emitted_only_once();
    void test_imagelinks_js_contains_intersection_observer();
    void test_imagelinks_js_contains_send_beacon();
    void test_imagelinks_js_contains_display_event_type();
    void test_imagelinks_js_contains_click_event_type();

    // --- append behaviour ---
    void test_imagelinks_appends_to_existing_html();
    void test_imagelinks_appends_css_to_existing_css();

    // --- Default grid settings ---
    void test_imagelinks_default_cols_desktop();
    void test_imagelinks_default_rows_desktop();
    void test_imagelinks_default_cols_tablet();
    void test_imagelinks_default_rows_tablet();
    void test_imagelinks_default_cols_mobile();
    void test_imagelinks_default_rows_mobile();

    // --- load / save ---
    void test_imagelinks_load_save_roundtrip_grid_settings();
    void test_imagelinks_load_save_roundtrip_items();
    void test_imagelinks_load_save_roundtrip_item_count();
    void test_imagelinks_load_ignores_unknown_keys();
    void test_imagelinks_load_empty_hash_gives_no_output();
    void test_imagelinks_save_writes_item_count();
    void test_imagelinks_save_writes_all_item_fields();

    // --- Accessors ---
    void test_imagelinks_items_accessor_matches_loaded_data();
    void test_imagelinks_grid_accessors_match_loaded_values();
};

// =============================================================================
// createEditWidget
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_create_edit_widget_returns_non_null()
{
    PageBlocImageLinks bloc;
    AbstractPageBlockWidget *w = bloc.createEditWidget();
    QVERIFY(w != nullptr);   // 1
    delete w;
}

void Test_Website_PageBlocImageLinks::test_imagelinks_create_edit_widget_has_no_parent()
{
    PageBlocImageLinks bloc;
    AbstractPageBlockWidget *w = bloc.createEditWidget();
    QVERIFY(w->parent() == nullptr);   // 2
    delete w;
}

// =============================================================================
// getName
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_get_name_is_not_empty()
{
    PageBlocImageLinks bloc;
    QVERIFY(!bloc.getName().isEmpty());   // 3
}

// =============================================================================
// Empty bloc (no items)
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_empty_bloc_produces_no_html()
{
    PageBlocImageLinks bloc;
    QVERIFY(htmlFrom(bloc).isEmpty());   // 4
}

void Test_Website_PageBlocImageLinks::test_imagelinks_empty_bloc_produces_no_css()
{
    PageBlocImageLinks bloc;
    QVERIFY(cssFrom(bloc).isEmpty());   // 5
}

void Test_Website_PageBlocImageLinks::test_imagelinks_empty_bloc_produces_no_js()
{
    PageBlocImageLinks bloc;
    QVERIFY(jsFrom(bloc).isEmpty());   // 6
}

void Test_Website_PageBlocImageLinks::test_imagelinks_empty_bloc_leaves_existing_html_unchanged()
{
    PageBlocImageLinks bloc;
    bloc.load({});
    QString html = QStringLiteral("pre-existing");
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(html, QStringLiteral("pre-existing"));   // 7
}

// =============================================================================
// Items with empty imageUrl
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_item_with_empty_image_url_produces_no_html()
{
    PageBlocImageLinks bloc;
    QVERIFY(htmlFrom(bloc, oneItemHash(QStringLiteral(""), QStringLiteral("url"),
                                      QStringLiteral("https://x.com"), QStringLiteral("X"))).isEmpty());   // 8
}

void Test_Website_PageBlocImageLinks::test_imagelinks_item_with_empty_image_url_produces_no_css()
{
    PageBlocImageLinks bloc;
    QVERIFY(cssFrom(bloc, oneItemHash(QStringLiteral(""), QStringLiteral("url"),
                                     QStringLiteral("https://x.com"), QStringLiteral("X"))).isEmpty());   // 9
}

void Test_Website_PageBlocImageLinks::test_imagelinks_item_with_empty_image_url_produces_no_js()
{
    PageBlocImageLinks bloc;
    QVERIFY(jsFrom(bloc, oneItemHash(QStringLiteral(""), QStringLiteral("url"),
                                    QStringLiteral("https://x.com"), QStringLiteral("X"))).isEmpty());   // 10
}

void Test_Website_PageBlocImageLinks::test_imagelinks_all_items_empty_image_url_no_section_tag()
{
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, oneItemHash(QStringLiteral(""), QStringLiteral("url"),
                                                   QStringLiteral("https://x.com"), QStringLiteral("X")));
    QVERIFY(!html.contains(QStringLiteral("<section")));   // 11
}

void Test_Website_PageBlocImageLinks::test_imagelinks_empty_image_url_items_preserved_in_save()
{
    // Items with empty imageUrl must survive a load/save roundtrip.
    PageBlocImageLinks bloc;
    const auto &h = oneItemHash(QStringLiteral(""), QStringLiteral("url"),
                                QStringLiteral("https://x.com"), QStringLiteral("Alt"));
    bloc.load(h);
    QHash<QString, QString> saved;
    bloc.save(saved);
    QCOMPARE(saved.value(QLatin1String(PageBlocImageLinks::KEY_ITEM_COUNT)), QStringLiteral("1"));   // 12
}

// =============================================================================
// Single valid item: HTML structure
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_single_item_produces_section_tag()
{
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                                   QStringLiteral("https://example.com"),
                                                   QStringLiteral("Example")));
    QVERIFY(html.contains(QStringLiteral("<section")));   // 13
    QVERIFY(html.contains(QStringLiteral("</section>")));   // 14
}

void Test_Website_PageBlocImageLinks::test_imagelinks_single_item_produces_a_tag()
{
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                                   QStringLiteral("https://example.com"),
                                                   QStringLiteral("Example")));
    QVERIFY(html.contains(QStringLiteral("<a ")));   // 15
    QVERIFY(html.contains(QStringLiteral("</a>")));   // 16
}

void Test_Website_PageBlocImageLinks::test_imagelinks_single_item_produces_img_tag()
{
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                                   QStringLiteral("https://example.com"),
                                                   QStringLiteral("Example")));
    QVERIFY(html.contains(QStringLiteral("<img ")));   // 17
}

void Test_Website_PageBlocImageLinks::test_imagelinks_single_item_image_url_in_output()
{
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, oneItemHash(QStringLiteral("https://example.com/photo.jpg"),
                                                   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                                   QStringLiteral("https://example.com"),
                                                   QStringLiteral("Photo")));
    QVERIFY(html.contains(QStringLiteral("https://example.com/photo.jpg")));   // 18
}

void Test_Website_PageBlocImageLinks::test_imagelinks_single_item_alt_text_in_output()
{
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                                   QStringLiteral("https://example.com"),
                                                   QStringLiteral("My Alt Text")));
    QVERIFY(html.contains(QStringLiteral("My Alt Text")));   // 19
}

void Test_Website_PageBlocImageLinks::test_imagelinks_single_item_lazy_loading()
{
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                                   QStringLiteral("https://example.com"),
                                                   QStringLiteral("Alt")));
    QVERIFY(html.contains(QStringLiteral("loading=\"lazy\"")));   // 20
}

void Test_Website_PageBlocImageLinks::test_imagelinks_single_item_link_id_zero()
{
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                                   QStringLiteral("https://example.com"),
                                                   QStringLiteral("Alt")));
    QVERIFY(html.contains(QStringLiteral("data-link-id=\"0\"")));   // 21
}

// =============================================================================
// Multiple items
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_two_items_both_appear_in_html()
{
    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocImageLinks::KEY_ITEM_COUNT), QStringLiteral("2"));
    h.insert(QStringLiteral("item_0_img"),    QStringLiteral("https://example.com/a.jpg"));
    h.insert(QStringLiteral("item_0_type"),   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL));
    h.insert(QStringLiteral("item_0_target"), QStringLiteral("https://a.com"));
    h.insert(QStringLiteral("item_0_alt"),    QStringLiteral("A"));
    h.insert(QStringLiteral("item_1_img"),    QStringLiteral("https://example.com/b.jpg"));
    h.insert(QStringLiteral("item_1_type"),   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL));
    h.insert(QStringLiteral("item_1_target"), QStringLiteral("https://b.com"));
    h.insert(QStringLiteral("item_1_alt"),    QStringLiteral("B"));

    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, h);
    QVERIFY(html.contains(QStringLiteral("https://example.com/a.jpg")));   // 22
    QVERIFY(html.contains(QStringLiteral("https://example.com/b.jpg")));   // 23
}

void Test_Website_PageBlocImageLinks::test_imagelinks_two_items_link_ids_sequential()
{
    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocImageLinks::KEY_ITEM_COUNT), QStringLiteral("2"));
    h.insert(QStringLiteral("item_0_img"),    QStringLiteral("https://example.com/a.jpg"));
    h.insert(QStringLiteral("item_0_type"),   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL));
    h.insert(QStringLiteral("item_0_target"), QStringLiteral("https://a.com"));
    h.insert(QStringLiteral("item_0_alt"),    QStringLiteral("A"));
    h.insert(QStringLiteral("item_1_img"),    QStringLiteral("https://example.com/b.jpg"));
    h.insert(QStringLiteral("item_1_type"),   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL));
    h.insert(QStringLiteral("item_1_target"), QStringLiteral("https://b.com"));
    h.insert(QStringLiteral("item_1_alt"),    QStringLiteral("B"));

    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, h);
    QVERIFY(html.contains(QStringLiteral("data-link-id=\"0\"")));   // 24
    QVERIFY(html.contains(QStringLiteral("data-link-id=\"1\"")));   // 25
}

void Test_Website_PageBlocImageLinks::test_imagelinks_mixed_valid_and_empty_items_only_valid_appear()
{
    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocImageLinks::KEY_ITEM_COUNT), QStringLiteral("2"));
    h.insert(QStringLiteral("item_0_img"),    QStringLiteral(""));          // empty → skip
    h.insert(QStringLiteral("item_0_type"),   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL));
    h.insert(QStringLiteral("item_0_target"), QStringLiteral("https://a.com"));
    h.insert(QStringLiteral("item_0_alt"),    QStringLiteral("A"));
    h.insert(QStringLiteral("item_1_img"),    QStringLiteral("https://example.com/b.jpg"));
    h.insert(QStringLiteral("item_1_type"),   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL));
    h.insert(QStringLiteral("item_1_target"), QStringLiteral("https://b.com"));
    h.insert(QStringLiteral("item_1_alt"),    QStringLiteral("B"));

    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, h);
    QVERIFY(html.count(QStringLiteral("<a ")) == 1);   // 26
    QVERIFY(html.contains(QStringLiteral("https://example.com/b.jpg")));   // 27
}

void Test_Website_PageBlocImageLinks::test_imagelinks_mixed_valid_and_empty_items_valid_link_ids_sequential()
{
    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocImageLinks::KEY_ITEM_COUNT), QStringLiteral("3"));
    h.insert(QStringLiteral("item_0_img"),    QStringLiteral("https://example.com/a.jpg"));
    h.insert(QStringLiteral("item_0_type"),   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL));
    h.insert(QStringLiteral("item_0_target"), QStringLiteral("https://a.com"));
    h.insert(QStringLiteral("item_0_alt"),    QStringLiteral("A"));
    h.insert(QStringLiteral("item_1_img"),    QStringLiteral(""));          // empty → skip
    h.insert(QStringLiteral("item_1_type"),   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL));
    h.insert(QStringLiteral("item_1_target"), QStringLiteral("https://b.com"));
    h.insert(QStringLiteral("item_1_alt"),    QStringLiteral("B"));
    h.insert(QStringLiteral("item_2_img"),    QStringLiteral("https://example.com/c.jpg"));
    h.insert(QStringLiteral("item_2_type"),   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL));
    h.insert(QStringLiteral("item_2_target"), QStringLiteral("https://c.com"));
    h.insert(QStringLiteral("item_2_alt"),    QStringLiteral("C"));

    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, h);
    QVERIFY(html.contains(QStringLiteral("data-link-id=\"0\"")));   // 28
    QVERIFY(html.contains(QStringLiteral("data-link-id=\"1\"")));   // 29
    QVERIFY(!html.contains(QStringLiteral("data-link-id=\"2\"")));  // 30 (only 2 visible)
}

// =============================================================================
// Link type resolution
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_category_link_type_resolves_to_category_path()
{
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                   QLatin1String(PageBlocImageLinks::LINK_TYPE_CATEGORY),
                                                   QStringLiteral("food"),
                                                   QStringLiteral("Food")));
    QVERIFY(html.contains(QStringLiteral("href=\"/category/food\"")));   // 31
}

void Test_Website_PageBlocImageLinks::test_imagelinks_page_link_type_resolves_to_root_path()
{
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                   QLatin1String(PageBlocImageLinks::LINK_TYPE_PAGE),
                                                   QStringLiteral("about-us"),
                                                   QStringLiteral("About")));
    QVERIFY(html.contains(QStringLiteral("href=\"/about-us\"")));   // 32
}

void Test_Website_PageBlocImageLinks::test_imagelinks_url_link_type_uses_target_as_is()
{
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                                   QStringLiteral("https://external.com/page"),
                                                   QStringLiteral("External")));
    QVERIFY(html.contains(QStringLiteral("href=\"https://external.com/page\"")));   // 33
}

// =============================================================================
// Grid settings in HTML
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_desktop_cols_in_style()
{
    QHash<QString, QString> h = oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                            QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                            QStringLiteral("https://example.com"),
                                            QStringLiteral("Alt"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_DESKTOP), QStringLiteral("4"));
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, h);
    QVERIFY(html.contains(QStringLiteral("--cols-d:4")));   // 34
}

void Test_Website_PageBlocImageLinks::test_imagelinks_tablet_cols_in_style()
{
    QHash<QString, QString> h = oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                            QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                            QStringLiteral("https://example.com"),
                                            QStringLiteral("Alt"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_TABLET), QStringLiteral("3"));
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, h);
    QVERIFY(html.contains(QStringLiteral("--cols-t:3")));   // 35
}

void Test_Website_PageBlocImageLinks::test_imagelinks_mobile_cols_in_style()
{
    QHash<QString, QString> h = oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                            QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                            QStringLiteral("https://example.com"),
                                            QStringLiteral("Alt"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_MOBILE), QStringLiteral("2"));
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, h);
    QVERIFY(html.contains(QStringLiteral("--cols-m:2")));   // 36
}

void Test_Website_PageBlocImageLinks::test_imagelinks_custom_cols_appear_in_style()
{
    QHash<QString, QString> h = oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                            QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                            QStringLiteral("https://example.com"),
                                            QStringLiteral("Alt"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_DESKTOP), QStringLiteral("6"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_TABLET),  QStringLiteral("3"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_MOBILE),  QStringLiteral("2"));
    PageBlocImageLinks bloc;
    const auto &html = htmlFrom(bloc, h);
    QVERIFY(html.contains(QStringLiteral("--cols-d:6")));   // 37
    QVERIFY(html.contains(QStringLiteral("--cols-t:3")));   // 38
    QVERIFY(html.contains(QStringLiteral("--cols-m:2")));   // 39
}

// =============================================================================
// CSS
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_css_emitted_on_first_call()
{
    PageBlocImageLinks bloc;
    const auto &css = cssFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                                QStringLiteral("https://example.com"),
                                                QStringLiteral("Alt")));
    QVERIFY(!css.isEmpty());   // 40
}

void Test_Website_PageBlocImageLinks::test_imagelinks_css_emitted_only_once()
{
    PageBlocImageLinks bloc;
    bloc.load(oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                          QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                          QStringLiteral("https://example.com"),
                          QStringLiteral("Alt")));
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    const int cssLen = css.length();

    // Second call with same cssDoneIds — CSS must NOT be appended again.
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css.length(), cssLen);   // 41
}

void Test_Website_PageBlocImageLinks::test_imagelinks_css_contains_image_links_grid_class()
{
    PageBlocImageLinks bloc;
    const auto &css = cssFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                                QStringLiteral("https://example.com"),
                                                QStringLiteral("Alt")));
    QVERIFY(css.contains(QStringLiteral(".image-links-grid")));   // 42
}

void Test_Website_PageBlocImageLinks::test_imagelinks_css_contains_media_queries()
{
    PageBlocImageLinks bloc;
    const auto &css = cssFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                                QStringLiteral("https://example.com"),
                                                QStringLiteral("Alt")));
    QVERIFY(css.contains(QStringLiteral("@media")));   // 43
}

void Test_Website_PageBlocImageLinks::test_imagelinks_css_contains_cols_d_variable()
{
    PageBlocImageLinks bloc;
    const auto &css = cssFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                                QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                                QStringLiteral("https://example.com"),
                                                QStringLiteral("Alt")));
    QVERIFY(css.contains(QStringLiteral("--cols-d")));   // 44
}

// =============================================================================
// JS
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_js_emitted_on_first_call()
{
    PageBlocImageLinks bloc;
    const auto &js = jsFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                              QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                              QStringLiteral("https://example.com"),
                                              QStringLiteral("Alt")));
    QVERIFY(!js.isEmpty());   // 45
}

void Test_Website_PageBlocImageLinks::test_imagelinks_js_emitted_only_once()
{
    PageBlocImageLinks bloc;
    bloc.load(oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                          QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                          QStringLiteral("https://example.com"),
                          QStringLiteral("Alt")));
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    const int jsLen = js.length();

    // Second call with same jsDoneIds — JS must NOT be appended again.
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(js.length(), jsLen);   // 46
}

void Test_Website_PageBlocImageLinks::test_imagelinks_js_contains_intersection_observer()
{
    PageBlocImageLinks bloc;
    const auto &js = jsFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                              QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                              QStringLiteral("https://example.com"),
                                              QStringLiteral("Alt")));
    QVERIFY(js.contains(QStringLiteral("IntersectionObserver")));   // 47
}

void Test_Website_PageBlocImageLinks::test_imagelinks_js_contains_send_beacon()
{
    PageBlocImageLinks bloc;
    const auto &js = jsFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                              QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                              QStringLiteral("https://example.com"),
                                              QStringLiteral("Alt")));
    QVERIFY(js.contains(QStringLiteral("sendBeacon")));   // 48
}

void Test_Website_PageBlocImageLinks::test_imagelinks_js_contains_display_event_type()
{
    PageBlocImageLinks bloc;
    const auto &js = jsFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                              QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                              QStringLiteral("https://example.com"),
                                              QStringLiteral("Alt")));
    QVERIFY(js.contains(QStringLiteral("il-display")));   // 49
}

void Test_Website_PageBlocImageLinks::test_imagelinks_js_contains_click_event_type()
{
    PageBlocImageLinks bloc;
    const auto &js = jsFrom(bloc, oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                              QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                              QStringLiteral("https://example.com"),
                                              QStringLiteral("Alt")));
    QVERIFY(js.contains(QStringLiteral("il-click")));   // 50
}

// =============================================================================
// Append behaviour
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_appends_to_existing_html()
{
    PageBlocImageLinks bloc;
    bloc.load(oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                          QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                          QStringLiteral("https://example.com"),
                          QStringLiteral("Alt")));
    QString html = QStringLiteral("existing");
    QString css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.startsWith(QStringLiteral("existing")));   // 51
    QVERIFY(html.contains(QStringLiteral("<section")));     // 52
}

void Test_Website_PageBlocImageLinks::test_imagelinks_appends_css_to_existing_css()
{
    PageBlocImageLinks bloc;
    bloc.load(oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                          QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                          QStringLiteral("https://example.com"),
                          QStringLiteral("Alt")));
    QString html;
    QString css = QStringLiteral("pre-css");
    QString js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(css.startsWith(QStringLiteral("pre-css")));   // 53
    QVERIFY(css.contains(QStringLiteral(".image-links-grid")));   // 54
}

// =============================================================================
// Default grid settings
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_default_cols_desktop()
{
    PageBlocImageLinks bloc;
    QCOMPARE(bloc.colsDesktop(), 4);   // 55
}

void Test_Website_PageBlocImageLinks::test_imagelinks_default_rows_desktop()
{
    PageBlocImageLinks bloc;
    QCOMPARE(bloc.rowsDesktop(), 2);   // 56
}

void Test_Website_PageBlocImageLinks::test_imagelinks_default_cols_tablet()
{
    PageBlocImageLinks bloc;
    QCOMPARE(bloc.colsTablet(), 2);   // 57
}

void Test_Website_PageBlocImageLinks::test_imagelinks_default_rows_tablet()
{
    PageBlocImageLinks bloc;
    QCOMPARE(bloc.rowsTablet(), 3);   // 58
}

void Test_Website_PageBlocImageLinks::test_imagelinks_default_cols_mobile()
{
    PageBlocImageLinks bloc;
    QCOMPARE(bloc.colsMobile(), 1);   // 59
}

void Test_Website_PageBlocImageLinks::test_imagelinks_default_rows_mobile()
{
    PageBlocImageLinks bloc;
    QCOMPARE(bloc.rowsMobile(), 4);   // 60
}

// =============================================================================
// load / save
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_load_save_roundtrip_grid_settings()
{
    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_DESKTOP), QStringLiteral("6"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_ROWS_DESKTOP), QStringLiteral("3"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_TABLET),  QStringLiteral("4"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_ROWS_TABLET),  QStringLiteral("5"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_MOBILE),  QStringLiteral("2"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_ROWS_MOBILE),  QStringLiteral("6"));

    PageBlocImageLinks bloc;
    bloc.load(h);
    QHash<QString, QString> saved;
    bloc.save(saved);

    QCOMPARE(saved.value(QLatin1String(PageBlocImageLinks::KEY_COLS_DESKTOP)), QStringLiteral("6"));   // 61
    QCOMPARE(saved.value(QLatin1String(PageBlocImageLinks::KEY_ROWS_DESKTOP)), QStringLiteral("3"));   // 62
    QCOMPARE(saved.value(QLatin1String(PageBlocImageLinks::KEY_COLS_TABLET)),  QStringLiteral("4"));   // 63
    QCOMPARE(saved.value(QLatin1String(PageBlocImageLinks::KEY_ROWS_TABLET)),  QStringLiteral("5"));   // 64
    QCOMPARE(saved.value(QLatin1String(PageBlocImageLinks::KEY_COLS_MOBILE)),  QStringLiteral("2"));   // 65
    QCOMPARE(saved.value(QLatin1String(PageBlocImageLinks::KEY_ROWS_MOBILE)),  QStringLiteral("6"));   // 66
}

void Test_Website_PageBlocImageLinks::test_imagelinks_load_save_roundtrip_items()
{
    const auto &h = oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                QLatin1String(PageBlocImageLinks::LINK_TYPE_CATEGORY),
                                QStringLiteral("food"),
                                QStringLiteral("Food category"));
    PageBlocImageLinks bloc;
    bloc.load(h);
    QHash<QString, QString> saved;
    bloc.save(saved);

    PageBlocImageLinks bloc2;
    bloc2.load(saved);
    QHash<QString, QString> saved2;
    bloc2.save(saved2);

    QCOMPARE(saved2.value(QStringLiteral("item_0_img")),    QStringLiteral("https://example.com/img.jpg"));   // 67
    QCOMPARE(saved2.value(QStringLiteral("item_0_type")),   QLatin1String(PageBlocImageLinks::LINK_TYPE_CATEGORY));   // 68
    QCOMPARE(saved2.value(QStringLiteral("item_0_target")), QStringLiteral("food"));   // 69
    QCOMPARE(saved2.value(QStringLiteral("item_0_alt")),    QStringLiteral("Food category"));   // 70
}

void Test_Website_PageBlocImageLinks::test_imagelinks_load_save_roundtrip_item_count()
{
    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocImageLinks::KEY_ITEM_COUNT), QStringLiteral("3"));
    for (int i = 0; i < 3; ++i) {
        const auto &p = QStringLiteral("item_") + QString::number(i) + QStringLiteral("_");
        h.insert(p + QStringLiteral("img"),    QStringLiteral("https://example.com/img.jpg"));
        h.insert(p + QStringLiteral("type"),   QLatin1String(PageBlocImageLinks::LINK_TYPE_URL));
        h.insert(p + QStringLiteral("target"), QStringLiteral("https://example.com"));
        h.insert(p + QStringLiteral("alt"),    QStringLiteral("Alt"));
    }

    PageBlocImageLinks bloc;
    bloc.load(h);
    QHash<QString, QString> saved;
    bloc.save(saved);
    QCOMPARE(saved.value(QLatin1String(PageBlocImageLinks::KEY_ITEM_COUNT)), QStringLiteral("3"));   // 71
}

void Test_Website_PageBlocImageLinks::test_imagelinks_load_ignores_unknown_keys()
{
    const auto &h = oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                QStringLiteral("https://example.com"),
                                QStringLiteral("Alt"));
    QHash<QString, QString> h2 = h;
    h2.insert(QStringLiteral("unknown_future_key"), QStringLiteral("value"));

    PageBlocImageLinks bloc;
    bloc.load(h2);   // must not throw
    QHash<QString, QString> saved;
    bloc.save(saved);
    QVERIFY(!saved.contains(QStringLiteral("unknown_future_key")));   // 72
}

void Test_Website_PageBlocImageLinks::test_imagelinks_load_empty_hash_gives_no_output()
{
    PageBlocImageLinks bloc;
    bloc.load({});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.isEmpty());   // 73
    QVERIFY(css.isEmpty());    // 74
    QVERIFY(js.isEmpty());     // 75
}

void Test_Website_PageBlocImageLinks::test_imagelinks_save_writes_item_count()
{
    const auto &h = oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                QLatin1String(PageBlocImageLinks::LINK_TYPE_URL),
                                QStringLiteral("https://example.com"),
                                QStringLiteral("Alt"));
    PageBlocImageLinks bloc;
    bloc.load(h);
    QHash<QString, QString> saved;
    bloc.save(saved);
    QVERIFY(saved.contains(QLatin1String(PageBlocImageLinks::KEY_ITEM_COUNT)));   // 76
    QCOMPARE(saved.value(QLatin1String(PageBlocImageLinks::KEY_ITEM_COUNT)), QStringLiteral("1"));   // 77
}

void Test_Website_PageBlocImageLinks::test_imagelinks_save_writes_all_item_fields()
{
    const auto &h = oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                QLatin1String(PageBlocImageLinks::LINK_TYPE_PAGE),
                                QStringLiteral("contact"),
                                QStringLiteral("Contact us"));
    PageBlocImageLinks bloc;
    bloc.load(h);
    QHash<QString, QString> saved;
    bloc.save(saved);
    QVERIFY(saved.contains(QStringLiteral("item_0_img")));    // 78
    QVERIFY(saved.contains(QStringLiteral("item_0_type")));   // 79
    QVERIFY(saved.contains(QStringLiteral("item_0_target"))); // 80
    QVERIFY(saved.contains(QStringLiteral("item_0_alt")));    // 81
    QCOMPARE(saved.value(QStringLiteral("item_0_img")),    QStringLiteral("https://example.com/img.jpg"));   // 82
    QCOMPARE(saved.value(QStringLiteral("item_0_type")),   QLatin1String(PageBlocImageLinks::LINK_TYPE_PAGE));   // 83
    QCOMPARE(saved.value(QStringLiteral("item_0_target")), QStringLiteral("contact"));   // 84
    QCOMPARE(saved.value(QStringLiteral("item_0_alt")),    QStringLiteral("Contact us"));   // 85
}

// =============================================================================
// Accessors
// =============================================================================

void Test_Website_PageBlocImageLinks::test_imagelinks_items_accessor_matches_loaded_data()
{
    const auto &h = oneItemHash(QStringLiteral("https://example.com/img.jpg"),
                                QLatin1String(PageBlocImageLinks::LINK_TYPE_CATEGORY),
                                QStringLiteral("tech"),
                                QStringLiteral("Tech"));
    PageBlocImageLinks bloc;
    bloc.load(h);
    const auto &items = bloc.items();
    QCOMPARE(items.size(), 1);   // 86
    QCOMPARE(items.at(0).imageUrl,   QStringLiteral("https://example.com/img.jpg"));   // 87
    QCOMPARE(items.at(0).linkType,   QLatin1String(PageBlocImageLinks::LINK_TYPE_CATEGORY));   // 88
    QCOMPARE(items.at(0).linkTarget, QStringLiteral("tech"));   // 89
    QCOMPARE(items.at(0).altText,    QStringLiteral("Tech"));   // 90
}

void Test_Website_PageBlocImageLinks::test_imagelinks_grid_accessors_match_loaded_values()
{
    QHash<QString, QString> h;
    h.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_DESKTOP), QStringLiteral("5"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_ROWS_DESKTOP), QStringLiteral("3"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_TABLET),  QStringLiteral("3"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_ROWS_TABLET),  QStringLiteral("4"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_COLS_MOBILE),  QStringLiteral("2"));
    h.insert(QLatin1String(PageBlocImageLinks::KEY_ROWS_MOBILE),  QStringLiteral("5"));

    PageBlocImageLinks bloc;
    bloc.load(h);
    QCOMPARE(bloc.colsDesktop(), 5);   // 91
    QCOMPARE(bloc.rowsDesktop(), 3);   // 92
    QCOMPARE(bloc.colsTablet(),  3);   // 93
    QCOMPARE(bloc.rowsTablet(),  4);   // 94
    QCOMPARE(bloc.colsMobile(),  2);   // 95
    QCOMPARE(bloc.rowsMobile(),  5);   // 96
}

QTEST_MAIN(Test_Website_PageBlocImageLinks)
#include "test_website_page_bloc_image_links.moc"
