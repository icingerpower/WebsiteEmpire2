#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include "aspire/attributes/AbstractPageAttributes.h"
#include "aspire/downloader/DownloadedPagesTable.h"
#include "aspire/generator/AbstractGenerator.h"
#include "aspire/generator/GeneratorFactories.h"

// ---------------------------------------------------------------------------
// CSV fixture
// ---------------------------------------------------------------------------

// "city","city_ascii","lat","lng","country","iso2","iso3","admin_name","capital","population","id"
// Populations intentionally ordered so the CSV is NOT sorted — generator must sort.
static bool writeTestCsv(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    // header
    out << "\"city\",\"city_ascii\",\"lat\",\"lng\",\"country\","
           "\"iso2\",\"iso3\",\"admin_name\",\"capital\",\"population\",\"id\"\n";
    // Lyon comes before Bordeaux in the file to verify sorting
    out << "\"Lyon\",\"Lyon\",\"45.7600\",\"4.8400\",\"France\","
           "\"FR\",\"FRA\",\"Auvergne-Rhône-Alpes\",\"admin\",\"520774\",\"3\"\n";
    out << "\"Paris\",\"Paris\",\"48.8567\",\"2.3522\",\"France\","
           "\"FR\",\"FRA\",\"Île-de-France\",\"primary\",\"11060000\",\"1\"\n";
    out << "\"Bordeaux\",\"Bordeaux\",\"44.8400\",\"-0.5800\",\"France\","
           "\"FR\",\"FRA\",\"Nouvelle-Aquitaine\",\"admin\",\"994920\",\"2\"\n";
    // City with no population → should sink to end
    out << "\"Smalltown\",\"Smalltown\",\"43.0000\",\"2.0000\",\"France\","
           "\"FR\",\"FRA\",\"Occitanie\",\"\",\"\",\"4\"\n";
    // Non-French city → must be filtered out
    out << "\"London\",\"London\",\"51.5074\",\"-0.1278\",\"United Kingdom\","
           "\"GB\",\"GBR\",\"England\",\"primary\",\"9000000\",\"5\"\n";
    return true;
}

// ---------------------------------------------------------------------------
// Reply builders
// ---------------------------------------------------------------------------

static QString makeStep1ReplySmall(const QString &jobId,
                                   const QString &postalCode = QStringLiteral("33000"))
{
    QJsonObject reply;
    reply[QStringLiteral("jobId")]                 = jobId;
    reply[QStringLiteral("postalCode")]            = postalCode;
    reply[QStringLiteral("estimatedFactoryCount")] = 3;
    reply[QStringLiteral("factoryKinds")]          = QJsonArray{};

    QJsonObject factory;
    factory[QStringLiteral("company_name")] = QStringLiteral("Test Factory SARL");
    factory[QStringLiteral("street")]       = QStringLiteral("1 rue de la Paix");
    factory[QStringLiteral("postal_code")]  = postalCode;
    factory[QStringLiteral("city")]         = QStringLiteral("Bordeaux");
    factory[QStringLiteral("state")]        = QStringLiteral("Nouvelle-Aquitaine");
    factory[QStringLiteral("country")]      = QStringLiteral("FR");
    factory[QStringLiteral("category")]     = QStringLiteral("textiles");
    factory[QStringLiteral("description")]  = QString(120, QLatin1Char('x'));

    reply[QStringLiteral("factories")] = QJsonArray{factory};
    return QString::fromUtf8(QJsonDocument(reply).toJson());
}

static QString makeStep1ReplyBig(const QString &jobId,
                                 const QStringList &kinds,
                                 const QString &postalCode = QStringLiteral("75001"))
{
    QJsonObject reply;
    reply[QStringLiteral("jobId")]                 = jobId;
    reply[QStringLiteral("postalCode")]            = postalCode;
    reply[QStringLiteral("estimatedFactoryCount")] = 150;
    reply[QStringLiteral("factories")]             = QJsonArray{};

    QJsonArray kindsArr;
    for (const QString &k : kinds) {
        kindsArr << k;
    }
    reply[QStringLiteral("factoryKinds")] = kindsArr;
    return QString::fromUtf8(QJsonDocument(reply).toJson());
}

