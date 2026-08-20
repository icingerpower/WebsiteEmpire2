#include <QtTest>
#include <QTemporaryDir>

#include "website/pages/AbstractPageType.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/attributes/CategoryTable.h"

// ---------------------------------------------------------------------------
// Regression guard for page-level (non-bloc) page_data keys.
//
// AbstractPageType::load()/save() dispatch strictly on a numeric bloc prefix
// ("0_", "1_", ...).  Keys without one — "__legal_def_id" and, critically,
// "tr:<lang>:_permalink_slug" — are owned by no bloc, so save() never emits them.
// IPageRepository::saveData() replaces the whole page_data row set
// (DELETE + INSERT), so any such key missing from the saved hash is DESTROYED.
//
// Historical bug (2026-08): PageTranslator rebuilt page data with save() and
// carried over only "__"-prefixed keys.  Translating a page into one language
// therefore wiped every previously stored tr:<other-lang>:_permalink_slug, so
// translated URLs across the whole site silently reverted to the English slug —
// only the most recently translated language kept its slugs.
// ---------------------------------------------------------------------------

class Test_Website_PageDataPreservation : public QObject
{
    Q_OBJECT

private slots:
    // --- AbstractPageType::isBlocKey ---
    void test_pagetype_is_bloc_key_true_for_numeric_prefix();
    void test_pagetype_is_bloc_key_true_for_multi_digit_prefix();
    void test_pagetype_is_bloc_key_false_for_double_underscore_key();
    void test_pagetype_is_bloc_key_false_for_permalink_slug_key();
    void test_pagetype_is_bloc_key_false_for_key_without_underscore();

    // --- AbstractPageType::preservePageLevelKeys ---
    void test_pagetype_preserve_copies_permalink_slug_keys();
    void test_pagetype_preserve_copies_double_underscore_keys();
    void test_pagetype_preserve_does_not_copy_bloc_keys();
    void test_pagetype_preserve_keeps_target_bloc_values_untouched();

    // --- full translator-style round-trip against a real repository ---
    void test_translator_roundtrip_keeps_other_language_permalink_slug();
    void test_translator_roundtrip_keeps_legal_def_id();
};

// ---------------------------------------------------------------------------
// isBlocKey
// ---------------------------------------------------------------------------

void Test_Website_PageDataPreservation::test_pagetype_is_bloc_key_true_for_numeric_prefix()
{
    QVERIFY(AbstractPageType::isBlocKey(QStringLiteral("0_categories")));
    QVERIFY(AbstractPageType::isBlocKey(QStringLiteral("1_text")));
    QVERIFY(AbstractPageType::isBlocKey(QStringLiteral("1_tr:fr:text")));
}

void Test_Website_PageDataPreservation::test_pagetype_is_bloc_key_true_for_multi_digit_prefix()
{
    QVERIFY(AbstractPageType::isBlocKey(QStringLiteral("10_text")));
    QVERIFY(AbstractPageType::isBlocKey(QStringLiteral("42_tr:de:text")));
}

void Test_Website_PageDataPreservation::test_pagetype_is_bloc_key_false_for_double_underscore_key()
{
    QVERIFY(!AbstractPageType::isBlocKey(QStringLiteral("__legal_def_id")));
}

void Test_Website_PageDataPreservation::test_pagetype_is_bloc_key_false_for_permalink_slug_key()
{
    // The key that the historical bug destroyed.
    QVERIFY(!AbstractPageType::isBlocKey(QStringLiteral("tr:fr:_permalink_slug")));
    QVERIFY(!AbstractPageType::isBlocKey(QStringLiteral("tr:de:_permalink_slug")));
}

void Test_Website_PageDataPreservation::test_pagetype_is_bloc_key_false_for_key_without_underscore()
{
    QVERIFY(!AbstractPageType::isBlocKey(QStringLiteral("categories")));
    QVERIFY(!AbstractPageType::isBlocKey(QString()));
}

// ---------------------------------------------------------------------------
// preservePageLevelKeys
// ---------------------------------------------------------------------------

void Test_Website_PageDataPreservation::test_pagetype_preserve_copies_permalink_slug_keys()
{
    const QHash<QString, QString> source = {
        {QStringLiteral("tr:de:_permalink_slug"), QStringLiteral("multiple-sklerose-gene")},
        {QStringLiteral("tr:pt:_permalink_slug"), QStringLiteral("esclerose-multipla-genes")},
    };
    QHash<QString, QString> target;
    AbstractPageType::preservePageLevelKeys(source, target);

    QCOMPARE(target.value(QStringLiteral("tr:de:_permalink_slug")),
             QStringLiteral("multiple-sklerose-gene"));
    QCOMPARE(target.value(QStringLiteral("tr:pt:_permalink_slug")),
             QStringLiteral("esclerose-multipla-genes"));
}

void Test_Website_PageDataPreservation::test_pagetype_preserve_copies_double_underscore_keys()
{
    const QHash<QString, QString> source = {
        {QStringLiteral("__legal_def_id"), QStringLiteral("privacy")},
    };
    QHash<QString, QString> target;
    AbstractPageType::preservePageLevelKeys(source, target);

    QCOMPARE(target.value(QStringLiteral("__legal_def_id")), QStringLiteral("privacy"));
}

