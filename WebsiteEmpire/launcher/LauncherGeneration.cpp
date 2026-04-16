#include "LauncherGeneration.h"

#include "gui/panes/GenStrategyTable.h"
#include "website/AbstractEngine.h"
#include "website/HostTable.h"
#include "website/WebsiteSettingsTable.h"
#include "website/pages/GenPageQueue.h"
#include "website/pages/GenScheduler.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/perf/GscDataSource.h"
#include "website/perf/GscSettings.h"
#include "website/perf/StatsDbDataSource.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include <csignal>
#include <atomic>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTextStream>

#include <QCoro/QCoroSignal>
#include <QCoro/QCoroTask>

static constexpr const char *CLAUDE_API_URL = "https://api.anthropic.com/v1/messages";
static constexpr const char *CLAUDE_MODEL   = "claude-sonnet-4-6";

const QString LauncherGeneration::OPTION_NAME     = QStringLiteral("generation");
const QString LauncherGeneration::OPTION_SESSIONS = QStringLiteral("sessions");
const QString LauncherGeneration::OPTION_LIMIT    = QStringLiteral("limit");

DECLARE_LAUNCHER(LauncherGeneration)

// ---------------------------------------------------------------------------
// Signal handling — graceful stop after current page(s) finish
// ---------------------------------------------------------------------------

static volatile sig_atomic_t g_stopRequested = 0;

static void handleSignal(int)
{
    g_stopRequested = 1;
}

// ---------------------------------------------------------------------------
// Shared counters (heap-allocated, outlive run()'s stack frame)
// ---------------------------------------------------------------------------

struct GenRunState {
    std::atomic<int> jobsCompleted{0};
    int              jobsLimit     = -1; // -1 = unlimited
    int              activeCount   = 0;
    QTextStream     *out           = nullptr;
};

// ---------------------------------------------------------------------------
// Per-session coroutine
// ---------------------------------------------------------------------------

