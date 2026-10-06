#include <QtTest>
#include <QTemporaryDir>
#include "website/EngineArticlesFashion.h"
#include "website/EngineArticles.h"
#include "website/HostTable.h"
#include "website/pages/GenPageQueue.h"
#include "website/pages/HubSeoTemplateDb.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/PageTypeFashionTagHub.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/taxonomy/TaxonomyDb.h"
#include "website/taxonomy/TaxonomyPageSettings.h"
#include "website/translation/HubSeoTranslator.h"

class Test_Website_TaxonomyPages : public QObject
{
    Q_OBJECT
private slots:
    void test_taxonomypages_upgrade_existing_published_stub_preserves_identity();
    void test_taxonomypages_generation_keeps_links_and_does_not_repeat();
    void test_taxonomypages_symptom_article_uses_existing_text_bloc();
    void test_taxonomypages_closing_translation_invalidated_after_edit();
    void test_taxonomypages_render_article_closing_before_related_links();
    void test_taxonomypages_translation_jobs_use_configured_source_language();
    void test_taxonomypages_publish_complete_symptom_article_without_members();
};

void Test_Website_TaxonomyPages::test_taxonomypages_upgrade_existing_published_stub_preserves_identity()
{
    QTemporaryDir temp;
    const QDir dir(temp.path());
    PageDb db(dir);
    PageRepositoryDb repo(db);
    TaxonomyDb(dir).sync(QStringLiteral("fashion_color"), {QStringLiteral("Burgundy")});
    TaxonomyDb(dir).sync(QStringLiteral("fashion_season"), {QStringLiteral("Winter")});
    const int id = repo.create(QStringLiteral("fashion_tag_hub"), QStringLiteral("/colors/burgundy"), QStringLiteral("en"));
    repo.saveData(id, {{QStringLiteral("0_dimension"), QStringLiteral("fashion_color")},
                       {QStringLiteral("0_tag_value"), QStringLiteral("Burgundy")}});
    repo.setGenerationState(id, PageGenerationState::Complete);
    repo.setGeneratedAt(id, QStringLiteral("2026-09-01T00:00:00Z"));
    const auto pending = TaxonomyPageSettings(dir).pendingPages(QStringLiteral("fashion_color"), QStringLiteral("en"), repo);
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending.first().id, id);
    QCOMPARE(pending.first().permalink, QStringLiteral("/colors/burgundy"));
    QCOMPARE(pending.first().generationState, PageGenerationState::Pending);
    QCOMPARE(repo.findAll().size(), 1);
    QCOMPARE(repo.loadData(id).value(QStringLiteral("0_tag_value")), QStringLiteral("Burgundy"));
}

void Test_Website_TaxonomyPages::test_taxonomypages_generation_keeps_links_and_does_not_repeat()
{
    QTemporaryDir temp;
    const QDir dir(temp.path());
    PageDb db(dir);
    PageRepositoryDb repo(db);
    CategoryTable categories(dir);
    TaxonomyDb(dir).sync(QStringLiteral("fashion_color"), {QStringLiteral("Burgundy")});
    TaxonomyPageSettings settings(dir);
    const auto pending = settings.pendingPages(QStringLiteral("fashion_color"), QStringLiteral("en"), repo);
    QCOMPARE(pending.size(), 1);
    GenPageQueue queue(QStringLiteral("fashion_tag_hub"), false, pending, categories, {}, {}, dir);
    const QString article = QStringLiteral("[TITLE level=\"1\"]Burgundy[/TITLE]\n\n") + QString(2100, QLatin1Char('a'));
    QVERIFY(queue.processContentAndMetadata(pending.first().id, article,
        QStringLiteral("{\"0_dimension\":\"wrong\",\"0_tag_value\":\"wrong\",\"3_text\":\"wrong\",\"1_facebook_title\":\"Burgundy ideas\"}"), repo));
    const auto data = repo.loadData(pending.first().id);
    QCOMPARE(data.value(QStringLiteral("3_text")), article);
    QCOMPARE(data.value(QStringLiteral("0_dimension")), QStringLiteral("fashion_color"));
    QCOMPARE(data.value(QStringLiteral("0_tag_value")), QStringLiteral("Burgundy"));
    QCOMPARE(data.value(QStringLiteral("1_facebook_title")), QStringLiteral("Burgundy ideas"));
    QVERIFY(settings.pendingPages(QStringLiteral("fashion_color"), QStringLiteral("en"), repo).isEmpty());
}

void Test_Website_TaxonomyPages::test_taxonomypages_symptom_article_uses_existing_text_bloc()
{
    QTemporaryDir temp;
    const QDir dir(temp.path());
    PageDb db(dir);
    PageRepositoryDb repo(db);
    CategoryTable categories(dir);
    TaxonomyDb(dir).sync(QStringLiteral("symptoms"), {QStringLiteral("Headache")});
    const auto pending = TaxonomyPageSettings(dir).pendingPages(QStringLiteral("symptoms"), QStringLiteral("en"), repo);
    QCOMPARE(pending.size(), 1);
    GenPageQueue queue(QStringLiteral("symptom_hub"), false, pending, categories, {}, {}, dir);
    const QString article = QStringLiteral("[TITLE level=\"1\"]Headache[/TITLE]\n\n") + QString(2100, QLatin1Char('b'));
    QVERIFY(queue.processContentAndMetadata(pending.first().id, article, {}, repo));
    const auto data = repo.loadData(pending.first().id);
    QCOMPARE(data.value(QStringLiteral("0_text")), article);
    QVERIFY(!data.contains(QStringLiteral("1_text")));
    QCOMPARE(pending.first().permalink, QStringLiteral("/symptoms/headache"));
}

