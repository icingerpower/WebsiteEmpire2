#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include "website/EngineArticles.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocCategoryLinks.h"
#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

struct Fixture {
    QTemporaryDir        dir;
    CategoryTable        categoryTable{QDir(dir.path())};
    PageBlocCategoryLinks bloc{categoryTable};

    // Loads ids and runs addCode; returns {html, css}.
    std::pair<QString, QString> addCodeFor(const QString &ids)
    {
        bloc.load({{QLatin1String(PageBlocCategoryLinks::KEY_CATEGORIES), ids}});
        EngineArticles engine;
        QString html, css, js;
        QSet<QString> cssDoneIds, jsDoneIds;
        bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
        return {html, css};
    }
};

} // namespace

// =============================================================================
// Test_Website_PageBlocCategoryLinks
// =============================================================================

class Test_Website_PageBlocCategoryLinks : public QObject
{
    Q_OBJECT

private slots:
    // --- getName ---
    void test_pagebloccatlinks_get_name_is_not_empty();

    // --- createEditWidget ---
    void test_pagebloccatlinks_create_edit_widget_returns_non_null();

    // --- getAiKeyClues ---
    void test_pagebloccatlinks_get_ai_key_clues_empty_when_no_categories();
    void test_pagebloccatlinks_get_ai_key_clues_contains_category_names();
    void test_pagebloccatlinks_get_ai_key_clues_key_is_categories();
    void test_pagebloccatlinks_get_ai_key_clues_hint_differs_from_primary_category();

    // --- load / save roundtrip ---
    void test_pagebloccatlinks_load_save_roundtrip_single_id();
    void test_pagebloccatlinks_load_save_roundtrip_multiple_ids_sorted();
    void test_pagebloccatlinks_load_empty_string_produces_empty_save();

    // --- addCode: empty / unknown ---
    void test_pagebloccatlinks_empty_ids_produces_no_html();
    void test_pagebloccatlinks_unknown_id_produces_no_html();

    // --- addCode: rendering ---
    void test_pagebloccatlinks_root_category_name_appears_in_output();
    void test_pagebloccatlinks_child_category_name_appears_in_output();
    void test_pagebloccatlinks_parent_name_used_as_group_label();
    void test_pagebloccatlinks_multiple_children_same_parent_in_same_group();
    void test_pagebloccatlinks_children_different_parents_in_separate_groups();
    void test_pagebloccatlinks_root_category_rendered_without_label();

    // --- addCode: CSS ---
    void test_pagebloccatlinks_css_emitted_on_first_call();
    void test_pagebloccatlinks_css_not_duplicated_on_second_call();
    void test_pagebloccatlinks_css_contains_cat_xref_class();

    // --- addCode: no JS ---
    void test_pagebloccatlinks_no_js_emitted();
};

// =============================================================================
// getName
// =============================================================================

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_get_name_is_not_empty()
{
    Fixture f;
    QVERIFY(!f.bloc.getName().isEmpty());
}

// =============================================================================
// createEditWidget
// =============================================================================

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_create_edit_widget_returns_non_null()
{
    Fixture f;
    QScopedPointer<AbstractPageBlockWidget> w(f.bloc.createEditWidget());
    QVERIFY(w != nullptr);
}

