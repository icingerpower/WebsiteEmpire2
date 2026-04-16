#include <QtTest>

#include <QDir>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "website/AbstractEngine.h"
#include "website/EngineArticles.h"
#include "website/HostTable.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/AbstractPageBloc.h"
#include "website/pages/blocs/PageBlocCategory.h"
#include "website/pages/blocs/PageBlocText.h"

// =============================================================================
// Helpers
// =============================================================================

namespace {

// Returns the first const AbstractPageBloc* of the first page type, cast to
// PageBlocCategory*.  Returns nullptr if the cast fails.
const PageBlocCategory *firstCategoryBloc(const EngineArticles &engine)
{
    if (engine.getPageTypes().isEmpty()) {
        return nullptr;
    }
    const auto &blocs = engine.getPageTypes().first()->getPageBlocs();
    if (blocs.isEmpty()) {
        return nullptr;
    }
    return dynamic_cast<const PageBlocCategory *>(blocs.first());
}

// Returns a non-const PageBlocCategory* via const_cast — valid because the
// underlying object is owned non-const by PageTypeArticle.
PageBlocCategory *firstCategoryBlocMut(EngineArticles &engine)
{
    return const_cast<PageBlocCategory *>(firstCategoryBloc(engine));
}

} // namespace

// =============================================================================
// Test_EngineArticles
// =============================================================================

class Test_EngineArticles : public QObject
{
    Q_OBJECT

private slots:
    // --- Identity ---
    void test_enginearticles_get_id_stable();
    void test_enginearticles_get_name_non_empty();
    void test_enginearticles_id_and_name_differ();

    // --- Registry ---
    void test_enginearticles_registered_in_all_engines();
    void test_enginearticles_prototype_id_matches();

    // --- Variations ---
    void test_enginearticles_variations_non_empty();
    void test_enginearticles_variations_ids_non_empty();
    void test_enginearticles_variations_names_non_empty();
    void test_enginearticles_variations_ids_unique();

    // --- create() ---
    void test_enginearticles_create_returns_non_null();
    void test_enginearticles_create_returns_new_instance();

    // --- getPageTypes() lifecycle ---
    void test_enginearticles_get_page_types_empty_before_init();
    void test_enginearticles_get_page_types_non_empty_after_init();
    void test_enginearticles_get_page_types_all_entries_non_null();
    void test_enginearticles_get_page_types_stable_ref();
    void test_enginearticles_get_page_types_valid_after_reinit();

    // --- Page type structure ---
    void test_enginearticles_page_type_has_two_blocs();
    void test_enginearticles_first_bloc_is_category_bloc();
    void test_enginearticles_second_bloc_renders_text_as_paragraph();

    // --- Category bloc: HTML rendering ---
    void test_enginearticles_category_bloc_renders_category_name();
    void test_enginearticles_category_bloc_renders_ul_tag();
    void test_enginearticles_category_bloc_empty_content_produces_no_html();
    void test_enginearticles_category_bloc_unknown_id_produces_no_html();
    void test_enginearticles_category_bloc_css_emitted_on_first_call();
    void test_enginearticles_category_bloc_css_not_duplicated();
    void test_enginearticles_category_bloc_multiple_categories_all_rendered();
    void test_enginearticles_category_bloc_category_order_preserved();

    // --- CategoryTable ---
    void test_enginearticles_category_table_accessible_after_init();
    void test_enginearticles_category_persists_after_reload();
    void test_enginearticles_category_translation_accessible();

    // --- Attribute integration ---
    void test_enginearticles_set_content_populates_attributes();
    void test_enginearticles_unknown_id_in_content_skipped_in_attributes();
    void test_enginearticles_attributes_empty_with_no_content();

    // --- Deletion cascade ---
    void test_enginearticles_remove_category_emits_content_changed();
    void test_enginearticles_remove_category_clears_attributes();
    void test_enginearticles_remove_parent_cascades_to_child_in_attributes();
    void test_enginearticles_remove_category_content_changed_signal_contains_remaining_ids();

