#include <QtTest>

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

#include "website/EngineArticles.h"
#include "website/HostTable.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocCategory.h"
#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

struct Fixture {
    QTemporaryDir        dir;
    CategoryTable        categoryTable{QDir(dir.path())};
    PageBlocCategory     bloc{categoryTable};

    // Runs addCode and returns {html, css}.
    std::pair<QString, QString> addCode()
    {
        EngineArticles engine;
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
        return {html, css};
    }

    // Loads a comma-separated list of category id strings and runs addCode.
    std::pair<QString, QString> addCodeFor(const QString &ids)
    {
        bloc.load({{QLatin1String(PageBlocCategory::KEY_CATEGORIES), ids}});
        return addCode();
    }
};

} // namespace

// =============================================================================
// Test_Website_PageBlocCategory
// =============================================================================

class Test_Website_PageBlocCategory : public QObject
{
    Q_OBJECT

private slots:
    // --- getName ---
    void test_pagebloccategory_get_name_is_not_empty();

    // --- createEditWidget ---
    void test_pagebloccategory_create_edit_widget_returns_non_null();

    // --- getAiKeyClues ---
    void test_pagebloccategory_get_ai_key_clues_empty_when_no_categories();
    void test_pagebloccategory_get_ai_key_clues_contains_category_names();
    void test_pagebloccategory_get_ai_key_clues_key_is_categories();

    // --- load / save roundtrip ---
    void test_pagebloccategory_load_save_roundtrip_single_id();
    void test_pagebloccategory_load_save_roundtrip_multiple_ids_sorted();
    void test_pagebloccategory_load_empty_string_produces_empty_save();

    // --- addCode: empty / unknown ---
    void test_pagebloccategory_empty_ids_produces_no_html();
    void test_pagebloccategory_unknown_id_produces_no_html();

    // --- addCode: breadcrumb structure ---
    void test_pagebloccategory_root_category_renders_name();
    void test_pagebloccategory_root_category_renders_ol_tag();
    void test_pagebloccategory_root_category_renders_nav_tag();
    void test_pagebloccategory_parent_child_both_appear_in_breadcrumb();
    void test_pagebloccategory_breadcrumb_order_is_root_then_leaf();
    void test_pagebloccategory_three_level_chain_rendered_in_order();

    // --- addCode: deepest category selected ---
    void test_pagebloccategory_deepest_selected_category_drives_chain();

    // --- addCode: CSS ---
    void test_pagebloccategory_css_emitted_on_first_call();
    void test_pagebloccategory_css_not_duplicated_on_second_call();
    void test_pagebloccategory_css_contains_breadcrumb_class();

    // --- addCode: no JS ---
    void test_pagebloccategory_no_js_emitted();

    // --- Bug fix: translated name must not drive the permalink ---
    void test_pagebloccategory_translated_name_uses_english_permalink();
};

// =============================================================================
// getName
// =============================================================================

void Test_Website_PageBlocCategory::test_pagebloccategory_get_name_is_not_empty()
{
    Fixture f;
    QVERIFY(!f.bloc.getName().isEmpty());
}

// =============================================================================
// createEditWidget
// =============================================================================

void Test_Website_PageBlocCategory::test_pagebloccategory_create_edit_widget_returns_non_null()
{
    Fixture f;
    QScopedPointer<AbstractPageBlockWidget> w(f.bloc.createEditWidget());
    QVERIFY(w != nullptr);
}

// =============================================================================
// getAiKeyClues
// =============================================================================

void Test_Website_PageBlocCategory::test_pagebloccategory_get_ai_key_clues_empty_when_no_categories()
{
    Fixture f;
    QVERIFY(f.bloc.getAiKeyClues().isEmpty());
}

void Test_Website_PageBlocCategory::test_pagebloccategory_get_ai_key_clues_contains_category_names()
{
    Fixture f;
    f.categoryTable.addCategory(QStringLiteral("Health"));
    const auto clues = f.bloc.getAiKeyClues();
    QVERIFY(!clues.isEmpty());
    const QString hint = clues.value(QLatin1String(PageBlocCategory::KEY_CATEGORIES));
    QVERIFY(hint.contains(QStringLiteral("Health")));
}

void Test_Website_PageBlocCategory::test_pagebloccategory_get_ai_key_clues_key_is_categories()
{
    Fixture f;
    f.categoryTable.addCategory(QStringLiteral("X"));
    QVERIFY(f.bloc.getAiKeyClues().contains(QLatin1String(PageBlocCategory::KEY_CATEGORIES)));
}

// =============================================================================
// load / save roundtrip
// =============================================================================

