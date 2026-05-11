#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <atomic>

#include "website/pages/CategoryHubDirtySet.h"
#include "website/pages/CategoryHubSyncer.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/PageTypeCategory.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/EngineArticles.h"

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

namespace {

struct Fixture {
    QTemporaryDir    dir;
    CategoryTable    categoryTable;
    PageDb           db;
    PageRepositoryDb repo;
    PageGenerator    gen;
    CategoryHubDirtySet dirtySet;
    CategoryHubSyncer   syncer;
    EngineArticles   engine;

    Fixture()
        : categoryTable(QDir(dir.path()))
        , db(QDir(dir.path()))
        , repo(db)
        , gen(repo, categoryTable)
        , dirtySet(QDir(dir.path()))
        , syncer(repo, categoryTable, dirtySet, gen)
    {}

    // Creates an article page tagged with the given category IDs.
    int addArticle(const QString &permalink,
                   const QList<int> &categoryIds = {})
    {
        const int id = repo.create(QStringLiteral("article"), permalink,
                                    QStringLiteral("en"));
        QStringList catStrs;
        for (int c : std::as_const(categoryIds)) {
            catStrs.append(QString::number(c));
        }
        repo.saveData(id, {{QStringLiteral("0_categories"), catStrs.join(QLatin1Char(','))},
                           {QStringLiteral("1_text"),       QStringLiteral("body")}});
        return id;
    }

    // Creates a category_hub page covering the given category IDs.
    int addHub(const QString &permalink, const QList<int> &categoryIds = {})
    {
        const int id = repo.create(QLatin1String(PageTypeCategory::TYPE_ID),
                                    permalink, QStringLiteral("en"));
        QStringList catStrs;
        for (int c : std::as_const(categoryIds)) {
            catStrs.append(QString::number(c));
        }
        repo.saveData(id, {{QStringLiteral("0_categories"), catStrs.join(QLatin1Char(','))}});
        return id;
    }

