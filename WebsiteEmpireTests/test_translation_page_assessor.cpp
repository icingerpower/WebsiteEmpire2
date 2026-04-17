#include <QtTest>

#include "test_translation_helpers.h"
#include "website/translation/PageAssessor.h"

class Test_PageAssessor : public QObject
{
    Q_OBJECT

private slots:
    void test_assessor_sets_langs_on_unassessed_page();
    void test_assessor_returns_count_of_updated_pages();
    void test_assessor_skips_already_assessed_page();
    void test_assessor_skips_translation_pages();
    void test_assessor_returns_zero_when_all_assessed();
    void test_assessor_empty_target_langs_returns_zero();
    void test_assessor_multiple_unassessed_pages_updated();
    void test_assessor_idempotent_second_run_returns_zero();
};

void Test_PageAssessor::test_assessor_sets_langs_on_unassessed_page()
{
    DbFixture f;
    const int id = f.repo.create(QStringLiteral("article"),
                                 QStringLiteral("/a.html"),
                                 QStringLiteral("fr"));
    const QStringList langs = {QStringLiteral("de"), QStringLiteral("it")};
    PageAssessor::assess(f.repo, langs);

    const auto &rec = f.repo.findById(id);
    QVERIFY(rec.has_value());
    QCOMPARE(rec->langCodesToTranslate, langs);
}

void Test_PageAssessor::test_assessor_returns_count_of_updated_pages()
{
    DbFixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/a.html"), QStringLiteral("fr"));
    f.repo.create(QStringLiteral("article"), QStringLiteral("/b.html"), QStringLiteral("fr"));
    const int count = PageAssessor::assess(f.repo, {QStringLiteral("de")});
    QCOMPARE(count, 2);
}

void Test_PageAssessor::test_assessor_skips_already_assessed_page()
{
    DbFixture f;
    const int id = f.repo.create(QStringLiteral("article"),
                                 QStringLiteral("/a.html"),
                                 QStringLiteral("fr"));
    // Pre-assess with one set of langs
    const QStringList firstLangs = {QStringLiteral("de")};
    f.repo.setLangCodesToTranslate(id, firstLangs);

    // Run assessor with different langs — must NOT overwrite
    const int count = PageAssessor::assess(f.repo, {QStringLiteral("it"), QStringLiteral("es")});
    QCOMPARE(count, 0);

    const auto &rec = f.repo.findById(id);
    QCOMPARE(rec->langCodesToTranslate, firstLangs);
}

void Test_PageAssessor::test_assessor_skips_translation_pages()
{
    DbFixture f;
    const int sourceId = f.repo.create(QStringLiteral("article"),
                                       QStringLiteral("/a.html"),
                                       QStringLiteral("fr"));
    // Assess the source first so it has langs set
    f.repo.setLangCodesToTranslate(sourceId, {QStringLiteral("de")});

    // Create a legacy translation page (sourcePageId != 0)
    const int trId = f.repo.createTranslation(sourceId,
                                              QStringLiteral("article"),
                                              QStringLiteral("/a-de.html"),
                                              QStringLiteral("de"));

    // Assessor should skip the translation page entirely
    const int count = PageAssessor::assess(f.repo, {QStringLiteral("it")});
    QCOMPARE(count, 0);

    const auto &trRec = f.repo.findById(trId);
    QVERIFY(trRec->langCodesToTranslate.isEmpty());
}

void Test_PageAssessor::test_assessor_returns_zero_when_all_assessed()
{
    DbFixture f;
    const int id = f.repo.create(QStringLiteral("article"),
                                 QStringLiteral("/a.html"),
                                 QStringLiteral("fr"));
    f.repo.setLangCodesToTranslate(id, {QStringLiteral("de")});
    const int count = PageAssessor::assess(f.repo, {QStringLiteral("de")});
    QCOMPARE(count, 0);
}

void Test_PageAssessor::test_assessor_empty_target_langs_returns_zero()
{
    DbFixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/a.html"), QStringLiteral("fr"));
    const int count = PageAssessor::assess(f.repo, {});
    QCOMPARE(count, 0);
}

void Test_PageAssessor::test_assessor_multiple_unassessed_pages_updated()
{
    DbFixture f;
    const int id1 = f.repo.create(QStringLiteral("article"), QStringLiteral("/a.html"), QStringLiteral("fr"));
    const int id2 = f.repo.create(QStringLiteral("article"), QStringLiteral("/b.html"), QStringLiteral("fr"));
    const int id3 = f.repo.create(QStringLiteral("article"), QStringLiteral("/c.html"), QStringLiteral("fr"));

    // Pre-assess only id2
    f.repo.setLangCodesToTranslate(id2, {QStringLiteral("es")});

    const QStringList langs = {QStringLiteral("de"), QStringLiteral("it")};
    const int count = PageAssessor::assess(f.repo, langs);

    // Only id1 and id3 should be updated
    QCOMPARE(count, 2);
    QCOMPARE(f.repo.findById(id1)->langCodesToTranslate, langs);
    QCOMPARE(f.repo.findById(id2)->langCodesToTranslate, QStringList{QStringLiteral("es")});
    QCOMPARE(f.repo.findById(id3)->langCodesToTranslate, langs);
}

void Test_PageAssessor::test_assessor_idempotent_second_run_returns_zero()
{
    DbFixture f;
    f.repo.create(QStringLiteral("article"), QStringLiteral("/a.html"), QStringLiteral("fr"));
    const QStringList langs = {QStringLiteral("de")};

    PageAssessor::assess(f.repo, langs);
    const int secondRun = PageAssessor::assess(f.repo, langs);

    QCOMPARE(secondRun, 0);
}

QTEST_MAIN(Test_PageAssessor)
#include "test_translation_page_assessor.moc"
