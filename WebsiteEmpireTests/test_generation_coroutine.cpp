/**
 * Regression / repro test for the SIGSEGV that occurred in the generation
 * launcher's coroutine pipeline on debug builds when using a long prompt
 * (~8000 chars, from strategies with extensive custom instructions).
 *
 * Root cause: QProcess was heap-allocated via unique_ptr in the coroutine.
 * At -O0, the unique_ptr may be placed on the CPU stack rather than in the
 * coroutine heap frame.  When the coroutine suspends, the stack is unwound
 * and the unique_ptr destructor frees the QProcess.  The pending Qt queued
 * connection (QCoroSignal::finished) then fires on the freed QProcess → SIGSEGV.
 *
 * Fix: stack-allocate QProcess so it always lives in the coroutine heap frame,
 * exactly as ClaudeRunner does in WebsiteAspire.
 *
 * The test replicates:
 *  1. runClaudePromptTest() — subprocess via QProcess in a coroutine frame
 *  2. runFullPipelineOnce() — mirrors runGenerationSession's variable layout:
 *       PageDb + PageRepositoryDb declared at top (non-default-constructible),
 *       QString locals spanning two co_await suspension points, GenPageQueue
 *       calls for buildMetadataPrompt / processContentAndMetadata.
 *
 * A fake "claude" Python script is created in a temp dir and prepended to PATH
 * so no real Claude API key is needed.  The fake script reads stdin (the prompt
 * file) and outputs a fixed ~14 000-char article body.
 */

#include <QtTest>
#include <QCoro/QCoroProcess>
#include <QCoro/QCoroTask>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

#include "website/pages/GenPageQueue.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/attributes/CategoryTable.h"

// ---------------------------------------------------------------------------
// Mirrors runClaudePrompt from LauncherGeneration.cpp
// ---------------------------------------------------------------------------

static QCoro::Task<QString> runClaudePromptTest(QString prompt)
{
    QString result;

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        co_return result;
    }

    const QString promptPath = tempDir.path() + QStringLiteral("/prompt.txt");
    {
        QFile f(promptPath);
        if (!f.open(QIODevice::WriteOnly)) {
            co_return result;
        }
        f.write(prompt.toUtf8());
    }

    QProcess process;
    process.setWorkingDirectory(tempDir.path());
    process.setProgram(QStringLiteral("claude"));
    process.setArguments({QStringLiteral("-p"), QStringLiteral("-"),
                          QStringLiteral("--dangerously-skip-permissions")});
    process.setStandardInputFile(promptPath);

    co_await qCoro(process).start();
    co_await qCoro(process).waitForFinished(-1);

    if (process.error() == QProcess::FailedToStart) {
        result = QStringLiteral("__ERROR__: claude executable not found in PATH");
    } else if (process.exitCode() != 0) {
        result = QStringLiteral("__ERROR__: ")
                 + QString::fromUtf8(process.readAllStandardError()).trimmed();
    } else {
        result = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    }
    co_return result;
}

// ---------------------------------------------------------------------------
// Mirrors runGenerationSession from LauncherGeneration.cpp:
//   - PageDb + PageRepositoryDb at top (non-default-constructible)
//   - PageRecord, QString locals inside the loop spanning both co_awaits
//   - Two calls to runClaudePromptTest, then processContentAndMetadata
// ---------------------------------------------------------------------------

