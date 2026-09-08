#include <QtTest>
#include <QTemporaryDir>

#include "website/pages/AbstractPageType.h"
#include "website/pages/PageTypeArticleFashion.h"
#include "website/pages/PageTypeArticleHealth.h"
#include "website/pages/PageTypeCategory.h"
#include "website/pages/PageTypeFashionTagHub.h"
#include "website/pages/PageTypeLegal.h"
#include "website/pages/PageTypeSymptomHub.h"
#include "website/pages/PageTypeSymptomIndex.h"
#include "website/pages/PageTypeTaxonomyIndex.h"
#include "website/pages/attributes/CategoryTable.h"

class Test_PageTypeRegistry : public QObject
{
    Q_OBJECT

private slots:
    // --- allTypeIds ---
    void test_registry_all_type_ids_not_empty();
    void test_registry_all_type_ids_contains_article();
    void test_registry_all_type_ids_contains_legal();
    void test_registry_all_type_ids_contains_category_hub();

    // --- createForTypeId ---
    void test_registry_create_category_hub_returns_non_null();
    void test_registry_create_category_hub_type_id_matches();
    void test_registry_create_article_returns_non_null();
    void test_registry_create_unknown_returns_null();
    void test_registry_create_article_type_id_matches();
    void test_registry_create_article_display_name_matches();
    void test_registry_create_returns_independent_instances();
    void test_registry_create_legal_returns_non_null();
    void test_registry_create_legal_type_id_matches();
    void test_registry_create_legal_display_name_matches();

    // --- PageTypeArticleHealth constants ---
    void test_registry_article_type_id_constant();
    void test_registry_article_display_name_constant();

    // --- PageTypeLegal constants ---
    void test_registry_legal_type_id_constant();
    void test_registry_legal_display_name_constant();

    // --- PageTypeCategory constants ---
    void test_registry_category_hub_type_id_constant();
    void test_registry_category_hub_display_name_constant();

    // --- getTypeId / getDisplayName via instance ---
    void test_registry_article_instance_get_type_id();
    void test_registry_article_instance_get_display_name();
    void test_registry_legal_instance_get_type_id();
    void test_registry_legal_instance_get_display_name();
    void test_registry_legal_type_id_differs_from_article();

    // --- isAutoManagedTypeId ---
    //
    // Regression coverage: a hardcoded literal list of these type ids drifted
    // out of sync (PanePages showing hub/index pages it should have excluded)
    // more than once before AbstractPageType::isAutoManagedTypeId() became the
    // single source of truth PanePages and PageGenerator both consume — see
    // that method's doc comment. fashion_tag_hub specifically is the type id
    // that regressed most recently.
    void test_registry_is_auto_managed_true_for_category_hub();
    void test_registry_is_auto_managed_true_for_symptom_hub();
    void test_registry_is_auto_managed_true_for_symptom_index();
    void test_registry_is_auto_managed_true_for_taxonomy_index();
    void test_registry_is_auto_managed_true_for_fashion_tag_hub();
    void test_registry_is_auto_managed_false_for_article_health();
    void test_registry_is_auto_managed_false_for_article_fashion();
    void test_registry_is_auto_managed_false_for_legal();
    void test_registry_is_auto_managed_false_for_unknown_type_id();
    void test_registry_all_type_ids_have_at_least_one_auto_managed_entry();
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {
struct Fixture {
    QTemporaryDir dir;
    CategoryTable categoryTable;
    Fixture() : categoryTable(QDir(dir.path())) {}
};
} // namespace

// ---------------------------------------------------------------------------
// allTypeIds
// ---------------------------------------------------------------------------

void Test_PageTypeRegistry::test_registry_all_type_ids_not_empty()
{
    QVERIFY(!AbstractPageType::allTypeIds().isEmpty());
}

void Test_PageTypeRegistry::test_registry_all_type_ids_contains_article()
{
    QVERIFY(AbstractPageType::allTypeIds().contains(
        QLatin1String(PageTypeArticleHealth::TYPE_ID)));
}

void Test_PageTypeRegistry::test_registry_all_type_ids_contains_legal()
{
    QVERIFY(AbstractPageType::allTypeIds().contains(
        QLatin1String(PageTypeLegal::TYPE_ID)));
}

void Test_PageTypeRegistry::test_registry_all_type_ids_contains_category_hub()
{
    QVERIFY(AbstractPageType::allTypeIds().contains(
        QLatin1String(PageTypeCategory::TYPE_ID)));
}

// ---------------------------------------------------------------------------
// createForTypeId
// ---------------------------------------------------------------------------

void Test_PageTypeRegistry::test_registry_create_article_returns_non_null()
{
    Fixture f;
    const auto &type = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeArticleHealth::TYPE_ID), f.categoryTable);
    QVERIFY(type != nullptr);
}

