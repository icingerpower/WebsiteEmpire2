#include <QtTest>
#include <QTemporaryDir>

#include "website/pages/PageDb.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageRepositoryDb.h"

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

namespace {
struct Fixture {
    QTemporaryDir    dir;
    PageDb           db;
    PageRepositoryDb repo;

    Fixture() : db(QDir(dir.path())), repo(db) {}
};
} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_PageRepository : public QObject
{
    Q_OBJECT

private slots:
    // --- create ---
    void test_pagerepo_create_returns_positive_id();
    void test_pagerepo_create_increments_id();

    // --- findById ---
    void test_pagerepo_findbyid_returns_record();
    void test_pagerepo_findbyid_stores_type_id();
    void test_pagerepo_findbyid_stores_permalink();
    void test_pagerepo_findbyid_stores_lang();
    void test_pagerepo_findbyid_timestamps_non_empty();
    void test_pagerepo_findbyid_unknown_returns_nullopt();

    // --- findAll ---
    void test_pagerepo_findall_empty_initially();
    void test_pagerepo_findall_returns_all_pages();
    void test_pagerepo_findall_ordered_by_id();

    // --- saveData / loadData ---
    void test_pagerepo_savedata_loaddata_roundtrip();
    void test_pagerepo_savedata_replaces_previous_data();
    void test_pagerepo_loaddata_unknown_id_returns_empty();
    void test_pagerepo_savedata_updates_updated_at();

    // --- remove ---
    void test_pagerepo_remove_deletes_page();
    void test_pagerepo_remove_deletes_page_data();
    void test_pagerepo_remove_unknown_id_is_noop();

    // --- updatePermalink ---
    void test_pagerepo_update_permalink_changes_permalink();
    void test_pagerepo_update_permalink_records_history();
    void test_pagerepo_update_permalink_same_value_no_history();
    void test_pagerepo_update_permalink_unknown_id_is_noop();

    // --- permalinkHistory ---
    void test_pagerepo_permalink_history_empty_initially();
    void test_pagerepo_permalink_history_grows_on_each_update();
    void test_pagerepo_permalink_history_chronological_order();

    // --- recordStrategyAttempt / strategyAttempts ---
    void test_pagerepo_strategy_attempts_empty_initially();
    void test_pagerepo_strategy_attempts_records_single_attempt();
    void test_pagerepo_strategy_attempts_appends_in_order();
    void test_pagerepo_strategy_attempts_scoped_to_page();
    void test_pagerepo_strategy_attempts_cascade_delete();

    // --- findGeneratedByTypeId ---
    void test_pagerepo_findgeneratedbytypeid_empty_initially();
    void test_pagerepo_findgeneratedbytypeid_returns_generated_pages();
    void test_pagerepo_findgeneratedbytypeid_excludes_ungenerated();
    void test_pagerepo_findgeneratedbytypeid_filters_by_type_id();
};

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------

void Test_PageRepository::test_pagerepo_create_returns_positive_id()
{
    Fixture f;
    QVERIFY(f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en")) > 0);
}

void Test_PageRepository::test_pagerepo_create_increments_id()
{
    Fixture f;
    const int id1 = f.repo.create(QStringLiteral("article"), QStringLiteral("/a.html"), QStringLiteral("en"));
    const int id2 = f.repo.create(QStringLiteral("article"), QStringLiteral("/b.html"), QStringLiteral("en"));
    QVERIFY(id2 > id1);
}

// ---------------------------------------------------------------------------
// findById
// ---------------------------------------------------------------------------

void Test_PageRepository::test_pagerepo_findbyid_returns_record()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    QVERIFY(f.repo.findById(id).has_value());
}

void Test_PageRepository::test_pagerepo_findbyid_stores_type_id()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    QCOMPARE(f.repo.findById(id)->typeId, QStringLiteral("article"));
}

void Test_PageRepository::test_pagerepo_findbyid_stores_permalink()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/my-page.html"), QStringLiteral("en"));
    QCOMPARE(f.repo.findById(id)->permalink, QStringLiteral("/my-page.html"));
}

void Test_PageRepository::test_pagerepo_findbyid_stores_lang()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("fr"));
    QCOMPARE(f.repo.findById(id)->lang, QStringLiteral("fr"));
}

void Test_PageRepository::test_pagerepo_findbyid_timestamps_non_empty()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    const auto &rec = f.repo.findById(id);
    QVERIFY(!rec->createdAt.isEmpty());
    QVERIFY(!rec->updatedAt.isEmpty());
}

void Test_PageRepository::test_pagerepo_findbyid_unknown_returns_nullopt()
{
    Fixture f;
    QVERIFY(!f.repo.findById(9999).has_value());
}

// ---------------------------------------------------------------------------
// findAll
// ---------------------------------------------------------------------------

void Test_PageRepository::test_pagerepo_findall_empty_initially()
{
    Fixture f;
    QVERIFY(f.repo.findAll().isEmpty());
}

