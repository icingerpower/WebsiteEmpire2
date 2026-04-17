#include "LauncherTranslate.h"

#include "website/AbstractEngine.h"
#include "website/HostTable.h"
#include "website/WebsiteSettingsTable.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/PageTranslator.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/translation/PageAssessor.h"
#include "website/translation/TranslationScheduler.h"
#include "website/translation/TranslationSettings.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

const QString LauncherTranslate::OPTION_NAME = QStringLiteral("translate");

DECLARE_LAUNCHER(LauncherTranslate)

QString LauncherTranslate::getOptionName() const { return OPTION_NAME; }
bool    LauncherTranslate::isFlag()         const { return true; }

void LauncherTranslate::run(const QString & /*value*/)
{
    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();

    // -------------------------------------------------------------------------
    // Resolve editing language from settings_global.csv
    // -------------------------------------------------------------------------
    WebsiteSettingsTable settingsTable(workingDir);
    const QString editingLang = settingsTable.editingLangCode();
    if (editingLang.isEmpty()) {
        qDebug() << "LauncherTranslate: editing language is not configured in settings.";
        QCoreApplication::quit();
        return;
    }

    // -------------------------------------------------------------------------
    // Resolve engine from settings.ini
    // -------------------------------------------------------------------------
    const auto &settings = WorkingDirectoryManager::instance()->settings();
    const QString engineId = settings->value(QStringLiteral("engineId")).toString();
    const AbstractEngine *proto = AbstractEngine::ALL_ENGINES().value(engineId, nullptr);
    if (!proto) {
        qDebug() << "LauncherTranslate: no engine configured (engineId =" << engineId << ").";
        QCoreApplication::quit();
        return;
    }

    // -------------------------------------------------------------------------
    // Build object graph
    // -------------------------------------------------------------------------
    // holder owns all QObject-derived resources; destroyed when translation ends.
    auto *holder = new QObject(nullptr);

    auto *hostTable = new HostTable(workingDir, holder);
    auto *engine    = proto->create(holder);
    engine->init(workingDir, *hostTable);

    auto *categoryTable = new CategoryTable(workingDir, holder);

    // PageDb and PageRepositoryDb are not QObjects; owned manually and deleted
    // in the finished-signal handler below (repo before db).
    auto *pageDb   = new PageDb(workingDir);
    auto *pageRepo = new PageRepositoryDb(*pageDb);

    // -------------------------------------------------------------------------
    // Load translation settings and run assessment + scheduling
    // -------------------------------------------------------------------------
    const TranslationSettings translationSettings(workingDir);

    if (translationSettings.isConfigured()) {
        const int assessed = PageAssessor::assess(*pageRepo, translationSettings.targetLangs);
        if (assessed > 0) {
            qDebug() << "[Translate] Assessed" << assessed << "new page(s).";
        }
    } else {
        qDebug() << "[Translate] No translation_settings.json found — skipping assessment.";
    }

    auto *translator = new PageTranslator(*pageRepo, *categoryTable, workingDir, holder);

    QObject::connect(translator, &PageTranslator::logMessage, holder, [](const QString &msg) {
        qDebug() << "[Translate]" << qPrintable(msg);
    });

    QObject::connect(translator, &PageTranslator::finished, holder,
                     [holder, pageRepo, pageDb](int translated, int errors) {
                         qDebug() << "[Translate] Done. Translated:" << translated
                                  << " Errors:" << errors;
                         // Destroy repo before db (repo holds a reference to db).
                         delete pageRepo;
                         delete pageDb;
                         holder->deleteLater();
                         QCoreApplication::quit();
                     });

    if (translationSettings.isConfigured()) {
        const QList<PageTranslator::TranslationJob> jobs =
            TranslationScheduler::buildJobs(*pageRepo, *categoryTable,
                                            translationSettings, editingLang);
        qDebug() << "[Translate] Scheduler queued" << jobs.size() << "job(s).";
        translator->startWithJobs(jobs);
    } else {
        // No settings file — fall back to the legacy full scan.
        translator->start(engine, editingLang);
    }
}
