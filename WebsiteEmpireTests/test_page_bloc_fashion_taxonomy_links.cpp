#include <QtTest>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "website/AbstractEngine.h"
#include "website/EngineArticlesFashion.h"
#include "website/HostTable.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocFashionTaxonomyLinks.h"
#include "website/taxonomy/TaxonomyDb.h"

class Test_Website_PageBlocFashionTaxonomyLinks : public QObject
{
    Q_OBJECT

private slots:
    void test_fashiontaxonomylinks_dimensions_has_ten_entries();
    void test_fashiontaxonomylinks_taxonomies_returns_ten_translatable_descriptors();
    void test_fashiontaxonomylinks_taxonomies_ids_match_dimensions();
    void test_fashiontaxonomylinks_engine_exposes_bloc_with_ten_taxonomies();
    void test_fashiontaxonomylinks_load_save_roundtrip_per_dimension();
    void test_fashiontaxonomylinks_save_omits_unset_dimensions_as_empty();
    void test_fashiontaxonomylinks_synctaxonomy_reads_correct_column_per_dimension();
    void test_fashiontaxonomylinks_synctaxonomy_reads_correct_column_for_new_dimensions();
    void test_fashiontaxonomylinks_synctaxonomy_unknown_id_is_noop();
    void test_fashiontaxonomylinks_addcode_noop_when_nothing_selected();
    void test_fashiontaxonomylinks_addcode_noop_when_hub_unavailable();
    void test_fashiontaxonomylinks_addcode_groups_tags_by_dimension_with_labels();
    void test_fashiontaxonomylinks_addcode_two_dimensions_do_not_share_a_group();
    void test_fashiontaxonomylinks_getaikeyclues_constrains_cardinality_and_lists_vocab();
    void test_fashiontaxonomylinks_getaikeyclues_empty_for_unsynced_dimension();
};

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_dimensions_has_ten_entries()
{
    QCOMPARE(PageBlocFashionTaxonomyLinks::dimensions().size(), 10);
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_taxonomies_returns_ten_translatable_descriptors()
{
    PageBlocFashionTaxonomyLinks bloc;
    const QList<TaxonomyDescriptor> descs = bloc.taxonomies();

    QCOMPARE(descs.size(), 10);
    for (const TaxonomyDescriptor &d : descs) {
        QVERIFY2(d.translatable, qPrintable(d.id + QStringLiteral(" must be translatable")));
        QVERIFY(!d.id.isEmpty());
        QVERIFY(!d.displayName.isEmpty());
    }
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_taxonomies_ids_match_dimensions()
{
    PageBlocFashionTaxonomyLinks bloc;
    QSet<QString> ids;
    for (const auto &d : bloc.taxonomies()) {
        ids.insert(d.id);
    }
    QCOMPARE(ids.size(), 10);
    QVERIFY(ids.contains(QStringLiteral("fashion_color")));
    QVERIFY(ids.contains(QStringLiteral("fashion_season")));
    QVERIFY(ids.contains(QStringLiteral("fashion_occasion")));
    QVERIFY(ids.contains(QStringLiteral("fashion_material")));
    QVERIFY(ids.contains(QStringLiteral("fashion_style")));
    QVERIFY(ids.contains(QStringLiteral("fashion_product_type")));
    QVERIFY(ids.contains(QStringLiteral("fashion_demographic")));
    QVERIFY(ids.contains(QStringLiteral("fashion_fit")));
    QVERIFY(ids.contains(QStringLiteral("fashion_pattern")));
    QVERIFY(ids.contains(QStringLiteral("fashion_culture")));
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_engine_exposes_bloc_with_ten_taxonomies()
{
    // Regression guard for the PaneTaxonomies / --translateCommon discovery
    // path: the bloc must actually be reachable through
    // EngineArticlesFashion -> PageTypeArticleFashion -> getPageBlocs().
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);

    int taxonomyCount = 0;
    for (const auto *pageType : engine.getPageTypes()) {
        for (const auto *bloc : pageType->getPageBlocs()) {
            taxonomyCount += bloc->taxonomies().size();
        }
    }
    QCOMPARE(taxonomyCount, 10);
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_load_save_roundtrip_per_dimension()
{
    PageBlocFashionTaxonomyLinks bloc;
    QHash<QString, QString> data;
    data.insert(QStringLiteral("fashion_color"), QStringLiteral("Burgundy,Navy"));
    data.insert(QStringLiteral("fashion_occasion"), QStringLiteral("Wedding Guest"));

    bloc.load(data);

    QHash<QString, QString> saved;
    bloc.save(saved);

    QCOMPARE(saved.value(QStringLiteral("fashion_color")), QStringLiteral("Burgundy,Navy"));
    QCOMPARE(saved.value(QStringLiteral("fashion_occasion")), QStringLiteral("Wedding Guest"));
    QCOMPARE(saved.value(QStringLiteral("fashion_season")), QString());
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_save_omits_unset_dimensions_as_empty()
{
    PageBlocFashionTaxonomyLinks bloc;
    QHash<QString, QString> saved;
    bloc.save(saved);

    // Every dimension gets a key, even with nothing selected — save() must
    // never silently drop a dimension's storage key.
    for (const auto &d : PageBlocFashionTaxonomyLinks::dimensions()) {
        QVERIFY(saved.contains(d.taxonomyId));
        QVERIFY(saved.value(d.taxonomyId).isEmpty());
    }
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_synctaxonomy_reads_correct_column_per_dimension()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sourceDbPath = QDir(dir.path()).filePath(QStringLiteral("fashion_source.db"));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("fashion_test_setup"));
        db.setDatabaseName(sourceDbPath);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE records (fashion_color_name TEXT, fashion_season_name TEXT)")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO records (fashion_color_name, fashion_season_name) VALUES "
            "('Burgundy', 'Autumn'), ('Navy', 'Winter')")));
    }
    QSqlDatabase::removeDatabase(QStringLiteral("fashion_test_setup"));

    PageBlocFashionTaxonomyLinks bloc;
    bloc.syncTaxonomy(QStringLiteral("fashion_color"), sourceDbPath, QDir(dir.path()));
    bloc.syncTaxonomy(QStringLiteral("fashion_season"), sourceDbPath, QDir(dir.path()));

    TaxonomyDb taxDb(QDir(dir.path()));
    const QStringList colors  = taxDb.load(QStringLiteral("fashion_color"));
    const QStringList seasons = taxDb.load(QStringLiteral("fashion_season"));

    QCOMPARE(colors.size(), 2);
    QVERIFY(colors.contains(QStringLiteral("Burgundy")));
    QVERIFY(colors.contains(QStringLiteral("Navy")));

    QCOMPARE(seasons.size(), 2);
    QVERIFY(seasons.contains(QStringLiteral("Autumn")));
    QVERIFY(seasons.contains(QStringLiteral("Winter")));
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_synctaxonomy_reads_correct_column_for_new_dimensions()
{
    // Proves the actual DB column names wired for the 5 dimensions added
    // alongside Product Type — a typo in any sourceColumn would silently
    // sync zero rows instead of failing loudly, so this exercises the real
    // SQL round-trip rather than trusting the static dimensions() literal.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sourceDbPath = QDir(dir.path()).filePath(QStringLiteral("fashion_source_new.db"));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("fashion_test_setup_new"));
        db.setDatabaseName(sourceDbPath);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE records (fashion_product_type_name TEXT, fashion_demographic_name TEXT, "
            "fashion_fit_silhouette_name TEXT, fashion_pattern_name TEXT, fashion_culture_name TEXT)")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO records (fashion_product_type_name, fashion_demographic_name, "
            "fashion_fit_silhouette_name, fashion_pattern_name, fashion_culture_name) VALUES "
            "('Dress', 'Petite', 'A-Line', 'Floral', 'Western/Mainstream')")));
    }
    QSqlDatabase::removeDatabase(QStringLiteral("fashion_test_setup_new"));

    PageBlocFashionTaxonomyLinks bloc;
    bloc.syncTaxonomy(QStringLiteral("fashion_product_type"), sourceDbPath, QDir(dir.path()));
    bloc.syncTaxonomy(QStringLiteral("fashion_demographic"), sourceDbPath, QDir(dir.path()));
    bloc.syncTaxonomy(QStringLiteral("fashion_fit"), sourceDbPath, QDir(dir.path()));
    bloc.syncTaxonomy(QStringLiteral("fashion_pattern"), sourceDbPath, QDir(dir.path()));
    bloc.syncTaxonomy(QStringLiteral("fashion_culture"), sourceDbPath, QDir(dir.path()));

    TaxonomyDb taxDb(QDir(dir.path()));
    QCOMPARE(taxDb.load(QStringLiteral("fashion_product_type")), QStringList{QStringLiteral("Dress")});
    QCOMPARE(taxDb.load(QStringLiteral("fashion_demographic")), QStringList{QStringLiteral("Petite")});
    QCOMPARE(taxDb.load(QStringLiteral("fashion_fit")), QStringList{QStringLiteral("A-Line")});
    QCOMPARE(taxDb.load(QStringLiteral("fashion_pattern")), QStringList{QStringLiteral("Floral")});
    QCOMPARE(taxDb.load(QStringLiteral("fashion_culture")), QStringList{QStringLiteral("Western/Mainstream")});
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_synctaxonomy_unknown_id_is_noop()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PageBlocFashionTaxonomyLinks bloc;
    // Must not crash and must not create any taxonomy row for a bogus id.
    bloc.syncTaxonomy(QStringLiteral("not_a_real_dimension"),
                      QStringLiteral("/does/not/matter.db"), QDir(dir.path()));

    TaxonomyDb taxDb(QDir(dir.path()));
    QCOMPARE(taxDb.count(QStringLiteral("not_a_real_dimension")), 0);
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_addcode_noop_when_nothing_selected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);

    PageBlocFashionTaxonomyLinks bloc;
    bloc.setWorkingDir(QDir(dir.path()));

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDone, jsDone);

    QVERIFY(html.isEmpty());
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_addcode_noop_when_hub_unavailable()
{
    // engine.isPageAvailable() defaults to permissive (true) until
    // setAvailablePages() is called — mirror PageBlocSymptomLinks' own
    // reliance on this guard by explicitly marking every page unavailable,
    // proving a selected tag with no hub page renders nothing (not a dead link).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);
    engine.setAvailablePages({{QString(), QSet<QString>{QStringLiteral("/some-other-page")}}});

    PageBlocFashionTaxonomyLinks bloc;
    bloc.setWorkingDir(QDir(dir.path()));

    QHash<QString, QString> data;
    data.insert(QStringLiteral("fashion_color"), QStringLiteral("Burgundy"));
    bloc.load(data);

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDone, jsDone);

    QVERIFY(html.isEmpty());
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_addcode_groups_tags_by_dimension_with_labels()
{
    // Regression coverage: addCode() used to flatten every dimension's tags
    // into one unlabeled <div>, indistinguishable from each other. Each
    // dimension must now render as its own labelled group.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);
    engine.setAvailablePages({{engine.getLangCode(0), QSet<QString>{
        QStringLiteral("/colors/burgundy"), QStringLiteral("/occasions/wedding-guest")}}});

    PageBlocFashionTaxonomyLinks bloc;
    bloc.setWorkingDir(QDir(dir.path()));
    QHash<QString, QString> data;
    data.insert(QStringLiteral("fashion_color"), QStringLiteral("Burgundy"));
    data.insert(QStringLiteral("fashion_occasion"), QStringLiteral("Wedding Guest"));
    bloc.load(data);

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDone, jsDone);

    QVERIFY2(html.contains(QStringLiteral(">Color:</span>")), qPrintable(html));
    QVERIFY2(html.contains(QStringLiteral(">Occasion:</span>")), qPrintable(html));
    QVERIFY(html.contains(QStringLiteral(">Burgundy</a>")));
    QVERIFY(html.contains(QStringLiteral(">Wedding Guest</a>")));
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_addcode_two_dimensions_do_not_share_a_group()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);
    engine.setAvailablePages({{engine.getLangCode(0), QSet<QString>{
        QStringLiteral("/colors/burgundy"), QStringLiteral("/occasions/wedding-guest")}}});

    PageBlocFashionTaxonomyLinks bloc;
    bloc.setWorkingDir(QDir(dir.path()));
    QHash<QString, QString> data;
    data.insert(QStringLiteral("fashion_color"), QStringLiteral("Burgundy"));
    data.insert(QStringLiteral("fashion_occasion"), QStringLiteral("Wedding Guest"));
    bloc.load(data);

    QString html, css, js;
    QSet<QString> cssDone, jsDone;
    bloc.addCode(QStringView{}, engine, 0, html, css, js, cssDone, jsDone);

    const int groupCount = html.count(QStringLiteral("fashion-taxonomy-links__group"));
    // Each group div contributes one opening-class occurrence (no closing tag
    // repeats the class name), so 2 populated dimensions -> exactly 2 hits.
    QCOMPARE(groupCount, 2);
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_getaikeyclues_constrains_cardinality_and_lists_vocab()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TaxonomyDb(QDir(dir.path())).sync(QStringLiteral("fashion_color"),
                                       {QStringLiteral("Burgundy"), QStringLiteral("Navy")});

    PageBlocFashionTaxonomyLinks bloc;
    bloc.setWorkingDir(QDir(dir.path()));
    const QHash<QString, QString> clues = bloc.getAiKeyClues();

    QVERIFY(clues.contains(QStringLiteral("fashion_color")));
    const QString &hint = clues.value(QStringLiteral("fashion_color"));
    QVERIFY2(hint.contains(QStringLiteral("0-3")), qPrintable(hint));
    QVERIFY2(hint.contains(QStringLiteral("Burgundy")), qPrintable(hint));
    QVERIFY2(hint.contains(QStringLiteral("Navy")), qPrintable(hint));
}

void Test_Website_PageBlocFashionTaxonomyLinks::test_fashiontaxonomylinks_getaikeyclues_empty_for_unsynced_dimension()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PageBlocFashionTaxonomyLinks bloc;
    bloc.setWorkingDir(QDir(dir.path()));

    // No dimension has been synced in this fresh working dir — no hints at all.
    QVERIFY(bloc.getAiKeyClues().isEmpty());
}

QTEST_MAIN(Test_Website_PageBlocFashionTaxonomyLinks)
#include "test_page_bloc_fashion_taxonomy_links.moc"
