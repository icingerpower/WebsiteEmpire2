#include <QtTest>
#include <QTemporaryDir>

#include "website/AbstractEngine.h"
#include "website/EngineArticlesFashion.h"
#include "website/HostTable.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/blocs/PageBlocFashionHubGrid.h"

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

namespace {

// PageBlocFashionTaxonomyLinks is bloc index 7 in PageTypeArticleFashion, so
// its raw page_data key is "7_fashion_color" once AbstractPageType::save()
// prefixes it — PageBlocFashionHubGrid::addCode() matches by suffix, so any
// prefix ending in "_fashion_color" would do; "7_" mirrors the real index.
const QString kColorKey = QStringLiteral("7_fashion_color");

// AbstractEngine::init() -> _load() -> _reconcileRows() expands the domain
// table to cover every CountryLangManager target language, so "en" does NOT
// reliably land at row 0 — test_website_engine_articles.cpp's own tests
// scan for the row whose getLangCode() == "en" rather than assuming an
// index; Fixture::enIndex() below does the same.
struct Fixture {
    QTemporaryDir    dir;
    HostTable        hostTable;
    PageDb           db;
    PageRepositoryDb repo;
    EngineArticlesFashion engine;
    PageBlocFashionHubGrid bloc;

    Fixture()
        : hostTable(QDir(dir.path()))
        , db(QDir(dir.path()))
        , repo(db)
    {
        engine.init(QDir(dir.path()), hostTable);
        bloc.bindContext(repo, QDir(dir.path()));
    }

    // The website index whose getLangCode() == "en", or 0 if reconciliation
    // somehow produced no such row (defensive fallback, should not happen).
    int enIndex() const
    {
        for (int row = 0; row < engine.rowCount(); ++row) {
            if (engine.getLangCode(row) == QStringLiteral("en")) {
                return row;
            }
        }
        return 0;
    }

    // Creates a Fashion article tagged with the given colors, with an H1 title.
    int addArticle(const QString &permalink, const QStringList &colors,
                   const QString &title = QStringLiteral("Title"))
    {
        const int id = repo.create(QStringLiteral("article_fashion"), permalink,
                                    QStringLiteral("en"));
        repo.saveData(id, {
            {kColorKey, colors.join(QLatin1Char(','))},
            {QStringLiteral("1_text"), QStringLiteral("[TITLE level=\"1\"]") + title
                                       + QStringLiteral("[/TITLE]Some body text.")},
        });
        return id;
    }