void Test_Website_PageBlocCategory::test_pagebloccategory_load_save_roundtrip_single_id()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Tech"));
    f.bloc.load({{QLatin1String(PageBlocCategory::KEY_CATEGORIES), QString::number(id)}});
    QHash<QString, QString> saved;
    f.bloc.save(saved);
    QCOMPARE(saved.value(QLatin1String(PageBlocCategory::KEY_CATEGORIES)), QString::number(id));
}

void Test_Website_PageBlocCategory::test_pagebloccategory_load_save_roundtrip_multiple_ids_sorted()
{
    Fixture f;
    const int id1 = f.categoryTable.addCategory(QStringLiteral("A"));
    const int id2 = f.categoryTable.addCategory(QStringLiteral("B"));
    f.bloc.load({{QLatin1String(PageBlocCategory::KEY_CATEGORIES),
                  QStringLiteral("%1,%2").arg(QString::number(id2), QString::number(id1))}});
    QHash<QString, QString> saved;
    f.bloc.save(saved);
    // Save output is sorted ascending.
    const QStringList parts =
        saved.value(QLatin1String(PageBlocCategory::KEY_CATEGORIES)).split(QLatin1Char(','));
    QVERIFY(parts.size() == 2);
    QVERIFY(parts[0].toInt() < parts[1].toInt());
}

void Test_Website_PageBlocCategory::test_pagebloccategory_load_empty_string_produces_empty_save()
{
    Fixture f;
    f.bloc.load({{QLatin1String(PageBlocCategory::KEY_CATEGORIES), QString()}});
    QHash<QString, QString> saved;
    f.bloc.save(saved);
    QVERIFY(saved.value(QLatin1String(PageBlocCategory::KEY_CATEGORIES)).isEmpty());
}

// =============================================================================
// addCode: empty / unknown
// =============================================================================

void Test_Website_PageBlocCategory::test_pagebloccategory_empty_ids_produces_no_html()
{
    Fixture f;
    const auto [html, css] = f.addCodeFor(QString());
    QVERIFY(html.isEmpty());
}

void Test_Website_PageBlocCategory::test_pagebloccategory_unknown_id_produces_no_html()
{
    Fixture f;
    const auto [html, css] = f.addCodeFor(QStringLiteral("9999"));
    QVERIFY(html.isEmpty());
}

// =============================================================================
// addCode: breadcrumb structure
// =============================================================================

void Test_Website_PageBlocCategory::test_pagebloccategory_root_category_renders_name()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Health"));
    const auto [html, css] = f.addCodeFor(QString::number(id));
    QVERIFY(html.contains(QStringLiteral("Health")));
}

void Test_Website_PageBlocCategory::test_pagebloccategory_root_category_renders_ol_tag()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Health"));
    const auto [html, css] = f.addCodeFor(QString::number(id));
    QVERIFY(html.contains(QStringLiteral("<ol")));
    QVERIFY(html.contains(QStringLiteral("</ol>")));
}

void Test_Website_PageBlocCategory::test_pagebloccategory_root_category_renders_nav_tag()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Health"));
    const auto [html, css] = f.addCodeFor(QString::number(id));
    QVERIFY(html.contains(QStringLiteral("<nav")));
    QVERIFY(html.contains(QStringLiteral("</nav>")));
}

void Test_Website_PageBlocCategory::test_pagebloccategory_parent_child_both_appear_in_breadcrumb()
{
    Fixture f;
    const int parentId = f.categoryTable.addCategory(QStringLiteral("Health"));
    const int childId  = f.categoryTable.addCategory(QStringLiteral("Bones"), parentId);
    const auto [html, css] = f.addCodeFor(QString::number(childId));
    QVERIFY(html.contains(QStringLiteral("Health")));
    QVERIFY(html.contains(QStringLiteral("Bones")));
}

void Test_Website_PageBlocCategory::test_pagebloccategory_breadcrumb_order_is_root_then_leaf()
{
    Fixture f;
    const int parentId = f.categoryTable.addCategory(QStringLiteral("Parent"));
    const int childId  = f.categoryTable.addCategory(QStringLiteral("Child"), parentId);
    const auto [html, css] = f.addCodeFor(QString::number(childId));
    QVERIFY(html.indexOf(QStringLiteral("Parent")) < html.indexOf(QStringLiteral("Child")));
}

void Test_Website_PageBlocCategory::test_pagebloccategory_three_level_chain_rendered_in_order()
{
    Fixture f;
    const int l1 = f.categoryTable.addCategory(QStringLiteral("Level1"));
    const int l2 = f.categoryTable.addCategory(QStringLiteral("Level2"), l1);
    const int l3 = f.categoryTable.addCategory(QStringLiteral("Level3"), l2);
    const auto [html, css] = f.addCodeFor(QString::number(l3));
    QVERIFY(html.indexOf(QStringLiteral("Level1")) < html.indexOf(QStringLiteral("Level2")));
    QVERIFY(html.indexOf(QStringLiteral("Level2")) < html.indexOf(QStringLiteral("Level3")));
}

