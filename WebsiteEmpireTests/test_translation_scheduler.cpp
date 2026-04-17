#include <QtTest>

#include "test_translation_helpers.h"
#include "website/pages/PageTypeLegal.h"
#include "website/translation/TranslationScheduler.h"
#include "website/translation/TranslationSettings.h"

class Test_TranslationScheduler : public QObject
{
    Q_OBJECT

private slots:
    void test_scheduler_skips_unassessed_pages();
    void test_scheduler_includes_incomplete_translation();
    void test_scheduler_skips_complete_translation();
    void test_scheduler_queues_all_target_langs_per_page();
    void test_scheduler_limit_caps_result();
    void test_scheduler_limit_zero_means_unlimited();
    void test_scheduler_priority_types_come_before_rest();
    void test_scheduler_legal_first_within_priority_tier();
    void test_scheduler_page_with_empty_data_not_queued();
};

void Test_TranslationScheduler::test_scheduler_skips_unassessed_pages()
{
    DbFixture f;
    // Page with empty langCodesToTranslate — must be skipped
    addPageWithText(f.repo, QStringLiteral("article"),
                    QStringLiteral("/a.html"), QStringLiteral("fr"));

    TranslationSettings settings;
    settings.targetLangs = {QStringLiteral("de")};

    const auto &jobs = TranslationScheduler::buildJobs(f.repo, f.categoryTable,
                                                        settings,
                                                        QStringLiteral("fr"));
    QVERIFY(jobs.isEmpty());
}

void Test_TranslationScheduler::test_scheduler_includes_incomplete_translation()
{
    DbFixture f;
    const int id = addPageWithText(f.repo, QStringLiteral("article"),
                                   QStringLiteral("/a.html"), QStringLiteral("fr"));
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("de")});

    TranslationSettings settings;
    settings.targetLangs = {QStringLiteral("de")};

    const auto &jobs = TranslationScheduler::buildJobs(f.repo, f.categoryTable,
                                                        settings,
                                                        QStringLiteral("fr"));
    QCOMPARE(jobs.size(), 1);
    QCOMPARE(jobs.at(0).pageId, id);
    QCOMPARE(jobs.at(0).targetLang, QStringLiteral("de"));
}

void Test_TranslationScheduler::test_scheduler_skips_complete_translation()
{
    DbFixture f;
    const int id = addPageWithText(f.repo, QStringLiteral("article"),
                                   QStringLiteral("/a.html"), QStringLiteral("fr"));
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("de")});
    addTranslation(f.repo, f.categoryTable, id,
                   QStringLiteral("fr"), QStringLiteral("de"),
                   QStringLiteral("Quelltexte"));

    TranslationSettings settings;
    settings.targetLangs = {QStringLiteral("de")};

    const auto &jobs = TranslationScheduler::buildJobs(f.repo, f.categoryTable,
                                                        settings,
                                                        QStringLiteral("fr"));
    // Translation is complete — no jobs queued
    QVERIFY(jobs.isEmpty());
}

void Test_TranslationScheduler::test_scheduler_queues_all_target_langs_per_page()
{
    DbFixture f;
    const int id = addPageWithText(f.repo, QStringLiteral("article"),
                                   QStringLiteral("/a.html"), QStringLiteral("fr"));
    f.repo.setLangCodesToTranslate(
        id, {QStringLiteral("de"), QStringLiteral("it"), QStringLiteral("es")});

    TranslationSettings settings;
    settings.targetLangs = {QStringLiteral("de"), QStringLiteral("it"), QStringLiteral("es")};

    const auto &jobs = TranslationScheduler::buildJobs(f.repo, f.categoryTable,
                                                        settings,
                                                        QStringLiteral("fr"));
    QCOMPARE(jobs.size(), 3);

    QStringList langs;
    for (const auto &j : std::as_const(jobs)) {
        QCOMPARE(j.pageId, id);
        langs.append(j.targetLang);
    }
    langs.sort();
    QCOMPARE(langs, (QStringList{QStringLiteral("de"), QStringLiteral("es"), QStringLiteral("it")}));
}

void Test_TranslationScheduler::test_scheduler_limit_caps_result()
{
    DbFixture f;
    for (int i = 0; i < 5; ++i) {
        const int id = addPageWithText(f.repo, QStringLiteral("article"),
                                       QStringLiteral("/page%1.html").arg(i),
                                       QStringLiteral("fr"));
        f.repo.setLangCodesToTranslate(id, {QStringLiteral("de")});
    }

    TranslationSettings settings;
    settings.targetLangs = {QStringLiteral("de")};
    settings.limitPerRun = 3;

    const auto &jobs = TranslationScheduler::buildJobs(f.repo, f.categoryTable,
                                                        settings,
                                                        QStringLiteral("fr"));
    QCOMPARE(jobs.size(), 3);
}

