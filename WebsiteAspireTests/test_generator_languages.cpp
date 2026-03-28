#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>

#include "aspire/attributes/languages/PageAttributesLangIdiom.h"
#include "aspire/attributes/languages/PageAttributesLangTag.h"
#include "aspire/attributes/languages/PageAttributesLangWord.h"
#include "aspire/downloader/DownloadedPagesTable.h"
#include "aspire/generator/AbstractGenerator.h"
#include "aspire/generator/GeneratorLanguages.h"

// ---------------------------------------------------------------------------
// Test fixtures helpers
// ---------------------------------------------------------------------------

static const QStringList TEST_LANGS = {QStringLiteral("fr"), QStringLiteral("de")};

static bool writeTestWords(const QString &path, const QStringList &words = {})
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    const QStringList w = words.isEmpty()
                              ? QStringList{QStringLiteral("apple"),
                                            QStringLiteral("run"),
                                            QStringLiteral("house"),
                                            QStringLiteral("beautiful"),
                                            QStringLiteral("eat")}
                              : words;
    for (const QString &word : w) {
        out << word << "\n";
    }
    return true;
}

struct Fixture {
    QTemporaryDir tmpDir;
    QString       wordsPath;

    Fixture() : wordsPath(tmpDir.path() + QStringLiteral("/words.txt")) {}

    bool setup(const QStringList &words = {})
    {
        return tmpDir.isValid() && writeTestWords(wordsPath, words);
    }

    GeneratorLanguages *makeGen(QObject *parent = nullptr) const
    {
        return new GeneratorLanguages(QDir(tmpDir.path()), wordsPath, TEST_LANGS, parent);
    }

    QString iniPath() const
    {
        return QDir(tmpDir.path()).filePath(QStringLiteral("language-learning.ini"));
    }
};

// Parse a raw JSON job string into an object.
static QJsonObject jobObj(const QString &raw)
{
    return QJsonDocument::fromJson(raw.toUtf8()).object();
}

// ---------------------------------------------------------------------------
// Reply builders
// ---------------------------------------------------------------------------

static QString makeTagsReply(const QString &jobId,
                              const QStringList &tagNames,
                              double percentage = 75.0)
{
    QJsonObject reply;
    reply[QStringLiteral("jobId")] = jobId;
    QJsonArray tags;
    for (const QString &n : tagNames) {
        QJsonObject t;
        t[QStringLiteral("name")]       = n;
        t[QStringLiteral("percentage")] = percentage;
        tags << t;
    }
    reply[QStringLiteral("tags")] = tags;
    return QString::fromUtf8(QJsonDocument(reply).toJson());
}

static QJsonArray makeIdioms(const QStringList &phrases, const QString &tag)
{
    QJsonArray arr;
    for (const QString &ph : phrases) {
        QJsonObject idiom;
        idiom[QStringLiteral("phrase")] = ph;
        idiom[QStringLiteral("tags")]   = QJsonArray{tag};
        arr << idiom;
    }
    return arr;
}

static QString makeWordEnReply(const QString &jobId,
                                const QStringList &tags,
                                const QStringList &idioms)
{
    QJsonObject reply;
    reply[QStringLiteral("jobId")] = jobId;
    QJsonArray tArr;
    for (const QString &t : tags) {
        tArr << t;
    }
    reply[QStringLiteral("tags")]   = tArr;
    reply[QStringLiteral("idioms")] = makeIdioms(idioms, tags.isEmpty()
                                                             ? QStringLiteral("general")
                                                             : tags.first());
    return QString::fromUtf8(QJsonDocument(reply).toJson());
}

static QString makeTransReply(const QString &jobId,
                               const QString &translatedWord,
                               const QStringList &tags,
                               const QStringList &idioms)
{
    QJsonObject reply;
    reply[QStringLiteral("jobId")]          = jobId;
    reply[QStringLiteral("translatedWord")] = translatedWord;
    QJsonArray tArr;
    for (const QString &t : tags) {
        tArr << t;
    }
    reply[QStringLiteral("tags")]   = tArr;
    reply[QStringLiteral("idioms")] = makeIdioms(idioms, tags.isEmpty()
                                                             ? QStringLiteral("general")
                                                             : tags.first());
    return QString::fromUtf8(QJsonDocument(reply).toJson());
}

