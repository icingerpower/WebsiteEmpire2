#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QTranslator>
#include <QJsonDocument>
#include <QJsonObject>

#include "ExceptionWithTitleText.h"
#include "website/EngineArticles.h"
#include "website/HostTable.h"
#include "website/commonblocs/CommonBlocPageLabels.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/PageTypeSymptomIndex.h"
#include "website/pages/PageTypeTaxonomyIndex.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/PageBlocConditionList.h"
#include "website/taxonomy/TaxonomyDb.h"
#include "website/theme/ThemeDefault.h"
#include "website/translation/CommonBlocTranslator.h"

// An installed desktop catalogue must never supply generated-page labels.
class PageLabelTestTranslator : public QTranslator
{
public:
    bool isEmpty() const override;
    QString translate(const char *, const char *, const char *, int) const override;
};

bool PageLabelTestTranslator::isEmpty() const
{
    return false;
}

QString PageLabelTestTranslator::translate(const char *, const char *, const char *, int) const
{
    return QStringLiteral("QT_CATALOGUE_LEAK");
}

class Test_Website_PageLabels : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_pagelabels_cli_jobs_and_theme_persistence();
    void test_pagelabels_missing_empty_stale_and_placeholder();
    void test_pagelabels_index_titles_and_stored_override();
    void test_pagelabels_condition_list_uses_requested_language();
    void test_pagelabels_jsonld_escapes_translated_title();
private:
    PageLabelTestTranslator m_translator;
};

void Test_Website_PageLabels::initTestCase()
{
    QVERIFY(QCoreApplication::installTranslator(&m_translator));
}

void Test_Website_PageLabels::cleanupTestCase()
{
    QVERIFY(QCoreApplication::removeTranslator(&m_translator));
}

void Test_Website_PageLabels::test_pagelabels_cli_jobs_and_theme_persistence()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir workingDir(dir.path());
    ThemeDefault theme(workingDir);
    const auto blocs = theme.getTranslationBlocs();
    AbstractCommonBloc *labels = nullptr;
    for (auto *bloc : blocs) {
        if (bloc->getId() == QStringLiteral("page_labels")) {
            labels = bloc;
        }
    }
    QVERIFY(labels);
    QCOMPARE(labels->sourceTexts().size(), 5);
    const auto jobs = CommonBlocTranslator::buildJobs({labels}, QStringLiteral("fr"),
                                                    {QStringLiteral("en"), QStringLiteral("fr"), QStringLiteral("es")});
    QCOMPARE(jobs.size(), 2);
    QCOMPARE(jobs.first().sourceLang, QStringLiteral("en"));
    QCOMPARE(jobs.first().targetLang, QStringLiteral("fr"));
    const auto sources = labels->sourceTexts();
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        labels->setTranslation(it.key(), QStringLiteral("es"), QStringLiteral("ES ") + it.value());
    }
    QVERIFY(CommonBlocTranslator::buildJobs({labels}, QStringLiteral("en"), {QStringLiteral("es")}).isEmpty());
    theme.saveBlocsData();
    ThemeDefault restored(workingDir);
    for (auto *bloc : restored.getTranslationBlocs()) {
        if (bloc->getId() == labels->getId()) {
            QCOMPARE(bloc->translatedText(QStringLiteral("all_symptoms"), QStringLiteral("es")),
                     QStringLiteral("ES All symptoms"));
        }
    }
    CommonBlocPageLabels reader;
    reader.load(workingDir);
    QCOMPARE(reader.text(QStringLiteral("all_symptoms"), QStringLiteral("es")), QStringLiteral("ES All symptoms"));
}

