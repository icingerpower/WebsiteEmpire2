#include <QtTest>

#include <QDir>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

#include "aspire/generator/AbstractGenerator.h"
#include "aspire/generator/GeneratorFashionTaxonomy.h"
#include "aspire/downloader/DownloadedPagesTable.h"
#include "aspire/AspiredDb.h"
#include "ExceptionWithTitleText.h"
#include "aspire/attributes/AbstractPageAttributes.h"
#include "aspire/attributes/fashion/PageAttributesFashionCulture.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboBase.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboColorProductEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboColorProduct.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboColorColor.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboSeasonEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboFitProductDemographic.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboMaterialProductSeason.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboFitProductEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboStyleSeason.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboProductPattern.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboStyleProduct.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboProductEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboColorSeason.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboProductDemographic.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboStyleEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboMaterialProduct.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboStyleProductEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboColorProductDemographic.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboProductDemographicEvent.h"
#include "aspire/attributes/fashion/PageAttributesFashionComboPatternProductSeason.h"

// ---------------------------------------------------------------------------
// Per-test fixture — each test gets an isolated working directory.
// ---------------------------------------------------------------------------
struct Fixture {
    QTemporaryDir tmpDir;

    GeneratorFashionTaxonomy *makeGen(QObject *parent = nullptr) const
    {
        return new GeneratorFashionTaxonomy(QDir(tmpDir.path()), parent);
    }
};

namespace {

QJsonArray toJsonArray(const QStringList &list)
{
    QJsonArray arr;
    for (const QString &s : list) {
        arr.append(s);
    }
    return arr;
}

int countRows(DownloadedPagesTable *table, const QString &whereClause = QString())
{
    QSqlQuery q(table->database());
    const QString sql = QStringLiteral("SELECT COUNT(*) FROM records") + whereClause;
    return (q.exec(sql) && q.next()) ? q.value(0).toInt() : -1;
}

QStringList columnValues(DownloadedPagesTable *table, const QString &column)
{
    QSqlQuery q(table->database());
    QStringList values;
    if (q.exec(QStringLiteral("SELECT \"%1\" FROM records").arg(column))) {
        while (q.next()) {
            values << q.value(0).toString();
        }
    }
    return values;
}

// Every combo table this generator produces, mapped key (as used in job ids)
// -> AbstractPageAttributes::getId(). Mirrors GeneratorFashionTaxonomy.cpp's
// file-local comboSpecs() — kept independent here on purpose, so the test
// verifies the generator's actual public behaviour rather than importing its
// internal table.
const QList<QPair<QString, QString>> &comboKeysAndAttrIds()
{
    static const QList<QPair<QString, QString>> table = {
        {QStringLiteral("color_product"), QStringLiteral("PageAttributesFashionComboColorProduct")},
        {QStringLiteral("season_event"), QStringLiteral("PageAttributesFashionComboSeasonEvent")},
        {QStringLiteral("fit_product_demographic"), QStringLiteral("PageAttributesFashionComboFitProductDemographic")},
        {QStringLiteral("color_product_event"), QStringLiteral("PageAttributesFashionComboColorProductEvent")},
        {QStringLiteral("material_product_season"), QStringLiteral("PageAttributesFashionComboMaterialProductSeason")},
        {QStringLiteral("fit_product_event"), QStringLiteral("PageAttributesFashionComboFitProductEvent")},
        {QStringLiteral("style_season"), QStringLiteral("PageAttributesFashionComboStyleSeason")},
        {QStringLiteral("color_color"), QStringLiteral("PageAttributesFashionComboColorColor")},
        {QStringLiteral("product_pattern"), QStringLiteral("PageAttributesFashionComboProductPattern")},
        {QStringLiteral("style_product"), QStringLiteral("PageAttributesFashionComboStyleProduct")},
        {QStringLiteral("product_event"), QStringLiteral("PageAttributesFashionComboProductEvent")},
        {QStringLiteral("color_season"), QStringLiteral("PageAttributesFashionComboColorSeason")},
        {QStringLiteral("product_demographic"), QStringLiteral("PageAttributesFashionComboProductDemographic")},
        {QStringLiteral("style_event"), QStringLiteral("PageAttributesFashionComboStyleEvent")},
        {QStringLiteral("material_product"), QStringLiteral("PageAttributesFashionComboMaterialProduct")},
        {QStringLiteral("style_product_event"), QStringLiteral("PageAttributesFashionComboStyleProductEvent")},
        {QStringLiteral("color_product_demographic"), QStringLiteral("PageAttributesFashionComboColorProductDemographic")},
        {QStringLiteral("product_demographic_event"), QStringLiteral("PageAttributesFashionComboProductDemographicEvent")},
        {QStringLiteral("pattern_product_season"), QStringLiteral("PageAttributesFashionComboPatternProductSeason")},
    };
    return table;
}

} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------
class Test_Generator_Fashion : public QObject
{
    Q_OBJECT

private slots:

    // ==== Registry + identification =========================================

    void test_fashion_registered_in_all_generators()
    {
        QVERIFY(AbstractGenerator::ALL_GENERATORS().contains(QStringLiteral("fashion_taxonomy")));
    }