static QString makeMissingReply(const QString &jobId,
                                 const QList<QPair<QString, QStringList>> &wordsAndIdioms)
{
    QJsonObject reply;
    reply[QStringLiteral("jobId")] = jobId;
    QJsonArray words;
    int rank = 10001;
    for (const auto &pair : wordsAndIdioms) {
        QJsonObject w;
        w[QStringLiteral("word")]   = pair.first;
        w[QStringLiteral("rank")]   = rank++;
        w[QStringLiteral("tags")]   = QJsonArray{QStringLiteral("grammar")};
        w[QStringLiteral("idioms")] = makeIdioms(pair.second, QStringLiteral("grammar"));
        words << w;
    }
    reply[QStringLiteral("words")] = words;
    return QString::fromUtf8(QJsonDocument(reply).toJson());
}

// Count rows in a result DB table.
static int countDbRows(const QString &dbPath)
{
    if (!QFile::exists(dbPath)) {
        return 0;
    }
    const QString conn = QStringLiteral("count_") +
                         QUuid::createUuid().toString(QUuid::WithoutBraces);
    int count = 0;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT COUNT(*) FROM records")) && q.next()) {
                count = q.value(0).toInt();
            }
        }
    }
    QSqlDatabase::removeDatabase(conn);
    return count;
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------
class Test_Generator_Languages : public QObject
{
    Q_OBJECT

private slots:

    // ==== slugify ==========================================================

    void test_slugify_lowercase()
    {
        QCOMPARE(GeneratorLanguages::slugify(QStringLiteral("Apple")),
                 QStringLiteral("apple"));
    }

    void test_slugify_spaces_to_hyphens()
    {
        QCOMPARE(GeneratorLanguages::slugify(QStringLiteral("run fast")),
                 QStringLiteral("run-fast"));
    }

    void test_slugify_drops_non_ascii()
    {
        QCOMPARE(GeneratorLanguages::slugify(QString::fromUtf8("caf\xc3\xa9")),
                 QStringLiteral("caf"));
    }

    void test_slugify_keeps_hyphen_and_dot()
    {
        QCOMPARE(GeneratorLanguages::slugify(QStringLiteral("well-being.v2")),
                 QStringLiteral("well-being.v2"));
    }

    // ==== Job-ID helpers ===================================================

    void test_tags_job_id()
    {
        QCOMPARE(GeneratorLanguages::tagsJobId(0),  QStringLiteral("tags/0"));
        QCOMPARE(GeneratorLanguages::tagsJobId(3),  QStringLiteral("tags/3"));
    }

    void test_word_en_job_id()
    {
        QCOMPARE(GeneratorLanguages::wordEnJobId(QStringLiteral("apple")),
                 QStringLiteral("word/en/apple"));
    }

    void test_trans_job_id()
    {
        QCOMPARE(GeneratorLanguages::transJobId(QStringLiteral("fr"),
                                                QStringLiteral("apple")),
                 QStringLiteral("trans/fr/apple"));
    }

    void test_missing_job_id()
    {
        QCOMPARE(GeneratorLanguages::missingJobId(QStringLiteral("fr")),
                 QStringLiteral("missing/fr"));
    }

    void test_is_tags_job_id()
    {
        QVERIFY( GeneratorLanguages::isTagsJobId(QStringLiteral("tags/0")));
        QVERIFY(!GeneratorLanguages::isTagsJobId(QStringLiteral("word/en/apple")));
        QVERIFY(!GeneratorLanguages::isTagsJobId(QStringLiteral("trans/fr/apple")));
    }

