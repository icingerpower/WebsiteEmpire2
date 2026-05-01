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
#include <QStringList>

#include <algorithm>

const QString LauncherTranslate::OPTION_NAME     = QStringLiteral("translate");
const QString LauncherTranslate::OPTION_LANGUAGE  = QStringLiteral("language");
const QString LauncherTranslate::OPTION_LIMIT     = QStringLiteral("limit");

DECLARE_LAUNCHER(LauncherTranslate)

QString LauncherTranslate::getOptionName() const { return OPTION_NAME; }
bool    LauncherTranslate::isFlag()         const { return true; }

void LauncherTranslate::run(const QString & /*value*/)
{
    // Parse optional sub-options from raw args.
    const QStringList args = QCoreApplication::arguments();

    // --language <code>: restrict to a single target language.
    const int langIdx = args.indexOf(QStringLiteral("--") + OPTION_LANGUAGE);
    const QString languageFilter = (langIdx >= 0 && langIdx + 1 < args.size())
                                   ? args.at(langIdx + 1)
                                   : QString();

    // --limit <n>: cap the number of translation jobs for this run.
    int limitOverride = -1;
    const int limitIdx = args.indexOf(QStringLiteral("--") + OPTION_LIMIT);
    if (limitIdx >= 0 && limitIdx + 1 < args.size()) {
        bool ok = false;
        const int n = args.at(limitIdx + 1).toInt(&ok);
        if (ok && n >= 1) {
            limitOverride = n;
        }
    }

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
    // Determine effective target languages:
    //   1. From translation_settings.json when it exists.
    //   2. Derived from the engine (all langs != editingLang) as a fallback so
    //      the command works even without a settings file.
    // -------------------------------------------------------------------------
    const TranslationSettings translationSettings(workingDir);

    QStringList effectiveTargetLangs;
    if (translationSettings.isConfigured()) {
        effectiveTargetLangs = translationSettings.targetLangs;
    } else {
        QSet<QString> seen;
        for (int i = 0; i < engine->rowCount(); ++i) {
            const QString lang = engine->getLangCode(i);
            if (!lang.isEmpty() && lang != editingLang && !seen.contains(lang)) {
                seen.insert(lang);
                effectiveTargetLangs.append(lang);
            }
        }
        if (effectiveTargetLangs.isEmpty()) {
            qDebug() << "[Translate] No target languages found in engine — nothing to do.";
            delete pageRepo;
            delete pageDb;
            holder->deleteLater();
            QCoreApplication::quit();
            return;
        }
        qDebug() << "[Translate] No translation_settings.json — "
                    "using engine languages:" << effectiveTargetLangs;
    }

    // -------------------------------------------------------------------------
    // Assessment: stamp target languages onto any un-assessed source pages.
    // -------------------------------------------------------------------------
    const int assessed = PageAssessor::assess(*pageRepo, effectiveTargetLangs);
    if (assessed > 0) {
        qDebug() << "[Translate] Assessed" << assessed << "new page(s).";
    }

    // -------------------------------------------------------------------------
    // Build effective settings for the scheduler (may be synthetic when no file)
    // -------------------------------------------------------------------------
    TranslationSettings effectiveSettings;
    effectiveSettings.targetLangs       = effectiveTargetLangs;
    effectiveSettings.limitPerRun       = translationSettings.limitPerRun;
    effectiveSettings.priorityPageTypes = translationSettings.priorityPageTypes;

    auto *translator = new PageTranslator(*pageRepo, *categoryTable, workingDir, holder);

    QObject::connect(translator, &PageTranslator::logMessage, holder, [](const QString &msg) {
        qDebug() << "[Translate]" << qPrintable(msg);
    });

    QObject::connect(translator, &PageTranslator::finished, holder,
                     [holder, pageRepo, pageDb](int translated, int errors) {
                         qDebug() << "[Translate] Done. Translated:" << translated
                                  << " Errors:" << errors;
                         delete pageRepo;
                         delete pageDb;
                         holder->deleteLater();
                         QCoreApplication::quit();
                     });

    QList<PageTranslator::TranslationJob> jobs =
        TranslationScheduler::buildJobs(*pageRepo, *categoryTable,
                                        effectiveSettings, editingLang);

    if (!languageFilter.isEmpty()) {
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                       [&languageFilter](const PageTranslator::TranslationJob &j) {
                           return j.targetLang != languageFilter;
                       }),
                   jobs.end());
        qDebug() << "[Translate] Language filter:" << languageFilter
                 << "→" << jobs.size() << "job(s).";
    }

    if (limitOverride > 0 && jobs.size() > limitOverride) {
        jobs.resize(limitOverride);
        qDebug() << "[Translate] Limit override:" << limitOverride << "job(s).";
    }

    qDebug() << "[Translate] Scheduler queued" << jobs.size() << "job(s).";
    translator->startWithJobs(jobs);
}
