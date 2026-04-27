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
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>

#include <QCoro/QCoroProcess>
#include <QCoro/QCoroTask>

const QString LauncherGeneration::OPTION_NAME     = QStringLiteral("generation");
const QString LauncherGeneration::OPTION_SESSIONS = QStringLiteral("sessions");
const QString LauncherGeneration::OPTION_LIMIT    = QStringLiteral("limit");
const QString LauncherGeneration::OPTION_STRATEGY = QStringLiteral("strategy");

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
// claude CLI helper — mirrors ClaudeRunner in WebsiteAspire; no API key needed
// ---------------------------------------------------------------------------

static QCoro::Task<QString> runClaudePrompt(QString prompt)
{
    // result declared first — single co_return at end, mirrors ClaudeRunner.
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

    // Exactly two co_awaits, nothing between them — mirrors ClaudeRunner.
    co_await qCoro(process).start();
    co_await qCoro(process).waitForFinished(-1);

    // All error checks and output reading happen after both co_awaits.
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
// Per-session coroutine
// ---------------------------------------------------------------------------

static QCoro::Task<void> runGenerationSession(GenPageQueue  *queue,
                                               QString        strategyId,
                                               AbstractEngine *engine,
                                               int             websiteIndex,
                                               GenRunState    *state,
                                               int             sessionIndex)
{
    int sNum = sessionIndex + 1;
    // PageRepositoryDb holds a PageDb& (reference member) so both types have deleted
    // move/default constructors.  Declaring them here — before any co_await — means
    // GCC knows they are always initialised in the frame and never needs to generate
    // a default-constructor call for them.
    QDir             workDir = WorkingDirectoryManager::instance()->workingDir();
    PageDb           pageDb(workDir);
    PageRepositoryDb pageRepo(pageDb);

    while (!g_stopRequested && queue->hasNext()) {
        // Honour --limit if set.
        if (state->jobsLimit >= 0
            && state->jobsCompleted.load() >= state->jobsLimit) {
            break;
        }

        PageRecord page = queue->peekNext();
        queue->advance();

        *(state->out) << QStringLiteral("[S%1] Generating page %2 (%3, %4 remaining)\n")
                             .arg(sNum)
                             .arg(page.permalink)
                             .arg(page.typeId)
                             .arg(queue->pendingCount());
        state->out->flush();

        // ---- Call 1: write article content as free-form text ----------------
        *(state->out) << QStringLiteral("[S%1] Building prompt...\n").arg(sNum);
        state->out->flush();

        const QString contentPrompt = queue->buildContentPrompt(page, *engine, websiteIndex);

        *(state->out) << QStringLiteral("[S%1] Prompt ready (%2 chars) — launching Claude (1/2: content)...\n")
                             .arg(sNum).arg(contentPrompt.size());
        state->out->flush();

        const QString articleText = co_await runClaudePrompt(contentPrompt);

        if (articleText.startsWith(QStringLiteral("__ERROR__"))) {
            *(state->out) << QStringLiteral("[S%1] FAIL (content): %2 — %3\n")
                                 .arg(sNum).arg(page.permalink, articleText);
            state->out->flush();
            continue;
        }

        *(state->out) << QStringLiteral("[S%1] Content done: %2 — launching Claude (2/2: metadata)...\n")
                             .arg(sNum).arg(page.permalink);
        state->out->flush();

        // ---- Call 2: metadata JSON from the article -------------------------
        const QString metadataPrompt = queue->buildMetadataPrompt(page, articleText);
        const QString metadataJson = co_await runClaudePrompt(metadataPrompt);
        // Metadata failures are tolerated: processContentAndMetadata() falls back
        // to saving 1_text only if metadataJson is empty or unparseable.
        if (metadataJson.startsWith(QStringLiteral("__ERROR__"))) {
            *(state->out) << QStringLiteral("[S%1] WARN (metadata): %2 — %3 — saving content only\n")
                                 .arg(sNum).arg(page.permalink, metadataJson);
            state->out->flush();
        }

        // ---- For source-DB-backed pages (id == 0), create the row first -----
        int pageId = page.id;
        if (pageId == 0) {
            pageId = pageRepo.create(page.typeId, page.permalink, page.lang);
            if (pageId <= 0) {
                // Permalink already exists (race between sessions) — skip.
                *(state->out) << QStringLiteral("[S%1] SKIP (exists): %2\n")
                                     .arg(sNum).arg(page.permalink);
                state->out->flush();
                continue;
            }
        }

        // ---- Save result ----------------------------------------------------
        const QString safeMetadata = metadataJson.startsWith(QStringLiteral("__ERROR__"))
                                     ? QString{} : metadataJson;
        if (queue->processContentAndMetadata(pageId, articleText, safeMetadata, pageRepo)) {
            pageRepo.recordStrategyAttempt(pageId, strategyId);
            state->jobsCompleted.fetch_add(1);
            *(state->out) << QStringLiteral("[S%1] OK: %2\n")
                                 .arg(sNum).arg(page.permalink);
        } else {
            *(state->out) << QStringLiteral("[S%1] FAIL (empty content): %2\n")
                                 .arg(sNum).arg(page.permalink);
        }
        state->out->flush();
    }

    if (g_stopRequested) {
        *(state->out) << QStringLiteral("[S%1] Stop requested — session finished.\n").arg(sNum);
        state->out->flush();
    }

    if (--(state->activeCount) == 0) {
        *(state->out) << QStringLiteral("Generation complete. %1 page(s) generated.\n")
                             .arg(state->jobsCompleted.load());
        state->out->flush();
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

    int     numSessions  = 1;
    int     jobsLimit    = -1;
    QString strategyFilter; // empty = all priority-1 strategies

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
        } else if (arg == QStringLiteral("--") + OPTION_STRATEGY) {
            strategyFilter = args.at(i + 1);
        }
    }

    // ---- Resolve engine and API key ---------------------------------------
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

    // ---- Build object graph -----------------------------------------------
    auto *holder = new QObject(nullptr);

    auto *hostTable  = new HostTable(workingDir, holder);
    auto *engine     = proto->create(holder);
    engine->init(workingDir, *hostTable);

    auto *categoryTable = new CategoryTable(workingDir, holder);

    // Determine primary domain and website index for the editing language.
    const WebsiteSettingsTable settingsTable(workingDir);
    const QString editingLang = settingsTable.editingLangCode();
    QString primaryDomain;
    int editingLangIndex = 0;
    for (int i = 0; i < engine->rowCount(); ++i) {
        const QString lang = engine->data(
            engine->index(i, AbstractEngine::COL_LANG_CODE)).toString();
        if (lang == editingLang) {
            primaryDomain    = engine->data(
                engine->index(i, AbstractEngine::COL_DOMAIN)).toString();
            editingLangIndex = i;
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
    out->flush();

    // ---- Strategy table and scheduler -------------------------------------
    auto *strategyTable = new GenStrategyTable(workingDir, holder);

    // Use a temporary page DB for scheduling (read-only queries).
    PageDb           schedDb(workingDir);
    PageRepositoryDb schedRepo(schedDb);

    // ---- Build virtual pages for source-DB-backed strategies ----------------
    // For strategies that reference an aspire DB (primaryAttrId != ""), we read
    // items from results_db/<primaryAttrId>.db and build PageRecord items with
    // id == 0 for those not yet present in pages.db.  The page row is created
    // atomically in runGenerationSession just before saving the generated content.

    static const QRegularExpression reNonAlnum(QStringLiteral("[^a-z0-9]+"));

    // Pre-load all existing permalinks once (avoid repeated findAll() calls).
    const QList<PageRecord> allExistingPages = schedRepo.findAll();
    QSet<QString> allExistingPermalinks;
    allExistingPermalinks.reserve(allExistingPages.size());
    for (const PageRecord &p : std::as_const(allExistingPages)) {
        allExistingPermalinks.insert(p.permalink);
    }

    // virtualPagesByStrategyId — keyed by stable strategy UUID
    QHash<QString, QList<PageRecord>> virtualPagesByStrategyId;

    // Build GUI-independent strategy list for the scheduler.
    // Normal generation only runs priority-1 strategies; higher priorities are
    // reserved for the --improve pass on underperforming pages.
    QList<GenScheduler::StrategyInfo> strategyInfos;
    strategyInfos.reserve(strategyTable->rowCount());
    for (int row = 0; row < strategyTable->rowCount(); ++row) {
        const int prio = strategyTable->priorityForRow(row);
        if (prio != 1) {
            continue;
        }
        GenScheduler::StrategyInfo info;
        info.strategyId         = strategyTable->idForRow(row);
        if (!strategyFilter.isEmpty() && info.strategyId != strategyFilter) {
            continue;
        }
        info.pageTypeId         = strategyTable->data(
            strategyTable->index(row, GenStrategyTable::COL_PAGE_TYPE)).toString();
        info.themeId            = strategyTable->themeIdForRow(row);
        info.customInstructions = strategyTable->customInstructionsForRow(row);
        info.primaryAttrId      = strategyTable->primaryAttrIdForRow(row);
        info.nonSvgImages       = strategyTable->data(
            strategyTable->index(row, GenStrategyTable::COL_NON_SVG_IMAGES)).toString()
            == QStringLiteral("Yes");
        info.priority           = prio;

        // For source-DB-backed strategies, read aspire DB and compute pending items.
        // A strategy qualifies when either primaryAttrId is set (convention path) or
        // a DB was manually linked via the GUI (primaryDbPath, may be relative).
        {
            // 1. Stored path — may be relative to working dir (portable Dropbox format).
            QString dbPath = strategyTable->primaryDbPathForRow(row);
            if (!dbPath.isEmpty() && !QFile::exists(dbPath)) {
                const QString abs = workingDir.absoluteFilePath(dbPath);
                if (QFile::exists(abs)) {
                    dbPath = QDir::cleanPath(abs);
                } else {
                    dbPath.clear(); // neither absolute nor relative resolved
                }
            }
            // 2. Convention path — only when primaryAttrId is set and stored path missing.
            if (dbPath.isEmpty() && !info.primaryAttrId.isEmpty()) {
                const QString stdPath = workingDir.filePath(
                    QStringLiteral("results_db/") + info.primaryAttrId
                    + QStringLiteral(".db"));
                if (QFile::exists(stdPath)) {
                    dbPath = stdPath;
                }
            }

            QList<PageRecord> virtualPages;

            if (!dbPath.isEmpty()) {
                *out << QStringLiteral("Source DB for '%1': %2 — found\n")
                            .arg(info.pageTypeId, dbPath);
                out->flush();
            } else if (!info.primaryAttrId.isEmpty()) {
                // Had a primaryAttrId but DB not found — report it so user can debug.
                const QString expected = workingDir.filePath(
                    QStringLiteral("results_db/") + info.primaryAttrId
                    + QStringLiteral(".db"));
                *out << QStringLiteral("Source DB for '%1': %2 — NOT FOUND\n")
                            .arg(info.pageTypeId, expected);
                out->flush();
            }

            if (!dbPath.isEmpty()) {
                const QString connName = QStringLiteral("gen_src_") + info.strategyId;
                QString nameColumn;
                QStringList names;
                {
                    QSqlDatabase srcDb = QSqlDatabase::addDatabase(
                        QStringLiteral("QSQLITE"), connName);
                    srcDb.setDatabaseName(dbPath);
                    if (srcDb.open()) {
                        QSqlQuery pq(srcDb);
                        if (pq.exec(QStringLiteral("PRAGMA table_info(records)"))) {
                            while (pq.next()) {
                                const QString col = pq.value(1).toString();
                                if (col != QStringLiteral("id") && nameColumn.isEmpty()) {
                                    nameColumn = col;
                                }
                            }
                        }
                        if (!nameColumn.isEmpty()) {
                            QSqlQuery q(srcDb);
                            q.exec(QStringLiteral("SELECT ") + nameColumn
                                   + QStringLiteral(" FROM records ORDER BY id"));
                            while (q.next()) {
                                const QString v = q.value(0).toString().trimmed();
                                if (!v.isEmpty()) {
                                    names << v;
                                }
                            }
                        }
                    }
                }
                QSqlDatabase::removeDatabase(connName);

                for (const QString &name : std::as_const(names)) {
                    QString slug = name.toLower();
                    slug.replace(reNonAlnum, QStringLiteral("-"));
                    while (slug.startsWith(QLatin1Char('-'))) { slug.remove(0, 1); }
                    while (slug.endsWith(QLatin1Char('-')))   { slug.chop(1); }
                    if (slug.isEmpty()) { continue; }

                    const QString permalink = QLatin1Char('/') + slug;
                    if (allExistingPermalinks.contains(permalink)) { continue; }

                    PageRecord vp;
                    vp.id        = 0;
                    vp.typeId    = info.pageTypeId;
                    vp.permalink = permalink;
                    vp.lang      = editingLang;
                    virtualPages.append(vp);
                }

                if (jobsLimit >= 0 && virtualPages.size() > jobsLimit) {
                    virtualPages = virtualPages.mid(0, jobsLimit);
                }
            }

            if (!virtualPages.isEmpty()) {
                info.pendingCountOverride = virtualPages.size();
                *out << QStringLiteral("  → %1 item(s) pending (already in pages.db excluded)\n")
                            .arg(info.pendingCountOverride);
                out->flush();
                virtualPagesByStrategyId.insert(info.strategyId, std::move(virtualPages));
            }
            // When virtualPages is empty (all items already in pages.db), leave
            // pendingCountOverride == -1 so the scheduler falls back to
            // findPendingByTypeId() — existing pending stubs still get processed.
        }

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
        GenPageQueue *queue = nullptr;

        if (virtualPagesByStrategyId.contains(alloc.strategyId)) {
            // Source-DB-backed strategy: use pre-built virtual pages
            // (works whether primaryAttrId is set or a DB was linked directly).
            auto &vPages = virtualPagesByStrategyId[alloc.strategyId];
            queue = new GenPageQueue(alloc.pageTypeId,
                                     alloc.nonSvgImages,
                                     vPages,
                                     *categoryTable,
                                     alloc.customInstructions);
        } else {
            // Classic strategy: read pending pages from pages.db.
            PageDb           *queueDb   = new PageDb(workingDir);
            PageRepositoryDb *queueRepo = new PageRepositoryDb(*queueDb);
            queue = new GenPageQueue(alloc.pageTypeId,
                                     alloc.nonSvgImages,
                                     *queueRepo,
                                     *categoryTable,
                                     alloc.customInstructions,
                                     jobsLimit);
            // queueDb / queueRepo intentionally leak — coroutines hold references
            // across suspension points; process exit cleans up.
            Q_UNUSED(queueDb)
            Q_UNUSED(queueRepo)
        }

        state->activeCount += alloc.sessionCount;

        *out << QStringLiteral("Strategy '%1': %2 session(s), %3 page(s) pending\n")
                    .arg(alloc.pageTypeId)
                    .arg(alloc.sessionCount)
                    .arg(queue->pendingCount());

        for (int s = 0; s < alloc.sessionCount; ++s) {
            runGenerationSession(queue, alloc.strategyId,
                                  engine, editingLangIndex,
                                  state, state->activeCount - alloc.sessionCount + s);
        }
    }

    out->flush();
    // app.exec() in main() drives the event loop until all sessions quit().
}
