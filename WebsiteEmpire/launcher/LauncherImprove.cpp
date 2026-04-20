#include "LauncherImprove.h"

#include "gui/panes/GenStrategyTable.h"
#include "website/AbstractEngine.h"
#include "website/HostTable.h"
#include "website/WebsiteSettingsTable.h"
#include "website/pages/GenPageQueue.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/PageRecord.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/perf/GscDataSource.h"
#include "website/perf/GscSettings.h"
#include "website/perf/StatsDbDataSource.h"
#include "website/perf/AbstractPerformanceDataSource.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include <csignal>
#include <atomic>
#include <algorithm>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

#include <QCoro/QCoroProcess>
#include <QCoro/QCoroTask>

const QString LauncherImprove::OPTION_NAME           = QStringLiteral("improve");
const QString LauncherImprove::OPTION_SESSIONS        = QStringLiteral("sessions");
const QString LauncherImprove::OPTION_LIMIT           = QStringLiteral("limit");
const QString LauncherImprove::OPTION_MAX_IMPRESSIONS = QStringLiteral("max-impressions");

DECLARE_LAUNCHER(LauncherImprove)

// ---------------------------------------------------------------------------
// Signal handling
// ---------------------------------------------------------------------------

static volatile sig_atomic_t g_improveStop = 0;

static void handleImproveSignal(int) { g_improveStop = 1; }

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------

struct ImproveRunState {
    std::atomic<int> jobsCompleted{0};
    int              jobsLimit   = -1;
    int              activeCount = 0;
    QTextStream     *out         = nullptr;
};

// ---------------------------------------------------------------------------
// Job descriptor — one page × one strategy to try next
// ---------------------------------------------------------------------------

struct ImproveJob {
    PageRecord page;
    QString    strategyId;
    QString    customInstructions;
    bool       nonSvgImages = false;
    QString    extraContext;
};

// ---------------------------------------------------------------------------
// claude CLI helper — mirrors ClaudeRunner in WebsiteAspire; no API key needed
// ---------------------------------------------------------------------------

static QCoro::Task<QString> runClaudePrompt(const QString &prompt)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        co_return QStringLiteral("__ERROR__: failed to create temp dir");
    }

    const QString promptPath = tempDir.path() + QStringLiteral("/prompt.txt");
    {
        QFile f(promptPath);
        if (!f.open(QIODevice::WriteOnly)) {
            co_return QStringLiteral("__ERROR__: failed to write prompt file");
        }
        f.write(prompt.toUtf8());
    }

    QProcess process;
    process.setProgram(QStringLiteral("claude"));
    process.setArguments({QStringLiteral("-p"), QStringLiteral("-"),
                          QStringLiteral("--dangerously-skip-permissions")});
    process.setStandardInputFile(promptPath);

    co_await qCoro(process).start();
    co_await qCoro(process).waitForFinished(-1);

    if (process.error() == QProcess::FailedToStart) {
        co_return QStringLiteral("__ERROR__: claude executable not found in PATH");
    }
    if (process.exitCode() != 0) {
        co_return QStringLiteral("__ERROR__: ")
            + QString::fromUtf8(process.readAllStandardError()).trimmed();
    }
    co_return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

// ---------------------------------------------------------------------------
// Per-job coroutine helper
//
// PageDb / PageRepositoryDb / GenPageQueue have no default constructors (and
// PageRepositoryDb holds a PageDb& member, so its move ctor is also deleted).
// Declaring them at the TOP of this coroutine — before any co_await — means
// GCC knows they are always initialised in the frame and never needs to
// generate a default-constructor call for them, which would ICE on
// non-default-constructible loop-local variables.
//
// Returns true when the page was saved successfully.
// ---------------------------------------------------------------------------

