#include "LauncherTranslateCommon.h"

#include "website/AbstractEngine.h"
#include "website/HostTable.h"
#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/theme/AbstractTheme.h"
#include "website/translation/CommonBlocTranslator.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QSet>

const QString LauncherTranslateCommon::OPTION_NAME = QStringLiteral("translateCommon");

DECLARE_LAUNCHER(LauncherTranslateCommon)

QString LauncherTranslateCommon::getOptionName() const { return OPTION_NAME; }
bool    LauncherTranslateCommon::isFlag()         const { return true; }

void LauncherTranslateCommon::run(const QString & /*value*/)
{
    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    const auto &settings  = WorkingDirectoryManager::instance()->settings();

    // -------------------------------------------------------------------------
    // Resolve engine
    // -------------------------------------------------------------------------
    const QString engineId = settings->value(QStringLiteral("engineId")).toString();
    const AbstractEngine *proto = AbstractEngine::ALL_ENGINES().value(engineId, nullptr);
    if (!proto) {
        qDebug() << "LauncherTranslateCommon: no engine configured (engineId =" << engineId << ").";
        QCoreApplication::quit();
        return;
    }

    auto *holder    = new QObject(nullptr);
    auto *hostTable = new HostTable(workingDir, holder);
    auto *engine    = proto->create(holder);
    engine->init(workingDir, *hostTable);

    // -------------------------------------------------------------------------
    // Resolve theme
    // -------------------------------------------------------------------------
    const QString themeId = settings->value(AbstractTheme::settingsKey()).toString();
    const AbstractTheme *themeProto = AbstractTheme::ALL_THEMES().value(themeId, nullptr);
    if (!themeProto) {
        qDebug() << "LauncherTranslateCommon: no theme configured (themeId =" << themeId << ").";
        holder->deleteLater();
        QCoreApplication::quit();
        return;
    }

    auto *theme = themeProto->create(workingDir, holder);

    // -------------------------------------------------------------------------
    // Determine source language
    // Source lang is stored in the theme's params INI by MainWindow; fall back
    // to the engine's first row language when not yet saved.
    // -------------------------------------------------------------------------
    QString sourceLang = theme->sourceLangCode();
    if (sourceLang.isEmpty() && engine->rowCount() > 0) {
        sourceLang = engine->getLangCode(0);
    }
    if (sourceLang.isEmpty()) {
        qDebug() << "LauncherTranslateCommon: cannot determine source language.";
        holder->deleteLater();
        QCoreApplication::quit();
        return;
    }

    // -------------------------------------------------------------------------
    // Collect unique target languages from the engine (excluding source lang)
    // -------------------------------------------------------------------------
    QSet<QString> seen;
    QStringList targetLangs;
    const int rowCount = engine->rowCount();
    for (int i = 0; i < rowCount; ++i) {
        const QString lang = engine->getLangCode(i);
        if (!lang.isEmpty() && lang != sourceLang && !seen.contains(lang)) {
            seen.insert(lang);
            targetLangs.append(lang);
        }
    }

    if (targetLangs.isEmpty()) {
        qDebug() << "LauncherTranslateCommon: no target languages in engine.";
        holder->deleteLater();
        QCoreApplication::quit();
        return;
    }

    // -------------------------------------------------------------------------
    // Build jobs and run
    // -------------------------------------------------------------------------
    const QList<AbstractCommonBloc *> blocs =
        theme->getTopBlocs() + theme->getBottomBlocs();
    const QList<CommonBlocTranslator::TranslationJob> jobs =
        CommonBlocTranslator::buildJobs(blocs, sourceLang, targetLangs);

    qDebug() << "[TranslateCommon] Scheduler queued" << jobs.size() << "job(s).";

    auto *translator = new CommonBlocTranslator(*theme, workingDir, holder);

    QObject::connect(translator, &CommonBlocTranslator::logMessage, holder,
                     [](const QString &msg) {
                         qDebug() << "[TranslateCommon]" << qPrintable(msg);
                     });

    QObject::connect(translator, &CommonBlocTranslator::finished, holder,
                     [holder](int translated, int errors) {
                         qDebug() << "[TranslateCommon] Done. Translated:" << translated
                                  << " Errors:" << errors;
                         holder->deleteLater();
                         QCoreApplication::quit();
                     });

    translator->startWithJobs(jobs);
}