static QCoro::Task<void> runGenerationSession(GenPageQueue  *queue,
                                               const QString  apiKey,
                                               AbstractEngine *engine,
                                               int             websiteIndex,
                                               GenRunState    *state,
                                               int             sessionIndex)
{
    const int sNum = sessionIndex + 1;
    QNetworkAccessManager nam;

    while (!g_stopRequested && queue->hasNext()) {
        // Honour --limit if set.
        if (state->jobsLimit >= 0
            && state->jobsCompleted.load() >= state->jobsLimit) {
            break;
        }

        const PageRecord page = queue->peekNext();
        queue->advance();

        *(state->out) << QStringLiteral("[S%1] Generating page %2 (%3, %4 remaining)\n")
                             .arg(sNum)
                             .arg(page.permalink)
                             .arg(page.typeId)
                             .arg(queue->pendingCount());
        state->out->flush();

        // ---- Build Claude API request -------------------------------------
        const QString prompt = queue->buildPrompt(page, *engine, websiteIndex);

        QJsonObject msgObj;
        msgObj[QStringLiteral("role")]    = QStringLiteral("user");
        msgObj[QStringLiteral("content")] = prompt;

        QJsonObject body;
        body[QStringLiteral("model")]      = QLatin1StringView(CLAUDE_MODEL);
        body[QStringLiteral("max_tokens")] = 4096;
        body[QStringLiteral("messages")]   = QJsonArray{msgObj};

        const QByteArray bodyBytes = QJsonDocument(body).toJson(QJsonDocument::Compact);

        QNetworkRequest req{QUrl(QLatin1StringView(CLAUDE_API_URL))};
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QByteArrayLiteral("application/json"));
        req.setRawHeader(QByteArrayLiteral("x-api-key"),         apiKey.toUtf8());
        req.setRawHeader(QByteArrayLiteral("anthropic-version"),
                         QByteArrayLiteral("2023-06-01"));

        QNetworkReply *reply = nam.post(req, bodyBytes);
        co_await qCoro(reply, &QNetworkReply::finished);

        if (reply->error() != QNetworkReply::NoError) {
            *(state->out) << QStringLiteral("[S%1] FAIL (network): %2 — %3\n")
                                 .arg(sNum)
                                 .arg(page.permalink, reply->errorString());
            state->out->flush();
            reply->deleteLater();
            continue;
        }

        // ---- Parse API response ------------------------------------------
        const QByteArray raw = reply->readAll();
        reply->deleteLater();

        const QJsonDocument doc   = QJsonDocument::fromJson(raw);
        const QJsonArray    cont  = doc.object()[QStringLiteral("content")].toArray();
        if (cont.isEmpty()) {
            const QString apiErr = doc.object()[QStringLiteral("error")]
                                       .toObject()[QStringLiteral("message")]
                                       .toString();
            *(state->out) << QStringLiteral("[S%1] FAIL (API): %2 — %3\n")
                                 .arg(sNum)
                                 .arg(page.permalink,
                                      apiErr.isEmpty() ? QStringLiteral("empty content")
                                                       : apiErr);
            state->out->flush();
            continue;
        }

        const QString responseText =
            cont.at(0).toObject()[QStringLiteral("text")].toString();

        // ---- Save result -------------------------------------------------
        // GenPageQueue::processReply needs a live IPageRepository.
        // We use a dedicated PageDb per session to avoid cross-thread locks.
        const QDir workDir = WorkingDirectoryManager::instance()->workingDir();
        PageDb            pageDb(workDir);
        PageRepositoryDb  pageRepo(pageDb);

        if (queue->processReply(page.id, responseText, pageRepo)) {
            state->jobsCompleted.fetch_add(1);
            *(state->out) << QStringLiteral("[S%1] OK: %2\n")
                                 .arg(sNum).arg(page.permalink);
        } else {
            *(state->out) << QStringLiteral("[S%1] FAIL (parse): %2 — could not parse JSON reply\n")
                                 .arg(sNum).arg(page.permalink);
        }
        state->out->flush();
    }

    if (g_stopRequested) {
        *(state->out) << QStringLiteral("[S%1] Stop requested — session finished.\n").arg(sNum);
        state->out->flush();
    }

    if (--(state->activeCount) == 0) {
        QCoreApplication::quit();
    }
    co_return;
}

// ---------------------------------------------------------------------------
// LauncherGeneration
// ---------------------------------------------------------------------------

QString LauncherGeneration::getOptionName() const { return OPTION_NAME; }
bool    LauncherGeneration::isFlag()        const { return true; }

