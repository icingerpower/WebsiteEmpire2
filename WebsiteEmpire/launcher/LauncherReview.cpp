#include "LauncherReview.h"

#include "website/AbstractEngine.h"
#include "website/HostTable.h"
#include "website/WebsiteSettingsTable.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/perf/AbstractPerformanceDataSource.h"
#include "website/perf/GscDataSource.h"
#include "website/perf/GscSettings.h"
#include "website/perf/StatsDbDataSource.h"
#include "website/review/AbstractReviewAction.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QObject>
#include <QTextStream>

const QString LauncherReview::OPTION_NAME = QStringLiteral("review");

DECLARE_LAUNCHER(LauncherReview)

QString LauncherReview::getOptionName() const { return OPTION_NAME; }
bool    LauncherReview::isFlag()        const { return true; }

void LauncherReview::run(const QString & /*value*/)
{
    QTextStream out(stdout);

    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();

    // ---- Resolve engine and primary domain --------------------------------
    const auto &settings = WorkingDirectoryManager::instance()->settings();
    const QString engineId = settings->value(QStringLiteral("engineId")).toString();
    const AbstractEngine *proto = AbstractEngine::ALL_ENGINES().value(engineId, nullptr);

    QString primaryDomain;
    if (proto) {
        auto *holder     = new QObject(nullptr);
        auto *hostTable  = new HostTable(workingDir, holder);
        auto *engine     = proto->create(holder);
        engine->init(workingDir, *hostTable);

        const WebsiteSettingsTable settingsTable(workingDir);
        const QString editingLang = settingsTable.editingLangCode();

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
        delete holder;
    }

    // ---- Performance data source ------------------------------------------
    AbstractPerformanceDataSource *dataSource = nullptr;
    GscSettings gscSettings(workingDir);
    GscDataSource gscSource(gscSettings);
    StatsDbDataSource statsSource(workingDir);

    if (gscSettings.isConfigured()) {
        dataSource = &gscSource;
        out << QStringLiteral("Performance source: Google Search Console\n");
    } else if (statsSource.isConfigured()) {
        dataSource = &statsSource;
        out << QStringLiteral("Performance source: stats.db\n");
    } else {
        out << QStringLiteral("Performance source: none (display counts will be 0)\n");
    }
    out.flush();

    // ---- Fetch performance data once ---------------------------------------
    QHash<QString, int> displayByPermalink; // permalink → impression/display count
    if (dataSource && !primaryDomain.isEmpty()) {
        const QList<UrlPerformance> perfData = dataSource->fetchData(primaryDomain);
        for (const UrlPerformance &perf : std::as_const(perfData)) {
            // URL from data sources may be absolute; extract the path component.
            QString path = perf.url;
            const int schemeEnd = path.indexOf(QStringLiteral("://"));
            if (schemeEnd >= 0) {
                const int slashPos = path.indexOf(QLatin1Char('/'), schemeEnd + 3);
                path = (slashPos >= 0) ? path.mid(slashPos) : QStringLiteral("/");
            }
            const int count = perf.impressions > 0 ? perf.impressions : perf.clicks;
            displayByPermalink.insert(path, count);
        }
    }

    // ---- Load pages --------------------------------------------------------
    PageDb           pageDb(workingDir);
    PageRepositoryDb pageRepo(pageDb);

    const QList<PageRecord> allPages = pageRepo.findAll();
    const QList<AbstractReviewAction *> actions = AbstractReviewAction::all();

    if (actions.isEmpty()) {
        out << QStringLiteral("No review actions registered — nothing to do.\n");
        out.flush();
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  &QCoreApplication::quit,
                                  Qt::QueuedConnection);
        return;
    }

    out << QStringLiteral("Running %1 review action(s) on %2 page(s)...\n")
               .arg(actions.size())
               .arg(allPages.size());
    out.flush();

    int totalChanges = 0;

    for (const PageRecord &page : std::as_const(allPages)) {
        if (page.sourcePageId != 0) {
            continue; // skip translations — only review source pages
        }

        const int displayCount = displayByPermalink.value(page.permalink, 0);

        for (AbstractReviewAction *action : std::as_const(actions)) {
            if (action->run(page, displayCount, pageRepo)) {
                out << QStringLiteral("[%1] %2 — display: %3 → flagged\n")
                           .arg(action->getDisplayName())
                           .arg(page.permalink)
                           .arg(displayCount);
                out.flush();
                ++totalChanges;
            }
        }
    }

    out << QStringLiteral("Review complete. %1 change(s) applied.\n").arg(totalChanges);
    out.flush();

    QMetaObject::invokeMethod(QCoreApplication::instance(),
                              &QCoreApplication::quit,
                              Qt::QueuedConnection);
}