void Test_TranslationScheduler::test_scheduler_limit_zero_means_unlimited()
{
    DbFixture f;
    for (int i = 0; i < 5; ++i) {
        const int id = addPageWithText(f.repo, QStringLiteral("article"),
                                       QStringLiteral("/page%1.html").arg(i),
                                       QStringLiteral("fr"));
        f.repo.setLangCodesToTranslate(id, {QStringLiteral("de")});
    }

    TranslationSettings settings;
    settings.targetLangs = {QStringLiteral("de")};
    settings.limitPerRun = 0;

    const auto &jobs = TranslationScheduler::buildJobs(f.repo, f.categoryTable,
                                                        settings,
                                                        QStringLiteral("fr"));
    QCOMPARE(jobs.size(), 5);
}

void Test_TranslationScheduler::test_scheduler_priority_types_come_before_rest()
{
    // legal page (rest tier) has lower id; article (priority tier) has higher id.
    // Priority tier must still come first.
    DbFixture f;
    const int legalId = addPageWithText(f.repo, QLatin1String(PageTypeLegal::TYPE_ID),
                                        QStringLiteral("/legal.html"), QStringLiteral("fr"));
    const int articleId = addPageWithText(f.repo, QStringLiteral("article"),
                                          QStringLiteral("/article.html"), QStringLiteral("fr"));
    f.repo.setLangCodesToTranslate(legalId,   {QStringLiteral("de")});
    f.repo.setLangCodesToTranslate(articleId, {QStringLiteral("de")});

    TranslationSettings settings;
    settings.targetLangs       = {QStringLiteral("de")};
    settings.priorityPageTypes = {QStringLiteral("article")}; // article = priority, legal = rest

    const auto &jobs = TranslationScheduler::buildJobs(f.repo, f.categoryTable,
                                                        settings,
                                                        QStringLiteral("fr"));
    QCOMPARE(jobs.size(), 2);
    QCOMPARE(jobs.at(0).pageId, articleId); // priority comes first
    QCOMPARE(jobs.at(1).pageId, legalId);
}

void Test_TranslationScheduler::test_scheduler_legal_first_within_priority_tier()
{
    // Both types in priorityPageTypes; article has lower id, legal has higher id.
    // Legal must be sorted to front within the priority tier.
    DbFixture f;
    const int articleId = addPageWithText(f.repo, QStringLiteral("article"),
                                          QStringLiteral("/article.html"), QStringLiteral("fr"));
    const int legalId = addPageWithText(f.repo, QLatin1String(PageTypeLegal::TYPE_ID),
                                        QStringLiteral("/legal.html"), QStringLiteral("fr"));
    f.repo.setLangCodesToTranslate(articleId, {QStringLiteral("de")});
    f.repo.setLangCodesToTranslate(legalId,   {QStringLiteral("de")});

    TranslationSettings settings;
    settings.targetLangs       = {QStringLiteral("de")};
    settings.priorityPageTypes = {QStringLiteral("article"),
                                  QLatin1String(PageTypeLegal::TYPE_ID)};

    const auto &jobs = TranslationScheduler::buildJobs(f.repo, f.categoryTable,
                                                        settings,
                                                        QStringLiteral("fr"));
    QCOMPARE(jobs.size(), 2);
    QCOMPARE(jobs.at(0).typeId, QLatin1String(PageTypeLegal::TYPE_ID)); // legal first
    QCOMPARE(jobs.at(1).typeId, QStringLiteral("article"));
}

void Test_TranslationScheduler::test_scheduler_page_with_empty_data_not_queued()
{
    DbFixture f;
    // Page created but no data saved — empty data means no translatable fields →
    // isTranslationComplete returns true → scheduler skips it.
    const int id = f.repo.create(QStringLiteral("article"),
                                 QStringLiteral("/a.html"),
                                 QStringLiteral("fr"));
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("de")});

    TranslationSettings settings;
    settings.targetLangs = {QStringLiteral("de")};

    const auto &jobs = TranslationScheduler::buildJobs(f.repo, f.categoryTable,
                                                        settings,
                                                        QStringLiteral("fr"));
    QVERIFY(jobs.isEmpty());
}

QTEST_MAIN(Test_TranslationScheduler)
#include "test_translation_scheduler.moc"