void LauncherGeneration::run(const QString & /*value*/)
{
    auto *out = new QTextStream(stdout);

    // ---- Parse sub-options from raw args ----------------------------------
    const QStringList args = QCoreApplication::arguments();

    int numSessions = 1;
    int jobsLimit   = -1;

    for (int i = 0; i < args.size() - 1; ++i) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--") + OPTION_SESSIONS) {
            bool ok = false;
            const int n = args.at(i + 1).toInt(&ok);
            if (ok && n >= 1 && n <= 10) {
                numSessions = n;
            }
        } else if (arg == QStringLiteral("--") + OPTION_LIMIT) {
            bool ok = false;
            const int n = args.at(i + 1).toInt(&ok);
            if (ok && n >= 1) {
                jobsLimit = n;
            }
        }
    }

    // ---- Resolve engine and API key ---------------------------------------
    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    const auto &settings  = WorkingDirectoryManager::instance()->settings();

    const QString apiKey = QProcessEnvironment::systemEnvironment()
                               .value(QStringLiteral("ANTHROPIC_API_KEY"));
    if (apiKey.isEmpty()) {
        *out << QStringLiteral("ERROR: ANTHROPIC_API_KEY environment variable is not set.\n");
        out->flush();
        QCoreApplication::quit();
        return;
    }

    const QString engineId = settings->value(QStringLiteral("engineId")).toString();
    const AbstractEngine *proto = AbstractEngine::ALL_ENGINES().value(engineId, nullptr);
    if (!proto) {
        *out << QStringLiteral("ERROR: no engine configured (engineId = %1).\n").arg(engineId);
        out->flush();
        QCoreApplication::quit();
        return;
    }

    // ---- Build object graph -----------------------------------------------
    auto *holder = new QObject(nullptr);

    auto *hostTable  = new HostTable(workingDir, holder);
    auto *engine     = proto->create(holder);
    engine->init(workingDir, *hostTable);

    auto *categoryTable = new CategoryTable(workingDir, holder);

    // Determine primary domain for performance weighting.
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

    // ---- Performance data source ------------------------------------------
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
        *out << QStringLiteral("Performance source: none (even distribution)\n");
    }

    // ---- Strategy table and scheduler -------------------------------------
    auto *strategyTable = new GenStrategyTable(workingDir, holder);

    // Use a temporary page DB for scheduling (read-only queries).
    PageDb           schedDb(workingDir);
    PageRepositoryDb schedRepo(schedDb);

    // Build GUI-independent strategy list for the scheduler.
    QList<GenScheduler::StrategyInfo> strategyInfos;
    strategyInfos.reserve(strategyTable->rowCount());
    for (int row = 0; row < strategyTable->rowCount(); ++row) {
        GenScheduler::StrategyInfo info;
        info.strategyId   = strategyTable->idForRow(row);
        info.pageTypeId   = strategyTable->data(
            strategyTable->index(row, GenStrategyTable::COL_PAGE_TYPE)).toString();
        info.themeId      = strategyTable->themeIdForRow(row);
        info.nonSvgImages = strategyTable->data(
            strategyTable->index(row, GenStrategyTable::COL_NON_SVG_IMAGES)).toString()
            == QStringLiteral("Yes");
        strategyInfos.append(info);
    }

    GenScheduler scheduler(strategyInfos, schedRepo, dataSource, primaryDomain);
    const QList<GenScheduler::StrategyAllocation> allocations =
        scheduler.computeAllocations(numSessions);

    if (allocations.isEmpty()) {
        *out << QStringLiteral("Nothing to generate. All pages are up to date.\n");
        out->flush();
        holder->deleteLater();
        QCoreApplication::quit();
        return;
    }

    // ---- Spawn coroutines -------------------------------------------------
    std::signal(SIGINT,  handleSignal);
    std::signal(SIGTERM, handleSignal);

    auto *state = new GenRunState;
    state->jobsLimit = jobsLimit;
    state->out       = out;

    for (const auto &alloc : std::as_const(allocations)) {
        // Each queue uses its own PageDb connection (thread-safe isolation).
        PageDb           *queueDb   = new PageDb(workingDir);
        PageRepositoryDb *queueRepo = new PageRepositoryDb(*queueDb);

        auto *queue = new GenPageQueue(alloc.pageTypeId,
                                        alloc.nonSvgImages,
                                        *queueRepo,
                                        *categoryTable,
                                        jobsLimit);

        state->activeCount += alloc.sessionCount;

        *out << QStringLiteral("Strategy '%1': %2 session(s), %3 page(s) pending\n")
                    .arg(alloc.pageTypeId)
                    .arg(alloc.sessionCount)
                    .arg(queue->pendingCount());

        for (int s = 0; s < alloc.sessionCount; ++s) {
            runGenerationSession(queue, apiKey, engine, 0 /*websiteIndex*/,
                                  state, state->activeCount - alloc.sessionCount + s);
        }

        // queueDb and queueRepo are intentionally not deleted here: the
        // coroutines reference them across suspension points.  They leak
        // on process exit — acceptable for a short-lived CLI tool.
        Q_UNUSED(queueDb)
        Q_UNUSED(queueRepo)
    }

    out->flush();
    // app.exec() in main() drives the event loop until all sessions quit().
}