    // --- Generator link ---
    void test_enginearticles_get_generator_id_empty();
};

// =============================================================================
// Identity
// =============================================================================

void Test_EngineArticles::test_enginearticles_get_id_stable()
{
    EngineArticles engine;
    QCOMPARE(engine.getId(), QStringLiteral("EngineArticles"));
    QCOMPARE(EngineArticles().getId(), QStringLiteral("EngineArticles"));
}

void Test_EngineArticles::test_enginearticles_get_name_non_empty()
{
    EngineArticles engine;
    QVERIFY(!engine.getName().isEmpty());
}

void Test_EngineArticles::test_enginearticles_id_and_name_differ()
{
    EngineArticles engine;
    QVERIFY(engine.getId() != engine.getName());
}

// =============================================================================
// Registry
// =============================================================================

void Test_EngineArticles::test_enginearticles_registered_in_all_engines()
{
    QVERIFY(AbstractEngine::ALL_ENGINES().contains(QStringLiteral("EngineArticles")));
}

void Test_EngineArticles::test_enginearticles_prototype_id_matches()
{
    const AbstractEngine *proto = AbstractEngine::ALL_ENGINES().value(QStringLiteral("EngineArticles"));
    QVERIFY(proto != nullptr);
    QCOMPARE(proto->getId(), QStringLiteral("EngineArticles"));
}

// =============================================================================
// Variations
// =============================================================================

void Test_EngineArticles::test_enginearticles_variations_non_empty()
{
    EngineArticles engine;
    QVERIFY(!engine.getVariations().isEmpty());
}

void Test_EngineArticles::test_enginearticles_variations_ids_non_empty()
{
    EngineArticles engine;
    for (const auto &v : engine.getVariations()) {
        QVERIFY(!v.id.isEmpty());
    }
}

void Test_EngineArticles::test_enginearticles_variations_names_non_empty()
{
    EngineArticles engine;
    for (const auto &v : engine.getVariations()) {
        QVERIFY(!v.name.isEmpty());
    }
}

void Test_EngineArticles::test_enginearticles_variations_ids_unique()
{
    EngineArticles engine;
    const QList<AbstractEngine::Variation> vars = engine.getVariations();
    QSet<QString> ids;
    for (const auto &v : vars) {
        ids.insert(v.id);
    }
    QCOMPARE(ids.size(), vars.size());
}

// =============================================================================
// create()
// =============================================================================

void Test_EngineArticles::test_enginearticles_create_returns_non_null()
{
    EngineArticles engine;
    QScopedPointer<AbstractEngine> created(engine.create());
    QVERIFY(created != nullptr);
}

void Test_EngineArticles::test_enginearticles_create_returns_new_instance()
{
    EngineArticles engine;
    QScopedPointer<AbstractEngine> a(engine.create());
    QScopedPointer<AbstractEngine> b(engine.create());
    QVERIFY(a.data() != b.data());
    QCOMPARE(a->getId(), engine.getId());
}

// =============================================================================
// getPageTypes() lifecycle
// =============================================================================

void Test_EngineArticles::test_enginearticles_get_page_types_empty_before_init()
{
    EngineArticles engine;
    QVERIFY(engine.getPageTypes().isEmpty());
}

void Test_EngineArticles::test_enginearticles_get_page_types_non_empty_after_init()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);
    QVERIFY(!engine.getPageTypes().isEmpty());
}

void Test_EngineArticles::test_enginearticles_get_page_types_all_entries_non_null()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);
    for (const AbstractPageType *pt : engine.getPageTypes()) {
        QVERIFY(pt != nullptr);
    }
}

void Test_EngineArticles::test_enginearticles_get_page_types_stable_ref()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);
    const QList<const AbstractPageType *> *first  = &engine.getPageTypes();
    const QList<const AbstractPageType *> *second = &engine.getPageTypes();
    QVERIFY(first == second);
}