static QCoro::Task<bool> processImproveJob(const ImproveJob &job,
                                            AbstractEngine   *engine,
                                            CategoryTable    *categoryTable,
                                            QTextStream      *out,
                                            int               sNum)
{
    QDir             workDir = WorkingDirectoryManager::instance()->workingDir();
    PageDb           pageDb(workDir);
    PageRepositoryDb pageRepo(pageDb);
    GenPageQueue     queue(job.page.typeId, job.nonSvgImages, pageRepo,
                           *categoryTable, job.customInstructions);

    // ---- Step 1: free-form draft with failure context ----------------------
    QString step1Prompt = queue.buildStep1Prompt(job.page, *engine, 0, job.extraContext);
    QString step1Text   = co_await runClaudePrompt(step1Prompt);

    if (step1Text.startsWith(QStringLiteral("__ERROR__"))) {
        *out << QStringLiteral("[S%1] FAIL (step1): %2 — %3\n")
                    .arg(sNum).arg(job.page.permalink, step1Text);
        out->flush();
        co_return false;
    }

    *out << QStringLiteral("[S%1] Step 1 done: %2\n").arg(sNum).arg(job.page.permalink);
    out->flush();

    // ---- Step 2: reformat into JSON schema ---------------------------------
    QString step2FullPrompt = QStringLiteral(
        "The following is a draft response to a web content writing request.\n\n"
        "=== Original Request ===\n")
        + step1Prompt
        + QStringLiteral("\n\n=== Draft Response ===\n")
        + step1Text
        + QStringLiteral("\n\n=== Formatting Task ===\n")
        + queue.buildStep2Prompt();
    QString responseText = co_await runClaudePrompt(step2FullPrompt);

    if (responseText.startsWith(QStringLiteral("__ERROR__"))) {
        *out << QStringLiteral("[S%1] FAIL (step2): %2 — %3\n")
                    .arg(sNum).arg(job.page.permalink, responseText);
        out->flush();
        co_return false;
    }

    // ---- Save and record attempt -------------------------------------------
    if (queue.processReply(job.page.id, responseText, pageRepo)) {
        pageRepo.recordStrategyAttempt(job.page.id, job.strategyId);
        co_return true;
    }

    *out << QStringLiteral("[S%1] FAIL (parse): %2 — could not parse JSON reply\n")
                .arg(sNum).arg(job.page.permalink);
    out->flush();
    co_return false;
}

// ---------------------------------------------------------------------------
// Per-session coroutine
// ---------------------------------------------------------------------------

static QCoro::Task<void> runImproveSession(const QList<ImproveJob> *jobs,
                                            std::atomic<int>        *cursor,
                                            AbstractEngine          *engine,
                                            ImproveRunState         *state,
                                            int                      sessionIndex,
                                            CategoryTable           *categoryTable)
{
    int sNum = sessionIndex + 1;

    while (!g_improveStop) {
        if (state->jobsLimit >= 0
            && state->jobsCompleted.load() >= state->jobsLimit) {
            break;
        }

        int idx = cursor->fetch_add(1);
        if (idx >= jobs->size()) {
            break;
        }

        const ImproveJob &job = jobs->at(idx);

        *(state->out) << QStringLiteral("[S%1] Improving %2 (strategy %3)\n")
                             .arg(sNum).arg(job.page.permalink, job.strategyId);
        state->out->flush();

        bool ok = co_await processImproveJob(job, engine, categoryTable,
                                              state->out, sNum);
        if (ok) {
            state->jobsCompleted.fetch_add(1);
            *(state->out) << QStringLiteral("[S%1] OK: %2\n")
                                 .arg(sNum).arg(job.page.permalink);
            state->out->flush();
        }
    }

    if (g_improveStop) {
        *(state->out) << QStringLiteral("[S%1] Stop requested — session finished.\n").arg(sNum);
        state->out->flush();
    }

    if (--(state->activeCount) == 0) {
        QCoreApplication::quit();
    }
    co_return;
}

// ---------------------------------------------------------------------------
// LauncherImprove
// ---------------------------------------------------------------------------

QString LauncherImprove::getOptionName() const { return OPTION_NAME; }
bool    LauncherImprove::isFlag()        const { return true; }