    void test_is_word_en_job_id()
    {
        QVERIFY( GeneratorLanguages::isWordEnJobId(QStringLiteral("word/en/apple")));
        QVERIFY(!GeneratorLanguages::isWordEnJobId(QStringLiteral("trans/fr/apple")));
        QVERIFY(!GeneratorLanguages::isWordEnJobId(QStringLiteral("tags/0")));
    }

    void test_is_trans_job_id()
    {
        QVERIFY( GeneratorLanguages::isTransJobId(QStringLiteral("trans/fr/apple")));
        QVERIFY(!GeneratorLanguages::isTransJobId(QStringLiteral("word/en/apple")));
        QVERIFY(!GeneratorLanguages::isTransJobId(QStringLiteral("missing/fr")));
    }

    void test_is_missing_job_id()
    {
        QVERIFY( GeneratorLanguages::isMissingJobId(QStringLiteral("missing/fr")));
        QVERIFY(!GeneratorLanguages::isMissingJobId(QStringLiteral("tags/0")));
        QVERIFY(!GeneratorLanguages::isMissingJobId(QStringLiteral("trans/fr/apple")));
    }

    void test_page_from_tags_job_id()
    {
        QCOMPARE(GeneratorLanguages::pageFromTagsJobId(QStringLiteral("tags/0")), 0);
        QCOMPARE(GeneratorLanguages::pageFromTagsJobId(QStringLiteral("tags/5")), 5);
    }

    void test_lang_code_from_word_en_job_id()
    {
        QCOMPARE(GeneratorLanguages::langCodeFromJobId(QStringLiteral("word/en/apple")),
                 QStringLiteral("en"));
    }

    void test_lang_code_from_trans_job_id()
    {
        QCOMPARE(GeneratorLanguages::langCodeFromJobId(QStringLiteral("trans/fr/apple")),
                 QStringLiteral("fr"));
    }

    void test_word_slug_from_word_en_job_id()
    {
        QCOMPARE(GeneratorLanguages::wordSlugFromJobId(QStringLiteral("word/en/apple")),
                 QStringLiteral("apple"));
    }

    void test_word_slug_from_trans_job_id()
    {
        QCOMPARE(GeneratorLanguages::wordSlugFromJobId(QStringLiteral("trans/fr/apple")),
                 QStringLiteral("apple"));
    }

    // ==== buildInitialJobIds ===============================================