void Test_EngineArticles::test_enginearticles_get_page_types_valid_after_reinit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);
    QVERIFY(!engine.getPageTypes().isEmpty());
    // Re-init must rebuild page types without crashing.
    engine.init(QDir(dir.path()), hostTable);
    QVERIFY(!engine.getPageTypes().isEmpty());
    QVERIFY(engine.getPageTypes().first() != nullptr);
}

// =============================================================================
// Page type structure
// =============================================================================

void Test_EngineArticles::test_enginearticles_page_type_has_two_blocs()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);
    QCOMPARE(engine.getPageTypes().first()->getPageBlocs().size(), 4);
}

void Test_EngineArticles::test_enginearticles_first_bloc_is_category_bloc()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);
    QVERIFY(firstCategoryBloc(engine) != nullptr);
}

void Test_EngineArticles::test_enginearticles_second_bloc_renders_text_as_paragraph()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const AbstractPageBloc *secondBloc = engine.getPageTypes().first()->getPageBlocs().at(1);
    const_cast<AbstractPageBloc *>(secondBloc)->load(
        {{QLatin1String(PageBlocText::KEY_TEXT), QStringLiteral("Hello")}});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    secondBloc->addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.contains(QStringLiteral("<p>Hello</p>")));
}

// =============================================================================
// Category bloc: HTML rendering
// =============================================================================

void Test_EngineArticles::test_enginearticles_category_bloc_renders_category_name()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const int id = engine.categoryTable().addCategory(QStringLiteral("Technology"));
    const AbstractPageBloc *bloc = firstCategoryBloc(engine);
    const_cast<AbstractPageBloc *>(bloc)->load(
        {{QLatin1String(PageBlocCategory::KEY_CATEGORIES), QString::number(id)}});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc->addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.contains(QStringLiteral("Technology")));
}

void Test_EngineArticles::test_enginearticles_category_bloc_renders_ul_tag()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const int id = engine.categoryTable().addCategory(QStringLiteral("Science"));
    const AbstractPageBloc *bloc = firstCategoryBloc(engine);
    const_cast<AbstractPageBloc *>(bloc)->load(
        {{QLatin1String(PageBlocCategory::KEY_CATEGORIES), QString::number(id)}});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc->addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.contains(QStringLiteral("<ul")));
    QVERIFY(html.contains(QStringLiteral("</ul>")));
}

void Test_EngineArticles::test_enginearticles_category_bloc_empty_content_produces_no_html()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const AbstractPageBloc *bloc = firstCategoryBloc(engine);
    // load() with empty categories → m_selectedIds is empty
    const_cast<AbstractPageBloc *>(bloc)->load(
        {{QLatin1String(PageBlocCategory::KEY_CATEGORIES), QString()}});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc->addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.isEmpty());
}

void Test_EngineArticles::test_enginearticles_category_bloc_unknown_id_produces_no_html()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const AbstractPageBloc *bloc = firstCategoryBloc(engine);
    // load() with an id that doesn't exist in CategoryTable → names list empty → no output
    const_cast<AbstractPageBloc *>(bloc)->load(
        {{QLatin1String(PageBlocCategory::KEY_CATEGORIES), QStringLiteral("9999")}});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc->addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.isEmpty());
}

void Test_EngineArticles::test_enginearticles_category_bloc_css_emitted_on_first_call()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const int id = engine.categoryTable().addCategory(QStringLiteral("Art"));
    const AbstractPageBloc *bloc = firstCategoryBloc(engine);
    const_cast<AbstractPageBloc *>(bloc)->load(
        {{QLatin1String(PageBlocCategory::KEY_CATEGORIES), QString::number(id)}});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc->addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(!css.isEmpty());
}

