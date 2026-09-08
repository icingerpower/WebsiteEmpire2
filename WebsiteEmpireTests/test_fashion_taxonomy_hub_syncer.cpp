#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <atomic>

#include "website/pages/FashionHubDirtySet.h"
#include "website/pages/FashionTaxonomyHubSyncer.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/PageTypeFashionTagHub.h"
#include "website/pages/blocs/PageBlocFashionHubGrid.h"
#include "website/pages/blocs/PageBlocFashionTaxonomyLinks.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/taxonomy/TaxonomyDb.h"
#include "website/EngineArticlesFashion.h"
#include "website/HostTable.h"

// ---------------------------------------------------------------------------
// Fixture — mirrors test_category_hub_syncer.cpp's Fixture, adapted for
// dimension/tag_value instead of category ids.
// ---------------------------------------------------------------------------

namespace {

const QString kColor = QStringLiteral("fashion_color");
// extractFashionTags()/PageBlocFashionHubGrid::addCode() match raw page_data
// keys by suffix ("_fashion_color"), since AbstractPageType::save() prefixes
// every bloc's keys with its index — any prefix works for these tests, "7_"
// mirrors PageBlocFashionTaxonomyLinks' real bloc index in PageTypeArticleFashion.
const QString kColorKey  = QStringLiteral("7_") + kColor;
const QString kSeasonKey = QStringLiteral("7_fashion_season");

struct Fixture {
    QTemporaryDir      dir;
    HostTable          hostTable;
    CategoryTable      categoryTable;
    PageDb             db;
    PageRepositoryDb   repo;
    PageGenerator      gen;
    FashionHubDirtySet dirtySet;
    FashionTaxonomyHubSyncer syncer;
    EngineArticlesFashion    engine;

    Fixture()
        : hostTable(QDir(dir.path()))
        , categoryTable(QDir(dir.path()))
        , db(QDir(dir.path()))
        , repo(db)
        , gen(repo, categoryTable)
        , dirtySet(QDir(dir.path()))
        , syncer(repo, QDir(dir.path()), dirtySet, gen)
    {
        engine.init(QDir(dir.path()), hostTable);
    }

    // Creates a Fashion article page tagged with the given color names.
    // PageBlocFashionTaxonomyLinks is bloc index 7 in PageTypeArticleFashion
    // (Category/Text/Social/AutoLink/CategoryLinks/SocialMedia/Meta = 0-6),
    // so its raw storage key is "7_fashion_color" once AbstractPageType::save()
    // prefixes it. extractFashionTags()/PageBlocFashionHubGrid::addCode() match
    // by suffix, so the exact prefix used here only needs to be *a* prefix
    // ending in "_fashion_color" — matching the real bloc index keeps the
    // fixture honest about what production code actually writes.
    int addArticle(const QString &permalink, const QStringList &colors = {})
    {
        const int id = repo.create(QStringLiteral("article_fashion"), permalink,
                                    QStringLiteral("en"));
        repo.saveData(id, {{QStringLiteral("7_") + kColor, colors.join(QLatin1Char(','))},
                           {QStringLiteral("1_text"), QStringLiteral("body")}});
        return id;
    }

    // Creates a fashion_tag_hub page covering (fashion_color, tagValue).
    // PageBlocFashionHubGrid is bloc index 0 in PageTypeFashionTagHub, so its
    // raw storage keys are "0_dimension" / "0_tag_value".
    int addHub(const QString &permalink, const QString &tagValue)
    {
        const int id = repo.create(QLatin1String(PageTypeFashionTagHub::TYPE_ID),
                                   permalink, QStringLiteral("en"));
        repo.saveData(id, {
            {QStringLiteral("0_") + QLatin1String(PageBlocFashionHubGrid::KEY_DIMENSION), kColor},
            {QStringLiteral("0_") + QLatin1String(PageBlocFashionHubGrid::KEY_TAG_VALUE), tagValue},
        });
        return id;
    }