void Test_PageRepository::test_pagerepo_findall_returns_all_pages()
{
    Fixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/a.html"), QStringLiteral("en"));
    f.repo.create(QStringLiteral("article"), QStringLiteral("/b.html"), QStringLiteral("en"));
    QCOMPARE(f.repo.findAll().size(), 2);
}

void Test_PageRepository::test_pagerepo_findall_ordered_by_id()
{
    Fixture f;
    const int id1 = f.repo.create(QStringLiteral("article"), QStringLiteral("/a.html"), QStringLiteral("en"));
    const int id2 = f.repo.create(QStringLiteral("article"), QStringLiteral("/b.html"), QStringLiteral("en"));
    const auto &all = f.repo.findAll();
    QCOMPARE(all.at(0).id, id1);
    QCOMPARE(all.at(1).id, id2);
}

// ---------------------------------------------------------------------------
// saveData / loadData
// ---------------------------------------------------------------------------

void Test_PageRepository::test_pagerepo_savedata_loaddata_roundtrip()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    const QHash<QString, QString> data = {
        {QStringLiteral("1_text"), QStringLiteral("hello world")},
        {QStringLiteral("0_categories"), QStringLiteral("1,2")},
    };
    f.repo.saveData(id, data);
    QCOMPARE(f.repo.loadData(id), data);
}

void Test_PageRepository::test_pagerepo_savedata_replaces_previous_data()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    f.repo.saveData(id, {{QStringLiteral("1_text"), QStringLiteral("old")}});
    const QHash<QString, QString> newData = {{QStringLiteral("1_text"), QStringLiteral("new")}};
    f.repo.saveData(id, newData);
    QCOMPARE(f.repo.loadData(id), newData);
}

void Test_PageRepository::test_pagerepo_loaddata_unknown_id_returns_empty()
{
    Fixture f;
    QVERIFY(f.repo.loadData(9999).isEmpty());
}

void Test_PageRepository::test_pagerepo_savedata_updates_updated_at()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    const QString before = f.repo.findById(id)->updatedAt;
    QVERIFY(!before.isEmpty());
    QTest::qSleep(1100); // ensure timestamp advances
    f.repo.saveData(id, {{QStringLiteral("k"), QStringLiteral("v")}});
    const auto &afterOpt = f.repo.findById(id);
    QVERIFY(afterOpt.has_value());
    const QString after = afterOpt->updatedAt;
    QVERIFY2(after > before,
             qPrintable(QStringLiteral("before=%1 after=%2").arg(before, after)));
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

void Test_PageRepository::test_pagerepo_remove_deletes_page()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    f.repo.remove(id);
    QVERIFY(!f.repo.findById(id).has_value());
}

void Test_PageRepository::test_pagerepo_remove_deletes_page_data()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    f.repo.saveData(id, {{QStringLiteral("k"), QStringLiteral("v")}});
    f.repo.remove(id);
    QVERIFY(f.repo.loadData(id).isEmpty());
}

void Test_PageRepository::test_pagerepo_remove_unknown_id_is_noop()
{
    Fixture f;
    f.repo.remove(9999); // must not throw or crash
    QVERIFY(f.repo.findAll().isEmpty());
}

// ---------------------------------------------------------------------------
// updatePermalink
// ---------------------------------------------------------------------------

void Test_PageRepository::test_pagerepo_update_permalink_changes_permalink()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/old.html"), QStringLiteral("en"));
    f.repo.updatePermalink(id, QStringLiteral("/new.html"));
    QCOMPARE(f.repo.findById(id)->permalink, QStringLiteral("/new.html"));
}

void Test_PageRepository::test_pagerepo_update_permalink_records_history()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/old.html"), QStringLiteral("en"));
    f.repo.updatePermalink(id, QStringLiteral("/new.html"));
    const auto &history = f.repo.permalinkHistory(id);
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.at(0).permalink, QStringLiteral("/old.html"));
}

void Test_PageRepository::test_pagerepo_update_permalink_same_value_no_history()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/same.html"), QStringLiteral("en"));
    f.repo.updatePermalink(id, QStringLiteral("/same.html"));
    QVERIFY(f.repo.permalinkHistory(id).isEmpty());
}

void Test_PageRepository::test_pagerepo_update_permalink_unknown_id_is_noop()
{
    Fixture f;
    f.repo.updatePermalink(9999, QStringLiteral("/x.html")); // must not throw
    QVERIFY(f.repo.findAll().isEmpty());
}

// ---------------------------------------------------------------------------
// permalinkHistory
// ---------------------------------------------------------------------------

void Test_PageRepository::test_pagerepo_permalink_history_empty_initially()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    QVERIFY(f.repo.permalinkHistory(id).isEmpty());
}

void Test_PageRepository::test_pagerepo_permalink_history_grows_on_each_update()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/v1.html"), QStringLiteral("en"));
    f.repo.updatePermalink(id, QStringLiteral("/v2.html"));
    f.repo.updatePermalink(id, QStringLiteral("/v3.html"));
    QCOMPARE(f.repo.permalinkHistory(id).size(), 2);
}