void Test_PageTypeRegistry::test_registry_create_unknown_returns_null()
{
    Fixture f;
    const auto &type = AbstractPageType::createForTypeId(
        QStringLiteral("no_such_type"), f.categoryTable);
    QVERIFY(type == nullptr);
}

void Test_PageTypeRegistry::test_registry_create_article_type_id_matches()
{
    Fixture f;
    const auto &type = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeArticleHealth::TYPE_ID), f.categoryTable);
    QCOMPARE(type->getTypeId(), QLatin1String(PageTypeArticleHealth::TYPE_ID));
}

void Test_PageTypeRegistry::test_registry_create_article_display_name_matches()
{
    Fixture f;
    const auto &type = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeArticleHealth::TYPE_ID), f.categoryTable);
    QCOMPARE(type->getDisplayName(), QLatin1String(PageTypeArticleHealth::DISPLAY_NAME));
}

void Test_PageTypeRegistry::test_registry_create_returns_independent_instances()
{
    Fixture f;
    const auto &t1 = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeArticleHealth::TYPE_ID), f.categoryTable);
    const auto &t2 = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeArticleHealth::TYPE_ID), f.categoryTable);
    QVERIFY(t1.get() != t2.get());
}

void Test_PageTypeRegistry::test_registry_create_legal_returns_non_null()
{
    Fixture f;
    const auto &type = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeLegal::TYPE_ID), f.categoryTable);
    QVERIFY(type != nullptr);
}

void Test_PageTypeRegistry::test_registry_create_legal_type_id_matches()
{
    Fixture f;
    const auto &type = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeLegal::TYPE_ID), f.categoryTable);
    QCOMPARE(type->getTypeId(), QLatin1String(PageTypeLegal::TYPE_ID));
}

void Test_PageTypeRegistry::test_registry_create_legal_display_name_matches()
{
    Fixture f;
    const auto &type = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeLegal::TYPE_ID), f.categoryTable);
    QCOMPARE(type->getDisplayName(), QLatin1String(PageTypeLegal::DISPLAY_NAME));
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

void Test_PageTypeRegistry::test_registry_create_category_hub_returns_non_null()
{
    Fixture f;
    QVERIFY(AbstractPageType::createForTypeId(
        QLatin1String(PageTypeCategory::TYPE_ID), f.categoryTable) != nullptr);
}

void Test_PageTypeRegistry::test_registry_create_category_hub_type_id_matches()
{
    Fixture f;
    const auto type = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeCategory::TYPE_ID), f.categoryTable);
    QCOMPARE(type->getTypeId(), QLatin1String(PageTypeCategory::TYPE_ID));
}

void Test_PageTypeRegistry::test_registry_article_type_id_constant()
{
    QCOMPARE(QLatin1String(PageTypeArticleHealth::TYPE_ID), QStringLiteral("article"));
}

void Test_PageTypeRegistry::test_registry_article_display_name_constant()
{
    QCOMPARE(QLatin1String(PageTypeArticleHealth::DISPLAY_NAME), QStringLiteral("Article"));
}

void Test_PageTypeRegistry::test_registry_legal_type_id_constant()
{
    QCOMPARE(QLatin1String(PageTypeLegal::TYPE_ID), QStringLiteral("legal"));
}

void Test_PageTypeRegistry::test_registry_legal_display_name_constant()
{
    QCOMPARE(QLatin1String(PageTypeLegal::DISPLAY_NAME), QStringLiteral("Legal"));
}

// ---------------------------------------------------------------------------
// Instance virtual methods
// ---------------------------------------------------------------------------

void Test_PageTypeRegistry::test_registry_article_instance_get_type_id()
{
    Fixture f;
    const PageTypeArticleHealth article(f.categoryTable);
    QCOMPARE(article.getTypeId(), QStringLiteral("article"));
}

void Test_PageTypeRegistry::test_registry_article_instance_get_display_name()
{
    Fixture f;
    const PageTypeArticleHealth article(f.categoryTable);
    QCOMPARE(article.getDisplayName(), QStringLiteral("Article"));
}