    // Creates stats.db with the schema and optionally one displays_clicks row.
    void createStatsDb(const QString &displayAt = {})
    {
        static std::atomic<int> s_counter{0};
        const QString conn = QStringLiteral("test_fashion_stats_setup_")
                             + QString::number(s_counter.fetch_add(1));
        {
            QSqlDatabase sdb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            sdb.setDatabaseName(QDir(dir.path()).filePath(QStringLiteral("stats.db")));
            sdb.open();
            QSqlQuery q(sdb);
            q.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS displays_clicks ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  page_id TEXT NOT NULL,"
                "  display_at TEXT NOT NULL,"
                "  clicked_at TEXT"
                ")"));
            q.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS page_session ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  page_id TEXT NOT NULL,"
                "  scrolling_percentage INTEGER NOT NULL,"
                "  time_on_page INTEGER NOT NULL,"
                "  is_final_page INTEGER NOT NULL"
                ")"));
            if (!displayAt.isEmpty()) {
                QSqlQuery ins(sdb);
                ins.prepare(QStringLiteral(
                    "INSERT INTO displays_clicks (page_id, display_at) VALUES (:pid, :at)"));
                ins.bindValue(QStringLiteral(":pid"), QStringLiteral("hub:/test.html"));
                ins.bindValue(QStringLiteral(":at"),  displayAt);
                ins.exec();
            }
            sdb.close();
        }
        QSqlDatabase::removeDatabase(conn);
    }

    QString openContentDb()
    {
        static std::atomic<int> s_cnt{0};
        const QString conn = QStringLiteral("test_fashion_syncer_content_")
                             + QString::number(s_cnt.fetch_add(1));
        QSqlDatabase cdb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        cdb.setDatabaseName(QDir(dir.path()).filePath(
            QLatin1String(PageGenerator::FILENAME)));
        cdb.open();
        return conn;
    }

    void closeContentDb(const QString &conn)
    {
        { QSqlDatabase::database(conn).close(); }
        QSqlDatabase::removeDatabase(conn);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_Website_FashionTaxonomyHub_Syncer : public QObject
{
    Q_OBJECT

private slots:
    // --- extractFashionTags ---
    void test_fashion_hub_syncer_extract_empty_data_returns_empty();
    void test_fashion_hub_syncer_extract_no_dimension_keys_returns_empty();
    void test_fashion_hub_syncer_extract_single_tag();
    void test_fashion_hub_syncer_extract_multiple_tags_same_dimension();
    void test_fashion_hub_syncer_extract_multiple_dimensions();
    void test_fashion_hub_syncer_extract_empty_value_string();

    // --- hubPageIdFor ---
    void test_fashion_hub_syncer_hub_id_for_no_hubs_returns_negative();
    void test_fashion_hub_syncer_hub_id_for_matching_hub_found();
    void test_fashion_hub_syncer_hub_id_for_non_matching_tag_not_found();
    void test_fashion_hub_syncer_hub_id_for_non_hub_pages_ignored();

    // --- onPageSaved ---
    void test_fashion_hub_syncer_on_page_saved_no_tags_marks_nothing();
    void test_fashion_hub_syncer_on_page_saved_with_tag_marks_existing_hub();
    void test_fashion_hub_syncer_on_page_saved_new_tag_creates_stub_and_marks_it();
    void test_fashion_hub_syncer_on_page_saved_marks_multiple_hubs();

    // --- syncStubs ---
    void test_fashion_hub_syncer_sync_stubs_empty_vocabulary_returns_zero();
    void test_fashion_hub_syncer_sync_stubs_creates_stub_for_missing_value();
    void test_fashion_hub_syncer_sync_stubs_skips_value_already_covered();
    void test_fashion_hub_syncer_sync_stubs_stub_type_is_fashion_tag_hub();
    void test_fashion_hub_syncer_sync_stubs_stub_is_pending_for_ai_generation();
    void test_fashion_hub_syncer_sync_stubs_returns_correct_count();
    void test_fashion_hub_syncer_sync_stubs_second_call_creates_no_duplicates();
    void test_fashion_hub_syncer_sync_stubs_covers_all_dimensions();

    // --- renderDirtyHubs ---
    void test_fashion_hub_syncer_render_dirty_empty_set_returns_zero();
    void test_fashion_hub_syncer_render_dirty_writes_hub_to_content_db();
    void test_fashion_hub_syncer_render_dirty_clears_dirty_set_after_render();
    void test_fashion_hub_syncer_render_dirty_stamps_generated_at();

    // --- markStaleByStats ---
    void test_fashion_hub_syncer_mark_stale_no_stats_db_returns_zero();
    void test_fashion_hub_syncer_mark_stale_newer_stats_marks_hub_dirty();
    void test_fashion_hub_syncer_mark_stale_older_stats_does_not_mark();
    void test_fashion_hub_syncer_mark_stale_hub_without_generated_at_skipped();
};

// ---------------------------------------------------------------------------
// extractFashionTags
// ---------------------------------------------------------------------------

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_extract_empty_data_returns_empty()
{
    QVERIFY(FashionTaxonomyHubSyncer::extractFashionTags({}).isEmpty());
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_extract_no_dimension_keys_returns_empty()
{
    QVERIFY(FashionTaxonomyHubSyncer::extractFashionTags(
        {{QStringLiteral("1_text"), QStringLiteral("hello")}}).isEmpty());
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_extract_single_tag()
{
    const auto result = FashionTaxonomyHubSyncer::extractFashionTags(
        {{kColorKey, QStringLiteral("Burgundy")}});
    QCOMPARE(result.value(kColor), QStringList{QStringLiteral("Burgundy")});
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_extract_multiple_tags_same_dimension()
{
    const auto result = FashionTaxonomyHubSyncer::extractFashionTags(
        {{kColorKey, QStringLiteral("Burgundy,Navy")}});
    QCOMPARE(result.value(kColor), QStringList({QStringLiteral("Burgundy"), QStringLiteral("Navy")}));
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_extract_multiple_dimensions()
{
    const auto result = FashionTaxonomyHubSyncer::extractFashionTags({
        {kColorKey, QStringLiteral("Burgundy")},
        {kSeasonKey, QStringLiteral("Winter")},
    });
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.value(kColor), QStringList{QStringLiteral("Burgundy")});
    QCOMPARE(result.value(QStringLiteral("fashion_season")), QStringList{QStringLiteral("Winter")});
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_extract_empty_value_string()
{
    QVERIFY(FashionTaxonomyHubSyncer::extractFashionTags({{kColorKey, QString()}}).isEmpty());
}

// ---------------------------------------------------------------------------
// hubPageIdFor
// ---------------------------------------------------------------------------

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_hub_id_for_no_hubs_returns_negative()
{
    Fixture f;
    QVERIFY(f.syncer.hubPageIdFor(kColor, QStringLiteral("Burgundy")) < 0);
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_hub_id_for_matching_hub_found()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    QCOMPARE(f.syncer.hubPageIdFor(kColor, QStringLiteral("Burgundy")), hubId);
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_hub_id_for_non_matching_tag_not_found()
{
    Fixture f;
    f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    QVERIFY(f.syncer.hubPageIdFor(kColor, QStringLiteral("Navy")) < 0);
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_hub_id_for_non_hub_pages_ignored()
{
    Fixture f;
    f.addArticle(QStringLiteral("/article.html"), {QStringLiteral("Burgundy")});
    QVERIFY(f.syncer.hubPageIdFor(kColor, QStringLiteral("Burgundy")) < 0);
}

// ---------------------------------------------------------------------------
// onPageSaved
// ---------------------------------------------------------------------------

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_on_page_saved_no_tags_marks_nothing()
{
    Fixture f;
    f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    f.syncer.onPageSaved({{QStringLiteral("1_text"), QStringLiteral("body")}});
    QVERIFY(f.dirtySet.isEmpty());
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_on_page_saved_with_tag_marks_existing_hub()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    f.syncer.onPageSaved({{kColorKey, QStringLiteral("Burgundy")}});
    QVERIFY(f.dirtySet.contains(hubId));
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_on_page_saved_new_tag_creates_stub_and_marks_it()
{
    Fixture f;
    QCOMPARE(f.repo.findAll().size(), 0);
    f.syncer.onPageSaved({{kColorKey, QStringLiteral("Burgundy")}});

    const QList<PageRecord> all = f.repo.findAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().typeId, QLatin1String(PageTypeFashionTagHub::TYPE_ID));
    QVERIFY(f.dirtySet.contains(all.first().id));
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_on_page_saved_marks_multiple_hubs()
{
    Fixture f;
    const int hub1 = f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    const int hub2 = f.addHub(QStringLiteral("/colors/navy"), QStringLiteral("Navy"));
    f.syncer.onPageSaved({{kColorKey, QStringLiteral("Burgundy,Navy")}});
    QVERIFY(f.dirtySet.contains(hub1));
    QVERIFY(f.dirtySet.contains(hub2));
}

// ---------------------------------------------------------------------------
// syncStubs
// ---------------------------------------------------------------------------

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_sync_stubs_empty_vocabulary_returns_zero()
{
    Fixture f;
    QCOMPARE(f.syncer.syncStubs(QStringLiteral("en")), 0);
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_sync_stubs_creates_stub_for_missing_value()
{
    Fixture f;
    TaxonomyDb(QDir(f.dir.path())).sync(kColor, {QStringLiteral("Burgundy")});
    const int created = f.syncer.syncStubs(QStringLiteral("en"));
    QCOMPARE(created, 1);

    const QList<PageRecord> pages = f.repo.findAll();
    const bool hasHub = std::any_of(pages.constBegin(), pages.constEnd(),
        [](const PageRecord &r) {
            return r.typeId == QLatin1String(PageTypeFashionTagHub::TYPE_ID);
        });
    QVERIFY(hasHub);
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_sync_stubs_skips_value_already_covered()
{
    Fixture f;
    TaxonomyDb(QDir(f.dir.path())).sync(kColor, {QStringLiteral("Burgundy")});
    f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    QCOMPARE(f.syncer.syncStubs(QStringLiteral("en")), 0);
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_sync_stubs_stub_type_is_fashion_tag_hub()
{
    Fixture f;
    TaxonomyDb(QDir(f.dir.path())).sync(kColor, {QStringLiteral("Burgundy")});
    f.syncer.syncStubs(QStringLiteral("en"));
    const QList<PageRecord> pages = f.repo.findAll();
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.first().typeId, QLatin1String(PageTypeFashionTagHub::TYPE_ID));
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_sync_stubs_stub_is_pending_for_ai_generation()
{
    Fixture f;
    TaxonomyDb(QDir(f.dir.path())).sync(kColor, {QStringLiteral("Burgundy")});
    f.syncer.syncStubs(QStringLiteral("en"));
    const QList<PageRecord> all = f.repo.findAll();
    QCOMPARE(all.size(), 1);
    QVERIFY(all.first().generatedAt.isEmpty());
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_sync_stubs_returns_correct_count()
{
    Fixture f;
    TaxonomyDb(QDir(f.dir.path())).sync(kColor,
        {QStringLiteral("Burgundy"), QStringLiteral("Navy"), QStringLiteral("Beige")});
    // Hub already exists for Burgundy.
    f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    const int created = f.syncer.syncStubs(QStringLiteral("en"));
    QCOMPARE(created, 2); // Navy, Beige
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_sync_stubs_second_call_creates_no_duplicates()
{
    Fixture f;
    TaxonomyDb(QDir(f.dir.path())).sync(kColor, {QStringLiteral("Burgundy")});
    f.syncer.syncStubs(QStringLiteral("en"));
    const int created2 = f.syncer.syncStubs(QStringLiteral("en"));
    QCOMPARE(created2, 0);
    QCOMPARE(f.repo.findAll().size(), 1);
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_sync_stubs_covers_all_dimensions()
{
    Fixture f;
    TaxonomyDb taxDb(QDir(f.dir.path()));
    for (const auto &dim : PageBlocFashionTaxonomyLinks::dimensions()) {
        taxDb.sync(dim.taxonomyId, {QStringLiteral("Value1")});
    }
    const int created = f.syncer.syncStubs(QStringLiteral("en"));
    QCOMPARE(created, PageBlocFashionTaxonomyLinks::dimensions().size());
}

// ---------------------------------------------------------------------------
// renderDirtyHubs
// ---------------------------------------------------------------------------

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_render_dirty_empty_set_returns_zero()
{
    Fixture f;
    QCOMPARE(f.syncer.renderDirtyHubs(QDir(f.dir.path()), QStringLiteral("example.com"),
                                       f.engine, 0), 0);
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_render_dirty_writes_hub_to_content_db()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    f.dirtySet.add(hubId);

    f.syncer.renderDirtyHubs(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/colors/burgundy'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
    f.closeContentDb(conn);
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_render_dirty_clears_dirty_set_after_render()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    f.dirtySet.add(hubId);

    f.syncer.renderDirtyHubs(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    QVERIFY(f.dirtySet.isEmpty());
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_render_dirty_stamps_generated_at()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    f.dirtySet.add(hubId);

    f.syncer.renderDirtyHubs(QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);

    const auto record = f.repo.findById(hubId);
    QVERIFY(record.has_value());
    QVERIFY(!record->generatedAt.isEmpty());
}

// ---------------------------------------------------------------------------
// markStaleByStats
// ---------------------------------------------------------------------------

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_mark_stale_no_stats_db_returns_zero()
{
    Fixture f;
    f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    QCOMPARE(f.syncer.markStaleByStats(QDir(f.dir.path())), 0);
    QVERIFY(f.dirtySet.isEmpty());
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_mark_stale_newer_stats_marks_hub_dirty()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    f.repo.setGeneratedAt(hubId, QStringLiteral("2024-01-01T10:00:00"));
    f.createStatsDb(QStringLiteral("2024-06-01T12:00:00"));
    QCOMPARE(f.syncer.markStaleByStats(QDir(f.dir.path())), 1);
    QVERIFY(f.dirtySet.contains(hubId));
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_mark_stale_older_stats_does_not_mark()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    f.repo.setGeneratedAt(hubId, QStringLiteral("2024-06-01T12:00:00"));
    f.createStatsDb(QStringLiteral("2024-01-01T10:00:00"));
    QCOMPARE(f.syncer.markStaleByStats(QDir(f.dir.path())), 0);
    QVERIFY(!f.dirtySet.contains(hubId));
}

void Test_Website_FashionTaxonomyHub_Syncer::test_fashion_hub_syncer_mark_stale_hub_without_generated_at_skipped()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/colors/burgundy"), QStringLiteral("Burgundy"));
    f.createStatsDb(QStringLiteral("2024-06-01T12:00:00"));
    QCOMPARE(f.syncer.markStaleByStats(QDir(f.dir.path())), 0);
    QVERIFY(!f.dirtySet.contains(hubId));
}

QTEST_MAIN(Test_Website_FashionTaxonomyHub_Syncer)
#include "test_fashion_taxonomy_hub_syncer.moc"