void Test_Website_PageLabels::test_pagelabels_missing_empty_stale_and_placeholder()
{
    CommonBlocPageLabels labels;
    QCOMPARE(labels.text(QStringLiteral("all_symptoms"), QStringLiteral("en")), QStringLiteral("All symptoms"));
    QVERIFY_EXCEPTION_THROWN(labels.text(QStringLiteral("all_symptoms"), QStringLiteral("es")), ExceptionWithTitleText);
    labels.setTranslation(QStringLiteral("all_symptoms"), QStringLiteral("es"), QStringLiteral("  "));
    QVERIFY_EXCEPTION_THROWN(labels.text(QStringLiteral("all_symptoms"), QStringLiteral("es")), ExceptionWithTitleText);
    QVERIFY_EXCEPTION_THROWN(labels.setTranslation(QStringLiteral("browse_symptoms_count"), QStringLiteral("es"),
                                                   QStringLiteral("Sin contador")), ExceptionWithTitleText);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    labels.setTranslation(QStringLiteral("all_symptoms"), QStringLiteral("es"), QStringLiteral("Todos los síntomas"));
    labels.save(QDir(dir.path()));
    {
        QSettings settings(dir.filePath(QStringLiteral("page_labels.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("tr_es/all_symptoms_hash"), QStringLiteral("stale"));
    }
    labels.load(QDir(dir.path()));
    QVERIFY_EXCEPTION_THROWN(labels.text(QStringLiteral("all_symptoms"), QStringLiteral("es")), ExceptionWithTitleText);
}

void Test_Website_PageLabels::test_pagelabels_index_titles_and_stored_override()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir workingDir(dir.path());
    CategoryTable categories(workingDir);
    PageDb db(workingDir);
    PageRepositoryDb repo(db);
    CommonBlocPageLabels labels;
    labels.setTranslation(QStringLiteral("browse_categories"), QStringLiteral("pt"), QStringLiteral("Todas & <categorias>"));
    labels.setTranslation(QStringLiteral("browse_symptoms"), QStringLiteral("es"), QStringLiteral("Buscar por síntoma"));
    labels.setTranslation(QStringLiteral("browse_symptoms_count"), QStringLiteral("es"), QStringLiteral("Buscar entre %1 síntomas"));
    labels.save(workingDir);
    PageTypeTaxonomyIndex taxonomy(categories);
    taxonomy.bindGenerationContext(repo, workingDir);
    QVERIFY(taxonomy.buildHeadMetaTags({}, QStringLiteral("pt"), {}).contains(QStringLiteral("<title>Todas &amp; &lt;categorias&gt;</title>")));
    QVERIFY_EXCEPTION_THROWN(taxonomy.buildHeadMetaTags({}, QStringLiteral("de"), {}), ExceptionWithTitleText);
    QVERIFY(taxonomy.buildHeadMetaTags({}, QStringLiteral("en"), {}).contains(QStringLiteral("<title>Browse all categories</title>")));
    taxonomy.load({{QStringLiteral("2_seo_title"), QStringLiteral("Stored title")}});
    QVERIFY(taxonomy.buildHeadMetaTags({}, QStringLiteral("de"), {}).contains(QStringLiteral("<title>Stored title</title>")));

    PageTypeSymptomIndex symptoms(categories);
    symptoms.bindGenerationContext(repo, workingDir);
    QVERIFY(symptoms.buildHeadMetaTags({}, QStringLiteral("es"), {}).contains(QStringLiteral("<title>Buscar por síntoma</title>")));
    TaxonomyDb(workingDir).sync(QStringLiteral("symptoms"), {QStringLiteral("Fever"), QStringLiteral("Cough")});
    QVERIFY(symptoms.buildHeadMetaTags({}, QStringLiteral("es"), {}).contains(QStringLiteral("<title>Buscar entre 2 síntomas</title>")));
    QVERIFY(symptoms.buildHeadMetaTags({}, QStringLiteral("en"), {}).contains(QStringLiteral("<title>Browse conditions by symptom — 2 symptoms</title>")));
    QVERIFY_EXCEPTION_THROWN(symptoms.buildHeadMetaTags({}, QStringLiteral("pt"), {}), ExceptionWithTitleText);
    symptoms.load({{QStringLiteral("2_seo_title"), QStringLiteral("Stored symptom title")}});
    QVERIFY(symptoms.buildHeadMetaTags({}, QStringLiteral("pt"), {}).contains(QStringLiteral("<title>Stored symptom title</title>")));
}

void Test_Website_PageLabels::test_pagelabels_condition_list_uses_requested_language()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir workingDir(dir.path());
    HostTable hosts(workingDir);
    EngineArticles engine;
    engine.init(workingDir, hosts);
    int esIndex = -1;
    for (int row = 0; row < engine.rowCount(); ++row) {
        if (engine.getLangCode(row) == QStringLiteral("es")) {
            esIndex = row;
        }
    }
    QVERIFY(esIndex >= 0);
    PageDb db(workingDir);
    PageRepositoryDb repo(db);
    const int article = repo.create(QStringLiteral("article"), QStringLiteral("/condition"), QStringLiteral("en"));
    repo.saveData(article, {{QStringLiteral("7_symptoms"), QStringLiteral("Fever")},
                            {QStringLiteral("1_text"), QStringLiteral("An article")}});
    CommonBlocPageLabels labels;
    labels.setTranslation(QStringLiteral("possible_conditions"), QStringLiteral("es"), QStringLiteral("Posibles <afecciones>"));
    labels.setTranslation(QStringLiteral("all_symptoms"), QStringLiteral("es"), QStringLiteral("Todos & síntomas"));
    labels.save(workingDir);
    PageBlocConditionList bloc;
    bloc.setRenderContext(QStringLiteral("/symptoms/fever"), workingDir);
    QString html, css, js;
    QSet<QString> cssIds, jsIds;
    bloc.addCode({}, engine, esIndex, html, css, js, cssIds, jsIds);
    QVERIFY(html.contains(QStringLiteral("<h2>Posibles &lt;afecciones&gt;</h2>")));
    QVERIFY(html.contains(QStringLiteral("Todos &amp; síntomas</a>")));
    QVERIFY(!html.contains(QStringLiteral("Possible conditions")));
    QVERIFY(!html.contains(QStringLiteral("All symptoms")));
    QVERIFY(!html.contains(QStringLiteral("QT_CATALOGUE_LEAK")));
    labels.setTranslation(QStringLiteral("all_symptoms"), QStringLiteral("es"), {});
    labels.save(workingDir);
    bloc.setRenderContext(QStringLiteral("/symptoms/fever"), workingDir);
    QVERIFY_EXCEPTION_THROWN(bloc.addCode({}, engine, esIndex, html, css, js, cssIds, jsIds), ExceptionWithTitleText);
}

void Test_Website_PageLabels::test_pagelabels_jsonld_escapes_translated_title()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDir workingDir(dir.path());
    CategoryTable categories(workingDir);
    PageDb db(workingDir);
    PageRepositoryDb repo(db);
    CommonBlocPageLabels labels;
    const QString title = QStringLiteral("Buscar \"síntomas\" </script>");
    labels.setTranslation(QStringLiteral("browse_symptoms"), QStringLiteral("es"), title);
    labels.setTranslation(QStringLiteral("browse_categories"), QStringLiteral("es"), title);
    labels.save(workingDir);
    PageTypeSymptomIndex symptoms(categories);
    PageTypeTaxonomyIndex taxonomy(categories);
    const QList<AbstractPageType *> pages{&symptoms, &taxonomy};
    for (auto *page : pages) {
        page->bindGenerationContext(repo, workingDir);
        page->setGenerationContext(QStringLiteral("/index"), QStringLiteral("en"), {QStringLiteral("es")}, {},
                                   {{QStringLiteral("es"), QStringLiteral("2026-09-14")}});
        const QString head = page->buildHeadMetaTags(QStringLiteral("https://example.test"), QStringLiteral("es"), QStringLiteral("/index"));
        QVERIFY(!head.contains(QStringLiteral("QT_CATALOGUE_LEAK")));
        const QString scriptTag = QStringLiteral("<script type=\"application/ld+json\">");
        const int start = head.indexOf(scriptTag) + scriptTag.size();
        const int end = head.indexOf(QStringLiteral("</script>"), start);
        const auto json = QJsonDocument::fromJson(head.mid(start, end - start).toUtf8());
        QVERIFY(json.isObject());
        QCOMPARE(json.object().value(QStringLiteral("name")).toString(), title);
        QCOMPARE(head.count(QStringLiteral("</script>")), 1);
    }
}

QTEST_MAIN(Test_Website_PageLabels)
#include "test_page_labels.moc"
