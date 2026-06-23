#include "LauncherPublish.h"

#include "website/AbstractEngine.h"
#include "website/HostTable.h"
#include "website/WebsiteSettingsTable.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/CategoryHubDirtySet.h"
#include "website/pages/CategoryHubSyncer.h"
#include "website/pages/SymptomHubSyncer.h"
#include "website/pages/TaxonomyIndexSyncer.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/translation/TranslationStatusTable.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QProcess>
#include <QSet>
#include <QTextStream>
#include <QThread>

const QString LauncherPublish::OPTION_NAME   = QStringLiteral("publish");
const QString LauncherPublish::OPTION_DEPLOY = QStringLiteral("deploy");
const QString LauncherPublish::OPTION_PORT   = QStringLiteral("port");

DECLARE_LAUNCHER(LauncherPublish)

QString LauncherPublish::getOptionName() const { return OPTION_NAME; }
bool    LauncherPublish::isFlag()        const { return true; }

// =============================================================================
// run
// =============================================================================

void LauncherPublish::run(const QString & /*value*/)
{
    QTextStream out(stdout);

    // ── Parse --deploy and --port ─────────────────────────────────────────────
    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    QString deployBase = workingDir.filePath(QStringLiteral("deploy"));
    int     basePort   = 8080;

    const QStringList args = QCoreApplication::arguments();
    for (int i = 0; i < args.size() - 1; ++i) {
        if (args.at(i) == QStringLiteral("--") + OPTION_DEPLOY) {
            const QString v = args.at(i + 1).trimmed();
            if (!v.isEmpty()) {
                deployBase = v;
            }
        } else if (args.at(i) == QStringLiteral("--") + OPTION_PORT) {
            bool ok = false;
            const int p = args.at(i + 1).toInt(&ok);
            if (ok && p > 0) {
                basePort = p;
            }
        }
    }

    // ── Load engine ────────────────────────────────────────────────────────────
    const auto &settings  = WorkingDirectoryManager::instance()->settings();
    const QString engineId = settings->value(QStringLiteral("engineId")).toString();
    const AbstractEngine *proto = AbstractEngine::ALL_ENGINES().value(engineId, nullptr);
    if (!proto) {
        out << QStringLiteral("ERROR: no engine configured (engineId = %1).\n").arg(engineId);
        out.flush();
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  &QCoreApplication::quit, Qt::QueuedConnection);
        return;
    }

    auto *holder    = new QObject(nullptr);
    auto *hostTable = new HostTable(workingDir, holder);
    auto *engine    = proto->create(holder);
    engine->init(workingDir, *hostTable);

    if (engine->rowCount() == 0) {
        out << QStringLiteral("ERROR: engine has no rows — configure at least one domain.\n");
        out.flush();
        holder->deleteLater();
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  &QCoreApplication::quit, Qt::QueuedConnection);
        return;
    }

    // ── Determine qualifying languages (≥ 5 fully-translated pages) ──────────
    PageDb           pageDb(workingDir);
    PageRepositoryDb pageRepo(pageDb);
    CategoryTable    categoryTable(workingDir);

    // Collect engine language codes for the single-pass count.
    QStringList engineLangs;
    const int rows = engine->rowCount();
    for (int i = 0; i < rows; ++i) {
        const QString lang = engine->getLangCode(i);
        if (!lang.isEmpty() && !engineLangs.contains(lang)) {
            engineLangs.append(lang);
        }
    }
    const QHash<QString, int> translatedCounts =
        TranslationStatusTable::countCompletedPerLang(pageRepo, categoryTable, engineLangs);

    struct LangToDeploy {
        int     engineIndex;
        QString lang;
        QString domain;
        int     port;
    };

    QList<LangToDeploy> targets;
    QSet<QString>       seenLangs;
    int portOffset = 0;

    for (int i = 0; i < rows; ++i) {
        const QString lang = engine->getLangCode(i);
        if (lang.isEmpty() || seenLangs.contains(lang)) {
            continue;
        }
        const int count = translatedCounts.value(lang, 0);
        if (count < 10) {
            out << QStringLiteral("Skipping lang=%1: only %2 page(s) (< 10).\n")
                   .arg(lang).arg(count);
            out.flush();
            continue;
        }
        seenLangs.insert(lang);
        const QString domain = engine->data(
            engine->index(i, AbstractEngine::COL_DOMAIN)).toString().trimmed();
        if (domain.isEmpty()) {
            out << QStringLiteral("ERROR: domain is empty for lang=%1 — set it in the Domains tab.\n")
                   .arg(lang);
            out.flush();
            return;
        }
        const int port = basePort + portOffset;
        targets.append({i, lang, domain, port});
        ++portOffset;
        out << QStringLiteral("Qualifying lang=%1 domain=%2 port=%3 (%4 pages).\n")
               .arg(lang, domain).arg(port).arg(count);
        out.flush();
    }

    if (targets.isEmpty()) {
        out << QStringLiteral("No qualifying languages found (need >= 5 pages each).\n");
        out.flush();
        holder->deleteLater();
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  &QCoreApplication::quit, Qt::QueuedConnection);
        return;
    }

    // ── Generate each qualifying language directly into its deploy directory ───
    PageGenerator       generator(pageRepo, categoryTable);
    {
        WebsiteSettingsTable siteSettings(workingDir);
        generator.setWebsiteContext(siteSettings.websiteName(), siteSettings.author());
    }
    CategoryHubDirtySet hubDirtySet(workingDir);
    CategoryHubSyncer   hubSyncer(pageRepo, categoryTable, hubDirtySet, generator);

    out << QStringLiteral("Syncing category hub stubs...\n");
    out.flush();
    hubSyncer.syncStubs(engine->getLangCode(0));
    hubSyncer.markStaleByStats(workingDir);

    out << QStringLiteral("Syncing symptom hub stubs...\n");
    out.flush();
    SymptomHubSyncer symptomSyncer(pageRepo);
    symptomSyncer.syncStubs(workingDir, engine->getLangCode(0));

    out << QStringLiteral("Syncing taxonomy index stubs...\n");
    out.flush();
    TaxonomyIndexSyncer taxonomySyncer(pageRepo);
    taxonomySyncer.syncStubs(engine->getLangCode(0));

    // ── Locate StaticWebsiteServe binary ─────────────────────────────────────
    const QString appDir = QCoreApplication::applicationDirPath();
    QString binaryPath = QStringLiteral("StaticWebsiteServe");
    const QStringList candidates = {
        appDir + QStringLiteral("/StaticWebsiteServe"),
        appDir + QStringLiteral("/../StaticWebsiteServe/StaticWebsiteServe"),
        appDir + QStringLiteral("/../../build/StaticWebsiteServe/StaticWebsiteServe"),
    };
    for (const QString &c : std::as_const(candidates)) {
        if (QFile::exists(c)) {
            binaryPath = QDir::cleanPath(c);
            break;
        }
    }

    // Kill all StaticWebsiteServe processes unconditionally — old deploys from
    // any directory (including stale trash-dir servers) must be gone before we
    // try to bind the ports again.
    QProcess killer;
    killer.start(QStringLiteral("bash"),
                 {QStringLiteral("-c"),
                  QStringLiteral("pkill -f StaticWebsiteServe 2>/dev/null; true")});
    killer.waitForFinished(3000);
    QThread::msleep(500);

    // Copy images.db once to the deploy base — shared across all languages.
    const QString srcImagesPath  = workingDir.filePath(QStringLiteral("images.db"));
    const QString sharedImages   = QDir(deployBase).filePath(QStringLiteral("images.db"));
    if (QFile::exists(srcImagesPath)) {
        if (!QDir().mkpath(deployBase)) {
            out << QStringLiteral("WARNING: could not create deploy dir: %1\n").arg(deployBase);
            out.flush();
        }
        if (QFile::exists(sharedImages)) {
            QFile::remove(sharedImages);
        }
        if (QFile::copy(srcImagesPath, sharedImages)) {
            out << QStringLiteral("→ images.db → %1 (shared)\n").arg(deployBase);
        } else {
            out << QStringLiteral("WARNING: failed to copy images.db to %1\n").arg(deployBase);
        }
        out.flush();
    }

    int totalPages = 0;

    for (const LangToDeploy &t : std::as_const(targets)) {
        const QString destDir = QDir(deployBase).filePath(t.lang);

        if (!QDir().mkpath(destDir)) {
            out << QStringLiteral("ERROR: could not create directory: %1\n").arg(destDir);
            out.flush();
            continue;
        }

        // Remove stale content.db (and WAL/SHM) so generation starts fresh.
        const QDir ddir(destDir);
        QFile::remove(ddir.filePath(QStringLiteral("content.db-wal")));
        QFile::remove(ddir.filePath(QStringLiteral("content.db-shm")));
        QFile::remove(ddir.filePath(QStringLiteral("content.db")));

        out << QStringLiteral("Generating pages for lang=%1 domain=%2...\n")
               .arg(t.lang, t.domain);
        out.flush();
        // Non-primary languages are served behind a /<lang>/ nginx path prefix,
        // so their sitemap URLs must include that prefix (e.g. https://example.com/fr).
        const QString primaryLang = engine->getLangCode(0);
        const QString sitemapBase = QStringLiteral("https://") + t.domain
            + (t.lang == primaryLang ? QString{} : QStringLiteral("/") + t.lang);
        const int n = generator.generateAll(workingDir, ddir, t.domain, *engine, t.engineIndex, sitemapBase);
        totalPages += n;
        out << QStringLiteral("  → %1 page(s) written to %2\n").arg(n).arg(destDir);
        out.flush();

        QProcess::startDetached(binaryPath,
                                {QStringLiteral("--port"),      QString::number(t.port),
                                 QStringLiteral("--lang"),      t.lang,
                                 QStringLiteral("--images-db"), sharedImages},
                                destDir);
        out << QStringLiteral("  → StaticWebsiteServe lang=%1 at http://localhost:%2/\n")
               .arg(t.lang).arg(t.port);
        out.flush();
    }

    hubDirtySet.clear();
    pageRepo.markAllCompleteAsPublished();
    out << QStringLiteral("Generated %1 page(s) total.\n").arg(totalPages);
    out.flush();

    holder->deleteLater();
    QMetaObject::invokeMethod(QCoreApplication::instance(),
                              &QCoreApplication::quit, Qt::QueuedConnection);
}