static QCoro::Task<bool> runFullPipelineOnce(GenPageQueue   &queue,
                                              const QDir     &workDir,
                                              const QString  &contentPrompt)
{
    // IMPORTANT: declared before first co_await — mirrors production code.
    PageDb           pageDb(workDir);
    PageRepositoryDb pageRepo(pageDb);

    const PageRecord page = queue.peekNext();
    queue.advance();

    // ---- Call 1: content (same suspension point as production) ---------------
    const QString articleText = co_await runClaudePromptTest(contentPrompt);

    if (articleText.startsWith(QStringLiteral("__ERROR__"))) {
        qWarning() << "Content call failed:" << articleText;
        co_return false;
    }

    // ---- Call 2: metadata (same suspension point as production) --------------
    const QString metadataJson = co_await runClaudePromptTest(
        queue.buildMetadataPrompt(page, articleText));

    const QString safeMetadata = metadataJson.startsWith(QStringLiteral("__ERROR__"))
                                 ? QString{} : metadataJson;

    // ---- For virtual pages (id == 0), create the row first ------------------
    int pageId = page.id;
    if (pageId == 0) {
        pageId = pageRepo.create(page.typeId, page.permalink, page.lang);
    }

    const bool ok = queue.processContentAndMetadata(pageId, articleText,
                                                    safeMetadata, pageRepo);
    if (ok) {
        pageRepo.recordStrategyAttempt(pageId, QStringLiteral("test_strategy"));
    }
    co_return ok;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/** Creates the fake "claude" script in binDir and returns its path. */
static QString createFakeClaude(const QDir &binDir)
{
    const QString path = binDir.filePath(QStringLiteral("claude"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream ts(&f);
    // Python script: read and discard stdin, then output a large article (~14 000 chars).
    ts << "#!/usr/bin/env python3\n"
       << "import sys, os\n"
       << "# Consume the prompt from stdin so the process does not block.\n"
       << "sys.stdin.read()\n"
       << "# Detect whether this is the metadata call by checking argv presence.\n"
       << "# Both calls use the same fake output strategy.\n"
       << "article = '[TITLE level=\"2\"]Understanding Osteoarthritis[/TITLE]\\n\\n'\n"
       << "paragraph = ('Osteoarthritis (OA) is a degenerative joint disease that '\n"
       << "             'affects millions of people worldwide. It is characterized by '\n"
       << "             'the breakdown of cartilage in joints, leading to pain, '\n"
       << "             'stiffness, and reduced mobility. ')\n"
       << "article += paragraph * 200\n"
       << "print(article)\n";
    f.close();
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                     QFile::ReadGroup | QFile::ExeGroup |
                     QFile::ReadOther | QFile::ExeOther);
    return path;
}

/**
 * Builds a content prompt matching the production "Healing" strategy size
 * (~8 000 chars) by appending long custom instructions.
 */
static QString buildLargeContentPrompt(const QString &permalink)
{
    QString prompt;
    prompt.reserve(8200);
    prompt += QStringLiteral("Write comprehensive, high-quality content for a web page "
                              "about the topic described by this permalink: ");
    prompt += permalink;
    prompt += QStringLiteral("\n\nPage type : article\nLanguage  : en\nImages    : no images\n\n"
                              "Write freely and naturally. Cover all important aspects of "
                              "the topic with relevant, engaging content. Include a title, "
                              "introduction, main body with key information, and a "
                              "conclusion. Write entirely in en.\n\n"
                              "Additional instructions:\n");

    // Pad with realistic-looking custom instructions to hit ~8 000 chars total.
    const QString instructionBlock = QStringLiteral(
        "Focus on evidence-based medical information. Cite relevant research where "
        "appropriate. Cover: (1) definition and causes, (2) symptoms and diagnosis, "
        "(3) conventional treatments, (4) alternative and complementary approaches, "
        "(5) lifestyle modifications, (6) prognosis and prevention strategies. "
        "Use a compassionate, patient-centred tone. Avoid sensationalism. "
        "Include practical actionable advice the reader can apply immediately. ");

    while (prompt.size() < 7800) {
        prompt += instructionBlock;
    }

    prompt += QStringLiteral(
        "\n\n── FORMATTING ───────────────────────────────────────────────────────────\n"
        "Use ONLY the shortcodes below. Do NOT use HTML tags.\n\n"
        "Headings: [TITLE level=\"2\"]heading[/TITLE]\n"
        "Bold: [BOLD]text[/BOLD]\n"
        "Return ONLY the article content — no preamble, no meta-commentary.");

    return prompt;
}

} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_GenerationCoroutine : public QObject
{
    Q_OBJECT

private slots:

    // ---- Basic subprocess coroutine: small prompt --------------------------

    void test_gencoroutine_runclaudeprompt_small_prompt_returns_output()
    {
        QTemporaryDir binDir;
        QVERIFY(binDir.isValid());
        QVERIFY(!createFakeClaude(QDir(binDir.path())).isEmpty());

        const QString origPath = qEnvironmentVariable("PATH");
        qputenv("PATH", (binDir.path() + QLatin1Char(':') + origPath).toUtf8());

        QEventLoop loop;
        QString result;

        // Keep the lambda alive through loop.exec() so the coroutine frame has
        // a valid closure pointer when it resumes after co_await.
        auto coro = [&]() -> QCoro::Task<> {
            result = co_await runClaudePromptTest(
                QStringLiteral("small prompt"));
            loop.quit();
        };
        auto task = coro();
        Q_UNUSED(task)

        loop.exec();
        qputenv("PATH", origPath.toUtf8());

        QVERIFY2(!result.startsWith(QStringLiteral("__ERROR__")),
                 qPrintable(result));
        QVERIFY(result.size() > 1000);
    }

    // ---- Basic subprocess coroutine: large prompt (~8 000 chars) -----------

    void test_gencoroutine_runclaudeprompt_large_prompt_returns_output()
    {
        QTemporaryDir binDir;
        QVERIFY(binDir.isValid());
        QVERIFY(!createFakeClaude(QDir(binDir.path())).isEmpty());

        const QString origPath = qEnvironmentVariable("PATH");
        qputenv("PATH", (binDir.path() + QLatin1Char(':') + origPath).toUtf8());

        const QString largePrompt = buildLargeContentPrompt(
            QStringLiteral("/osteoarthritis"));
        QVERIFY(largePrompt.size() >= 7800);

        QEventLoop loop;
        QString result;

        // Keep the lambda alive through loop.exec().
        auto coro = [&]() -> QCoro::Task<> {
            result = co_await runClaudePromptTest(largePrompt);
            loop.quit();
        };
        auto task = coro();
        Q_UNUSED(task)

        loop.exec();
        qputenv("PATH", origPath.toUtf8());

        QVERIFY2(!result.startsWith(QStringLiteral("__ERROR__")),
                 qPrintable(result));
        QVERIFY(result.size() > 1000);
    }

    // ---- Full two-call pipeline: mirrors runGenerationSession exactly -------

    void test_gencoroutine_full_pipeline_small_prompt_saves_page()
    {
        QTemporaryDir binDir;
        QVERIFY(binDir.isValid());
        QVERIFY(!createFakeClaude(QDir(binDir.path())).isEmpty());

        const QString origPath = qEnvironmentVariable("PATH");
        qputenv("PATH", (binDir.path() + QLatin1Char(':') + origPath).toUtf8());

        QTemporaryDir workTmpDir;
        QVERIFY(workTmpDir.isValid());
        const QDir workDir(workTmpDir.path());

        CategoryTable categoryTable(workDir);

        PageRecord vp;
        vp.id        = 0;
        vp.typeId    = QStringLiteral("article");
        vp.permalink = QStringLiteral("/osteoarthritis");
        vp.lang      = QStringLiteral("en");

        GenPageQueue queue(QStringLiteral("article"), false,
                           QList<PageRecord>{vp}, categoryTable);

        const QString contentPrompt = QStringLiteral(
            "Write a short article about osteoarthritis.");

        QEventLoop loop;
        bool pipelineOk = false;

        // Keep the lambda alive through loop.exec().
        auto coro = [&]() -> QCoro::Task<> {
            pipelineOk = co_await runFullPipelineOnce(
                queue, workDir, contentPrompt);
            loop.quit();
        };
        auto task = coro();
        Q_UNUSED(task)

        loop.exec();
        qputenv("PATH", origPath.toUtf8());

        QVERIFY(pipelineOk);

        // Verify the page was persisted and generated_at was set.
        PageDb db(workDir);
        PageRepositoryDb repo(db);
        const QList<PageRecord> generated =
            repo.findGeneratedByTypeId(QStringLiteral("article"));
        QCOMPARE(generated.size(), 1);
        QCOMPARE(generated.at(0).permalink, QStringLiteral("/osteoarthritis"));
    }

    // ---- Full two-call pipeline: LARGE prompt — repro for the SIGSEGV ------
    //
    // This is the case that crashed in production: a strategy with long custom
    // instructions produces an ~8 000-char content prompt.  The large prompt
    // exercises the coroutine frame layout that caused the SIGSEGV in the
    // GCC debug build.

    void test_gencoroutine_full_pipeline_large_prompt_saves_page()
    {
        QTemporaryDir binDir;
        QVERIFY(binDir.isValid());
        QVERIFY(!createFakeClaude(QDir(binDir.path())).isEmpty());

        const QString origPath = qEnvironmentVariable("PATH");
        qputenv("PATH", (binDir.path() + QLatin1Char(':') + origPath).toUtf8());

        QTemporaryDir workTmpDir;
        QVERIFY(workTmpDir.isValid());
        const QDir workDir(workTmpDir.path());

        CategoryTable categoryTable(workDir);

        PageRecord vp;
        vp.id        = 0;
        vp.typeId    = QStringLiteral("article");
        vp.permalink = QStringLiteral("/osteoarthritis");
        vp.lang      = QStringLiteral("en");

        // Build queue with long custom instructions (same as Healing strategy).
        const QString customInstructions = buildLargeContentPrompt(QString{});
        GenPageQueue queue(QStringLiteral("article"), false,
                           QList<PageRecord>{vp}, categoryTable,
                           customInstructions);

        const QString contentPrompt = buildLargeContentPrompt(
            QStringLiteral("/osteoarthritis"));

        QVERIFY(contentPrompt.size() >= 7800);

        QEventLoop loop;
        bool pipelineOk = false;

        // Keep the lambda alive through loop.exec().
        auto coro = [&]() -> QCoro::Task<> {
            pipelineOk = co_await runFullPipelineOnce(
                queue, workDir, contentPrompt);
            loop.quit();
        };
        auto task = coro();
        Q_UNUSED(task)

        loop.exec();
        qputenv("PATH", origPath.toUtf8());

        QVERIFY(pipelineOk);

        PageDb db(workDir);
        PageRepositoryDb repo(db);
        const QList<PageRecord> generated =
            repo.findGeneratedByTypeId(QStringLiteral("article"));
        QCOMPARE(generated.size(), 1);
        QCOMPARE(generated.at(0).permalink, QStringLiteral("/osteoarthritis"));

        // Verify 1_text was saved.
        const QHash<QString, QString> data = repo.loadData(generated.at(0).id);
        QVERIFY(!data.value(QStringLiteral("1_text")).isEmpty());
    }
};

QTEST_MAIN(Test_GenerationCoroutine)
#include "test_generation_coroutine.moc"