void Test_PageRepository::test_pagerepo_permalink_history_chronological_order()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/v1.html"), QStringLiteral("en"));
    f.repo.updatePermalink(id, QStringLiteral("/v2.html"));
    f.repo.updatePermalink(id, QStringLiteral("/v3.html"));
    const auto &history = f.repo.permalinkHistory(id);
    QCOMPARE(history.at(0).permalink, QStringLiteral("/v1.html"));
    QCOMPARE(history.at(1).permalink, QStringLiteral("/v2.html"));
}

// ---------------------------------------------------------------------------
// recordStrategyAttempt / strategyAttempts
// ---------------------------------------------------------------------------

void Test_PageRepository::test_pagerepo_strategy_attempts_empty_initially()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    QVERIFY(f.repo.strategyAttempts(id).isEmpty());
}

void Test_PageRepository::test_pagerepo_strategy_attempts_records_single_attempt()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    f.repo.recordStrategyAttempt(id, QStringLiteral("strategy-abc"));
    const QStringList attempts = f.repo.strategyAttempts(id);
    QCOMPARE(attempts.size(), 1);
    QCOMPARE(attempts.at(0), QStringLiteral("strategy-abc"));
}

void Test_PageRepository::test_pagerepo_strategy_attempts_appends_in_order()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    f.repo.recordStrategyAttempt(id, QStringLiteral("strategy-1"));
    f.repo.recordStrategyAttempt(id, QStringLiteral("strategy-2"));
    f.repo.recordStrategyAttempt(id, QStringLiteral("strategy-3"));
    const QStringList attempts = f.repo.strategyAttempts(id);
    QCOMPARE(attempts.size(), 3);
    QCOMPARE(attempts.at(0), QStringLiteral("strategy-1"));
    QCOMPARE(attempts.at(1), QStringLiteral("strategy-2"));
    QCOMPARE(attempts.at(2), QStringLiteral("strategy-3"));
}

void Test_PageRepository::test_pagerepo_strategy_attempts_scoped_to_page()
{
    Fixture f;
    const int id1 = f.repo.create(QStringLiteral("article"), QStringLiteral("/a.html"), QStringLiteral("en"));
    const int id2 = f.repo.create(QStringLiteral("article"), QStringLiteral("/b.html"), QStringLiteral("en"));
    f.repo.recordStrategyAttempt(id1, QStringLiteral("strategy-x"));
    QVERIFY(f.repo.strategyAttempts(id2).isEmpty());
    QCOMPARE(f.repo.strategyAttempts(id1).size(), 1);
}

void Test_PageRepository::test_pagerepo_strategy_attempts_cascade_delete()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    f.repo.recordStrategyAttempt(id, QStringLiteral("strategy-abc"));
    f.repo.remove(id);
    QVERIFY(f.repo.strategyAttempts(id).isEmpty());
}

// ---------------------------------------------------------------------------
// findGeneratedByTypeId
// ---------------------------------------------------------------------------

void Test_PageRepository::test_pagerepo_findgeneratedbytypeid_empty_initially()
{
    Fixture f;
    QVERIFY(f.repo.findGeneratedByTypeId(QStringLiteral("article")).isEmpty());
}

void Test_PageRepository::test_pagerepo_findgeneratedbytypeid_returns_generated_pages()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    f.repo.setGeneratedAt(id, QStringLiteral("2024-01-01T00:00:00Z"));
    const QList<PageRecord> results = f.repo.findGeneratedByTypeId(QStringLiteral("article"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.at(0).id, id);
}

void Test_PageRepository::test_pagerepo_findgeneratedbytypeid_excludes_ungenerated()
{
    Fixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    // No setGeneratedAt call — page remains ungenerated
    QVERIFY(f.repo.findGeneratedByTypeId(QStringLiteral("article")).isEmpty());
}

void Test_PageRepository::test_pagerepo_findgeneratedbytypeid_filters_by_type_id()
{
    Fixture f;
    const int id1 = f.repo.create(QStringLiteral("article"), QStringLiteral("/a.html"), QStringLiteral("en"));
    const int id2 = f.repo.create(QStringLiteral("product"), QStringLiteral("/b.html"), QStringLiteral("en"));
    f.repo.setGeneratedAt(id1, QStringLiteral("2024-01-01T00:00:00Z"));
    f.repo.setGeneratedAt(id2, QStringLiteral("2024-01-01T00:00:00Z"));
    const QList<PageRecord> articles = f.repo.findGeneratedByTypeId(QStringLiteral("article"));
    QCOMPARE(articles.size(), 1);
    QCOMPARE(articles.at(0).id, id1);
    const QList<PageRecord> products = f.repo.findGeneratedByTypeId(QStringLiteral("product"));
    QCOMPARE(products.size(), 1);
    QCOMPARE(products.at(0).id, id2);
}

QTEST_MAIN(Test_PageRepository)
#include "test_page_repository.moc"
