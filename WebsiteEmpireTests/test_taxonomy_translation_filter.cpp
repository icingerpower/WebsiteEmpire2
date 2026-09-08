#include <QtTest>

#include <QTemporaryDir>

#include "website/EngineArticles.h"
#include "website/EngineArticlesFashion.h"
#include "website/HostTable.h"
#include "website/taxonomy/TaxonomyTranslationFilter.h"

class Test_Website_TaxonomyTranslationFilter : public QObject
{
    Q_OBJECT

private slots:
    void test_taxonomytranslationfilter_health_engine_excludes_symptoms();
    void test_taxonomytranslationfilter_fashion_engine_includes_all_ten();
    void test_taxonomytranslationfilter_intersects_with_all_types();
    void test_taxonomytranslationfilter_null_engine_returns_empty();
};

void Test_Website_TaxonomyTranslationFilter::test_taxonomytranslationfilter_health_engine_excludes_symptoms()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticles engine;
    engine.init(QDir(dir.path()), hostTable);

    const QStringList allTypes = {QStringLiteral("symptoms")};
    const QStringList result = TaxonomyTranslationFilter::filterTranslatable(&engine, allTypes);

    QVERIFY(result.isEmpty());
}

void Test_Website_TaxonomyTranslationFilter::test_taxonomytranslationfilter_fashion_engine_includes_all_ten()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);

    const QStringList allTypes = {
        QStringLiteral("fashion_color"), QStringLiteral("fashion_season"),
        QStringLiteral("fashion_occasion"), QStringLiteral("fashion_material"),
        QStringLiteral("fashion_style"), QStringLiteral("fashion_product_type"),
        QStringLiteral("fashion_demographic"), QStringLiteral("fashion_fit"),
        QStringLiteral("fashion_pattern"), QStringLiteral("fashion_culture")};
    const QStringList result = TaxonomyTranslationFilter::filterTranslatable(&engine, allTypes);

    QCOMPARE(result.size(), 10);
    for (const QString &type : allTypes) {
        QVERIFY(result.contains(type));
    }
}

void Test_Website_TaxonomyTranslationFilter::test_taxonomytranslationfilter_intersects_with_all_types()
{
    // A translatable id the engine declares but that isn't in allTypes (i.e.
    // never actually synced into taxonomy.db) must not appear in the result.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HostTable hostTable(QDir(dir.path()));
    EngineArticlesFashion engine;
    engine.init(QDir(dir.path()), hostTable);

    const QStringList allTypes = {QStringLiteral("fashion_color")}; // only this one was synced
    const QStringList result = TaxonomyTranslationFilter::filterTranslatable(&engine, allTypes);

    QCOMPARE(result, QStringList{QStringLiteral("fashion_color")});
}

void Test_Website_TaxonomyTranslationFilter::test_taxonomytranslationfilter_null_engine_returns_empty()
{
    const QStringList result = TaxonomyTranslationFilter::filterTranslatable(
        nullptr, {QStringLiteral("symptoms")});
    QVERIFY(result.isEmpty());
}

QTEST_MAIN(Test_Website_TaxonomyTranslationFilter)
#include "test_taxonomy_translation_filter.moc"
