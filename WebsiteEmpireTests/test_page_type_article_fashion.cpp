#include <QtTest>

#include <QTemporaryDir>

#include "website/pages/AbstractPageType.h"
#include "website/pages/PageTypeArticleFashion.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocFashionTaxonomyLinks.h"
#include "website/pages/blocs/PageBlocSymptomLinks.h"

// =============================================================================
// Test_Website_PageTypeArticleFashion
// =============================================================================

class Test_Website_PageTypeArticleFashion : public QObject
{
    Q_OBJECT

private slots:
    void test_articlefashion_get_type_id();
    void test_articlefashion_get_display_name_non_empty();
    void test_articlefashion_has_exactly_eight_blocs();
    void test_articlefashion_no_symptom_links_bloc();
    void test_articlefashion_has_fashion_taxonomy_links_bloc();
    void test_articlefashion_registered_in_all_type_ids();
    void test_articlefashion_create_for_type_id_returns_instance();
    void test_articlefashion_create_for_type_id_matches_type_id();
};

void Test_Website_PageTypeArticleFashion::test_articlefashion_get_type_id()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CategoryTable categoryTable{QDir(dir.path())};
    PageTypeArticleFashion article{categoryTable};
    QCOMPARE(article.getTypeId(), QStringLiteral("article_fashion"));
}

void Test_Website_PageTypeArticleFashion::test_articlefashion_get_display_name_non_empty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CategoryTable categoryTable{QDir(dir.path())};
    PageTypeArticleFashion article{categoryTable};
    QVERIFY(!article.getDisplayName().isEmpty());
}

void Test_Website_PageTypeArticleFashion::test_articlefashion_has_exactly_eight_blocs()
{
    // The 7 generic PageTypeArticleBase blocs plus PageBlocFashionTaxonomyLinks
    // (the Fashion-vertical equivalent of PageTypeArticleHealth's 8th bloc,
    // PageBlocSymptomLinks).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CategoryTable categoryTable{QDir(dir.path())};
    PageTypeArticleFashion article{categoryTable};
    QCOMPARE(article.getPageBlocs().size(), 8);
}

void Test_Website_PageTypeArticleFashion::test_articlefashion_no_symptom_links_bloc()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CategoryTable categoryTable{QDir(dir.path())};
    PageTypeArticleFashion article{categoryTable};
    for (const auto *bloc : article.getPageBlocs()) {
        QVERIFY(dynamic_cast<const PageBlocSymptomLinks *>(bloc) == nullptr);
    }
}

void Test_Website_PageTypeArticleFashion::test_articlefashion_has_fashion_taxonomy_links_bloc()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CategoryTable categoryTable{QDir(dir.path())};
    PageTypeArticleFashion article{categoryTable};
    bool found = false;
    for (const auto *bloc : article.getPageBlocs()) {
        if (dynamic_cast<const PageBlocFashionTaxonomyLinks *>(bloc) != nullptr) {
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

void Test_Website_PageTypeArticleFashion::test_articlefashion_registered_in_all_type_ids()
{
    QVERIFY(AbstractPageType::allTypeIds().contains(QStringLiteral("article_fashion")));
}

void Test_Website_PageTypeArticleFashion::test_articlefashion_create_for_type_id_returns_instance()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CategoryTable categoryTable{QDir(dir.path())};
    auto created = AbstractPageType::createForTypeId(QStringLiteral("article_fashion"), categoryTable);
    QVERIFY(created != nullptr);
}

void Test_Website_PageTypeArticleFashion::test_articlefashion_create_for_type_id_matches_type_id()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CategoryTable categoryTable{QDir(dir.path())};
    auto created = AbstractPageType::createForTypeId(QStringLiteral("article_fashion"), categoryTable);
    QVERIFY(created != nullptr);
    QCOMPARE(created->getTypeId(), QStringLiteral("article_fashion"));
}

QTEST_MAIN(Test_Website_PageTypeArticleFashion)
#include "test_page_type_article_fashion.moc"