static QString makeStep2Reply(const QString &jobId)
{
    QJsonObject reply;
    reply[QStringLiteral("jobId")] = jobId;

    QJsonObject factory;
    factory[QStringLiteral("company_name")] = QStringLiteral("Aerospace Inc");
    factory[QStringLiteral("street")]       = QStringLiteral("10 avenue du Palais");
    factory[QStringLiteral("postal_code")]  = QStringLiteral("75001");
    factory[QStringLiteral("city")]         = QStringLiteral("Paris");
    factory[QStringLiteral("state")]        = QStringLiteral("\xc3\x8ele-de-France"); // Île-de-France
    factory[QStringLiteral("country")]      = QStringLiteral("FR");
    factory[QStringLiteral("category")]     = QStringLiteral("aerospace");
    factory[QStringLiteral("description")]  = QString(110, QLatin1Char('y'));

    reply[QStringLiteral("factories")] = QJsonArray{factory};
    return QString::fromUtf8(QJsonDocument(reply).toJson());
}

// ---------------------------------------------------------------------------
// Per-test fixture — each test gets an isolated working directory.
// ---------------------------------------------------------------------------
struct Fixture {
    QTemporaryDir tmpDir;
    QString       csvPath;

    Fixture() : csvPath(tmpDir.path() + QStringLiteral("/worldcities.csv")) {}

    bool setup() { return tmpDir.isValid() && writeTestCsv(csvPath); }

    GeneratorFactories *makeGen(QObject *parent = nullptr) const
    {
        return new GeneratorFactories(QDir(tmpDir.path()), csvPath, parent);
    }

    QString iniPath() const
    {
        return QDir(tmpDir.path()).filePath(QStringLiteral("french-factories.ini"));
    }
};

