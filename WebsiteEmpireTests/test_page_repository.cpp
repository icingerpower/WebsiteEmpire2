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

    // Creates a source page with non-empty 1_text — makes it eligible for findPagesForUpdate.
    int createWithContent(const QString &typeId, const QString &permalink) {
        const int id = repo.create(typeId, permalink, QStringLiteral("en"));
        repo.saveData(id, {{QStringLiteral("1_text"), QStringLiteral("body text")}});
        return id;
    }
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
    void test_pagerepo_savedata_replaces_previous();
    void test_pagerepo_loaddata_unknown_id_returns_empty();
    void test_pagerepo_savedata_updates_updated_at();

    // --- remove ---
    void test_pagerepo_remove_deletes_page();
    void test_pagerepo_remove_deletes_page_rows();
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

    // --- recordUpdateAttempt ---
    void test_pagerepo_record_update_attempt_scoped_to_page();
    void test_pagerepo_record_update_attempt_cascade_delete();

    // --- findPagesForUpdate ---
    void test_pagerepo_findpagesforupdate_empty_without_1_text();
    void test_pagerepo_findpagesforupdate_finds_page_with_1_text();
    void test_pagerepo_findpagesforupdate_filters_by_type_id();
    void test_pagerepo_findpagesforupdate_excludes_translation_pages();
    void test_pagerepo_findpagesforupdate_limit_caps_results();
    void test_pagerepo_findpagesforupdate_unattempted_pages_come_first();
    void test_pagerepo_findpagesforupdate_order_is_per_prompt_not_global();
    void test_pagerepo_findpagesforupdate_skip_key_excludes_pages();
    void test_pagerepo_findpagesforupdate_skip_key_allows_empty_value();
    void test_pagerepo_findpagesforupdate_no_skip_key_returns_all_with_content();
    void test_pagerepo_findpagesforupdate_skipkey_not_skipped_before_this_prompt_ran();
    void test_pagerepo_findpagesforupdate_skipkey_skipped_after_this_prompt_ran();
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

void Test_PageRepository::test_pagerepo_savedata_replaces_previous()
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

void Test_PageRepository::test_pagerepo_remove_deletes_page_rows()
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

// ---------------------------------------------------------------------------
// recordUpdateAttempt
// ---------------------------------------------------------------------------

void Test_PageRepository::test_pagerepo_record_update_attempt_scoped_to_page()
{
    Fixture f;
    const int id1 = f.repo.create(QStringLiteral("article"), QStringLiteral("/a.html"), QStringLiteral("en"));
    const int id2 = f.repo.create(QStringLiteral("article"), QStringLiteral("/b.html"), QStringLiteral("en"));
    f.repo.recordUpdateAttempt(id1, QStringLiteral("prompt-1"));
    // id2 must have no recorded attempt
    QVERIFY(f.repo.lastUpdateAttemptAt(id2, QStringLiteral("prompt-1")).isEmpty());
    QVERIFY(!f.repo.lastUpdateAttemptAt(id1, QStringLiteral("prompt-1")).isEmpty());
}

void Test_PageRepository::test_pagerepo_record_update_attempt_cascade_delete()
{
    Fixture f;
    const int id = f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    f.repo.recordUpdateAttempt(id, QStringLiteral("prompt-1"));
    f.repo.remove(id);
    QVERIFY(f.repo.lastUpdateAttemptAt(id, QStringLiteral("prompt-1")).isEmpty());
}

// ---------------------------------------------------------------------------
// findPagesForUpdate
// ---------------------------------------------------------------------------

void Test_PageRepository::test_pagerepo_findpagesforupdate_empty_without_1_text()
{
    Fixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/p.html"), QStringLiteral("en"));
    // No 1_text saved — must not be returned
    QVERIFY(f.repo.findPagesForUpdate(QStringLiteral("article"), QStringLiteral("prompt-1"), -1, {}).isEmpty());
}

void Test_PageRepository::test_pagerepo_findpagesforupdate_finds_page_with_1_text()
{
    Fixture f;
    const int id = f.createWithContent(QStringLiteral("article"), QStringLiteral("/p.html"));
    const auto pages = f.repo.findPagesForUpdate(QStringLiteral("article"), QStringLiteral("prompt-1"), -1, {});
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.at(0).id, id);
}