void Test_EngineArticles::test_enginearticles_category_bloc_css_not_duplicated()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const int id = engine.categoryTable().addCategory(QStringLiteral("Art"));
    const AbstractPageBloc *bloc = firstCategoryBloc(engine);
    const_cast<AbstractPageBloc *>(bloc)->load(
        {{QLatin1String(PageBlocCategory::KEY_CATEGORIES), QString::number(id)}});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc->addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    const QString cssAfterFirst = css;
    QVERIFY(!cssAfterFirst.isEmpty()); // sanity: CSS was actually emitted
    // Second call on the same page (same cssDoneIds) must not re-append CSS.
    bloc->addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QCOMPARE(css, cssAfterFirst);
}

void Test_EngineArticles::test_enginearticles_category_bloc_multiple_categories_all_rendered()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const int idA = engine.categoryTable().addCategory(QStringLiteral("Alpha"));
    const int idB = engine.categoryTable().addCategory(QStringLiteral("Beta"));
    const AbstractPageBloc *bloc = firstCategoryBloc(engine);
    const_cast<AbstractPageBloc *>(bloc)->load(
        {{QLatin1String(PageBlocCategory::KEY_CATEGORIES),
          QStringLiteral("%1,%2").arg(QString::number(idA), QString::number(idB))}});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc->addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.contains(QStringLiteral("Alpha")));
    QVERIFY(html.contains(QStringLiteral("Beta")));
}

void Test_EngineArticles::test_enginearticles_category_bloc_category_order_preserved()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const int idA = engine.categoryTable().addCategory(QStringLiteral("First"));
    const int idB = engine.categoryTable().addCategory(QStringLiteral("Second"));
    const AbstractPageBloc *bloc = firstCategoryBloc(engine);
    const_cast<AbstractPageBloc *>(bloc)->load(
        {{QLatin1String(PageBlocCategory::KEY_CATEGORIES),
          QStringLiteral("%1,%2").arg(QString::number(idA), QString::number(idB))}});
    QString html, css, js;
    QSet<QString> cssDoneIds, jsDoneIds;
    bloc->addCode(QStringView{}, engine, 0, html, css, js, cssDoneIds, jsDoneIds);
    QVERIFY(html.indexOf(QStringLiteral("First")) < html.indexOf(QStringLiteral("Second")));
}

// =============================================================================
// CategoryTable
// =============================================================================

void Test_EngineArticles::test_enginearticles_category_table_accessible_after_init()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);
    // Must not crash; addCategory returns a positive id.
    const int id = engine.categoryTable().addCategory(QStringLiteral("Test"));
    QVERIFY(id > 0);
}

void Test_EngineArticles::test_enginearticles_category_persists_after_reload()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));

    int savedId = 0;
    {
        EngineArticles engine;
        engine.init(QDir(dir.path()), hostTable);
        savedId = engine.categoryTable().addCategory(QStringLiteral("Persisted"));
    }

    EngineArticles engine2;
    engine2.init(QDir(dir.path()), hostTable);
    QVERIFY(engine2.categoryTable().categoryById(savedId) != nullptr);
    QCOMPARE(engine2.categoryTable().categoryById(savedId)->name,
             QStringLiteral("Persisted"));
}

void Test_EngineArticles::test_enginearticles_category_translation_accessible()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const int id = engine.categoryTable().addCategory(QStringLiteral("Food"));
    engine.categoryTable().setTranslation(id, QStringLiteral("fr"), QStringLiteral("Alimentation"));
    QCOMPARE(engine.categoryTable().translationFor(id, QStringLiteral("fr")),
             QStringLiteral("Alimentation"));
    // Unknown lang code falls back to English name.
    QCOMPARE(engine.categoryTable().translationFor(id, QStringLiteral("de")),
             QStringLiteral("Food"));
}

// =============================================================================
// Attribute integration
// =============================================================================

void Test_EngineArticles::test_enginearticles_set_content_populates_attributes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const int id = engine.categoryTable().addCategory(QStringLiteral("Science"));
    PageBlocCategory *bloc = firstCategoryBlocMut(engine);
    QVERIFY(bloc != nullptr);

    QVERIFY(bloc->getAttributes().isEmpty());
    bloc->setContent(QString::number(id));
    QCOMPARE(bloc->getAttributes().size(), 1);
    QCOMPARE(bloc->getAttributes().first()->id(),   id);
    QCOMPARE(bloc->getAttributes().first()->name(), QStringLiteral("Science"));
}