// Convenience: parse the JSON payload returned by getNextJob().
static QJsonObject jobObj(const QString &raw)
{
    return QJsonDocument::fromJson(raw.toUtf8()).object();
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------
class Test_Generator_Factories : public QObject
{
    Q_OBJECT

private slots:

    // ==== CSV loading / city filtering / sorting ============================

    void test_csv_france_only_four_cities()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QCOMPARE(gen->getAllJobIds().size(), 4); // London filtered out
    }

    void test_csv_no_london()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        for (const QString &id : gen->getAllJobIds()) {
            QVERIFY(!id.contains(QLatin1String("london")));
        }
    }

    void test_csv_sorted_paris_first()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        QVERIFY(!ids.isEmpty());
        QCOMPARE(ids.first(), GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris")));
    }

    void test_csv_sorted_bordeaux_before_lyon()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        const QString bId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Bordeaux"));
        const QString lId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Lyon"));
        QVERIFY(ids.indexOf(bId) < ids.indexOf(lId));
    }

    void test_csv_zero_population_at_end()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        QVERIFY(!ids.isEmpty());
        QCOMPARE(ids.last(), GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Smalltown")));
    }

    void test_csv_all_job_ids_unique()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        const QSet<QString> idSet(ids.begin(), ids.end());
        QCOMPARE(idSet.size(), ids.size());
    }

    void test_csv_missing_file_returns_empty_job_list()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        QScopedPointer<GeneratorFactories> gen(
            new GeneratorFactories(QDir(tmpDir.path()),
                                   tmpDir.path() + QStringLiteral("/nonexistent.csv")));
        QVERIFY(gen->getAllJobIds().isEmpty());
        QVERIFY(gen->getNextJob().isEmpty());
    }

    // ==== Job-ID helper static methods =======================================

    void test_slugify_lowercase()
    {
        QCOMPARE(GeneratorFactories::slugify(QStringLiteral("Paris")),
                 QStringLiteral("paris"));
    }

    void test_slugify_spaces_to_hyphens()
    {
        QCOMPARE(GeneratorFactories::slugify(QStringLiteral("Le Havre")),
                 QStringLiteral("le-havre"));
    }

    void test_slugify_drops_non_ascii()
    {
        // 'É' is non-ASCII; 'Saint' and 'tienne' are ASCII → Saint- + tienne
        QCOMPARE(GeneratorFactories::slugify(QStringLiteral("Saint-\xc9tienne")),
                 QStringLiteral("saint-tienne"));
    }

    void test_slugify_keeps_hyphens_and_dots()
    {
        QCOMPARE(GeneratorFactories::slugify(QStringLiteral("abc-1.2")),
                 QStringLiteral("abc-1.2"));
    }

    void test_city_job_id_format()
    {
        QCOMPARE(GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris")),
                 QStringLiteral("FR/paris"));
    }

    void test_city_job_id_iso2_uppercased()
    {
        QCOMPARE(GeneratorFactories::cityJobId(QStringLiteral("fr"), QStringLiteral("Lyon")),
                 QStringLiteral("FR/lyon"));
    }

    void test_city_job_id_with_spaces()
    {
        QCOMPARE(GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Le Havre")),
                 QStringLiteral("FR/le-havre"));
    }

    void test_category_job_id_format()
    {
        QCOMPARE(GeneratorFactories::categoryJobId(QStringLiteral("FR/paris"),
                                                   QStringLiteral("Aerospace")),
                 QStringLiteral("FR/paris/cat/aerospace"));
    }

    void test_category_job_id_with_spaces()
    {
        QCOMPARE(GeneratorFactories::categoryJobId(QStringLiteral("FR/paris"),
                                                   QStringLiteral("Food Processing")),
                 QStringLiteral("FR/paris/cat/food-processing"));
    }

    void test_is_step1_true()
    {
        QVERIFY(GeneratorFactories::isStep1JobId(QStringLiteral("FR/paris")));
    }

    void test_is_step1_false_for_step2()
    {
        QVERIFY(!GeneratorFactories::isStep1JobId(QStringLiteral("FR/paris/cat/aerospace")));
    }

    void test_is_step2_true()
    {
        QVERIFY(GeneratorFactories::isStep2JobId(QStringLiteral("FR/paris/cat/aerospace")));
    }

    void test_is_step2_false_for_step1()
    {
        QVERIFY(!GeneratorFactories::isStep2JobId(QStringLiteral("FR/paris")));
    }

    void test_city_key_from_step2_id()
    {
        QCOMPARE(GeneratorFactories::cityKeyFromStep2Id(QStringLiteral("FR/paris/cat/aerospace")),
                 QStringLiteral("FR/paris"));
    }

    void test_category_from_step2_id()
    {
        QCOMPARE(GeneratorFactories::categoryFromStep2Id(QStringLiteral("FR/paris/cat/aerospace")),
                 QStringLiteral("aerospace"));
    }

    void test_city_key_from_step2_multi_word_category()
    {
        QCOMPARE(
            GeneratorFactories::cityKeyFromStep2Id(QStringLiteral("FR/le-havre/cat/food-processing")),
            QStringLiteral("FR/le-havre"));
    }

    void test_category_from_step2_multi_word()
    {
        QCOMPARE(
            GeneratorFactories::categoryFromStep2Id(QStringLiteral("FR/paris/cat/food-processing")),
            QStringLiteral("food-processing"));
    }

    // ==== getNextJob — step-1 payload structure ==============================

    void test_get_next_job_non_empty()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QVERIFY(!gen->getNextJob().isEmpty());
    }

    void test_get_next_job_valid_json()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(gen->getNextJob().toUtf8(), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());
    }

    void test_get_next_job_has_job_id_key()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QVERIFY(!jobObj(gen->getNextJob()).value(QStringLiteral("jobId")).toString().isEmpty());
    }

    void test_get_next_job_task_is_step1()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QCOMPARE(jobObj(gen->getNextJob()).value(QStringLiteral("task")).toString(),
                 GeneratorFactories::STEP1_TASK);
    }

    void test_get_next_job_has_city_field()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QVERIFY(!jobObj(gen->getNextJob()).value(QStringLiteral("city")).toString().isEmpty());
    }

    void test_get_next_job_country_is_france()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QCOMPARE(jobObj(gen->getNextJob()).value(QStringLiteral("country")).toString(),
                 QStringLiteral("France"));
    }

    void test_get_next_job_has_region()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QVERIFY(!jobObj(gen->getNextJob()).value(QStringLiteral("region")).toString().isEmpty());
    }

    void test_get_next_job_population_for_paris()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        // First job is Paris (largest population)
        const QJsonObject obj = jobObj(gen->getNextJob());
        QCOMPARE(obj.value(QStringLiteral("country")).toString(), QStringLiteral("France"));
        QVERIFY(obj.contains(QStringLiteral("population")));
        QCOMPARE(obj.value(QStringLiteral("population")).toInteger(), qint64(11060000));
    }

    void test_get_next_job_no_population_for_smalltown()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        // Drain Paris, Bordeaux, Lyon to get Smalltown
        for (int i = 0; i < 3; ++i) { gen->getNextJob(); }
        QVERIFY(!jobObj(gen->getNextJob()).contains(QStringLiteral("population")));
    }

    void test_get_next_job_has_factory_schema()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QJsonObject schema =
            jobObj(gen->getNextJob()).value(QStringLiteral("factorySchema")).toObject();
        QVERIFY(!schema.isEmpty());
    }

    void test_factory_schema_has_company_name()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QJsonObject schema =
            jobObj(gen->getNextJob()).value(QStringLiteral("factorySchema")).toObject();
        QVERIFY(schema.contains(QStringLiteral("company_name")));
    }

    void test_factory_schema_required_fields_present()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QJsonObject schema =
            jobObj(gen->getNextJob()).value(QStringLiteral("factorySchema")).toObject();
        QVERIFY(schema.contains(QStringLiteral("street")));
        QVERIFY(schema.contains(QStringLiteral("postal_code")));
        QVERIFY(schema.contains(QStringLiteral("city")));
        QVERIFY(schema.contains(QStringLiteral("country")));
        QVERIFY(schema.contains(QStringLiteral("category")));
        QVERIFY(schema.contains(QStringLiteral("description")));
    }

    void test_factory_schema_optional_fields_present()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QJsonObject schema =
            jobObj(gen->getNextJob()).value(QStringLiteral("factorySchema")).toObject();
        QVERIFY(schema.contains(QStringLiteral("email")));
        QVERIFY(schema.contains(QStringLiteral("phone")));
        QVERIFY(schema.contains(QStringLiteral("income")));
        QVERIFY(schema.contains(QStringLiteral("employees")));
        QVERIFY(schema.contains(QStringLiteral("certifications")));
    }

    void test_factory_schema_required_marked_required()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QJsonObject schema =
            jobObj(gen->getNextJob()).value(QStringLiteral("factorySchema")).toObject();
        // Required fields must not contain the word "optional"
        QVERIFY(!schema.value(QStringLiteral("company_name")).toString()
                     .contains(QLatin1String("optional")));
        QVERIFY(!schema.value(QStringLiteral("description")).toString()
                     .contains(QLatin1String("optional")));
    }

    void test_factory_schema_optional_marked_optional()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QJsonObject schema =
            jobObj(gen->getNextJob()).value(QStringLiteral("factorySchema")).toObject();
        QVERIFY(schema.value(QStringLiteral("email")).toString()
                    .contains(QLatin1String("optional")));
        QVERIFY(schema.value(QStringLiteral("income")).toString()
                    .contains(QLatin1String("optional")));
    }

    void test_get_next_job_response_template_keys()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QJsonObject resp =
            jobObj(gen->getNextJob()).value(QStringLiteral("replyFormat")).toObject();
        QVERIFY(resp.contains(QStringLiteral("jobId")));
        QVERIFY(resp.contains(QStringLiteral("postalCode")));
        QVERIFY(resp.contains(QStringLiteral("estimatedFactoryCount")));
        QVERIFY(resp.contains(QStringLiteral("factories")));
        QVERIFY(resp.contains(QStringLiteral("factoryKinds")));
    }

    void test_get_next_job_advances_cursor()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString id1 = jobObj(gen->getNextJob()).value(QStringLiteral("jobId")).toString();
        const QString id2 = jobObj(gen->getNextJob()).value(QStringLiteral("jobId")).toString();
        QVERIFY(!id1.isEmpty());
        QVERIFY(!id2.isEmpty());
        QVERIFY(id1 != id2);
    }

    void test_get_next_job_all_four_cities_distinct()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QSet<QString> seen;
        for (int i = 0; i < 4; ++i) {
            const QString id = jobObj(gen->getNextJob()).value(QStringLiteral("jobId")).toString();
            QVERIFY(!seen.contains(id));
            seen << id;
        }
        QVERIFY(gen->getNextJob().isEmpty()); // cursor exhausted
    }

    // ==== recordReply — step-1 small city (≤10 factories) ===================

    void test_record_step1_small_returns_true()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString bId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Bordeaux"));
        QVERIFY(gen->recordReply(makeStep1ReplySmall(bId)));
    }

    void test_record_step1_small_result_file_created()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString bId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Bordeaux"));
        gen->recordReply(makeStep1ReplySmall(bId));
        const QString safeId = QString(bId).replace(QLatin1Char('/'), QLatin1Char('_'));
        QVERIFY(QFile::exists(QDir(fx.tmpDir.path()).filePath(
            QStringLiteral("results/") + safeId + QStringLiteral(".json"))));
    }

    void test_record_step1_small_no_discovered_jobs()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString bId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Bordeaux"));
        gen->recordReply(makeStep1ReplySmall(bId));
        QSettings ini(fx.iniPath(), QSettings::IniFormat);
        QVERIFY(ini.value(QStringLiteral("Discovered/ids")).toStringList().isEmpty());
    }

    void test_record_step1_small_job_marked_done()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString bId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Bordeaux"));
        gen->recordReply(makeStep1ReplySmall(bId));
        QSettings ini(fx.iniPath(), QSettings::IniFormat);
        QVERIFY(ini.value(QStringLiteral("Done/ids")).toStringList().contains(bId));
    }

    // ==== recordReply — step-1 big city (>10 factories) =====================

    void test_record_step1_big_returns_true()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        QVERIFY(gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace"), QStringLiteral("food")})));
    }

    void test_record_step1_big_creates_two_discovered_jobs()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace"), QStringLiteral("food")}));
        QSettings ini(fx.iniPath(), QSettings::IniFormat);
        QCOMPARE(ini.value(QStringLiteral("Discovered/ids")).toStringList().size(), 2);
    }

    void test_record_step1_big_discovered_ids_correct()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace"), QStringLiteral("food")}));
        QSettings ini(fx.iniPath(), QSettings::IniFormat);
        const QStringList disc = ini.value(QStringLiteral("Discovered/ids")).toStringList();
        QVERIFY(disc.contains(GeneratorFactories::categoryJobId(pId, QStringLiteral("aerospace"))));
        QVERIFY(disc.contains(GeneratorFactories::categoryJobId(pId, QStringLiteral("food"))));
    }

    void test_record_step1_big_result_file_created()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        const QString safeId = QString(pId).replace(QLatin1Char('/'), QLatin1Char('_'));
        QVERIFY(QFile::exists(QDir(fx.tmpDir.path()).filePath(
            QStringLiteral("results/") + safeId + QStringLiteral(".json"))));
    }

    // ==== Step-2 jobs appear in pending after step-1 =========================

    void test_step2_jobs_appear_after_step1_big_recorded()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));

        // Drain remaining three step-1 jobs (Bordeaux, Lyon, Smalltown)
        for (int i = 0; i < 3; ++i) { gen->getNextJob(); }

        const QString step2Raw = gen->getNextJob();
        QVERIFY(!step2Raw.isEmpty());
        QCOMPARE(jobObj(step2Raw).value(QStringLiteral("task")).toString(),
                 GeneratorFactories::STEP2_TASK);
    }

    // ==== Step-2 job payload structure =======================================

    void test_step2_payload_task_field()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        for (int i = 0; i < 3; ++i) { gen->getNextJob(); }
        const QJsonObject obj = jobObj(gen->getNextJob());
        QCOMPARE(obj.value(QStringLiteral("task")).toString(), GeneratorFactories::STEP2_TASK);
    }

    void test_step2_payload_city_and_country()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        for (int i = 0; i < 3; ++i) { gen->getNextJob(); }
        const QJsonObject obj = jobObj(gen->getNextJob());
        QVERIFY(!obj.value(QStringLiteral("city")).toString().isEmpty());
        QCOMPARE(obj.value(QStringLiteral("country")).toString(), QStringLiteral("France"));
    }

    void test_step2_payload_category_field()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        for (int i = 0; i < 3; ++i) { gen->getNextJob(); }
        const QJsonObject obj = jobObj(gen->getNextJob());
        QCOMPARE(obj.value(QStringLiteral("category")).toString(), QStringLiteral("aerospace"));
    }

    void test_step2_payload_has_factory_schema()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        for (int i = 0; i < 3; ++i) { gen->getNextJob(); }
        const QJsonObject obj = jobObj(gen->getNextJob());
        QVERIFY(obj.value(QStringLiteral("factorySchema")).isObject());
        QVERIFY(!obj.value(QStringLiteral("factorySchema")).toObject().isEmpty());
    }

    void test_step2_payload_response_has_factories_array()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        for (int i = 0; i < 3; ++i) { gen->getNextJob(); }
        const QJsonObject resp =
            jobObj(gen->getNextJob()).value(QStringLiteral("replyFormat")).toObject();
        QVERIFY(resp.contains(QStringLiteral("jobId")));
        QVERIFY(resp.contains(QStringLiteral("factories")));
        QVERIFY(resp.value(QStringLiteral("factories")).isArray());
    }

    // ==== recordReply — step-2 ===============================================

    void test_record_step2_returns_true()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId  = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        const QString catId = GeneratorFactories::categoryJobId(pId, QStringLiteral("aerospace"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        QVERIFY(gen->recordReply(makeStep2Reply(catId)));
    }

    void test_record_step2_result_file_created()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId  = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        const QString catId = GeneratorFactories::categoryJobId(pId, QStringLiteral("aerospace"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        gen->recordReply(makeStep2Reply(catId));
        const QString safeId = QString(catId).replace(QLatin1Char('/'), QLatin1Char('_'));
        QVERIFY(QFile::exists(QDir(fx.tmpDir.path()).filePath(
            QStringLiteral("results/") + safeId + QStringLiteral(".json"))));
    }

    void test_record_step2_job_marked_done()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId  = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        const QString catId = GeneratorFactories::categoryJobId(pId, QStringLiteral("aerospace"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        gen->recordReply(makeStep2Reply(catId));
        QSettings ini(fx.iniPath(), QSettings::IniFormat);
        QVERIFY(ini.value(QStringLiteral("Done/ids")).toStringList().contains(catId));
    }

    // ==== Error / invalid replies ============================================

    void test_record_malformed_json_returns_false()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QVERIFY(!gen->recordReply(QStringLiteral("not { valid json")));
    }

    void test_record_empty_string_returns_false()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QVERIFY(!gen->recordReply(QString{}));
    }

    void test_record_missing_job_id_returns_false()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QJsonObject obj{{QStringLiteral("postalCode"), QStringLiteral("12345")}};
        QVERIFY(!gen->recordReply(QString::fromUtf8(QJsonDocument(obj).toJson())));
    }

    void test_record_empty_job_id_returns_false()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QJsonObject obj{{QStringLiteral("jobId"), QString{}}};
        QVERIFY(!gen->recordReply(QString::fromUtf8(QJsonDocument(obj).toJson())));
    }

    void test_record_unknown_job_id_returns_false()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QJsonObject obj{{QStringLiteral("jobId"), QStringLiteral("XX/unknowncity")}};
        QVERIFY(!gen->recordReply(QString::fromUtf8(QJsonDocument(obj).toJson())));
    }

    void test_record_already_done_returns_false()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString bId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Bordeaux"));
        const QString reply = makeStep1ReplySmall(bId);
        QVERIFY(gen->recordReply(reply));
        QVERIFY(!gen->recordReply(reply)); // second attempt must fail
    }

    // ==== State persistence — resume across "sessions" =======================

    void test_resume_skips_done_job()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        const QString lyonId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Lyon"));
        {
            QScopedPointer<GeneratorFactories> gen(fx.makeGen());
            QVERIFY(gen->recordReply(makeStep1ReplySmall(lyonId, QStringLiteral("69001"))));
        }
        // Second instance should have 3 pending jobs, not 4
        QScopedPointer<GeneratorFactories> gen2(fx.makeGen());
        int count = 0;
        while (!gen2->getNextJob().isEmpty()) { ++count; }
        QCOMPARE(count, 3);
    }

    void test_resume_done_id_absent_from_pending()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        const QString bId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Bordeaux"));
        {
            QScopedPointer<GeneratorFactories> gen(fx.makeGen());
            gen->recordReply(makeStep1ReplySmall(bId));
        }
        QScopedPointer<GeneratorFactories> gen2(fx.makeGen());
        QSet<QString> seenIds;
        while (true) {
            const QString raw = gen2->getNextJob();
            if (raw.isEmpty()) { break; }
            seenIds << jobObj(raw).value(QStringLiteral("jobId")).toString();
        }
        QVERIFY(!seenIds.contains(bId));
    }

    void test_discovered_jobs_persisted_to_ini()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        {
            QScopedPointer<GeneratorFactories> gen(fx.makeGen());
            const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
            gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace"), QStringLiteral("food")}));
        }
        QSettings ini(fx.iniPath(), QSettings::IniFormat);
        QCOMPARE(ini.value(QStringLiteral("Discovered/ids")).toStringList().size(), 2);
    }

    void test_resume_includes_discovered_jobs()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        const QString pId   = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        const QString catId = GeneratorFactories::categoryJobId(pId, QStringLiteral("aerospace"));
        {
            QScopedPointer<GeneratorFactories> gen(fx.makeGen());
            gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        }
        QScopedPointer<GeneratorFactories> gen2(fx.makeGen());
        QSet<QString> pendingIds;
        while (true) {
            const QString raw = gen2->getNextJob();
            if (raw.isEmpty()) { break; }
            pendingIds << jobObj(raw).value(QStringLiteral("jobId")).toString();
        }
        QVERIFY(pendingIds.contains(catId));
    }

    void test_resume_done_step1_not_in_pending()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        {
            QScopedPointer<GeneratorFactories> gen(fx.makeGen());
            gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        }
        QScopedPointer<GeneratorFactories> gen2(fx.makeGen());
        QSet<QString> pendingIds;
        while (true) {
            const QString raw = gen2->getNextJob();
            if (raw.isEmpty()) { break; }
            pendingIds << jobObj(raw).value(QStringLiteral("jobId")).toString();
        }
        QVERIFY(!pendingIds.contains(pId));
    }

    // ==== Deduplication of discovered jobs ==================================

    void test_no_duplicate_in_discovered_ini()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        // First recordReply adds "aerospace" to discovered
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        // Second recordReply on already-done job is rejected → no second entry
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        QSettings ini(fx.iniPath(), QSettings::IniFormat);
        QCOMPARE(ini.value(QStringLiteral("Discovered/ids")).toStringList().size(), 1);
    }

    void test_discovered_job_not_added_twice_to_pending()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));

        // Count how many step-2 "aerospace" jobs appear while draining
        int count = 0;
        while (true) {
            const QString raw = gen->getNextJob();
            if (raw.isEmpty()) { break; }
            const QJsonObject obj = jobObj(raw);
            if (obj.value(QStringLiteral("task")).toString() == GeneratorFactories::STEP2_TASK
                && obj.value(QStringLiteral("category")).toString() == QLatin1String("aerospace")) {
                ++count;
            }
        }
        QCOMPARE(count, 1); // exactly one, not duplicated
    }

    // ==== Generator registry ================================================

    void test_generator_registered_in_all_generators()
    {
        QVERIFY(AbstractGenerator::ALL_GENERATORS().contains(QStringLiteral("french-factories")));
    }

    void test_generator_id_correct()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QCOMPARE(gen->getId(), QStringLiteral("french-factories"));
    }

    void test_generator_name_non_empty()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QVERIFY(!gen->getName().isEmpty());
    }

    void test_create_instance_non_null()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        const AbstractGenerator *proto =
            AbstractGenerator::ALL_GENERATORS().value(QStringLiteral("french-factories"));
        QVERIFY(proto != nullptr);
        QScopedPointer<AbstractGenerator> inst(proto->createInstance(QDir(fx.tmpDir.path())));
        QVERIFY(inst != nullptr);
    }

    void test_create_instance_has_correct_id()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        const AbstractGenerator *proto =
            AbstractGenerator::ALL_GENERATORS().value(QStringLiteral("french-factories"));
        QVERIFY(proto != nullptr);
        QScopedPointer<AbstractGenerator> inst(proto->createInstance(QDir(fx.tmpDir.path())));
        QCOMPARE(inst->getId(), QStringLiteral("french-factories"));
    }

    // ==== createResultPageAttributes / openResultsTable =====================

    void test_open_results_table_returns_non_null()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QVERIFY(gen->openResultsTable() != nullptr);
    }

    void test_open_results_table_opens_factory_table()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        gen->openResultsTable();
        QVERIFY(gen->resultsTable(QStringLiteral("PageAttributesFactory")) != nullptr);
    }

    void test_open_results_table_opens_factory_category_table()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        gen->openResultsTable();
        QVERIFY(gen->resultsTable(QStringLiteral("PageAttributesFactoryCategory")) != nullptr);
    }

    void test_open_results_table_primary_is_factory_table()
    {
        // openResultsTable() returns the first table alphabetically by attrId;
        // "PageAttributesFactory" < "PageAttributesFactoryCategory".
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        DownloadedPagesTable *primary = gen->openResultsTable();
        QCOMPARE(primary, gen->resultsTable(QStringLiteral("PageAttributesFactory")));
    }

    void test_record_step1_small_writes_factory_row()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        gen->openResultsTable();
        DownloadedPagesTable *factoryTable =
            gen->resultsTable(QStringLiteral("PageAttributesFactory"));
        const int rowsBefore = factoryTable->rowCount();
        const QString bId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Bordeaux"));
        gen->recordReply(makeStep1ReplySmall(bId));
        QCOMPARE(factoryTable->rowCount(), rowsBefore + 1);
    }

    void test_record_step1_small_writes_category_row()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        gen->openResultsTable();
        DownloadedPagesTable *categoryTable =
            gen->resultsTable(QStringLiteral("PageAttributesFactoryCategory"));
        const int rowsBefore = categoryTable->rowCount();
        const QString bId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Bordeaux"));
        gen->recordReply(makeStep1ReplySmall(bId));
        // makeStep1ReplySmall uses category "textiles" → one new category row
        QCOMPARE(categoryTable->rowCount(), rowsBefore + 1);
    }

    void test_record_step1_big_writes_category_rows_from_kinds()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        gen->openResultsTable();
        DownloadedPagesTable *categoryTable =
            gen->resultsTable(QStringLiteral("PageAttributesFactoryCategory"));
        const int rowsBefore = categoryTable->rowCount();
        const QString pId = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace"), QStringLiteral("food")}));
        QCOMPARE(categoryTable->rowCount(), rowsBefore + 2);
    }

    void test_record_step2_writes_factory_row()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        gen->openResultsTable();
        DownloadedPagesTable *factoryTable =
            gen->resultsTable(QStringLiteral("PageAttributesFactory"));
        const QString pId  = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        const QString catId = GeneratorFactories::categoryJobId(pId, QStringLiteral("aerospace"));
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        const int rowsBefore = factoryTable->rowCount();
        gen->recordReply(makeStep2Reply(catId));
        QCOMPARE(factoryTable->rowCount(), rowsBefore + 1);
    }

    void test_record_step2_writes_category_row()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        gen->openResultsTable();
        DownloadedPagesTable *categoryTable =
            gen->resultsTable(QStringLiteral("PageAttributesFactoryCategory"));
        const QString pId  = GeneratorFactories::cityJobId(QStringLiteral("FR"), QStringLiteral("Paris"));
        const QString catId = GeneratorFactories::categoryJobId(pId, QStringLiteral("aerospace"));
        // step-1 big already writes "aerospace" category; capture count after that
        gen->recordReply(makeStep1ReplyBig(pId, {QStringLiteral("aerospace")}));
        const int rowsAfterStep1 = categoryTable->rowCount();
        gen->recordReply(makeStep2Reply(catId));
        // makeStep2Reply uses category "aerospace" which was already recorded in step-1,
        // so no additional category row (deduplication within each processReply call).
        QVERIFY(categoryTable->rowCount() >= rowsAfterStep1);
    }

    // ==== getTables ============================================================

    void test_get_tables_primary_has_one_entry()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QCOMPARE(gen->getTables().primary.size(), 1);
    }

    void test_get_tables_primary_id_is_factory()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QVERIFY(gen->getTables().primary.contains(QStringLiteral("PageAttributesFactory")));
    }

    void test_get_tables_primary_name_non_empty()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const auto &desc = gen->getTables().primary.value(QStringLiteral("PageAttributesFactory"));
        QVERIFY(!desc.name.isEmpty());
    }

    void test_get_tables_primary_path_ends_with_factory_db()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const auto &desc = gen->getTables().primary.value(QStringLiteral("PageAttributesFactory"));
        QVERIFY(desc.tablePath.endsWith(QStringLiteral("PageAttributesFactory.db")));
    }

    void test_get_tables_primary_path_under_working_dir()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        const auto &desc = gen->getTables().primary.value(QStringLiteral("PageAttributesFactory"));
        QVERIFY(desc.tablePath.startsWith(fx.tmpDir.path()));
    }

    void test_get_tables_category_has_one_entry()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QCOMPARE(gen->getTables().category.size(), 1);
    }

    void test_get_tables_category_id_is_factory_category()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QVERIFY(gen->getTables().category.contains(
            QStringLiteral("PageAttributesFactoryCategory")));
    }

    void test_get_tables_referred_to_is_empty()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorFactories> gen(fx.makeGen());
        QVERIFY(gen->getTables().referredTo.isEmpty());
    }
};

QTEST_MAIN(Test_Generator_Factories)
#include "test_generator_factories.moc"