void Test_PageRepository::test_pagerepo_findpagesforupdate_filters_by_type_id()
{
    Fixture f;
    const int articleId = f.createWithContent(QStringLiteral("article"), QStringLiteral("/a.html"));
    f.createWithContent(QStringLiteral("product"), QStringLiteral("/b.html"));
    const auto pages = f.repo.findPagesForUpdate(QStringLiteral("article"), QStringLiteral("prompt-1"), -1, {});
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.at(0).id, articleId);
}

void Test_PageRepository::test_pagerepo_findpagesforupdate_excludes_translation_pages()
{
    Fixture f;
    const int srcId = f.createWithContent(QStringLiteral("article"), QStringLiteral("/en/p.html"));
    // Create a translation (source_page_id set)
    const int trId = f.repo.createTranslation(srcId, QStringLiteral("article"), QStringLiteral("/fr/p.html"), QStringLiteral("fr"));
    f.repo.saveData(trId, {{QStringLiteral("1_text"), QStringLiteral("translated content")}});
    const auto pages = f.repo.findPagesForUpdate(QStringLiteral("article"), QStringLiteral("prompt-1"), -1, {});
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.at(0).id, srcId);
}

void Test_PageRepository::test_pagerepo_findpagesforupdate_limit_caps_results()
{
    Fixture f;
    f.createWithContent(QStringLiteral("article"), QStringLiteral("/a.html"));
    f.createWithContent(QStringLiteral("article"), QStringLiteral("/b.html"));
    f.createWithContent(QStringLiteral("article"), QStringLiteral("/c.html"));
    const auto pages = f.repo.findPagesForUpdate(QStringLiteral("article"), QStringLiteral("prompt-1"), 2, {});
    QCOMPARE(pages.size(), 2);
}

void Test_PageRepository::test_pagerepo_findpagesforupdate_unattempted_pages_come_first()
{
    Fixture f;
    const int id1 = f.createWithContent(QStringLiteral("article"), QStringLiteral("/a.html"));
    const int id2 = f.createWithContent(QStringLiteral("article"), QStringLiteral("/b.html"));
    // Record an attempt for id1 — id2 (unattempted) must appear first
    f.repo.recordUpdateAttempt(id1, QStringLiteral("prompt-1"));
    const auto pages = f.repo.findPagesForUpdate(QStringLiteral("article"), QStringLiteral("prompt-1"), -1, {});
    QCOMPARE(pages.size(), 2);
    QCOMPARE(pages.at(0).id, id2);
    QCOMPARE(pages.at(1).id, id1);
}

void Test_PageRepository::test_pagerepo_findpagesforupdate_order_is_per_prompt_not_global()
{
    Fixture f;
    const int id1 = f.createWithContent(QStringLiteral("article"), QStringLiteral("/a.html"));
    const int id2 = f.createWithContent(QStringLiteral("article"), QStringLiteral("/b.html"));
    // Record attempt for id1 under prompt-A only
    f.repo.recordUpdateAttempt(id1, QStringLiteral("prompt-A"));
    // For prompt-B: neither page has been attempted → ordered by id → id1 first
    const auto pagesB = f.repo.findPagesForUpdate(QStringLiteral("article"), QStringLiteral("prompt-B"), -1, {});
    QCOMPARE(pagesB.size(), 2);
    QCOMPARE(pagesB.at(0).id, id1);
    // For prompt-A: id1 was attempted → id2 (unattempted) comes first
    const auto pagesA = f.repo.findPagesForUpdate(QStringLiteral("article"), QStringLiteral("prompt-A"), -1, {});
    QCOMPARE(pagesA.size(), 2);
    QCOMPARE(pagesA.at(0).id, id2);
}