void Test_Website_PageDataPreservation::test_pagetype_preserve_does_not_copy_bloc_keys()
{
    // Bloc keys are the page type's own output — copying stale ones back would
    // resurrect values the blocs deliberately dropped.
    const QHash<QString, QString> source = {
        {QStringLiteral("1_text"),       QStringLiteral("stale english")},
        {QStringLiteral("0_categories"), QStringLiteral("7")},
    };
    QHash<QString, QString> target;
    AbstractPageType::preservePageLevelKeys(source, target);

    QVERIFY(!target.contains(QStringLiteral("1_text")));
    QVERIFY(!target.contains(QStringLiteral("0_categories")));
    QVERIFY(target.isEmpty());
}

void Test_Website_PageDataPreservation::test_pagetype_preserve_keeps_target_bloc_values_untouched()
{
    const QHash<QString, QString> source = {
        {QStringLiteral("1_text"),                QStringLiteral("stale english")},
        {QStringLiteral("tr:fr:_permalink_slug"), QStringLiteral("mon-article")},
    };
    QHash<QString, QString> target = {
        {QStringLiteral("1_text"), QStringLiteral("fresh translated")},
    };
    AbstractPageType::preservePageLevelKeys(source, target);

    QCOMPARE(target.value(QStringLiteral("1_text")), QStringLiteral("fresh translated"));
    QCOMPARE(target.value(QStringLiteral("tr:fr:_permalink_slug")), QStringLiteral("mon-article"));
}

// ---------------------------------------------------------------------------
// Full round-trip: mirrors exactly what PageTranslator does when it finalises a
// translation — load existing data, hand it to the page type, rebuild via save(),
// preserve page-level keys, write back through the real repository.
//
// Dropping the preservePageLevelKeys() call makes these two tests fail, which is
// precisely the production bug.
// ---------------------------------------------------------------------------

namespace {

// Replays PageTranslator's finalisation for one page and returns the reloaded data.
QHash<QString, QString> translatorRoundTrip(PageRepositoryDb &repo,
                                             CategoryTable    &categoryTable,
                                             int               pageId,
                                             const QString    &newFrenchText)
{
    const QHash<QString, QString> existing = repo.loadData(pageId);

    auto type = AbstractPageType::createForTypeId(QStringLiteral("article"), categoryTable);
    type->load(existing);
    // applyTranslation expects the bloc-qualified field id ("<i>_<blocFieldId>");
    // bloc 1 of an article is the text bloc.
    type->applyTranslation(QStringView{}, QStringLiteral("1_text"),
                           QStringLiteral("fr"), newFrenchText);

    QHash<QString, QString> finalData;
    type->save(finalData);
    AbstractPageType::preservePageLevelKeys(existing, finalData);

    repo.saveData(pageId, finalData);
    return repo.loadData(pageId);
}

} // namespace

void Test_Website_PageDataPreservation::test_translator_roundtrip_keeps_other_language_permalink_slug()
{
    QTemporaryDir    dir;
    CategoryTable    categoryTable{QDir(dir.path())};
    PageDb           db{QDir(dir.path())};
    PageRepositoryDb repo{db};

    const int id = repo.create(QStringLiteral("article"),
                                QStringLiteral("/multiple-sclerosis-genes-biomarkers"),
                                QStringLiteral("en"));
    repo.saveData(id, {
        {QStringLiteral("1_text"),                QStringLiteral("<p>English.</p>")},
        {QStringLiteral("0_categories"),          QString()},
        // German was translated in an earlier run — its slug must survive a
        // later French translation of the very same page.
        {QStringLiteral("tr:de:_permalink_slug"),
         QStringLiteral("multiple-sklerose-gene-und-biomarker")},
    });

    const QHash<QString, QString> reloaded =
        translatorRoundTrip(repo, categoryTable, id, QStringLiteral("<p>Français.</p>"));

    QCOMPARE(reloaded.value(QStringLiteral("tr:de:_permalink_slug")),
             QStringLiteral("multiple-sklerose-gene-und-biomarker"));
    // Sanity: the French translation was actually written.
    QVERIFY(reloaded.contains(QStringLiteral("1_tr:fr:text")));
}

void Test_Website_PageDataPreservation::test_translator_roundtrip_keeps_legal_def_id()
{
    QTemporaryDir    dir;
    CategoryTable    categoryTable{QDir(dir.path())};
    PageDb           db{QDir(dir.path())};
    PageRepositoryDb repo{db};

    const int id = repo.create(QStringLiteral("article"),
                                QStringLiteral("/privacy"),
                                QStringLiteral("en"));
    repo.saveData(id, {
        {QStringLiteral("1_text"),         QStringLiteral("<p>English.</p>")},
        {QStringLiteral("0_categories"),   QString()},
        {QStringLiteral("__legal_def_id"), QStringLiteral("privacy")},
    });

    const QHash<QString, QString> reloaded =
        translatorRoundTrip(repo, categoryTable, id, QStringLiteral("<p>Français.</p>"));

    QCOMPARE(reloaded.value(QStringLiteral("__legal_def_id")), QStringLiteral("privacy"));
}

QTEST_MAIN(Test_Website_PageDataPreservation)
#include "test_page_data_preservation.moc"
