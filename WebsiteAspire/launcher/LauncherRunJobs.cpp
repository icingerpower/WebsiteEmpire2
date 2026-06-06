#include "LauncherRunJobs.h"

#include <csignal>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QException>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

#include <QCoro/QCoroTask>

#include "ClaudeRunner.h"
#include "aspire/generator/AbstractGenerator.h"
#include "workingdirectory/WorkingDirectoryManager.h"
#include "aicli/AbstractCli.h"

const QString LauncherRunJobs::OPTION_NAME     = QStringLiteral("runjobs");
const QString LauncherRunJobs::OPTION_SESSIONS = QStringLiteral("sessions");

DECLARE_LAUNCHER(LauncherRunJobs)

// ---------------------------------------------------------------------------
// SIGINT / SIGTERM — graceful stop after current job(s) finish
// ---------------------------------------------------------------------------

static volatile sig_atomic_t g_stopRequested = 0;

static void handleSignal(int)
{
    g_stopRequested = 1;
}

// ---------------------------------------------------------------------------
// Per-session coroutine (fire-and-forget)
// ---------------------------------------------------------------------------

// NOTE: logDir and sessionIndex are intentionally passed by value.
// runSession() is a coroutine that suspends across co_await points; by the
// time it resumes the caller's stack frame (LauncherRunJobs::run) has already
// returned.  Any reference parameter would dangle after the first suspension.
static QCoro::Task<void> runSession(AbstractGenerator *gen,
                                    QDir               logDir,
                                    QTextStream       *out,
                                    int                sessionIndex,
                                    int               *activeCount,
                                    AbstractCli       *cli)
{
    const int sNum = sessionIndex + 1; // 1-based for display

    while (!g_stopRequested) {
        const QString jobJson = gen->getNextJob();
        if (jobJson.isEmpty()) {
            *out << QStringLiteral("[S%1] No more pending jobs.\n").arg(sNum);
            out->flush();
            break;
        }

        const QJsonDocument jobDoc = QJsonDocument::fromJson(jobJson.toUtf8());
        const QString jobId = jobDoc.object().value(QStringLiteral("jobId")).toString();

        *out << QStringLiteral("[S%1] Running: %2 (%3 pending)\n")
                    .arg(sNum)
                    .arg(jobId)
                    .arg(gen->pendingCount());
        out->flush();

        const ClaudeJobResult result = co_await runClaudeJob(jobJson, cli);
        writeClaudeJobLog(logDir, jobId, result);

        if (!result.processStarted) {
            *out << QStringLiteral("[S%1] FATAL: '%2' not found in PATH. Stopping.\n")
                        .arg(sNum).arg(cli->getExecutable());
            out->flush();
            break;
        }

        if (result.json.isEmpty()) {
            *out << QStringLiteral("[S%1] FAIL: %2 — exit %3, no JSON (%4 ms). See claude_logs/\n")
                        .arg(sNum)
                        .arg(jobId)
                        .arg(result.exitCode)
                        .arg(result.durationMs);
            out->flush();
            continue;
        }

        try {
            if (gen->recordReply(result.json)) {
                *out << QStringLiteral("[S%1] OK: %2 (%3 ms)\n")
                            .arg(sNum)
                            .arg(jobId)
                            .arg(result.durationMs);
            } else {
                *out << QStringLiteral("[S%1] FAIL: %2 — unknown jobId or malformed reply\n")
                            .arg(sNum)
                            .arg(jobId);
            }
        } catch (const QException &ex) {
            *out << QStringLiteral("[S%1] FAIL: %2 — %3\n")
                        .arg(sNum)
                        .arg(jobId)
                        .arg(QString::fromUtf8(ex.what()));
        }
        out->flush();
    }

    if (g_stopRequested) {
        *out << QStringLiteral("[S%1] Stop requested — session finished.\n").arg(sNum);
        out->flush();
    }

    if (--(*activeCount) == 0) {
        QCoreApplication::quit();
    }
}

// ---------------------------------------------------------------------------

QString LauncherRunJobs::getOptionName() const
{
    return OPTION_NAME;
}

void LauncherRunJobs::registerOptions(QCommandLineParser &parser)
{
    parser.addOption(QCommandLineOption(
        OPTION_SESSIONS,
        QCoreApplication::tr("Number of parallel Claude sessions (1-10, default 1)."),
        QStringLiteral("n"),
        QStringLiteral("1")));
}

void LauncherRunJobs::run(const QString &value)
{
    auto *out = new QTextStream(stdout);

    const auto &allGen = AbstractGenerator::ALL_GENERATORS();
    if (!allGen.contains(value)) {
        *out << QStringLiteral("FAILURE\nUnknown generator id: ") << value
             << QStringLiteral("\n");
        out->flush();
        QCoreApplication::quit();
        return;
    }

    // Read --sessions and --cli from the raw argument list.
    const QStringList args = QCoreApplication::arguments();
    int numSessions = 1;
    AbstractCli *cli = nullptr;
    for (int i = 0; i < args.size() - 1; ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--") + OPTION_SESSIONS) {
            bool ok = false;
            const int n = args.at(i + 1).toInt(&ok);
            if (ok && n >= 1 && n <= 10) {
                numSessions = n;
            }
        } else if (arg == QStringLiteral("--") + AbstractLauncher::OPTION_CLI) {
            const QString name = args.at(i + 1);
            for (AbstractCli *c : AbstractCli::ALL_CLIS()) {
                if (c->getName() == name) {
                    cli = c;
                    break;
                }
            }
        }
    }
    if (!cli && !AbstractCli::ALL_CLIS().isEmpty()) {
        cli = AbstractCli::ALL_CLIS().first();
    }
    if (!cli) {
        *out << QStringLiteral("FAILURE\nNo AI CLI registered.\n");
        out->flush();
        QCoreApplication::quit();
        return;
    }

    QDir workDir(WorkingDirectoryManager::instance()->workingDir().path()
                 + QStringLiteral("/generator"));
    workDir.mkpath(QStringLiteral("."));

    const QDir logDir(workDir.filePath(QStringLiteral("claude_logs")));

    AbstractGenerator *gen = allGen.value(value)->createInstance(workDir);

    // Open the results table so recordResultPage() writes to the DB.
    gen->openResultsTable();

    // Install signal handlers for graceful shutdown.
    std::signal(SIGINT,  handleSignal);
    std::signal(SIGTERM, handleSignal);

    *out << QStringLiteral("Starting %1 session(s) for generator '%2' using %3.\n"
                           "Logs: %4\n"
                           "Press Ctrl+C to stop after the current job(s) finish.\n")
                .arg(numSessions)
                .arg(value, cli->getName(), logDir.path());
    out->flush();

    // activeCount is heap-allocated so it outlives this stack frame.
    // It is freed implicitly when the app exits.
    auto *activeCount = new int(numSessions);

    for (int i = 0; i < numSessions; ++i) {
        // Fire-and-forget: QCoro keeps the coroutine alive via the awaitable
        // registered with the event loop for each co_await inside runSession().
        runSession(gen, logDir, out, i, activeCount, cli);
    }
    // exec() in main() drives the event loop until all sessions call quit().
}