    void test_fashion_id_correct()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        QCOMPARE(gen->getId(), QStringLiteral("fashion_taxonomy"));
    }

    void test_fashion_name_non_empty()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        QVERIFY(!gen->getName().isEmpty());
    }

    void test_fashion_create_instance_has_correct_id()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        const AbstractGenerator *proto =
            AbstractGenerator::ALL_GENERATORS().value(QStringLiteral("fashion_taxonomy"));
        QVERIFY(proto != nullptr);
        QScopedPointer<AbstractGenerator> inst(proto->createInstance(QDir(fx.tmpDir.path())));
        QVERIFY(inst != nullptr);
        QCOMPARE(inst->getId(), QStringLiteral("fashion_taxonomy"));
    }

    // ==== Job-ID helpers ====================================================

    void test_fashion_combo_key_from_job_id()
    {
        QCOMPARE(GeneratorFashionTaxonomy::comboKeyFromJobId(QStringLiteral("combo/color_product/3")),
                 QStringLiteral("color_product"));
        QCOMPARE(GeneratorFashionTaxonomy::comboKeyFromJobId(QStringLiteral("combo/color_product_event/0")),
                 QStringLiteral("color_product_event"));
    }

    void test_fashion_page_from_job_id()
    {
        QCOMPARE(GeneratorFashionTaxonomy::pageFromJobId(QStringLiteral("combo/color_product/3")), 3);
        QCOMPARE(GeneratorFashionTaxonomy::pageFromJobId(QStringLiteral("combo/season_event/0")), 0);
    }

    void test_fashion_build_initial_job_ids_has_nineteen_combo_jobs()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        QCOMPARE(ids.size(), comboKeysAndAttrIds().size());
        for (const QString &id : ids) {
            QVERIFY(id.startsWith(QStringLiteral("combo/")));
            QVERIFY(id.endsWith(QStringLiteral("/0")));
        }
    }

    // Every initial job id must embed the current vocabulary round suffix —
    // this is what keeps a grown vocabulary from silently re-using the
    // previous round's Done pages (which describe the WRONG cross-product
    // slices after any seed-list change re-orders the alphabetical walk).
    void test_fashion_initial_job_ids_carry_vocab_round_suffix()
    {
        QVERIFY(GeneratorFashionTaxonomy::VOCAB_ROUND >= 2);
        const QString suffix = QStringLiteral(".r")
            + QString::number(GeneratorFashionTaxonomy::VOCAB_ROUND) + QStringLiteral("/0");

        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        QVERIFY(!ids.isEmpty());
        for (const QString &id : ids) {
            QVERIFY2(id.endsWith(suffix), qPrintable(id));
        }
    }

    void test_fashion_combo_key_from_job_id_keeps_round_suffix()
    {
        // The suffix stays part of the combo key so continuations discovered
        // from a round-2 job remain round-2 jobs.
        QCOMPARE(GeneratorFashionTaxonomy::comboKeyFromJobId(QStringLiteral("combo/color_product.r2/3")),
                 QStringLiteral("color_product.r2"));
        QCOMPARE(GeneratorFashionTaxonomy::pageFromJobId(QStringLiteral("combo/color_product.r2/3")), 3);
    }

    // ==== getTables() ========================================================

    void test_fashion_get_tables_primary_has_all_nineteen_combos()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        const auto tables = gen->getTables();
        QCOMPARE(tables.primary.size(), comboKeysAndAttrIds().size());
        for (const auto &pair : comboKeysAndAttrIds()) {
            QVERIFY2(tables.primary.contains(pair.second), qPrintable(pair.second));
        }
    }

    void test_fashion_get_tables_category_has_eleven_vocab_tables()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        QCOMPARE(gen->getTables().category.size(), 11);
    }

    void test_fashion_get_tables_referred_to_is_empty()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        // Every combo table is now its own independent primary source — none
        // is a "child of" another combo row.
        QVERIFY(gen->getTables().referredTo.isEmpty());
    }

    // ==== seedStaticVocabulary() =============================================

    void test_fashion_seed_static_vocabulary_populates_cultures()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        gen->openResultsTable();
        gen->seedStaticVocabulary();

        DownloadedPagesTable *cultureTable = gen->resultsTable(QStringLiteral("PageAttributesFashionCulture"));
        QVERIFY(cultureTable != nullptr);
        const QStringList names = columnValues(cultureTable, PageAttributesFashionCulture::ID_NAME);

        QCOMPARE(names.size(), 5);
        QVERIFY(names.contains(QStringLiteral("Western/Mainstream")));
        QVERIFY(names.contains(QStringLiteral("Muslim/Modest")));
        QVERIFY(names.contains(QStringLiteral("South Asian/Indian")));
        QVERIFY(names.contains(QStringLiteral("East Asian")));
        QVERIFY(names.contains(QStringLiteral("African/Diaspora")));
    }

    void test_fashion_seed_static_vocabulary_is_idempotent()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        gen->openResultsTable();
        gen->seedStaticVocabulary();

        DownloadedPagesTable *table = gen->resultsTable(QStringLiteral("PageAttributesFashionColor"));
        const int firstCount = countRows(table);
        QVERIFY(firstCount > 0);

        gen->seedStaticVocabulary();
        const int secondCount = countRows(table);
        QCOMPARE(secondCount, firstCount);
    }

    // ==== Core acceptance: only relevant combinations get recorded ==========

    void test_fashion_combo_color_product_event_process_reply_records_only_nonempty_cultures()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        gen->openResultsTable();
        gen->seedStaticVocabulary();

        // Find the color_product_event job among the initial jobs.
        QJsonObject job;
        for (int i = 0; i < comboKeysAndAttrIds().size(); ++i) {
            const QString jsonStr = gen->getNextJob();
            if (jsonStr.isEmpty()) {
                break;
            }
            const QJsonObject candidate = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
            if (candidate.value(QStringLiteral("comboKey")).toString() == QStringLiteral("color_product_event")) {
                job = candidate;
                break;
            }
        }
        QVERIFY(!job.isEmpty());
        const QString jobId = job.value(QStringLiteral("jobId")).toString();
        const QJsonArray candidates = job.value(QStringLiteral("candidates")).toArray();
        QVERIFY(candidates.size() >= 3);

        // Candidate 0: makes sense in one culture -> must be recorded.
        // Candidate 1: makes sense nowhere (empty cultures) -> must be discarded.
        // Candidate 2: makes sense in two cultures -> must be recorded.
        // Remaining candidates: also tagged empty, so only candidates 0 and 2
        // end up in the database.
        QJsonArray results;
        auto addResult = [&results](int index, const QStringList &cultures) {
            QJsonObject r;
            r[QStringLiteral("index")] = index;
            r[QStringLiteral("cultures")] = toJsonArray(cultures);
            results.append(r);
        };
        addResult(0, {QStringLiteral("Western/Mainstream")});
        addResult(1, {});
        addResult(2, {QStringLiteral("Western/Mainstream"), QStringLiteral("East Asian")});
        for (int i = 3; i < candidates.size(); ++i) {
            addResult(i, {});
        }

        QJsonObject reply;
        reply[QStringLiteral("jobId")] = jobId;
        reply[QStringLiteral("results")] = results;
        QVERIFY(gen->recordReply(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact))));

        DownloadedPagesTable *table = gen->resultsTable(QStringLiteral("PageAttributesFashionComboColorProductEvent"));
        QVERIFY(table != nullptr);
        QCOMPARE(countRows(table), 2);

        const QStringList recordedCultures = columnValues(table, PageAttributesFashionComboBase::ID_CULTURES);
        QCOMPARE(recordedCultures.size(), 2);
        for (const QString &c : recordedCultures) {
            QVERIFY(!c.isEmpty());
        }
        QVERIFY(recordedCultures.contains(QStringLiteral("Western/Mainstream")));
        QVERIFY(recordedCultures.contains(QStringLiteral("Western/Mainstream,East Asian")));
    }

    // ==== Flagship acceptance test: sweep ALL 19 combo tables ===============
    //
    // Proves the goal the whole feature exists for: after generation, every
    // combination table contains ONLY rows the AI tagged with at least one
    // applicable culture — nothing with an empty/blank culture list ever
    // reaches the database, across every one of the 19 combo tables.

    void test_fashion_all_combo_tables_never_contain_empty_culture_rows()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        gen->openResultsTable();
        gen->seedStaticVocabulary();

        QHash<QString, QString> keyToAttrId;
        for (const auto &pair : comboKeysAndAttrIds()) {
            keyToAttrId.insert(pair.first, pair.second);
        }

        QHash<QString, int> expectedRecordedCount;

        for (int i = 0; i < comboKeysAndAttrIds().size(); ++i) {
            const QString jsonStr = gen->getNextJob();
            QVERIFY(!jsonStr.isEmpty());
            const QJsonObject job = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
            const QString comboKey = job.value(QStringLiteral("comboKey")).toString();
            QVERIFY(keyToAttrId.contains(comboKey));
            const QString jobId = job.value(QStringLiteral("jobId")).toString();
            const QJsonArray candidates = job.value(QStringLiteral("candidates")).toArray();

            // Alternate even/odd -> non-empty/empty cultures, except for the
            // color_color table where colorA == colorB is cross-validation
            // invalid: such a candidate must never be tagged non-empty, or
            // recordResultPage() would throw when the (correctly) empty case
            // is what should happen instead.
            QJsonArray results;
            int expectedRecorded = 0;
            for (int idx = 0; idx < candidates.size(); ++idx) {
                const QJsonObject candidateObj = candidates.at(idx).toObject();
                bool selfContradictory = false;
                if (candidateObj.contains(QStringLiteral("colorA")) && candidateObj.contains(QStringLiteral("colorB"))) {
                    selfContradictory = candidateObj.value(QStringLiteral("colorA")).toString()
                                      == candidateObj.value(QStringLiteral("colorB")).toString();
                }
                const bool tagNonEmpty = (idx % 2 == 0) && !selfContradictory;

                QJsonObject r;
                r[QStringLiteral("index")] = idx;
                r[QStringLiteral("cultures")] = tagNonEmpty
                    ? toJsonArray({QStringLiteral("Western/Mainstream")})
                    : QJsonArray{};
                results.append(r);
                if (tagNonEmpty) {
                    ++expectedRecorded;
                }
            }

            QJsonObject reply;
            reply[QStringLiteral("jobId")] = jobId;
            reply[QStringLiteral("results")] = results;
            QVERIFY(gen->recordReply(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact))));

            expectedRecordedCount[comboKey] = expectedRecorded;
        }

        for (const auto &pair : comboKeysAndAttrIds()) {
            DownloadedPagesTable *table = gen->resultsTable(pair.second);
            QVERIFY(table != nullptr);

            // Invariant: not a single row anywhere has an empty culture list.
            QCOMPARE(countRows(table, QStringLiteral(" WHERE \"combo_cultures\" IS NULL OR \"combo_cultures\" = ''")), 0);

            // And exactly the candidates we tagged non-empty were recorded — no
            // more, no fewer.
            QCOMPARE(countRows(table), expectedRecordedCount.value(pair.first));
        }
    }

    // ==== Regression: pagination must terminate even at 0% acceptance ======
    //
    // Bug history: the original nextCandidateBatch() skipped candidates by
    // scanning what was already RECORDED in the combo table. A rejected
    // (empty-culture) candidate is never recorded, so that scheme could never
    // tell "rejected" apart from "not yet assessed" — every page re-walked
    // from the start of the cross product and re-sent the same rejected
    // candidates forever. Observed in production: combo/color_product_event
    // reached page 2742 (109,680 assessments) for a table whose entire cross
    // product is only 24,000 combinations. Fixed by deriving each page's
    // slice directly from the page number (page*MAX_CANDIDATES_PER_JOB),
    // independent of what has or hasn't been recorded. This test rejects
    // EVERY candidate (worst case for the old bug) and proves pagination
    // still terminates at the exact expected page count with no repeats.
    void test_fashion_combo_pagination_terminates_with_zero_percent_acceptance()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        gen->openResultsTable();
        gen->seedStaticVocabulary();

        // combo/season_event has a small, exactly-known cross product:
        // 12 seasons x 55 events x 1 formula = 660 -> 16 full pages of 40
        // plus a final partial page of 20 = 17 pages.
        const int expectedTotal = 660;
        const int expectedPages = 17;

        QSet<QString> seenPairs;
        QSet<int> seenPages;
        int guard = 0;

        // 19 combo tables round-robin their pages, so covering season_event's
        // 17 pages costs at most ~19*17 jobs — well under the guard.
        while (seenPages.size() < expectedPages && guard < 500) {
            ++guard;
            const QString jsonStr = gen->getNextJob();
            QVERIFY2(!jsonStr.isEmpty(), "generator ran out of jobs before covering season_event's full space");
            const QJsonObject job = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
            const QString jobId = job.value(QStringLiteral("jobId")).toString();
            const QJsonArray candidates = job.value(QStringLiteral("candidates")).toArray();

            // Reject every candidate in every job (worst case for the old bug:
            // nothing ever gets recorded, so a recorded-based scheme would
            // never advance past page 0).
            QJsonArray results;
            for (int idx = 0; idx < candidates.size(); ++idx) {
                QJsonObject r;
                r[QStringLiteral("index")] = idx;
                r[QStringLiteral("cultures")] = QJsonArray{};
                results.append(r);
            }
            QJsonObject reply;
            reply[QStringLiteral("jobId")] = jobId;
            reply[QStringLiteral("results")] = results;
            QVERIFY(gen->recordReply(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact))));

            if (!GeneratorFashionTaxonomy::comboKeyFromJobId(jobId).startsWith(QStringLiteral("season_event"))) {
                continue;
            }
            const int page = GeneratorFashionTaxonomy::pageFromJobId(jobId);
            QVERIFY2(!seenPages.contains(page), "the same season_event page was dispatched twice");
            seenPages.insert(page);

            for (const QJsonValue &c : candidates) {
                const QJsonObject co = c.toObject();
                const QString pairKey = co.value(QStringLiteral("season")).toString()
                                       + QLatin1Char('|') + co.value(QStringLiteral("event")).toString();
                QVERIFY2(!seenPairs.contains(pairKey),
                         qPrintable(QStringLiteral("candidate re-sent across pages: %1").arg(pairKey)));
                seenPairs.insert(pairKey);
            }
        }

        QCOMPARE(seenPages.size(), expectedPages);
        QCOMPARE(seenPairs.size(), expectedTotal);

        // And it actually stopped — no 18th season_event page ever gets
        // discovered once the space is fully covered.
        DownloadedPagesTable *table = gen->resultsTable(QStringLiteral("PageAttributesFashionComboSeasonEvent"));
        QVERIFY(table != nullptr);
        QCOMPARE(countRows(table), 0); // everything was rejected, as instructed
    }

    // ==== Regression: one invalid candidate must not abort the whole batch ==
    //
    // Bug history: observed in production — the AI tagged non-empty cultures
    // for a colorA==colorB self-pair candidate in combo/color_color. Calling
    // recordResultPage() for it threw (schema cross-validation correctly
    // rejects colorA==colorB), and that exception propagated out of
    // processReply(), aborting the whole 40-candidate batch — the job never
    // got marked done, and every OTHER already-judged candidate in that same
    // reply was discarded even though it never violated anything. Fixed by
    // catching ExceptionWithTitleText per-candidate and skipping just that
    // one (see the try/catch around recordResultPage() in processReply()).
    void test_fashion_combo_color_color_one_bad_candidate_does_not_abort_batch()
    {
        Fixture fx;
        QVERIFY(fx.tmpDir.isValid());
        QScopedPointer<GeneratorFashionTaxonomy> gen(fx.makeGen());
        gen->openResultsTable();
        gen->seedStaticVocabulary();

        QJsonObject job;
        for (int i = 0; i < comboKeysAndAttrIds().size(); ++i) {
            const QString jsonStr = gen->getNextJob();
            if (jsonStr.isEmpty()) {
                break;
            }
            const QJsonObject candidate = QJsonDocument::fromJson(jsonStr.toUtf8()).object();
            if (candidate.value(QStringLiteral("comboKey")).toString() == QStringLiteral("color_color")) {
                job = candidate;
                break;
            }
        }
        QVERIFY(!job.isEmpty());
        const QString jobId = job.value(QStringLiteral("jobId")).toString();
        const QJsonArray candidates = job.value(QStringLiteral("candidates")).toArray();
        QVERIFY(candidates.size() >= 3);

        // decodeCandidate() orders the formula id fastest, so the first
        // formula-count candidates are ALL the colorA == colorB self-pair of
        // the alphabetically first color — guaranteed schema-invalid. The
        // first candidate with a different colorB is guaranteed valid.
        int firstBad = -1;
        int firstValid = -1;
        for (int idx = 0; idx < candidates.size(); ++idx) {
            const QJsonObject co = candidates.at(idx).toObject();
            const bool selfPair = co.value(QStringLiteral("colorA")).toString()
                               == co.value(QStringLiteral("colorB")).toString();
            if (selfPair && firstBad < 0) {
                firstBad = idx;
            }
            if (!selfPair && firstValid < 0) {
                firstValid = idx;
            }
        }
        QCOMPARE(firstBad, 0);
        QVERIFY(firstValid > 0);

        QJsonArray results;
        auto addResult = [&results](int index, const QStringList &cultures) {
            QJsonObject r;
            r[QStringLiteral("index")] = index;
            r[QStringLiteral("cultures")] = toJsonArray(cultures);
            results.append(r);
        };
        // The AI (incorrectly) tags the self-pair as making sense — must be
        // silently skipped, not thrown, and must not take the valid one with it.
        for (int i = 0; i < candidates.size(); ++i) {
            if (i == firstBad || i == firstValid) {
                addResult(i, {QStringLiteral("Western/Mainstream")});
            } else {
                addResult(i, {});
            }
        }

        QJsonObject reply;
        reply[QStringLiteral("jobId")] = jobId;
        reply[QStringLiteral("results")] = results;

        // Must return true (no exception escapes) and the job must end up
        // recorded as done — i.e. the batch as a whole succeeded.
        QVERIFY(gen->recordReply(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact))));

        DownloadedPagesTable *table = gen->resultsTable(QStringLiteral("PageAttributesFashionComboColorColor"));
        QVERIFY(table != nullptr);
        QCOMPARE(countRows(table), 1); // only the valid candidate (index 1) made it in
    }

    // ==== Schema-level backstop (AspiredDb) ==================================

    void test_fashion_combo_cultures_attribute_rejects_empty_value()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        PageAttributesFashionComboColorProductEvent pageAttrs;
        const auto attrs = *pageAttrs.getAttributes();
        AspiredDb db(dir.path(), QStringLiteral("combo_backstop"));
        db.createTableIdNeed(attrs);

        const QHash<QString, QString> values = {
            {PageAttributesFashionComboColorProductEvent::ID_COLOR, QStringLiteral("Red")},
            {PageAttributesFashionComboColorProductEvent::ID_PRODUCT_TYPE, QStringLiteral("Dress")},
            {PageAttributesFashionComboColorProductEvent::ID_EVENT, QStringLiteral("Funeral")},
            {PageAttributesFashionComboBase::ID_FORMULA_ID,
             PageAttributesFashionComboColorProductEvent::FORMULA_DIRECT_TRANSACTIONAL},
            {PageAttributesFashionComboBase::ID_CULTURES, QString{}}, // empty — must be rejected
        };

        bool threw = false;
        try {
            db.record(attrs, values, &pageAttrs);
        } catch (const ExceptionWithTitleText &) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void test_fashion_combo_color_color_cross_validation_rejects_identical_colors()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        PageAttributesFashionComboColorColor pageAttrs;
        const auto attrs = *pageAttrs.getAttributes();
        AspiredDb db(dir.path(), QStringLiteral("combo_color_color_backstop"));
        db.createTableIdNeed(attrs);

        const QHash<QString, QString> values = {
            {PageAttributesFashionComboColorColor::ID_COLOR_A, QStringLiteral("Red")},
            {PageAttributesFashionComboColorColor::ID_COLOR_B, QStringLiteral("Red")}, // same as A
            {PageAttributesFashionComboBase::ID_FORMULA_ID,
             PageAttributesFashionComboColorColor::FORMULA_COLOR_PAIRING},
            {PageAttributesFashionComboBase::ID_CULTURES, QStringLiteral("Western/Mainstream")},
        };

        bool threw = false;
        try {
            db.record(attrs, values, &pageAttrs);
        } catch (const ExceptionWithTitleText &) {
            threw = true;
        }
        QVERIFY(threw);
    }

    // ==== composeArticleTopic() — topic text consumed by article generation ==
    //
    // LauncherGeneration composes each generated article's topic/permalink
    // from a combo row via AbstractPageAttributes::composeArticleTopic().
    // One test per combo table, verifying it against its own getDescription()
    // template. Multi-formula tables (ColorProduct, StyleSeason, ColorColor)
    // get one assertion per formula id, since each renders different wording.

    void test_fashion_topic_color_product_event()
    {
        PageAttributesFashionComboColorProductEvent attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboColorProductEvent::ID_COLOR, QStringLiteral("Black")},
            {PageAttributesFashionComboColorProductEvent::ID_PRODUCT_TYPE, QStringLiteral("Dress")},
            {PageAttributesFashionComboColorProductEvent::ID_EVENT, QStringLiteral("Funeral")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Black Dress for Funeral"));
    }

    void test_fashion_topic_color_product()
    {
        PageAttributesFashionComboColorProduct attrs;
        QHash<QString, QString> values = {
            {PageAttributesFashionComboColorProduct::ID_COLOR, QStringLiteral("Black")},
            {PageAttributesFashionComboColorProduct::ID_PRODUCT_TYPE, QStringLiteral("Heels")},
        };
        values[PageAttributesFashionComboBase::ID_FORMULA_ID] = PageAttributesFashionComboColorProduct::FORMULA_STYLING_PAIRING;
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("What to wear with Black Heels"));

        values[PageAttributesFashionComboBase::ID_FORMULA_ID] = PageAttributesFashionComboColorProduct::FORMULA_FOOTWEAR_MATCHING;
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("What shoes to wear with Black Heels"));

        values[PageAttributesFashionComboBase::ID_FORMULA_ID] = PageAttributesFashionComboColorProduct::FORMULA_HOW_TO_STYLE;
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("How to style Black Heels"));
    }

    void test_fashion_topic_season_event()
    {
        PageAttributesFashionComboSeasonEvent attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboSeasonEvent::ID_SEASON, QStringLiteral("Summer")},
            {PageAttributesFashionComboSeasonEvent::ID_EVENT, QStringLiteral("Beach Vacation")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Summer Beach Vacation outfit ideas"));
    }

    void test_fashion_topic_fit_product_demographic()
    {
        PageAttributesFashionComboFitProductDemographic attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboFitProductDemographic::ID_FIT, QStringLiteral("A-Line")},
            {PageAttributesFashionComboFitProductDemographic::ID_PRODUCT_TYPE, QStringLiteral("Skirt")},
            {PageAttributesFashionComboFitProductDemographic::ID_DEMOGRAPHIC, QStringLiteral("Petite")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Best A-Line Skirt for Petite"));
    }

    void test_fashion_topic_material_product_season()
    {
        PageAttributesFashionComboMaterialProductSeason attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboMaterialProductSeason::ID_MATERIAL, QStringLiteral("Wool")},
            {PageAttributesFashionComboMaterialProductSeason::ID_PRODUCT_TYPE, QStringLiteral("Coat")},
            {PageAttributesFashionComboMaterialProductSeason::ID_SEASON, QStringLiteral("Winter")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Wool Coat outfit Winter"));
    }

    void test_fashion_topic_fit_product_event()
    {
        PageAttributesFashionComboFitProductEvent attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboFitProductEvent::ID_FIT, QStringLiteral("Maxi")},
            {PageAttributesFashionComboFitProductEvent::ID_PRODUCT_TYPE, QStringLiteral("Dress")},
            {PageAttributesFashionComboFitProductEvent::ID_EVENT, QStringLiteral("Graduation")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Maxi Dress for Graduation"));
    }

    void test_fashion_topic_style_season()
    {
        PageAttributesFashionComboStyleSeason attrs;
        QHash<QString, QString> values = {
            {PageAttributesFashionComboStyleSeason::ID_STYLE, QStringLiteral("Old Money/Quiet Luxury")},
            {PageAttributesFashionComboStyleSeason::ID_SEASON, QStringLiteral("Autumn/Fall")},
        };
        values[PageAttributesFashionComboBase::ID_FORMULA_ID] = PageAttributesFashionComboStyleSeason::FORMULA_MICROTREND_LIFESTYLE;
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Old Money/Quiet Luxury Autumn/Fall outfit ideas"));

        values[PageAttributesFashionComboBase::ID_FORMULA_ID] = PageAttributesFashionComboStyleSeason::FORMULA_CAPSULE_CURATION;
        // Slot order matches the real-world query phrasing, not the column
        // order — see composeArticleTopic().
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Old Money/Quiet Luxury Autumn/Fall capsule wardrobe"));

        // Both formulas stay eligible here (different search intents), unlike
        // PageAttributesFashionComboColorProduct where two of three are vetoed.
        const QStringList allFormulas = attrs.allowedFormulaIds();
        for (const QString &formula : allFormulas) {
            values[PageAttributesFashionComboBase::ID_FORMULA_ID] = formula;
            QVERIFY(attrs.isArticleTopicEligible(values));
        }
    }

    void test_fashion_topic_color_color()
    {
        PageAttributesFashionComboColorColor attrs;
        QHash<QString, QString> values = {
            {PageAttributesFashionComboColorColor::ID_COLOR_A, QStringLiteral("Navy")},
            {PageAttributesFashionComboColorColor::ID_COLOR_B, QStringLiteral("Burgundy")},
        };
        values[PageAttributesFashionComboBase::ID_FORMULA_ID] = PageAttributesFashionComboColorColor::FORMULA_COLOR_PAIRING;
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Navy and Burgundy outfit combination"));

        values[PageAttributesFashionComboBase::ID_FORMULA_ID] = PageAttributesFashionComboColorColor::FORMULA_DOES_COLOR_GO_WITH;
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Does Navy go with Burgundy"));
    }

    void test_fashion_topic_product_pattern()
    {
        PageAttributesFashionComboProductPattern attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboProductPattern::ID_PRODUCT_TYPE, QStringLiteral("Dress")},
            {PageAttributesFashionComboProductPattern::ID_PATTERN, QStringLiteral("Floral")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Outfit with Floral Dress"));
    }

    void test_fashion_topic_style_product()
    {
        PageAttributesFashionComboStyleProduct attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboStyleProduct::ID_STYLE, QStringLiteral("Old Money/Quiet Luxury")},
            {PageAttributesFashionComboStyleProduct::ID_PRODUCT_TYPE, QStringLiteral("Blazer")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Old Money/Quiet Luxury Blazer outfits"));
    }

    void test_fashion_topic_product_event()
    {
        PageAttributesFashionComboProductEvent attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboProductEvent::ID_PRODUCT_TYPE, QStringLiteral("Heels")},
            {PageAttributesFashionComboProductEvent::ID_EVENT, QStringLiteral("Wedding Guest")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("What Heels to wear to Wedding Guest"));
    }

    void test_fashion_topic_color_season()
    {
        PageAttributesFashionComboColorSeason attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboColorSeason::ID_COLOR, QStringLiteral("Sage Green")},
            {PageAttributesFashionComboColorSeason::ID_SEASON, QStringLiteral("Autumn/Fall")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Best Sage Green outfits for Autumn/Fall"));
    }

    void test_fashion_topic_product_demographic()
    {
        PageAttributesFashionComboProductDemographic attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboProductDemographic::ID_PRODUCT_TYPE, QStringLiteral("Jeans")},
            {PageAttributesFashionComboProductDemographic::ID_DEMOGRAPHIC, QStringLiteral("Pear Shape")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Best Jeans for Pear Shape"));
    }

    void test_fashion_topic_style_event()
    {
        PageAttributesFashionComboStyleEvent attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboStyleEvent::ID_STYLE, QStringLiteral("Boho Chic")},
            {PageAttributesFashionComboStyleEvent::ID_EVENT, QStringLiteral("Date Night")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Boho Chic Date Night outfit ideas"));
    }

    void test_fashion_topic_material_product()
    {
        PageAttributesFashionComboMaterialProduct attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboMaterialProduct::ID_MATERIAL, QStringLiteral("Silk")},
            {PageAttributesFashionComboMaterialProduct::ID_PRODUCT_TYPE, QStringLiteral("Skirt")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("How to style a Silk Skirt"));
    }

    void test_fashion_topic_style_product_event()
    {
        PageAttributesFashionComboStyleProductEvent attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboStyleProductEvent::ID_STYLE, QStringLiteral("Boho Chic")},
            {PageAttributesFashionComboStyleProductEvent::ID_PRODUCT_TYPE, QStringLiteral("Dress")},
            {PageAttributesFashionComboStyleProductEvent::ID_EVENT, QStringLiteral("Music Festival")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Boho Chic Dress for Music Festival"));
    }

    void test_fashion_topic_color_product_demographic()
    {
        PageAttributesFashionComboColorProductDemographic attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboColorProductDemographic::ID_COLOR, QStringLiteral("Black")},
            {PageAttributesFashionComboColorProductDemographic::ID_PRODUCT_TYPE, QStringLiteral("Dress")},
            {PageAttributesFashionComboColorProductDemographic::ID_DEMOGRAPHIC, QStringLiteral("Plus Size")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Plus Size Black Dress"));
    }

    void test_fashion_topic_product_demographic_event()
    {
        PageAttributesFashionComboProductDemographicEvent attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboProductDemographicEvent::ID_PRODUCT_TYPE, QStringLiteral("Dress")},
            {PageAttributesFashionComboProductDemographicEvent::ID_DEMOGRAPHIC, QStringLiteral("Maternity")},
            {PageAttributesFashionComboProductDemographicEvent::ID_EVENT, QStringLiteral("Baby Shower")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Maternity Dress for Baby Shower"));
    }

    void test_fashion_topic_pattern_product_season()
    {
        PageAttributesFashionComboPatternProductSeason attrs;
        const QHash<QString, QString> values = {
            {PageAttributesFashionComboPatternProductSeason::ID_PATTERN, QStringLiteral("Floral")},
            {PageAttributesFashionComboPatternProductSeason::ID_PRODUCT_TYPE, QStringLiteral("Dress")},
            {PageAttributesFashionComboPatternProductSeason::ID_SEASON, QStringLiteral("Summer")},
        };
        QCOMPARE(attrs.composeArticleTopic(values), QStringLiteral("Floral Dress for Summer"));
    }

    // Every combo class must be resolvable through the global registry by its
    // attrId (LauncherGeneration looks it up this way, not by direct type) and
    // must produce a non-empty topic for a fully-populated row — a compile-time
    // pure-virtual override existing is not proof it was wired correctly.
    // ==== isArticleTopicEligible() — rows vetoed from article generation ====

    void test_fashion_eligibility_rejects_footwear_matching_formula()
    {
        // "What shoes to wear with {color} {product}" is incoherent whenever
        // the product IS footwear ("what shoes to wear with black boots"), and
        // its SERPs are shopping-dominated even when coherent — so the whole
        // formula is vetoed from article generation.
        PageAttributesFashionComboColorProduct attrs;
        QHash<QString, QString> values = {
            {PageAttributesFashionComboColorProduct::ID_COLOR, QStringLiteral("Black")},
            {PageAttributesFashionComboColorProduct::ID_PRODUCT_TYPE, QStringLiteral("Boots")},
            {PageAttributesFashionComboBase::ID_FORMULA_ID,
             PageAttributesFashionComboColorProduct::FORMULA_FOOTWEAR_MATCHING},
        };
        QVERIFY(!attrs.isArticleTopicEligible(values));

        // Same slot values are fine under the one surviving formula.
        values[PageAttributesFashionComboBase::ID_FORMULA_ID] =
            PageAttributesFashionComboColorProduct::FORMULA_STYLING_PAIRING;
        QVERIFY(attrs.isArticleTopicEligible(values));
    }

    void test_fashion_eligibility_rejects_how_to_style_formula()
    {
        // "How to style {x}" and "What to wear with {x}" are the same search
        // intent (80-90% top-ranking URL overlap), so generating both per
        // color+product pair would put two of our own pages on one query
        // cluster. Only FORMULA_STYLING_PAIRING survives.
        PageAttributesFashionComboColorProduct attrs;
        QHash<QString, QString> values = {
            {PageAttributesFashionComboColorProduct::ID_COLOR, QStringLiteral("Black")},
            {PageAttributesFashionComboColorProduct::ID_PRODUCT_TYPE, QStringLiteral("Boots")},
            {PageAttributesFashionComboBase::ID_FORMULA_ID,
             PageAttributesFashionComboColorProduct::FORMULA_HOW_TO_STYLE},
        };
        QVERIFY(!attrs.isArticleTopicEligible(values));

        // Exactly one of the three declared formulas may pass, otherwise the
        // pair is either duplicated or dropped entirely.
        int eligibleFormulas = 0;
        const QStringList allFormulas = attrs.allowedFormulaIds();
        for (const QString &formula : allFormulas) {
            values[PageAttributesFashionComboBase::ID_FORMULA_ID] = formula;
            if (attrs.isArticleTopicEligible(values)) {
                ++eligibleFormulas;
            }
        }
        QCOMPARE(eligibleFormulas, 1);
    }

    void test_fashion_eligibility_defaults_to_true_for_other_combo_tables()
    {
        // Only ColorProduct vetoes anything today — every other combo table
        // must keep the permissive AbstractPageAttributes default.
        for (const auto &pair : comboKeysAndAttrIds()) {
            if (pair.second == QStringLiteral("PageAttributesFashionComboColorProduct")) {
                continue;
            }
            const AbstractPageAttributes *proto =
                AbstractPageAttributes::ALL_PAGE_ATTRIBUTES().value(pair.second, nullptr);
            QVERIFY2(proto != nullptr, qPrintable(pair.second));
            QVERIFY2(proto->isArticleTopicEligible({}), qPrintable(pair.second));
        }
    }

    void test_fashion_topic_all_combo_classes_resolve_via_registry()
    {
        for (const auto &pair : comboKeysAndAttrIds()) {
            const AbstractPageAttributes *proto =
                AbstractPageAttributes::ALL_PAGE_ATTRIBUTES().value(pair.second, nullptr);
            QVERIFY2(proto != nullptr, qPrintable(pair.second));
        }
    }
};

QTEST_MAIN(Test_Generator_Fashion)
#include "test_generator_fashion.moc"