// =============================================================================
// getAiKeyClues
// =============================================================================

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_get_ai_key_clues_empty_when_no_categories()
{
    Fixture f;
    QVERIFY(f.bloc.getAiKeyClues().isEmpty());
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_get_ai_key_clues_contains_category_names()
{
    Fixture f;
    f.categoryTable.addCategory(QStringLiteral("Knee"));
    const auto clues = f.bloc.getAiKeyClues();
    QVERIFY(!clues.isEmpty());
    const QString hint = clues.value(QLatin1String(PageBlocCategoryLinks::KEY_CATEGORIES));
    QVERIFY(hint.contains(QStringLiteral("Knee")));
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_get_ai_key_clues_key_is_categories()
{
    Fixture f;
    f.categoryTable.addCategory(QStringLiteral("X"));
    QVERIFY(f.bloc.getAiKeyClues().contains(
        QLatin1String(PageBlocCategoryLinks::KEY_CATEGORIES)));
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_get_ai_key_clues_hint_differs_from_primary_category()
{
    // The hint must clarify that these are cross-reference categories, not the
    // primary breadcrumb category, so the AI can distinguish the two blocs.
    Fixture f;
    f.categoryTable.addCategory(QStringLiteral("Knee"));

    PageBlocCategoryLinks linksBloc(f.categoryTable);
    const QString linksHint =
        linksBloc.getAiKeyClues().value(QLatin1String(PageBlocCategoryLinks::KEY_CATEGORIES));
    QVERIFY(!linksHint.isEmpty());
    // The cross-reference hint must not be identical to a generic category hint.
    QVERIFY(linksHint.contains(QStringLiteral("cross-reference"),
                                Qt::CaseInsensitive));
}

// =============================================================================
// load / save roundtrip
// =============================================================================

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_load_save_roundtrip_single_id()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Knee"));
    f.bloc.load({{QLatin1String(PageBlocCategoryLinks::KEY_CATEGORIES), QString::number(id)}});
    QHash<QString, QString> saved;
    f.bloc.save(saved);
    QCOMPARE(saved.value(QLatin1String(PageBlocCategoryLinks::KEY_CATEGORIES)),
             QString::number(id));
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_load_save_roundtrip_multiple_ids_sorted()
{
    Fixture f;
    const int id1 = f.categoryTable.addCategory(QStringLiteral("A"));
    const int id2 = f.categoryTable.addCategory(QStringLiteral("B"));
    f.bloc.load({{QLatin1String(PageBlocCategoryLinks::KEY_CATEGORIES),
                  QStringLiteral("%1,%2").arg(QString::number(id2), QString::number(id1))}});
    QHash<QString, QString> saved;
    f.bloc.save(saved);
    const QStringList parts =
        saved.value(QLatin1String(PageBlocCategoryLinks::KEY_CATEGORIES))
             .split(QLatin1Char(','));
    QVERIFY(parts.size() == 2);
    QVERIFY(parts[0].toInt() < parts[1].toInt());
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_load_empty_string_produces_empty_save()
{
    Fixture f;
    f.bloc.load({{QLatin1String(PageBlocCategoryLinks::KEY_CATEGORIES), QString()}});
    QHash<QString, QString> saved;
    f.bloc.save(saved);
    QVERIFY(saved.value(QLatin1String(PageBlocCategoryLinks::KEY_CATEGORIES)).isEmpty());
}

// =============================================================================
// addCode: empty / unknown
// =============================================================================

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_empty_ids_produces_no_html()
{
    Fixture f;
    const auto [html, css] = f.addCodeFor(QString());
    QVERIFY(html.isEmpty());
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_unknown_id_produces_no_html()
{
    Fixture f;
    const auto [html, css] = f.addCodeFor(QStringLiteral("9999"));
    QVERIFY(html.isEmpty());
}

// =============================================================================
// addCode: rendering
// =============================================================================

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_root_category_name_appears_in_output()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Knee"));
    const auto [html, css] = f.addCodeFor(QString::number(id));
    QVERIFY(html.contains(QStringLiteral("Knee")));
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_child_category_name_appears_in_output()
{
    Fixture f;
    const int parentId = f.categoryTable.addCategory(QStringLiteral("Body Parts"));
    const int childId  = f.categoryTable.addCategory(QStringLiteral("Knee"), parentId);
    const auto [html, css] = f.addCodeFor(QString::number(childId));
    QVERIFY(html.contains(QStringLiteral("Knee")));
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_parent_name_used_as_group_label()
{
    // When the selected category has a parent, the parent name must appear as
    // the group label (inside <strong>) in the rendered output.
    Fixture f;
    const int parentId = f.categoryTable.addCategory(QStringLiteral("Body Parts"));
    const int childId  = f.categoryTable.addCategory(QStringLiteral("Knee"), parentId);
    const auto [html, css] = f.addCodeFor(QString::number(childId));
    QVERIFY(html.contains(QStringLiteral("Body Parts")));
    QVERIFY(html.contains(QStringLiteral("<strong>")));
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_multiple_children_same_parent_in_same_group()
{
    // Knee and Shoulder share the "Body Parts" parent → rendered in one <p>.
    Fixture f;
    const int parentId   = f.categoryTable.addCategory(QStringLiteral("Body Parts"));
    const int kneeId     = f.categoryTable.addCategory(QStringLiteral("Knee"),     parentId);
    const int shoulderId = f.categoryTable.addCategory(QStringLiteral("Shoulder"), parentId);
    const auto [html, css] = f.addCodeFor(
        QStringLiteral("%1,%2").arg(QString::number(kneeId), QString::number(shoulderId)));
    // Both names appear.
    QVERIFY(html.contains(QStringLiteral("Knee")));
    QVERIFY(html.contains(QStringLiteral("Shoulder")));
    // Only one label group for their shared parent.
    QCOMPARE(html.count(QStringLiteral("Body Parts")), 1);
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_children_different_parents_in_separate_groups()
{
    // Knee (Body Parts) and Fibula (Bones) → two separate <p> groups.
    Fixture f;
    const int bpId     = f.categoryTable.addCategory(QStringLiteral("Body Parts"));
    const int bonesId  = f.categoryTable.addCategory(QStringLiteral("Bones"));
    const int kneeId   = f.categoryTable.addCategory(QStringLiteral("Knee"),   bpId);
    const int fibulaId = f.categoryTable.addCategory(QStringLiteral("Fibula"), bonesId);
    const auto [html, css] = f.addCodeFor(
        QStringLiteral("%1,%2").arg(QString::number(kneeId), QString::number(fibulaId)));
    QVERIFY(html.contains(QStringLiteral("Body Parts")));
    QVERIFY(html.contains(QStringLiteral("Bones")));
    QVERIFY(html.contains(QStringLiteral("Knee")));
    QVERIFY(html.contains(QStringLiteral("Fibula")));
    // Two separate <p> tags.
    QVERIFY(html.count(QStringLiteral("<p>")) >= 2);
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_root_category_rendered_without_label()
{
    // A root category (parentId == 0) has no parent → no <strong> label emitted.
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Standalone"));
    const auto [html, css] = f.addCodeFor(QString::number(id));
    QVERIFY(html.contains(QStringLiteral("Standalone")));
    QVERIFY(!html.contains(QStringLiteral("<strong>")));
}

// =============================================================================
// addCode: CSS
// =============================================================================

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_css_emitted_on_first_call()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Knee"));
    const auto [html, css] = f.addCodeFor(QString::number(id));
    QVERIFY(!css.isEmpty());
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_css_not_duplicated_on_second_call()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Knee"));
    f.bloc.load({{QLatin1String(PageBlocCategoryLinks::KEY_CATEGORIES), QString::number(id)}});

    EngineArticles engine;
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    f.bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    const QString cssAfterFirst = css;
    QVERIFY(!cssAfterFirst.isEmpty());
    f.bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css, cssAfterFirst);
}

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_css_contains_cat_xref_class()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Knee"));
    const auto [html, css] = f.addCodeFor(QString::number(id));
    QVERIFY(css.contains(QStringLiteral(".cat-xref")));
}

// =============================================================================
// addCode: no JS
// =============================================================================

void Test_Website_PageBlocCategoryLinks::test_pagebloccatlinks_no_js_emitted()
{
    Fixture f;
    const int id = f.categoryTable.addCategory(QStringLiteral("Knee"));
    f.bloc.load({{QLatin1String(PageBlocCategoryLinks::KEY_CATEGORIES), QString::number(id)}});
    EngineArticles engine;
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    f.bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(js.isEmpty());
}

QTEST_MAIN(Test_Website_PageBlocCategoryLinks)
#include "test_website_page_bloc_category_links.moc"