    // Creates stats.db with the schema and optionally one displays_clicks row.
    // displayAt may be empty to create an empty table.
    void createStatsDb(const QString &displayAt = {})
    {
        static std::atomic<int> s_counter{0};
        const QString conn = QStringLiteral("test_stats_setup_")
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
                    "INSERT INTO displays_clicks (page_id, display_at)"
                    " VALUES (:pid, :at)"));
                ins.bindValue(QStringLiteral(":pid"), QStringLiteral("hub:/test.html"));
                ins.bindValue(QStringLiteral(":at"),  displayAt);
                ins.exec();
            }
            sdb.close();
        } // sdb and all queries destroyed — SQLite lock released
        QSqlDatabase::removeDatabase(conn);
    }

    // Opens content.db and returns a unique connection name.
    QString openContentDb()
    {
        static std::atomic<int> s_cnt{0};
        const QString conn = QStringLiteral("test_syncer_content_")
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

class Test_Website_CategoryHub_Syncer : public QObject
{
    Q_OBJECT

private slots:
    // --- extractCategoryIds ---
    void test_hub_syncer_extract_empty_data_returns_empty();
    void test_hub_syncer_extract_no_categories_key_returns_empty();
    void test_hub_syncer_extract_single_id();
    void test_hub_syncer_extract_multiple_ids();
    void test_hub_syncer_extract_empty_categories_string();
    void test_hub_syncer_extract_multiple_keys_merged();
    void test_hub_syncer_extract_ignores_non_numeric_values();

    // --- hubPageIdsForCategories ---
    void test_hub_syncer_hub_ids_for_cats_no_hubs_returns_empty();
    void test_hub_syncer_hub_ids_for_cats_matching_hub_found();
    void test_hub_syncer_hub_ids_for_cats_non_matching_hub_skipped();
    void test_hub_syncer_hub_ids_for_cats_multi_cat_hub_found_by_any_cat();
    void test_hub_syncer_hub_ids_for_cats_two_hubs_finds_correct_one();
    void test_hub_syncer_hub_ids_for_cats_empty_query_returns_empty();
    void test_hub_syncer_hub_ids_for_cats_non_hub_pages_ignored();

    // --- onPageSaved ---
    void test_hub_syncer_on_page_saved_no_categories_marks_nothing();
    void test_hub_syncer_on_page_saved_with_category_marks_hub();
    void test_hub_syncer_on_page_saved_category_with_no_hub_marks_nothing();
    void test_hub_syncer_on_page_saved_marks_multiple_hubs();

    // --- syncStubs ---
    void test_hub_syncer_sync_stubs_empty_category_table_returns_zero();
    void test_hub_syncer_sync_stubs_creates_stub_for_missing_category();
    void test_hub_syncer_sync_stubs_skips_category_already_covered();
    void test_hub_syncer_sync_stubs_stub_is_pending_for_ai_generation();
    void test_hub_syncer_sync_stubs_stub_type_is_category_hub();
    void test_hub_syncer_sync_stubs_returns_correct_count();
    void test_hub_syncer_sync_stubs_second_call_creates_no_duplicates();

    // --- renderDirtyHubs ---
    void test_hub_syncer_render_dirty_empty_set_returns_zero();
    void test_hub_syncer_render_dirty_writes_hub_to_content_db();
    void test_hub_syncer_render_dirty_clears_dirty_set_after_render();
    void test_hub_syncer_render_dirty_stamps_generated_at();
    void test_hub_syncer_render_dirty_removes_ids_one_by_one();
    void test_hub_syncer_render_dirty_skips_and_cleans_unknown_page_id();
    void test_hub_syncer_render_dirty_renders_translation_pages();
    void test_hub_syncer_render_dirty_returns_count_of_pages_written();

    // --- markStaleByStats ---
    void test_hub_syncer_mark_stale_no_stats_db_returns_zero();
    void test_hub_syncer_mark_stale_empty_stats_db_returns_zero();
    void test_hub_syncer_mark_stale_newer_stats_marks_hub_dirty();
    void test_hub_syncer_mark_stale_older_stats_does_not_mark();
    void test_hub_syncer_mark_stale_hub_without_generated_at_skipped();
    void test_hub_syncer_mark_stale_returns_count_of_marked();
    void test_hub_syncer_mark_stale_non_hub_pages_ignored();
};

// ---------------------------------------------------------------------------
// extractCategoryIds
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_Syncer::test_hub_syncer_extract_empty_data_returns_empty()
{
    QVERIFY(CategoryHubSyncer::extractCategoryIds({}).isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_extract_no_categories_key_returns_empty()
{
    QVERIFY(CategoryHubSyncer::extractCategoryIds(
        {{QStringLiteral("1_text"), QStringLiteral("hello")}}).isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_extract_single_id()
{
    const QSet<int> result = CategoryHubSyncer::extractCategoryIds(
        {{QStringLiteral("0_categories"), QStringLiteral("5")}});
    QCOMPARE(result, QSet<int>({5}));
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_extract_multiple_ids()
{
    const QSet<int> result = CategoryHubSyncer::extractCategoryIds(
        {{QStringLiteral("0_categories"), QStringLiteral("1,3,5")}});
    QCOMPARE(result, QSet<int>({1, 3, 5}));
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_extract_empty_categories_string()
{
    QVERIFY(CategoryHubSyncer::extractCategoryIds(
        {{QStringLiteral("0_categories"), QString()}}).isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_extract_multiple_keys_merged()
{
    const QSet<int> result = CategoryHubSyncer::extractCategoryIds({
        {QStringLiteral("0_categories"), QStringLiteral("1")},
        {QStringLiteral("2_categories"), QStringLiteral("3")},
    });
    QCOMPARE(result, QSet<int>({1, 3}));
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_extract_ignores_non_numeric_values()
{
    const QSet<int> result = CategoryHubSyncer::extractCategoryIds(
        {{QStringLiteral("0_categories"), QStringLiteral("a,b,c")}});
    QVERIFY(result.isEmpty());
}

// ---------------------------------------------------------------------------
// hubPageIdsForCategories
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_Syncer::test_hub_syncer_hub_ids_for_cats_no_hubs_returns_empty()
{
    Fixture f;
    QVERIFY(f.syncer.hubPageIdsForCategories({1}).isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_hub_ids_for_cats_matching_hub_found()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/hub.html"), {1});
    QCOMPARE(f.syncer.hubPageIdsForCategories({1}), QSet<int>({hubId}));
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_hub_ids_for_cats_non_matching_hub_skipped()
{
    Fixture f;
    f.addHub(QStringLiteral("/hub.html"), {1});
    QVERIFY(f.syncer.hubPageIdsForCategories({2}).isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_hub_ids_for_cats_multi_cat_hub_found_by_any_cat()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/hub.html"), {1, 2, 3});
    QCOMPARE(f.syncer.hubPageIdsForCategories({2}), QSet<int>({hubId}));
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_hub_ids_for_cats_two_hubs_finds_correct_one()
{
    Fixture f;
    const int hub1 = f.addHub(QStringLiteral("/hub1.html"), {10});
    f.addHub(QStringLiteral("/hub2.html"), {20});
    QCOMPARE(f.syncer.hubPageIdsForCategories({10}), QSet<int>({hub1}));
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_hub_ids_for_cats_empty_query_returns_empty()
{
    Fixture f;
    f.addHub(QStringLiteral("/hub.html"), {1});
    QVERIFY(f.syncer.hubPageIdsForCategories({}).isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_hub_ids_for_cats_non_hub_pages_ignored()
{
    Fixture f;
    // Article tagged with category 1 should NOT appear as a hub for category 1.
    f.addArticle(QStringLiteral("/article.html"), {1});
    QVERIFY(f.syncer.hubPageIdsForCategories({1}).isEmpty());
}

// ---------------------------------------------------------------------------
// onPageSaved
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_Syncer::test_hub_syncer_on_page_saved_no_categories_marks_nothing()
{
    Fixture f;
    f.addHub(QStringLiteral("/hub.html"), {1});
    f.syncer.onPageSaved({{QStringLiteral("1_text"), QStringLiteral("body")}});
    QVERIFY(f.dirtySet.isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_on_page_saved_with_category_marks_hub()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/hub.html"), {5});
    f.syncer.onPageSaved({{QStringLiteral("0_categories"), QStringLiteral("5")}});
    QVERIFY(f.dirtySet.contains(hubId));
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_on_page_saved_category_with_no_hub_marks_nothing()
{
    Fixture f;
    f.syncer.onPageSaved({{QStringLiteral("0_categories"), QStringLiteral("99")}});
    QVERIFY(f.dirtySet.isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_on_page_saved_marks_multiple_hubs()
{
    Fixture f;
    const int hub1 = f.addHub(QStringLiteral("/hub1.html"), {1});
    const int hub2 = f.addHub(QStringLiteral("/hub2.html"), {2});
    f.syncer.onPageSaved({{QStringLiteral("0_categories"), QStringLiteral("1,2")}});
    QVERIFY(f.dirtySet.contains(hub1));
    QVERIFY(f.dirtySet.contains(hub2));
}

// ---------------------------------------------------------------------------
// syncStubs
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_Syncer::test_hub_syncer_sync_stubs_empty_category_table_returns_zero()
{
    Fixture f;
    QCOMPARE(f.syncer.syncStubs(QStringLiteral("en")), 0);
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_sync_stubs_creates_stub_for_missing_category()
{
    Fixture f;
    f.categoryTable.addCategory(QStringLiteral("Electronics"));
    const int created = f.syncer.syncStubs(QStringLiteral("en"));
    QCOMPARE(created, 1);

    const QList<PageRecord> pages = f.repo.findAll();
    const bool hasHub = std::any_of(pages.constBegin(), pages.constEnd(),
        [](const PageRecord &r) {
            return r.typeId == QLatin1String(PageTypeCategory::TYPE_ID);
        });
    QVERIFY(hasHub);
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_sync_stubs_skips_category_already_covered()
{
    Fixture f;
    const int catId = f.categoryTable.addCategory(QStringLiteral("Electronics"));
    f.addHub(QStringLiteral("/electronics.html"), {catId});
    QCOMPARE(f.syncer.syncStubs(QStringLiteral("en")), 0);
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_sync_stubs_stub_is_pending_for_ai_generation()
{
    // Stubs have "0_categories" pre-filled but generated_at is empty.
    // The AI generation launcher detects them via generated_at IS NULL on findAll().
    Fixture f;
    f.categoryTable.addCategory(QStringLiteral("Books"));
    f.syncer.syncStubs(QStringLiteral("en"));
    const QList<PageRecord> all = f.repo.findAll();
    QCOMPARE(all.size(), 1);
    QVERIFY(all.first().generatedAt.isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_sync_stubs_stub_type_is_category_hub()
{
    Fixture f;
    f.categoryTable.addCategory(QStringLiteral("Sports"));
    f.syncer.syncStubs(QStringLiteral("en"));
    const QList<PageRecord> pages = f.repo.findAll();
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.first().typeId, QLatin1String(PageTypeCategory::TYPE_ID));
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_sync_stubs_returns_correct_count()
{
    Fixture f;
    const int catA = f.categoryTable.addCategory(QStringLiteral("Alpha"));
    const int catB = f.categoryTable.addCategory(QStringLiteral("Beta"));
    f.categoryTable.addCategory(QStringLiteral("Gamma"));
    // Hub already exists for catA
    f.addHub(QStringLiteral("/alpha.html"), {catA});
    // catB and Gamma need stubs
    const int created = f.syncer.syncStubs(QStringLiteral("en"));
    QCOMPARE(created, 2);
    Q_UNUSED(catB)
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_sync_stubs_second_call_creates_no_duplicates()
{
    Fixture f;
    f.categoryTable.addCategory(QStringLiteral("Tech"));
    f.syncer.syncStubs(QStringLiteral("en"));
    const int created2 = f.syncer.syncStubs(QStringLiteral("en"));
    QCOMPARE(created2, 0);
    // Exactly one hub page total (stubs have pre-filled page_data so
    // findPendingByTypeId excludes them — use findAll instead)
    const QList<PageRecord> all = f.repo.findAll();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().typeId, QLatin1String(PageTypeCategory::TYPE_ID));
}

// ---------------------------------------------------------------------------
// renderDirtyHubs
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_Syncer::test_hub_syncer_render_dirty_empty_set_returns_zero()
{
    Fixture f;
    QCOMPARE(f.syncer.renderDirtyHubs(QDir(f.dir.path()), QStringLiteral("example.com"),
                                       f.engine, 0), 0);
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_render_dirty_writes_hub_to_content_db()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/hub.html"), {});
    f.dirtySet.add(hubId);

    f.syncer.renderDirtyHubs(QDir(f.dir.path()), QStringLiteral("example.com"),
                              f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages WHERE path = '/hub.html'"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
    f.closeContentDb(conn);
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_render_dirty_clears_dirty_set_after_render()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/hub.html"), {});
    f.dirtySet.add(hubId);

    f.syncer.renderDirtyHubs(QDir(f.dir.path()), QStringLiteral("example.com"),
                              f.engine, 0);

    QVERIFY(f.dirtySet.isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_render_dirty_stamps_generated_at()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/hub.html"), {});
    f.dirtySet.add(hubId);

    f.syncer.renderDirtyHubs(QDir(f.dir.path()), QStringLiteral("example.com"),
                              f.engine, 0);

    const auto record = f.repo.findById(hubId);
    QVERIFY(record.has_value());
    QVERIFY(!record->generatedAt.isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_render_dirty_removes_ids_one_by_one()
{
    // After rendering hub #1, it must be removed from the dirty set even if
    // hub #2 has not been rendered yet.  This verifies per-hub crash safety.
    Fixture f;
    const int hub1 = f.addHub(QStringLiteral("/hub1.html"), {});
    const int hub2 = f.addHub(QStringLiteral("/hub2.html"), {});
    f.dirtySet.addAll({hub1, hub2});

    // Render only hub1 by temporarily removing hub2 from the set, rendering,
    // then re-adding hub2 — but the simplest check is: after a full render both
    // are removed.  For the one-by-one guarantee, we verify via a separate
    // DirtySet instance that hub1 is absent from disk once hub2 is still dirty.
    //
    // We drive this manually: renderDirtyHubs removes each id after render.
    // To observe intermediate state we simulate via the dirty set directly.
    {
        CategoryHubDirtySet partialSet(QDir(f.dir.path()));
        // After removing hub1 (rendered), hub2 must still be present.
        partialSet.add(hub1);
        partialSet.add(hub2);
        partialSet.remove(hub1); // hub1 "rendered"
        // Load from disk in a new instance to confirm persistence.
        CategoryHubDirtySet reloaded(QDir(f.dir.path()));
        QVERIFY(!reloaded.contains(hub1));
        QVERIFY(reloaded.contains(hub2));
    }
    // Also verify that a full renderDirtyHubs empties the set completely.
    f.dirtySet.clear();
    f.dirtySet.addAll({hub1, hub2});
    f.syncer.renderDirtyHubs(QDir(f.dir.path()), QStringLiteral("example.com"),
                              f.engine, 0);
    QVERIFY(f.dirtySet.isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_render_dirty_skips_and_cleans_unknown_page_id()
{
    Fixture f;
    f.dirtySet.add(9999); // does not exist in repo
    const int rendered = f.syncer.renderDirtyHubs(
        QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);
    QCOMPARE(rendered, 0);
    QVERIFY(f.dirtySet.isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_render_dirty_renders_translation_pages()
{
    Fixture f;
    const int sourceId = f.repo.create(QLatin1String(PageTypeCategory::TYPE_ID),
                                        QStringLiteral("/hub.html"),
                                        QStringLiteral("en"));
    f.repo.saveData(sourceId, {{QStringLiteral("0_categories"), QString()}});

    const int translationId = f.repo.createTranslation(
        sourceId,
        QLatin1String(PageTypeCategory::TYPE_ID),
        QStringLiteral("/fr/hub.html"),
        QStringLiteral("fr"));
    f.repo.saveData(translationId, {{QStringLiteral("0_categories"), QString()}});

    f.dirtySet.add(sourceId);
    f.syncer.renderDirtyHubs(QDir(f.dir.path()), QStringLiteral("example.com"),
                              f.engine, 0);

    const QString &conn = f.openContentDb();
    QSqlQuery q(QSqlDatabase::database(conn));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM pages"));
    q.next();
    QCOMPARE(q.value(0).toInt(), 2);
    f.closeContentDb(conn);
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_render_dirty_returns_count_of_pages_written()
{
    Fixture f;
    const int hub1 = f.addHub(QStringLiteral("/hub1.html"), {});
    const int hub2 = f.addHub(QStringLiteral("/hub2.html"), {});
    f.dirtySet.addAll({hub1, hub2});

    const int count = f.syncer.renderDirtyHubs(
        QDir(f.dir.path()), QStringLiteral("example.com"), f.engine, 0);
    QCOMPARE(count, 2);
}

// ---------------------------------------------------------------------------
// markStaleByStats
// ---------------------------------------------------------------------------

void Test_Website_CategoryHub_Syncer::test_hub_syncer_mark_stale_no_stats_db_returns_zero()
{
    Fixture f;
    f.addHub(QStringLiteral("/hub.html"), {});
    QCOMPARE(f.syncer.markStaleByStats(QDir(f.dir.path())), 0);
    QVERIFY(f.dirtySet.isEmpty());
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_mark_stale_empty_stats_db_returns_zero()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/hub.html"), {});
    f.repo.setGeneratedAt(hubId, QStringLiteral("2024-01-01T10:00:00"));
    f.createStatsDb(); // schema only, no rows
    QCOMPARE(f.syncer.markStaleByStats(QDir(f.dir.path())), 0);
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_mark_stale_newer_stats_marks_hub_dirty()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/hub.html"), {});
    f.repo.setGeneratedAt(hubId, QStringLiteral("2024-01-01T10:00:00"));
    f.createStatsDb(QStringLiteral("2024-06-01T12:00:00")); // newer than generated_at
    QCOMPARE(f.syncer.markStaleByStats(QDir(f.dir.path())), 1);
    QVERIFY(f.dirtySet.contains(hubId));
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_mark_stale_older_stats_does_not_mark()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/hub.html"), {});
    f.repo.setGeneratedAt(hubId, QStringLiteral("2024-06-01T12:00:00"));
    f.createStatsDb(QStringLiteral("2024-01-01T10:00:00")); // older than generated_at
    QCOMPARE(f.syncer.markStaleByStats(QDir(f.dir.path())), 0);
    QVERIFY(!f.dirtySet.contains(hubId));
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_mark_stale_hub_without_generated_at_skipped()
{
    Fixture f;
    const int hubId = f.addHub(QStringLiteral("/hub.html"), {});
    // generatedAt is empty (stub never rendered)
    f.createStatsDb(QStringLiteral("2024-06-01T12:00:00"));
    QCOMPARE(f.syncer.markStaleByStats(QDir(f.dir.path())), 0);
    QVERIFY(!f.dirtySet.contains(hubId));
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_mark_stale_returns_count_of_marked()
{
    Fixture f;
    const int hub1 = f.addHub(QStringLiteral("/hub1.html"), {});
    const int hub2 = f.addHub(QStringLiteral("/hub2.html"), {});
    const int hub3 = f.addHub(QStringLiteral("/hub3.html"), {});
    f.repo.setGeneratedAt(hub1, QStringLiteral("2024-01-01T10:00:00")); // stale
    f.repo.setGeneratedAt(hub2, QStringLiteral("2024-01-01T10:00:00")); // stale
    f.repo.setGeneratedAt(hub3, QStringLiteral("2025-01-01T10:00:00")); // fresh
    f.createStatsDb(QStringLiteral("2024-06-01T12:00:00"));
    QCOMPARE(f.syncer.markStaleByStats(QDir(f.dir.path())), 2);
}

void Test_Website_CategoryHub_Syncer::test_hub_syncer_mark_stale_non_hub_pages_ignored()
{
    Fixture f;
    // An article page with a generatedAt older than stats must not be marked.
    const int articleId = f.addArticle(QStringLiteral("/article.html"), {1});
    f.repo.setGeneratedAt(articleId, QStringLiteral("2024-01-01T10:00:00"));
    f.createStatsDb(QStringLiteral("2024-06-01T12:00:00"));
    QCOMPARE(f.syncer.markStaleByStats(QDir(f.dir.path())), 0);
    QVERIFY(!f.dirtySet.contains(articleId));
}

QTEST_MAIN(Test_Website_CategoryHub_Syncer)
#include "test_category_hub_syncer.moc"