    void loadHub(const QString &dimension, const QString &tagValue)
    {
        bloc.load({
            {QLatin1String(PageBlocFashionHubGrid::KEY_DIMENSION), dimension},
            {QLatin1String(PageBlocFashionHubGrid::KEY_TAG_VALUE), tagValue},
        });
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_Website_PageBlocFashionHubGrid : public QObject
{
    Q_OBJECT

private slots:
    void test_fashion_hub_grid_load_save_round_trip();
    void test_fashion_hub_grid_no_dimension_addcode_is_noop();
    void test_fashion_hub_grid_no_repo_addcode_is_noop();
    void test_fashion_hub_grid_matching_article_rendered();
    void test_fashion_hub_grid_non_matching_tag_excluded();
    void test_fashion_hub_grid_non_fashion_article_excluded();
    void test_fashion_hub_grid_multiple_matching_articles_all_rendered();
    void test_fashion_hub_grid_last_rendered_count_zero_before_addcode();
    void test_fashion_hub_grid_last_rendered_count_matches_matches();
    void test_fashion_hub_grid_translated_tag_name_empty_when_unset();
};

void Test_Website_PageBlocFashionHubGrid::test_fashion_hub_grid_load_save_round_trip()
{
    PageBlocFashionHubGrid bloc;
    bloc.load({
        {QLatin1String(PageBlocFashionHubGrid::KEY_DIMENSION), QStringLiteral("fashion_color")},
        {QLatin1String(PageBlocFashionHubGrid::KEY_TAG_VALUE), QStringLiteral("Burgundy")},
    });
    QCOMPARE(bloc.dimension(), QStringLiteral("fashion_color"));
    QCOMPARE(bloc.tagValue(),  QStringLiteral("Burgundy"));

    QHash<QString, QString> saved;
    bloc.save(saved);
    QCOMPARE(saved.value(QLatin1String(PageBlocFashionHubGrid::KEY_DIMENSION)),
             QStringLiteral("fashion_color"));
    QCOMPARE(saved.value(QLatin1String(PageBlocFashionHubGrid::KEY_TAG_VALUE)),
             QStringLiteral("Burgundy"));
}

void Test_Website_PageBlocFashionHubGrid::test_fashion_hub_grid_no_dimension_addcode_is_noop()
{
    Fixture f;
    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    f.bloc.addCode(QStringView{}, f.engine, 0, html, css, js, cssDone, jsDone);
    QVERIFY(html.isEmpty());
    QCOMPARE(f.bloc.lastRenderedCount(), 0);
}

void Test_Website_PageBlocFashionHubGrid::test_fashion_hub_grid_no_repo_addcode_is_noop()
{
    QTemporaryDir dir;
    HostTable hostTable{QDir(dir.path())};
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);

    PageBlocFashionHubGrid bloc; // bindContext() never called
    bloc.load({
        {QLatin1String(PageBlocFashionHubGrid::KEY_DIMENSION), QStringLiteral("fashion_color")},
        {QLatin1String(PageBlocFashionHubGrid::KEY_TAG_VALUE), QStringLiteral("Burgundy")},
    });
    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDone, jsDone);
    QVERIFY(html.isEmpty());
}

void Test_Website_PageBlocFashionHubGrid::test_fashion_hub_grid_matching_article_rendered()
{
    Fixture f;
    f.addArticle(QStringLiteral("/burgundy-dress"), {QStringLiteral("Burgundy")},
                QStringLiteral("Burgundy Dress Ideas"));
    f.engine.setAvailablePages({{QStringLiteral("en"), {QStringLiteral("/burgundy-dress")}}});
    f.loadHub(QStringLiteral("fashion_color"), QStringLiteral("Burgundy"));

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    f.bloc.addCode(QStringView{}, f.engine, f.enIndex(), html, css, js, cssDone, jsDone);

    QVERIFY(html.contains(QStringLiteral("Burgundy Dress Ideas")));
    QVERIFY(html.contains(QStringLiteral("burgundy-dress")));
    QCOMPARE(f.bloc.lastRenderedCount(), 1);
}

void Test_Website_PageBlocFashionHubGrid::test_fashion_hub_grid_non_matching_tag_excluded()
{
    Fixture f;
    f.addArticle(QStringLiteral("/navy-dress"), {QStringLiteral("Navy")});
    f.engine.setAvailablePages({{QStringLiteral("en"), {QStringLiteral("/navy-dress")}}});
    f.loadHub(QStringLiteral("fashion_color"), QStringLiteral("Burgundy"));

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    f.bloc.addCode(QStringView{}, f.engine, f.enIndex(), html, css, js, cssDone, jsDone);

    QVERIFY(html.isEmpty());
    QCOMPARE(f.bloc.lastRenderedCount(), 0);
}

void Test_Website_PageBlocFashionHubGrid::test_fashion_hub_grid_non_fashion_article_excluded()
{
    Fixture f;
    // A non-Fashion "article" type page happens to carry the same raw key —
    // must not be picked up by a fashion_tag_hub.
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/other"),
                                 QStringLiteral("en"));
    f.repo.saveData(id, {{kColorKey, QStringLiteral("Burgundy")},
                        {QStringLiteral("1_text"), QStringLiteral("body")}});
    f.engine.setAvailablePages({{QStringLiteral("en"), {QStringLiteral("/other")}}});
    f.loadHub(QStringLiteral("fashion_color"), QStringLiteral("Burgundy"));

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    f.bloc.addCode(QStringView{}, f.engine, f.enIndex(), html, css, js, cssDone, jsDone);

    QCOMPARE(f.bloc.lastRenderedCount(), 0);
}

void Test_Website_PageBlocFashionHubGrid::test_fashion_hub_grid_multiple_matching_articles_all_rendered()
{
    Fixture f;
    f.addArticle(QStringLiteral("/a"), {QStringLiteral("Burgundy")}, QStringLiteral("A"));
    f.addArticle(QStringLiteral("/b"), {QStringLiteral("Burgundy")}, QStringLiteral("B"));
    f.engine.setAvailablePages({{QStringLiteral("en"),
                                {QStringLiteral("/a"), QStringLiteral("/b")}}});
    f.loadHub(QStringLiteral("fashion_color"), QStringLiteral("Burgundy"));

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    f.bloc.addCode(QStringView{}, f.engine, f.enIndex(), html, css, js, cssDone, jsDone);

    QCOMPARE(f.bloc.lastRenderedCount(), 2);
}

void Test_Website_PageBlocFashionHubGrid::test_fashion_hub_grid_last_rendered_count_zero_before_addcode()
{
    PageBlocFashionHubGrid bloc;
    QCOMPARE(bloc.lastRenderedCount(), 0);
}

void Test_Website_PageBlocFashionHubGrid::test_fashion_hub_grid_last_rendered_count_matches_matches()
{
    Fixture f;
    f.addArticle(QStringLiteral("/a"), {QStringLiteral("Burgundy")});
    f.addArticle(QStringLiteral("/b"), {QStringLiteral("Navy")});
    f.engine.setAvailablePages({{QStringLiteral("en"),
                                {QStringLiteral("/a"), QStringLiteral("/b")}}});
    f.loadHub(QStringLiteral("fashion_color"), QStringLiteral("Burgundy"));

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    f.bloc.addCode(QStringView{}, f.engine, f.enIndex(), html, css, js, cssDone, jsDone);

    QCOMPARE(f.bloc.lastRenderedCount(), 1); // only /a matches
}

void Test_Website_PageBlocFashionHubGrid::test_fashion_hub_grid_translated_tag_name_empty_when_unset()
{
    PageBlocFashionHubGrid bloc;
    QVERIFY(bloc.translatedTagName(QStringLiteral("fr")).isEmpty());
}

QTEST_MAIN(Test_Website_PageBlocFashionHubGrid)
#include "test_page_bloc_fashion_hub_grid.moc"