void Test_PageTypeRegistry::test_registry_legal_instance_get_type_id()
{
    Fixture f;
    const PageTypeLegal legal(f.categoryTable);
    QCOMPARE(legal.getTypeId(), QStringLiteral("legal"));
}

void Test_PageTypeRegistry::test_registry_legal_instance_get_display_name()
{
    Fixture f;
    const PageTypeLegal legal(f.categoryTable);
    QCOMPARE(legal.getDisplayName(), QStringLiteral("Legal"));
}

void Test_PageTypeRegistry::test_registry_category_hub_type_id_constant()
{
    QCOMPARE(QLatin1String(PageTypeCategory::TYPE_ID), QStringLiteral("category_hub"));
}

void Test_PageTypeRegistry::test_registry_category_hub_display_name_constant()
{
    QCOMPARE(QLatin1String(PageTypeCategory::DISPLAY_NAME), QStringLiteral("Category Hub"));
}

void Test_PageTypeRegistry::test_registry_legal_type_id_differs_from_article()
{
    QVERIFY(QLatin1String(PageTypeLegal::TYPE_ID) != QLatin1String(PageTypeArticleHealth::TYPE_ID));
}

// ---------------------------------------------------------------------------
// isAutoManagedTypeId
// ---------------------------------------------------------------------------

void Test_PageTypeRegistry::test_registry_is_auto_managed_true_for_category_hub()
{
    QVERIFY(AbstractPageType::isAutoManagedTypeId(QLatin1String(PageTypeCategory::TYPE_ID)));
}

void Test_PageTypeRegistry::test_registry_is_auto_managed_true_for_symptom_hub()
{
    QVERIFY(AbstractPageType::isAutoManagedTypeId(QLatin1String(PageTypeSymptomHub::TYPE_ID)));
}

void Test_PageTypeRegistry::test_registry_is_auto_managed_true_for_symptom_index()
{
    QVERIFY(AbstractPageType::isAutoManagedTypeId(QLatin1String(PageTypeSymptomIndex::TYPE_ID)));
}

void Test_PageTypeRegistry::test_registry_is_auto_managed_true_for_taxonomy_index()
{
    QVERIFY(AbstractPageType::isAutoManagedTypeId(QLatin1String(PageTypeTaxonomyIndex::TYPE_ID)));
}

void Test_PageTypeRegistry::test_registry_is_auto_managed_true_for_fashion_tag_hub()
{
    // The exact regression: fashion_tag_hub pages (e.g. /colors/amethyst) were
    // showing up in PanePages instead of exclusively in PaneGeneratedPages,
    // because it was missing from the old hardcoded exclusion lists.
    QVERIFY(AbstractPageType::isAutoManagedTypeId(QLatin1String(PageTypeFashionTagHub::TYPE_ID)));
}

void Test_PageTypeRegistry::test_registry_is_auto_managed_false_for_article_health()
{
    QVERIFY(!AbstractPageType::isAutoManagedTypeId(QLatin1String(PageTypeArticleHealth::TYPE_ID)));
}

void Test_PageTypeRegistry::test_registry_is_auto_managed_false_for_article_fashion()
{
    QVERIFY(!AbstractPageType::isAutoManagedTypeId(QLatin1String(PageTypeArticleFashion::TYPE_ID)));
}

void Test_PageTypeRegistry::test_registry_is_auto_managed_false_for_legal()
{
    QVERIFY(!AbstractPageType::isAutoManagedTypeId(QLatin1String(PageTypeLegal::TYPE_ID)));
}

void Test_PageTypeRegistry::test_registry_is_auto_managed_false_for_unknown_type_id()
{
    QVERIFY(!AbstractPageType::isAutoManagedTypeId(QStringLiteral("no_such_type")));
}

void Test_PageTypeRegistry::test_registry_all_type_ids_have_at_least_one_auto_managed_entry()
{
    // Guards against isAutoManagedTypeId() silently becoming a permanent
    // "always false" no-op (e.g. a future registry refactor that drops the
    // flag) — at least the 5 known hub/index types must report true.
    int autoManagedCount = 0;
    for (const QString &typeId : AbstractPageType::allTypeIds()) {
        if (AbstractPageType::isAutoManagedTypeId(typeId)) {
            ++autoManagedCount;
        }
    }
    QVERIFY(autoManagedCount >= 5);
}

QTEST_MAIN(Test_PageTypeRegistry)
#include "test_page_type_registry.moc"