// =============================================================================
// addCode: deepest category selected
// =============================================================================

void Test_Website_PageBlocCategory::test_pagebloccategory_deepest_selected_category_drives_chain()
{
    // When two categories are selected, the one with the deeper chain determines
    // the breadcrumb trail.  A root category (depth 0) plus a child (depth 1):
    // the child's chain is rendered, so the parent's name appears twice only once.
    Fixture f;
    const int rootId  = f.categoryTable.addCategory(QStringLiteral("Root"));
    const int childId = f.categoryTable.addCategory(QStringLiteral("Child"), rootId);
    // Load both; deepest (childId) wins → chain is [Root, Child].
    const auto [html, css] =
        f.addCodeFor(QStringLiteral("%1,%2").arg(QString::number(rootId),
                                                  QString::number(childId)));
    QVERIFY(html.contains(QStringLiteral("Root")));
    QVERIFY(html.contains(QStringLiteral("Child")));
}

// =============================================================================
// addCode: CSS
// =============================================================================

void Test_Website_PageBlocCategory::test_pagebloccategory_css_emitted_on_first_call()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Art"));
    const auto [html, css] = f.addCodeFor(QString::number(id));
    QVERIFY(!css.isEmpty());
}

void Test_Website_PageBlocCategory::test_pagebloccategory_css_not_duplicated_on_second_call()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Art"));
    f.bloc.load({{QLatin1String(PageBlocCategory::KEY_CATEGORIES), QString::number(id)}});

    EngineArticles engine;
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    f.bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    const QString cssAfterFirst = css;
    QVERIFY(!cssAfterFirst.isEmpty());
    f.bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css, cssAfterFirst);
}

void Test_Website_PageBlocCategory::test_pagebloccategory_css_contains_breadcrumb_class()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Science"));
    const auto [html, css] = f.addCodeFor(QString::number(id));
    QVERIFY(css.contains(QStringLiteral(".breadcrumb")));
}

// =============================================================================
// addCode: no JS
// =============================================================================

void Test_Website_PageBlocCategory::test_pagebloccategory_no_js_emitted()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Tech"));
    f.bloc.load({{QLatin1String(PageBlocCategory::KEY_CATEGORIES), QString::number(id)}});
    EngineArticles engine;
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    f.bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(js.isEmpty());
}

// =============================================================================
// Bug fix: translated name must not drive the permalink
// =============================================================================

void Test_Website_PageBlocCategory::test_pagebloccategory_translated_name_uses_english_permalink()
{
    // Regression test for commit 297774c6:
    // PageBlocCategory was building the breadcrumb href from the *translated*
    // category name, e.g. "Santé" → /sant-.html.  That permalink did not exist
    // in the available-pages map, so isPageAvailable returned false and the
    // link was rendered as plain text.  The fix always derives the permalink
    // from the English canonical name (row->name), so /health.html is
    // generated regardless of the active language.

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Write engine_domains.csv with French at websiteIndex 0.
    {
        const QString csvPath =
            QDir(dir.path()).absoluteFilePath(QStringLiteral("engine_domains.csv"));
        QFile csv(csvPath);
        QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&csv);
        out << QStringLiteral("Enabled;LangCode;Language;Theme;Domain;HostId;HostFolder\n");
        out << QStringLiteral("1;fr;French;default;fr.example.com;;\n");
    }

    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    // Register /health.html as an available page for the "fr" language.
    QHash<QString, QSet<QString>> availablePages;
    availablePages[QStringLiteral("fr")].insert(QStringLiteral("/health.html"));
    engine.setAvailablePages(availablePages);

    // Set up a category "Health" with the French translation "Santé".
    CategoryTable categoryTable(QDir(dir.path()));
    const int id = categoryTable.addCategory(QStringLiteral("Health"));
    categoryTable.setTranslation(id, QStringLiteral("fr"), QStringLiteral("Santé"));

    // Wire the bloc to the category table and select the "Health" category.
    PageBlocCategory bloc(categoryTable);
    bloc.load({{QLatin1String(PageBlocCategory::KEY_CATEGORIES), QString::number(id)}});

    // Generate HTML for websiteIndex 0 (French).
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);

    // The breadcrumb must contain the English-derived permalink "health.html".
    QVERIFY2(html.contains(QStringLiteral("health.html")),
             qPrintable(QStringLiteral("Expected 'health.html' in html, got: ") + html));

    // The French-derived slug "sant" must NOT appear as part of an href.
    // (The visible link text "Santé" is fine, but the href must be English-based.)
    QVERIFY2(!html.contains(QStringLiteral("sant-")),
             qPrintable(QStringLiteral("French slug 'sant-' must not appear in href, got: ") + html));
}

QTEST_MAIN(Test_Website_PageBlocCategory)
#include "test_website_page_bloc_category.moc"
