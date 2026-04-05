#include <QtTest>
#include <QTemporaryDir>

#include "website/pages/AbstractPageType.h"
#include "website/pages/PageTypeArticle.h"
#include "website/pages/PageTypeLegal.h"
#include "website/pages/attributes/CategoryTable.h"

class Test_PageTypeRegistry : public QObject
{
    Q_OBJECT

private slots:
    // --- allTypeIds ---
    void test_registry_all_type_ids_not_empty();
    void test_registry_all_type_ids_contains_article();
    void test_registry_all_type_ids_contains_legal();

    // --- createForTypeId ---
    void test_registry_create_article_returns_non_null();
    void test_registry_create_unknown_returns_null();
    void test_registry_create_article_type_id_matches();
    void test_registry_create_article_display_name_matches();
    void test_registry_create_returns_independent_instances();
    void test_registry_create_legal_returns_non_null();
    void test_registry_create_legal_type_id_matches();
    void test_registry_create_legal_display_name_matches();

    // --- PageTypeArticle constants ---
    void test_registry_article_type_id_constant();
    void test_registry_article_display_name_constant();

    // --- PageTypeLegal constants ---
    void test_registry_legal_type_id_constant();
    void test_registry_legal_display_name_constant();

    // --- getTypeId / getDisplayName via instance ---
    void test_registry_article_instance_get_type_id();
    void test_registry_article_instance_get_display_name();
    void test_registry_legal_instance_get_type_id();
    void test_registry_legal_instance_get_display_name();
    void test_registry_legal_type_id_differs_from_article();
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
        QLatin1String(PageTypeArticle::TYPE_ID)));
}

void Test_PageTypeRegistry::test_registry_all_type_ids_contains_legal()
{
    QVERIFY(AbstractPageType::allTypeIds().contains(
        QLatin1String(PageTypeLegal::TYPE_ID)));
}

// ---------------------------------------------------------------------------
// createForTypeId
// ---------------------------------------------------------------------------

void Test_PageTypeRegistry::test_registry_create_article_returns_non_null()
{
    Fixture f;
    const auto &type = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeArticle::TYPE_ID), f.categoryTable);
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
        QLatin1String(PageTypeArticle::TYPE_ID), f.categoryTable);
    QCOMPARE(type->getTypeId(), QLatin1String(PageTypeArticle::TYPE_ID));
}

void Test_PageTypeRegistry::test_registry_create_article_display_name_matches()
{
    Fixture f;
    const auto &type = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeArticle::TYPE_ID), f.categoryTable);
    QCOMPARE(type->getDisplayName(), QLatin1String(PageTypeArticle::DISPLAY_NAME));
}

void Test_PageTypeRegistry::test_registry_create_returns_independent_instances()
{
    Fixture f;
    const auto &t1 = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeArticle::TYPE_ID), f.categoryTable);
    const auto &t2 = AbstractPageType::createForTypeId(
        QLatin1String(PageTypeArticle::TYPE_ID), f.categoryTable);
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

void Test_PageTypeRegistry::test_registry_article_type_id_constant()
{
    QCOMPARE(QLatin1String(PageTypeArticle::TYPE_ID), QStringLiteral("article"));
}

void Test_PageTypeRegistry::test_registry_article_display_name_constant()
{
    QCOMPARE(QLatin1String(PageTypeArticle::DISPLAY_NAME), QStringLiteral("Article"));
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
    const PageTypeArticle article(f.categoryTable);
    QCOMPARE(article.getTypeId(), QStringLiteral("article"));
}

void Test_PageTypeRegistry::test_registry_article_instance_get_display_name()
{
    Fixture f;
    const PageTypeArticle article(f.categoryTable);
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

void Test_PageTypeRegistry::test_registry_legal_type_id_differs_from_article()
{
    QVERIFY(QLatin1String(PageTypeLegal::TYPE_ID) != QLatin1String(PageTypeArticle::TYPE_ID));
}

QTEST_MAIN(Test_PageTypeRegistry)
#include "test_page_type_registry.moc"
