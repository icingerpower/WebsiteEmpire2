#include <QtTest>

#include <QTemporaryDir>

#include "website/AbstractEngine.h"
#include "website/EngineArticlesFashion.h"
#include "website/HostTable.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocSymptomLinks.h"

// =============================================================================
// Test_Website_EngineArticlesFashion
// =============================================================================

class Test_Website_EngineArticlesFashion : public QObject
{
    Q_OBJECT

private slots:
    void test_enginearticlesfashion_get_id_stable();
    void test_enginearticlesfashion_get_name_non_empty();
    void test_enginearticlesfashion_registered_in_all_engines();
    void test_enginearticlesfashion_get_generator_id_is_fashion_taxonomy();
    void test_enginearticlesfashion_variations_non_empty();
    void test_enginearticlesfashion_create_returns_new_instance();
    void test_enginearticlesfashion_get_page_types_empty_before_init();
    void test_enginearticlesfashion_get_page_types_has_exactly_one_entry_after_init();
    void test_enginearticlesfashion_page_type_id_is_article_fashion();
    void test_enginearticlesfashion_page_type_has_no_symptom_links_bloc();
};

void Test_Website_EngineArticlesFashion::test_enginearticlesfashion_get_id_stable()
{
    EngineArticlesFashion engine;
    QCOMPARE(engine.getId(), QStringLiteral("EngineArticlesFashion"));
}

void Test_Website_EngineArticlesFashion::test_enginearticlesfashion_get_name_non_empty()
{
    EngineArticlesFashion engine;
    QVERIFY(!engine.getName().isEmpty());
}

void Test_Website_EngineArticlesFashion::test_enginearticlesfashion_registered_in_all_engines()
{
    QVERIFY(AbstractEngine::ALL_ENGINES().contains(QStringLiteral("EngineArticlesFashion")));
}

void Test_Website_EngineArticlesFashion::test_enginearticlesfashion_get_generator_id_is_fashion_taxonomy()
{
    // Lets DialogAddGeneration's "Source table" picker scope itself to this
    // engine's own combo tables instead of listing every registered
    // generator's tables (Health, Factories, Languages included).
    EngineArticlesFashion engine;
    QCOMPARE(engine.getGeneratorId(), QStringLiteral("fashion_taxonomy"));
}

void Test_Website_EngineArticlesFashion::test_enginearticlesfashion_variations_non_empty()
{
    EngineArticlesFashion engine;
    QVERIFY(!engine.getVariations().isEmpty());
}

void Test_Website_EngineArticlesFashion::test_enginearticlesfashion_create_returns_new_instance()
{
    EngineArticlesFashion engine;
    QScopedPointer<AbstractEngine> created(engine.create());
    QVERIFY(created != nullptr);
    QCOMPARE(created->getId(), engine.getId());
}

void Test_Website_EngineArticlesFashion::test_enginearticlesfashion_get_page_types_empty_before_init()
{
    EngineArticlesFashion engine;
    QVERIFY(engine.getPageTypes().isEmpty());
}

void Test_Website_EngineArticlesFashion::test_enginearticlesfashion_get_page_types_has_exactly_one_entry_after_init()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);
    QCOMPARE(engine.getPageTypes().size(), 1);
}

void Test_Website_EngineArticlesFashion::test_enginearticlesfashion_page_type_id_is_article_fashion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);
    QCOMPARE(engine.getPageTypes().first()->getTypeId(), QStringLiteral("article_fashion"));
}

void Test_Website_EngineArticlesFashion::test_enginearticlesfashion_page_type_has_no_symptom_links_bloc()
{
    // Regression guard: a Fashion site must never carry the Health-only
    // symptom-links bloc — this is the whole point of the vertical split.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);
    for (const auto *bloc : engine.getPageTypes().first()->getPageBlocs()) {
        QVERIFY(dynamic_cast<const PageBlocSymptomLinks *>(bloc) == nullptr);
    }
}

QTEST_MAIN(Test_Website_EngineArticlesFashion)
#include "test_engine_articles_fashion.moc"