void Test_Website_TaxonomyPages::test_taxonomypages_closing_translation_invalidated_after_edit()
{
    QTemporaryDir temp;
    const QDir dir(temp.path());
    TaxonomyPageSettings settings(dir);
    const QString source = QStringLiteral("Explore related conditions.");
    settings.setClosingText(QStringLiteral("symptoms"), source);
    HubSeoTemplateDb translations(dir);
    translations.set(QStringLiteral("taxonomy_closing_symptoms"), settings.translationKey(source), QStringLiteral("fr"),
                     QStringLiteral("Explorez les maladies associées."));
    QCOMPARE(settings.translatedClosingText(QStringLiteral("symptoms"), QStringLiteral("fr")),
             QStringLiteral("Explorez les maladies associées."));
    const QString edited = QStringLiteral("Read our complete condition articles.");
    settings.setClosingText(QStringLiteral("symptoms"), edited);
    QCOMPARE(settings.translatedClosingText(QStringLiteral("symptoms"), QStringLiteral("fr")), edited);
    QVERIFY(settings.translationTemplates().value(QStringLiteral("taxonomy_closing_symptoms"))
            .contains(settings.translationKey(edited)));
    settings.setClosingText(QStringLiteral("symptoms"), {});
    QVERIFY(settings.translationTemplates().isEmpty());
}

void Test_Website_TaxonomyPages::test_taxonomypages_render_article_closing_before_related_links()
{
    QTemporaryDir temp;
    const QDir dir(temp.path());
    PageDb db(dir);
    PageRepositoryDb repo(db);
    CategoryTable categories(dir);
    HostTable hosts(dir);
    EngineArticlesFashion engine;
    engine.init(dir, hosts);
    int en = -1;
    for (int row = 0; row < engine.rowCount(); ++row) {
        if (engine.getLangCode(row) == QStringLiteral("en")) {
            en = row;
            break;
        }
    }
    QVERIFY(en >= 0);
    const int member = repo.create(QStringLiteral("article_fashion"), QStringLiteral("/burgundy-dress"), QStringLiteral("en"));
    repo.saveData(member, {{QStringLiteral("7_fashion_color"), QStringLiteral("Burgundy")},
                          {QStringLiteral("1_text"), QStringLiteral("[TITLE level=\"1\"]A Burgundy Dress[/TITLE]\n\nDress ideas")}});
    PageTypeFashionTagHub hub(categories);
    hub.load({{QStringLiteral("0_dimension"), QStringLiteral("fashion_color")},
              {QStringLiteral("0_tag_value"), QStringLiteral("Burgundy")},
              {QStringLiteral("3_text"), QStringLiteral("Full burgundy article")}});
    hub.bindGenerationContext(repo, dir);
    TaxonomyPageSettings(dir).setClosingText(QStringLiteral("fashion_color"), QStringLiteral("Explore related looks."));
    QString html, css, js;
    QSet<QString> cssIds, jsIds;
    hub.addCode({}, engine, en, html, css, js, cssIds, jsIds);
    const int article = html.indexOf(QStringLiteral("Full burgundy article"));
    const int closing = html.indexOf(QStringLiteral("Explore related looks."));
    const int link = html.indexOf(QStringLiteral("/burgundy-dress"));
    QVERIFY(article >= 0);
    QVERIFY(closing > article);
    QVERIFY(link > closing);
}

void Test_Website_TaxonomyPages::test_taxonomypages_translation_jobs_use_configured_source_language()
{
    QTemporaryDir temp;
    const QDir dir(temp.path());
    TaxonomyPageSettings settings(dir);
    settings.setClosingText(QStringLiteral("symptoms"), QStringLiteral("Explorez les maladies associées."));
    HubSeoTranslator translator(dir, nullptr);
    const auto jobs = translator.buildJobs(settings.translationTemplates(), QStringLiteral("fr"),
                                           {QStringLiteral("fr"), QStringLiteral("de")});
    QCOMPARE(jobs.size(), 1);
    QCOMPARE(jobs.first().sourceLang, QStringLiteral("fr"));
    QCOMPARE(jobs.first().targetLang, QStringLiteral("de"));
}

void Test_Website_TaxonomyPages::test_taxonomypages_publish_complete_symptom_article_without_members()
{
    QTemporaryDir temp;
    const QDir dir(temp.path());
    PageDb db(dir);
    PageRepositoryDb repo(db);
    CategoryTable categories(dir);
    HostTable hosts(dir);
    EngineArticles engine;
    engine.init(dir, hosts);
    int en = -1;
    for (int row = 0; row < engine.rowCount(); ++row) {
        if (engine.getLangCode(row) == QStringLiteral("en")) {
            en = row;
            break;
        }
    }
    QVERIFY(en >= 0);
    const int id = repo.create(QStringLiteral("symptom_hub"), QStringLiteral("/symptoms/headache"), QStringLiteral("en"));
    repo.saveData(id, {{QStringLiteral("0_text"), QStringLiteral("[TITLE level=\"1\"]Headache[/TITLE]\n\nComplete article")},
                      {QStringLiteral("_taxonomyArticle"), QStringLiteral("1")}});
    repo.setGenerationState(id, PageGenerationState::Complete);
    TaxonomyPageSettings(dir).setClosingText(QStringLiteral("symptoms"), QStringLiteral("Explore related conditions."));
    PageGenerator generator(repo, categories);
    QCOMPARE(generator.generateAll(dir, QStringLiteral("example.com"), engine, en), 1);
    QVERIFY(engine.isPageAvailable(QStringLiteral("/symptoms/headache"), en));
}

QTEST_MAIN(Test_Website_TaxonomyPages)
#include "test_taxonomy_pages.moc"