    void test_initial_jobs_first_is_tags0()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        QVERIFY(!ids.isEmpty());
        QCOMPARE(ids.first(), QStringLiteral("tags/0"));
    }

    void test_initial_jobs_contains_word_en_for_each_word()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        QVERIFY(ids.contains(QStringLiteral("word/en/apple")));
        QVERIFY(ids.contains(QStringLiteral("word/en/run")));
        QVERIFY(ids.contains(QStringLiteral("word/en/house")));
    }

    void test_initial_jobs_contains_missing_per_lang()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        QVERIFY(ids.contains(QStringLiteral("missing/fr")));
        QVERIFY(ids.contains(QStringLiteral("missing/de")));
    }

    void test_initial_jobs_missing_after_words()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        const int wordIdx    = ids.indexOf(QStringLiteral("word/en/apple"));
        const int missingIdx = ids.indexOf(QStringLiteral("missing/fr"));
        QVERIFY(wordIdx   != -1);
        QVERIFY(missingIdx != -1);
        QVERIFY(wordIdx < missingIdx);
    }

    void test_initial_jobs_all_unique()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        const QSet<QString> idSet(ids.begin(), ids.end());
        QCOMPARE(idSet.size(), ids.size());
    }

    void test_initial_jobs_no_trans_jobs()
    {
        // Trans jobs are discovered, not initial.
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        for (const QString &id : gen->getAllJobIds()) {
            QVERIFY(!GeneratorLanguages::isTransJobId(id));
        }
    }

    void test_missing_file_gives_empty_jobs()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QScopedPointer<GeneratorLanguages> gen(
            new GeneratorLanguages(QDir(tmp.path()),
                                   tmp.path() + QStringLiteral("/nonexistent.txt"),
                                   TEST_LANGS));
        // Only tags/0 and missing/* jobs; no word/en/* jobs.
        for (const QString &id : gen->getAllJobIds()) {
            QVERIFY(!GeneratorLanguages::isWordEnJobId(id));
        }
    }

    // ==== Payload structure ================================================

    void test_tags_payload_has_task()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        const QString raw = gen->getNextJob(); // tags/0 is first
        QCOMPARE(jobObj(raw).value(QStringLiteral("task")).toString(),
                 GeneratorLanguages::TASK_TAGS);
    }

    void test_tags_payload_has_job_id()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        const QString jobId = jobObj(gen->getNextJob())
                                  .value(QStringLiteral("replyFormat")).toObject()
                                  .value(QStringLiteral("jobId")).toString();
        QCOMPARE(jobId, QStringLiteral("tags/0"));
    }

    // Helper: consume all initial tags/<N> jobs and return the raw JSON of the
    // first word/en job.  Records a minimal reply for each tag job so state
    // advances correctly.
    static QString consumeTagPagesGetFirstWord(GeneratorLanguages *gen)
    {
        for (int p = 0; p < GeneratorLanguages::INITIAL_TAGS_PAGES; ++p) {
            const QString raw = gen->getNextJob();
            gen->recordReply(makeTagsReply(GeneratorLanguages::tagsJobId(p),
                                           {QStringLiteral("tag%1").arg(p)}));
            (void)raw;
        }
        return gen->getNextJob(); // first word/en job
    }

    void test_word_en_payload_has_task()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();
        const QString raw = consumeTagPagesGetFirstWord(gen.data());
        QCOMPARE(jobObj(raw).value(QStringLiteral("task")).toString(),
                 GeneratorLanguages::TASK_WORD_EN);
    }

    void test_word_en_payload_has_word_field()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();
        const QJsonObject obj = jobObj(consumeTagPagesGetFirstWord(gen.data()));
        QVERIFY(!obj.value(QStringLiteral("word")).toString().isEmpty());
    }

    void test_word_en_payload_has_rank()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();
        const QJsonObject obj = jobObj(consumeTagPagesGetFirstWord(gen.data()));
        QVERIFY(obj.value(QStringLiteral("rank")).toInt() >= 1);
    }

    void test_word_en_payload_has_idioms_needed()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();
        const QJsonObject obj = jobObj(consumeTagPagesGetFirstWord(gen.data()));
        QCOMPARE(obj.value(QStringLiteral("idiomsNeeded")).toInt(),
                 GeneratorLanguages::IDIOMS_PER_WORD);
    }

    // ==== Full round-trip: tags → word/en → trans → missing ===============

    void test_tags_reply_records_tags()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();
        gen->getNextJob(); // tags/0

        const QString reply = makeTagsReply(
            QStringLiteral("tags/0"),
            {QStringLiteral("food"), QStringLiteral("travel"), QStringLiteral("sport")});
        QVERIFY(gen->recordReply(reply));

        const QString dbPath = QDir(fx.tmpDir.path())
                                   .filePath(QStringLiteral("results_db/PageAttributesLangTag.db"));
        QCOMPARE(countDbRows(dbPath), 3);
    }

    void test_tags_reply_full_batch_spawns_continuation()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();
        gen->getNextJob(); // tags/0

        // Build a reply with exactly MAX_TAGS_PER_JOB entries.
        QStringList tags;
        for (int i = 0; i < GeneratorLanguages::MAX_TAGS_PER_JOB; ++i) {
            tags << QStringLiteral("tag%1").arg(i);
        }
        gen->recordReply(makeTagsReply(QStringLiteral("tags/0"), tags));

        const int before = gen->pendingCount();
        // tags/1 should now be in the pending queue.
        bool foundTags1 = false;
        while (gen->pendingCount() > 0) {
            const QString job = gen->getNextJob();
            if (jobObj(job).value(QStringLiteral("replyFormat")).toObject()
                    .value(QStringLiteral("jobId")).toString() == QLatin1String("tags/1")) {
                foundTags1 = true;
                break;
            }
            gen->recordReply(makeWordEnReply(
                jobObj(job).value(QStringLiteral("replyFormat")).toObject()
                    .value(QStringLiteral("jobId")).toString(),
                {QStringLiteral("tag0")},
                {QStringLiteral("Dummy phrase")}));
        }
        QVERIFY(foundTags1);
        (void)before;
    }

    void test_word_en_reply_records_word()
    {
        Fixture fx;
        QVERIFY(fx.setup({QStringLiteral("apple")}));
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();

        gen->getNextJob(); // tags/0
        gen->recordReply(makeTagsReply(QStringLiteral("tags/0"), {QStringLiteral("food")}));

        gen->getNextJob(); // word/en/apple
        gen->recordReply(makeWordEnReply(
            QStringLiteral("word/en/apple"),
            {QStringLiteral("food")},
            {QStringLiteral("An apple a day"), QStringLiteral("Apple pie recipe")}));

        const QString dbPath = QDir(fx.tmpDir.path())
                                   .filePath(QStringLiteral("results_db/PageAttributesLangWord.db"));
        QCOMPARE(countDbRows(dbPath), 1);
    }

    void test_word_en_reply_records_idioms()
    {
        Fixture fx;
        QVERIFY(fx.setup({QStringLiteral("apple")}));
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();

        gen->getNextJob();
        gen->recordReply(makeTagsReply(QStringLiteral("tags/0"), {QStringLiteral("food")}));
        gen->getNextJob();
        gen->recordReply(makeWordEnReply(
            QStringLiteral("word/en/apple"),
            {QStringLiteral("food")},
            {QStringLiteral("Apple one"), QStringLiteral("Apple two"),
             QStringLiteral("Apple three")}));

        const QString dbPath = QDir(fx.tmpDir.path())
                                   .filePath(QStringLiteral("results_db/PageAttributesLangIdiom.db"));
        QCOMPARE(countDbRows(dbPath), 3);
    }

    void test_word_en_reply_spawns_trans_jobs()
    {
        Fixture fx;
        QVERIFY(fx.setup({QStringLiteral("apple")}));
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();

        gen->getNextJob(); // tags/0
        gen->recordReply(makeTagsReply(QStringLiteral("tags/0"), {QStringLiteral("food")}));

        const int pendingBefore = gen->pendingCount();
        gen->getNextJob(); // word/en/apple
        gen->recordReply(makeWordEnReply(
            QStringLiteral("word/en/apple"),
            {QStringLiteral("food")},
            {QStringLiteral("Apple phrase")}));

        // Should have spawned 2 trans jobs (fr + de).
        QCOMPARE(gen->pendingCount(), pendingBefore - 1 + 2);
    }

    void test_trans_reply_records_word()
    {
        Fixture fx;
        QVERIFY(fx.setup({QStringLiteral("apple")}));
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();

        gen->getNextJob(); // tags/0
        gen->recordReply(makeTagsReply(QStringLiteral("tags/0"), {QStringLiteral("food")}));
        gen->getNextJob(); // word/en/apple
        gen->recordReply(makeWordEnReply(QStringLiteral("word/en/apple"),
                                         {QStringLiteral("food")},
                                         {QStringLiteral("Apple phrase")}));

        // Advance to trans/fr/apple (first discovered job).
        QString transJob;
        while (gen->pendingCount() > 0) {
            const QString raw = gen->getNextJob();
            const QString jid = jobObj(raw).value(QStringLiteral("replyFormat"))
                                    .toObject().value(QStringLiteral("jobId")).toString();
            if (GeneratorLanguages::isTransJobId(jid) &&
                GeneratorLanguages::langCodeFromJobId(jid) == QLatin1String("fr")) {
                transJob = jid;
                gen->recordReply(makeTransReply(
                    jid, QStringLiteral("pomme"), {QStringLiteral("food")},
                    {QStringLiteral("La pomme est rouge")}));
                break;
            }
            // Skip non-trans jobs.
            gen->recordReply(makeWordEnReply(jid, {QStringLiteral("food")},
                                              {QStringLiteral("phrase")}));
        }

        const QString dbPath = QDir(fx.tmpDir.path())
                                   .filePath(QStringLiteral("results_db/PageAttributesLangWord.db"));
        // 1 English + 1 French word.
        QCOMPARE(countDbRows(dbPath), 2);
    }

    void test_missing_reply_records_words_and_idioms()
    {
        Fixture fx;
        QVERIFY(fx.setup({QStringLiteral("apple")}));
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();

        // Drain all initial + discovered jobs quickly.
        while (gen->pendingCount() > 0) {
            const QString raw = gen->getNextJob();
            const QString jid = jobObj(raw).value(QStringLiteral("replyFormat"))
                                    .toObject().value(QStringLiteral("jobId")).toString();
            if (GeneratorLanguages::isTagsJobId(jid)) {
                gen->recordReply(makeTagsReply(jid, {QStringLiteral("food")}));
            } else if (GeneratorLanguages::isWordEnJobId(jid)) {
                gen->recordReply(makeWordEnReply(jid, {QStringLiteral("food")},
                                                  {QStringLiteral("A phrase")}));
            } else if (GeneratorLanguages::isTransJobId(jid)) {
                gen->recordReply(makeTransReply(jid, QStringLiteral("mot"),
                                                {QStringLiteral("food")},
                                                {QStringLiteral("Une phrase")}));
            } else if (GeneratorLanguages::isMissingJobId(jid)) {
                gen->recordReply(makeMissingReply(
                    jid,
                    {{QStringLiteral("être"),
                      {QStringLiteral("Je suis"), QStringLiteral("Tu es")}}}));
            }
        }

        const QString wordDbPath = QDir(fx.tmpDir.path())
                                       .filePath(QStringLiteral("results_db/PageAttributesLangWord.db"));
        // apple(en) + mot(fr) + mot(de) + être(fr) = 4 rows minimum
        QVERIFY(countDbRows(wordDbPath) >= 4);
    }

    // ==== Uniqueness: replaying the same reply must not add duplicate rows ===

    void test_no_duplicate_tags_on_replay()
    {
        Fixture fx;
        QVERIFY(fx.setup({QStringLiteral("apple")}));
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();

        gen->getNextJob();
        const QString reply = makeTagsReply(QStringLiteral("tags/0"),
                                             {QStringLiteral("food"), QStringLiteral("travel")});
        gen->recordReply(reply);
        const QString dbPath = QDir(fx.tmpDir.path())
                                   .filePath(QStringLiteral("results_db/PageAttributesLangTag.db"));
        QCOMPARE(countDbRows(dbPath), 2);

        // Feed the same tags again via a new generator instance on the same dir.
        QScopedPointer<GeneratorLanguages> gen2(
            new GeneratorLanguages(QDir(fx.tmpDir.path()), fx.wordsPath, TEST_LANGS));
        gen2->openResultsTable();
        gen2->getNextJob(); // tags/0 is already done so this returns word/en/apple
        // Directly call recordReply with another tags/0 reply to test dedup.
        gen2->recordReply(reply); // should be rejected: jobId already done
        QCOMPARE(countDbRows(dbPath), 2); // no new rows
    }

    void test_no_duplicate_words_on_replay()
    {
        Fixture fx;
        QVERIFY(fx.setup({QStringLiteral("apple")}));
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();

        gen->getNextJob(); // tags/0
        gen->recordReply(makeTagsReply(QStringLiteral("tags/0"), {QStringLiteral("food")}));
        gen->getNextJob(); // word/en/apple
        const QString wordReply = makeWordEnReply(QStringLiteral("word/en/apple"),
                                                   {QStringLiteral("food")},
                                                   {QStringLiteral("Apple phrase one")});
        gen->recordReply(wordReply);

        const QString dbPath = QDir(fx.tmpDir.path())
                                   .filePath(QStringLiteral("results_db/PageAttributesLangWord.db"));
        const int rowsAfterFirst = countDbRows(dbPath);

        // Second generator on same dir: word/en/apple is already done.
        QScopedPointer<GeneratorLanguages> gen2(
            new GeneratorLanguages(QDir(fx.tmpDir.path()), fx.wordsPath, TEST_LANGS));
        gen2->openResultsTable();
        gen2->recordReply(wordReply); // jobId already done — must be rejected
        QCOMPARE(countDbRows(dbPath), rowsAfterFirst);
    }

    void test_no_duplicate_idioms_same_job()
    {
        Fixture fx;
        QVERIFY(fx.setup({QStringLiteral("apple")}));
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();

        gen->getNextJob();
        gen->recordReply(makeTagsReply(QStringLiteral("tags/0"), {QStringLiteral("food")}));
        gen->getNextJob();
        // Two idiom entries with the same phrase — only one should be inserted.
        QJsonObject reply;
        reply[QStringLiteral("jobId")] = QStringLiteral("word/en/apple");
        reply[QStringLiteral("tags")]  = QJsonArray{QStringLiteral("food")};
        QJsonObject idiomObj;
        idiomObj[QStringLiteral("phrase")] = QStringLiteral("Duplicate phrase");
        idiomObj[QStringLiteral("tags")]   = QJsonArray{QStringLiteral("food")};
        reply[QStringLiteral("idioms")] = QJsonArray{idiomObj, idiomObj}; // same phrase twice
        gen->recordReply(QString::fromUtf8(QJsonDocument(reply).toJson()));

        const QString dbPath = QDir(fx.tmpDir.path())
                                   .filePath(QStringLiteral("results_db/PageAttributesLangIdiom.db"));
        QCOMPARE(countDbRows(dbPath), 1); // not 2
    }

    void test_no_duplicate_trans_words()
    {
        // If two English words translate to the same French word, the second
        // insert should be silently dropped (same word+lang_code conflict).
        Fixture fx;
        QVERIFY(fx.setup({QStringLiteral("big"), QStringLiteral("large")}));
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();

        gen->getNextJob(); // tags/0
        gen->recordReply(makeTagsReply(QStringLiteral("tags/0"), {QStringLiteral("size")}));
        gen->getNextJob(); // word/en/big
        gen->recordReply(makeWordEnReply(QStringLiteral("word/en/big"),
                                          {QStringLiteral("size")},
                                          {QStringLiteral("Big deal")}));
        gen->getNextJob(); // word/en/large
        gen->recordReply(makeWordEnReply(QStringLiteral("word/en/large"),
                                          {QStringLiteral("size")},
                                          {QStringLiteral("Large order")}));

        // Find and record trans/fr/big → "grand".
        // Find and record trans/fr/large → also "grand" (duplicate).
        int transCount = 0;
        while (gen->pendingCount() > 0 && transCount < 2) {
            const QString raw = gen->getNextJob();
            const QString jid = jobObj(raw).value(QStringLiteral("replyFormat"))
                                    .toObject().value(QStringLiteral("jobId")).toString();
            if (GeneratorLanguages::isTransJobId(jid) &&
                GeneratorLanguages::langCodeFromJobId(jid) == QLatin1String("fr")) {
                gen->recordReply(makeTransReply(jid, QStringLiteral("grand"),
                                                 {QStringLiteral("size")},
                                                 {QStringLiteral("C'est grand")}));
                ++transCount;
            } else {
                gen->recordReply(makeWordEnReply(jid, {QStringLiteral("size")},
                                                  {QStringLiteral("phrase")}));
            }
        }

        // Regardless of how many trans jobs returned "grand", the DB should
        // have at most 1 French word "grand".
        const QString dbPath = QDir(fx.tmpDir.path())
                                   .filePath(QStringLiteral("results_db/PageAttributesLangWord.db"));
        QSqlDatabase db = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"),
            QStringLiteral("dedup_check_") +
                QUuid::createUuid().toString(QUuid::WithoutBraces));
        db.setDatabaseName(dbPath);
        int grandCount = 0;
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM records WHERE %1 = ? AND %2 = ?")
                      .arg(PageAttributesLangWord::ID_WORD,
                           PageAttributesLangWord::ID_LANG_CODE));
            q.addBindValue(QStringLiteral("grand"));
            q.addBindValue(QStringLiteral("fr"));
            if (q.exec() && q.next()) {
                grandCount = q.value(0).toInt();
            }
        }
        const QString connName = db.connectionName();
        db.close();
        QSqlDatabase::removeDatabase(connName);

        QVERIFY(grandCount <= 1);
    }

    // ==== defaultLangCodes =================================================

    // ==== Multiple initial tag pages =======================================

    void test_initial_jobs_has_multiple_tag_pages()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        for (int p = 0; p < GeneratorLanguages::INITIAL_TAGS_PAGES; ++p) {
            QVERIFY(ids.contains(GeneratorLanguages::tagsJobId(p)));
        }
    }

    void test_initial_tag_pages_come_before_word_jobs()
    {
        Fixture fx;
        QVERIFY(fx.setup());
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        const QStringList ids = gen->getAllJobIds();
        const int lastTagPage =
            ids.indexOf(GeneratorLanguages::tagsJobId(GeneratorLanguages::INITIAL_TAGS_PAGES - 1));
        const int firstWord = ids.indexOf(QStringLiteral("word/en/apple"));
        QVERIFY(lastTagPage != -1 && firstWord != -1);
        QVERIFY(lastTagPage < firstWord);
    }

    // ==== Context capping keeps payloads bounded ===========================

    void test_word_payload_idioms_capped()
    {
        // Generate more than MAX_CONTEXT_ITEMS English idioms, then verify
        // that the next word payload contains at most MAX_CONTEXT_ITEMS idioms.
        const int overCap = GeneratorLanguages::MAX_CONTEXT_ITEMS + 5;
        QStringList manyWords;
        for (int i = 0; i < overCap; ++i) {
            manyWords << QStringLiteral("word%1").arg(i);
        }
        Fixture fx;
        QVERIFY(fx.setup(manyWords));
        QScopedPointer<GeneratorLanguages> gen(fx.makeGen());
        gen->openResultsTable();

        // Drain tags pages.
        for (int p = 0; p < GeneratorLanguages::INITIAL_TAGS_PAGES; ++p) {
            gen->getNextJob();
            gen->recordReply(makeTagsReply(GeneratorLanguages::tagsJobId(p),
                                           {QStringLiteral("tag%1").arg(p)}));
        }

        // Record one idiom per word until we exceed MAX_CONTEXT_ITEMS words.
        for (int i = 0; i < overCap; ++i) {
            const QString jobId =
                GeneratorLanguages::wordEnJobId(GeneratorLanguages::slugify(manyWords.at(i)));
            gen->getNextJob();
            gen->recordReply(makeWordEnReply(jobId, {QStringLiteral("tag0")},
                                              {QStringLiteral("idiom for %1").arg(manyWords.at(i))}));
        }

        // The NEXT word/en payload must have existingIdioms.size() <= MAX_CONTEXT_ITEMS.
        const QString nextJob = gen->getNextJob();
        const QJsonObject obj = jobObj(nextJob);
        const int idiomCount  = obj.value(QStringLiteral("existingIdioms")).toArray().size();
        QVERIFY(idiomCount <= GeneratorLanguages::MAX_CONTEXT_ITEMS);
    }

    // ==== defaultLangCodes =================================================

    void test_default_lang_codes_count()
    {
        QCOMPARE(GeneratorLanguages::defaultLangCodes().size(), 40);
    }

    void test_default_lang_codes_no_english()
    {
        QVERIFY(!GeneratorLanguages::defaultLangCodes().contains(QStringLiteral("en")));
    }

    void test_default_lang_codes_includes_french()
    {
        QVERIFY(GeneratorLanguages::defaultLangCodes().contains(QStringLiteral("fr")));
    }
};

QTEST_MAIN(Test_Generator_Languages)
#include "test_generator_languages.moc"