void Test_PageRepository::test_pagerepo_findpagesforupdate_skip_key_excludes_pages()
{
    Fixture f;
    const int id1 = f.createWithContent(QStringLiteral("article"), QStringLiteral("/a.html"));
    const int id2 = f.createWithContent(QStringLiteral("article"), QStringLiteral("/b.html"));
    // Set the skip key on id1 only AND record that prompt-1 already ran on id1
    f.repo.saveData(id1, {
        {QStringLiteral("1_text"),       QStringLiteral("body text")},
        {QStringLiteral("0_categories"), QStringLiteral("1,2")},
    });
    f.repo.recordUpdateAttempt(id1, QStringLiteral("prompt-1"));
    const auto pages = f.repo.findPagesForUpdate(
        QStringLiteral("article"), QStringLiteral("prompt-1"), -1, QStringLiteral("0_categories"));
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.at(0).id, id2);
}

void Test_PageRepository::test_pagerepo_findpagesforupdate_skip_key_allows_empty_value()
{
    Fixture f;
    const int id = f.createWithContent(QStringLiteral("article"), QStringLiteral("/a.html"));
    // Save the skip key with an empty value — must NOT be excluded
    f.repo.saveData(id, {
        {QStringLiteral("1_text"),      QStringLiteral("body text")},
        {QStringLiteral("0_categories"), QStringLiteral("")},
    });
    const auto pages = f.repo.findPagesForUpdate(
        QStringLiteral("article"), QStringLiteral("prompt-1"), -1, QStringLiteral("0_categories"));
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.at(0).id, id);
}

void Test_PageRepository::test_pagerepo_findpagesforupdate_no_skip_key_returns_all_with_content()
{
    Fixture f;
    const int id1 = f.createWithContent(QStringLiteral("article"), QStringLiteral("/a.html"));
    const int id2 = f.createWithContent(QStringLiteral("article"), QStringLiteral("/b.html"));
    // Even if some key is set, no skip filter means both pages returned
    f.repo.saveData(id1, {
        {QStringLiteral("1_text"),      QStringLiteral("body text")},
        {QStringLiteral("0_categories"), QStringLiteral("1,2")},
    });
    const auto pages = f.repo.findPagesForUpdate(QStringLiteral("article"), QStringLiteral("prompt-1"), -1, {});
    QCOMPARE(pages.size(), 2);
    // Both ids present
    const QList<int> ids = {pages.at(0).id, pages.at(1).id};
    QVERIFY(ids.contains(id1));
    QVERIFY(ids.contains(id2));
}

void Test_PageRepository::test_pagerepo_findpagesforupdate_skipkey_not_skipped_before_this_prompt_ran()
{
    // Key set by a DIFFERENT prompt must not cause this prompt to skip the page.
    Fixture f;
    const int id = f.createWithContent(QStringLiteral("article"), QStringLiteral("/a.html"));
    f.repo.saveData(id, {
        {QStringLiteral("1_text"),       QStringLiteral("body text")},
        {QStringLiteral("0_categories"), QStringLiteral("1,2")},
    });
    f.repo.recordUpdateAttempt(id, QStringLiteral("prompt-1")); // prompt-1 set the key
    // prompt-2 has never run — must still see the page even though 0_categories is non-empty
    const auto pages = f.repo.findPagesForUpdate(
        QStringLiteral("article"), QStringLiteral("prompt-2"), -1, QStringLiteral("0_categories"));
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.at(0).id, id);
}

void Test_PageRepository::test_pagerepo_findpagesforupdate_skipkey_skipped_after_this_prompt_ran()
{
    // Once THIS prompt has run AND the key is non-empty, the page must be skipped.
    Fixture f;
    const int id = f.createWithContent(QStringLiteral("article"), QStringLiteral("/a.html"));
    f.repo.saveData(id, {
        {QStringLiteral("1_text"),       QStringLiteral("body text")},
        {QStringLiteral("0_categories"), QStringLiteral("1,2")},
    });
    f.repo.recordUpdateAttempt(id, QStringLiteral("prompt-2")); // this prompt ran
    const auto pages = f.repo.findPagesForUpdate(
        QStringLiteral("article"), QStringLiteral("prompt-2"), -1, QStringLiteral("0_categories"));
    QCOMPARE(pages.size(), 0);
}

QTEST_MAIN(Test_PageRepository)
#include "test_page_repository.moc"