void LauncherImprove::run(const QString & /*value*/)
{
    auto *out = new QTextStream(stdout);

    const QStringList args = QCoreApplication::arguments();

    int numSessions    = 1;
    int jobsLimit      = -1;
    int maxImpressions = 0;

    for (int i = 0; i < args.size() - 1; ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--") + OPTION_SESSIONS) {
            bool ok = false;
            const int n = args.at(i + 1).toInt(&ok);
            if (ok && n >= 1 && n <= 10) { numSessions = n; }
        } else if (arg == QStringLiteral("--") + OPTION_LIMIT) {
            bool ok = false;
            const int n = args.at(i + 1).toInt(&ok);
            if (ok && n >= 1) { jobsLimit = n; }
        } else if (arg == QStringLiteral("--") + OPTION_MAX_IMPRESSIONS) {
            bool ok = false;
            const int n = args.at(i + 1).toInt(&ok);
            if (ok && n >= 0) { maxImpressions = n; }
        }
    }

    // ---- Resolve engine --------------------------------------------------------
    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    const auto &settings  = WorkingDirectoryManager::instance()->settings();

    const QString engineId = settings->value(QStringLiteral("engineId")).toString();
    const AbstractEngine *proto = AbstractEngine::ALL_ENGINES().value(engineId, nullptr);
    if (!proto) {
        *out << QStringLiteral("ERROR: no engine configured (engineId = %1).\n").arg(engineId);
        out->flush();
        QCoreApplication::quit();
        return;
    }

    auto *holder = new QObject(nullptr);

    auto *hostTable = new HostTable(workingDir, holder);
    auto *engine    = proto->create(holder);
    engine->init(workingDir, *hostTable);

    auto *categoryTable = new CategoryTable(workingDir, holder);

    const WebsiteSettingsTable settingsTable(workingDir);
    const QString editingLang = settingsTable.editingLangCode();
    QString primaryDomain;
    for (int i = 0; i < engine->rowCount(); ++i) {
        const QString lang = engine->data(
            engine->index(i, AbstractEngine::COL_LANG_CODE)).toString();
        if (lang == editingLang) {
            primaryDomain = engine->data(
                engine->index(i, AbstractEngine::COL_DOMAIN)).toString();
            break;
        }
    }
    if (primaryDomain.isEmpty() && engine->rowCount() > 0) {
        primaryDomain = engine->data(
            engine->index(0, AbstractEngine::COL_DOMAIN)).toString();
    }

    // ---- Load strategies grouped by pageTypeId, sorted by priority ----------

    struct StrategyDesc {
        QString strategyId;
        QString pageTypeId;
        QString customInstructions;
        bool    nonSvgImages = false;
        int     priority     = 1;
    };

    auto *strategyTable = new GenStrategyTable(workingDir, holder);

    QList<StrategyDesc> allStrategies;
    allStrategies.reserve(strategyTable->rowCount());
    for (int row = 0; row < strategyTable->rowCount(); ++row) {
        StrategyDesc desc;
        desc.strategyId         = strategyTable->idForRow(row);
        desc.pageTypeId         = strategyTable->data(
            strategyTable->index(row, GenStrategyTable::COL_PAGE_TYPE)).toString();
        desc.customInstructions = strategyTable->customInstructionsForRow(row);
        desc.nonSvgImages       = strategyTable->data(
            strategyTable->index(row, GenStrategyTable::COL_NON_SVG_IMAGES)).toString()
            == QStringLiteral("Yes");
        desc.priority           = strategyTable->priorityForRow(row);
        allStrategies.append(desc);
    }

    std::stable_sort(allStrategies.begin(), allStrategies.end(),
                     [](const StrategyDesc &a, const StrategyDesc &b) {
                         return a.priority < b.priority;
                     });

    // ---- Load performance data ----------------------------------------------
    AbstractPerformanceDataSource *dataSource = nullptr;
    GscSettings gscSettings(workingDir);
    GscDataSource gscSource(gscSettings);
    StatsDbDataSource statsSource(workingDir);

    if (gscSettings.isConfigured()) {
        dataSource = &gscSource;
        *out << QStringLiteral("Performance source: Google Search Console\n");
    } else if (statsSource.isConfigured()) {
        dataSource = &statsSource;
        *out << QStringLiteral("Performance source: stats.db\n");
    } else {
        *out << QStringLiteral("Performance source: none — all generated pages are candidates\n");
    }

    QHash<QString, int> impressionsByPermalink;
    if (dataSource && dataSource->isConfigured()) {
        const QList<UrlPerformance> perf = dataSource->fetchData(primaryDomain);
        for (const UrlPerformance &entry : std::as_const(perf)) {
            impressionsByPermalink[entry.url] = entry.impressions;
        }
    }

    // ---- Build job list ------------------------------------------------------
    PageDb           schedDb(workingDir);
    PageRepositoryDb schedRepo(schedDb);

    // Collect distinct page type IDs referenced by at least one strategy.
    QList<QString> allPageTypeIds;
    for (const StrategyDesc &desc : std::as_const(allStrategies)) {
        if (!allPageTypeIds.contains(desc.pageTypeId)) {
            allPageTypeIds.append(desc.pageTypeId);
        }
    }

    auto *jobs   = new QList<ImproveJob>;
    auto *cursor = new std::atomic<int>{0};

    bool limitReached = false;
    for (const QString &typeId : std::as_const(allPageTypeIds)) {
        if (limitReached) { break; }

        const QList<PageRecord> generated = schedRepo.findGeneratedByTypeId(typeId);

        // Strategies for this page type, already globally sorted by priority.
        QList<StrategyDesc> typeStrategies;
        for (const StrategyDesc &desc : std::as_const(allStrategies)) {
            if (desc.pageTypeId == typeId) {
                typeStrategies.append(desc);
            }
        }
        if (typeStrategies.isEmpty()) { continue; }

        for (const PageRecord &page : std::as_const(generated)) {
            // Skip pages above the impressions threshold.
            const int impressions = impressionsByPermalink.value(page.permalink, 0);
            if (impressions > maxImpressions) { continue; }

            // Find the first untried strategy for this page (priority order).
            const QStringList attempted = schedRepo.strategyAttempts(page.id);

            bool         foundStrategy = false;
            StrategyDesc nextDesc;
            for (const StrategyDesc &desc : std::as_const(typeStrategies)) {
                if (!attempted.contains(desc.strategyId)) {
                    nextDesc       = desc;
                    foundStrategy  = true;
                    break;
                }
            }
            if (!foundStrategy) { continue; }

            // Build per-page failure context paragraph.
            QString context = QStringLiteral(
                "This page previously generated content that is underperforming in "
                "search engines (impressions: %1). "
                "Please write significantly different content using a fresh angle, "
                "different structure, or alternative keyword focus to improve "
                "search visibility.")
                .arg(impressions);
            if (!attempted.isEmpty()) {
                context += QStringLiteral("\nPreviously tried %1 strategy/strategies.")
                    .arg(attempted.size());
            }

            ImproveJob job;
            job.page               = page;
            job.strategyId         = nextDesc.strategyId;
            job.customInstructions = nextDesc.customInstructions;
            job.nonSvgImages       = nextDesc.nonSvgImages;
            job.extraContext       = context;
            jobs->append(job);

            if (jobsLimit >= 0 && jobs->size() >= jobsLimit) {
                limitReached = true;
                break;
            }
        }
    }

    if (jobs->isEmpty()) {
        *out << QStringLiteral("Nothing to improve. No underperforming pages found "
                               "with untried strategies.\n");
        out->flush();
        holder->deleteLater();
        QCoreApplication::quit();
        return;
    }

    *out << QStringLiteral("%1 page(s) queued for improvement, %2 session(s)\n")
                .arg(jobs->size()).arg(numSessions);
    out->flush();

    // ---- Spawn coroutines ----------------------------------------------------
    std::signal(SIGINT,  handleImproveSignal);
    std::signal(SIGTERM, handleImproveSignal);

    auto *state = new ImproveRunState;
    state->jobsLimit   = jobsLimit;
    state->out         = out;
    state->activeCount = numSessions;

    for (int s = 0; s < numSessions; ++s) {
        runImproveSession(jobs, cursor, engine, state, s, categoryTable);
    }

    out->flush();
    // app.exec() in main() drives the event loop until all sessions quit().
}