void Test_EngineArticles::test_enginearticles_unknown_id_in_content_skipped_in_attributes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    PageBlocCategory *bloc = firstCategoryBlocMut(engine);
    QVERIFY(bloc != nullptr);
    bloc->setContent(QStringLiteral("9999"));
    QVERIFY(bloc->getAttributes().isEmpty());
}

void Test_EngineArticles::test_enginearticles_attributes_empty_with_no_content()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const PageBlocCategory *bloc = firstCategoryBloc(engine);
    QVERIFY(bloc != nullptr);
    QVERIFY(bloc->getAttributes().isEmpty());
}

// =============================================================================
// Deletion cascade
// =============================================================================

void Test_EngineArticles::test_enginearticles_remove_category_emits_content_changed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const int id = engine.categoryTable().addCategory(QStringLiteral("ToRemove"));
    PageBlocCategory *bloc = firstCategoryBlocMut(engine);
    QVERIFY(bloc != nullptr);
    bloc->setContent(QString::number(id));

    QSignalSpy spy(bloc, &PageBlocCategory::contentChanged);
    engine.categoryTable().removeCategory(id);
    QCOMPARE(spy.count(), 1);
}

void Test_EngineArticles::test_enginearticles_remove_category_clears_attributes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const int id = engine.categoryTable().addCategory(QStringLiteral("Gone"));
    PageBlocCategory *bloc = firstCategoryBlocMut(engine);
    QVERIFY(bloc != nullptr);
    bloc->setContent(QString::number(id));
    QCOMPARE(bloc->getAttributes().size(), 1);

    engine.categoryTable().removeCategory(id);
    QVERIFY(bloc->getAttributes().isEmpty());
}

void Test_EngineArticles::test_enginearticles_remove_parent_cascades_to_child_in_attributes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const int parentId = engine.categoryTable().addCategory(QStringLiteral("Parent"));
    const int childId  = engine.categoryTable().addCategory(QStringLiteral("Child"), parentId);

    PageBlocCategory *bloc = firstCategoryBlocMut(engine);
    QVERIFY(bloc != nullptr);
    bloc->setContent(QStringLiteral("%1,%2").arg(parentId).arg(childId));
    QCOMPARE(bloc->getAttributes().size(), 2);

    // Removing the parent cascades — both parent and child ids are deleted.
    engine.categoryTable().removeCategory(parentId);
    QVERIFY(bloc->getAttributes().isEmpty());
}

void Test_EngineArticles::test_enginearticles_remove_category_content_changed_signal_contains_remaining_ids()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const int keepId   = engine.categoryTable().addCategory(QStringLiteral("Keep"));
    const int removeId = engine.categoryTable().addCategory(QStringLiteral("Remove"));

    PageBlocCategory *bloc = firstCategoryBlocMut(engine);
    QVERIFY(bloc != nullptr);
    bloc->setContent(QStringLiteral("%1,%2").arg(keepId).arg(removeId));

    QSignalSpy spy(bloc, &PageBlocCategory::contentChanged);
    engine.categoryTable().removeCategory(removeId);

    QCOMPARE(spy.count(), 1);
    const QString newContent = spy.first().first().toString();
    // Remaining content must contain the kept id.
    QVERIFY(newContent.contains(QString::number(keepId)));
    // Removed id must no longer appear.
    QVERIFY(!newContent.contains(QString::number(removeId)));
}

// =============================================================================
// Generator link

void Test_EngineArticles::test_enginearticles_get_generator_id_empty()
{
    // EngineArticles has no associated aspire generator — should return empty.
    EngineArticles engine;
    QVERIFY(engine.getGeneratorId().isEmpty());
}

QTEST_MAIN(Test_EngineArticles)
#include "test_website_engine_articles.moc"
