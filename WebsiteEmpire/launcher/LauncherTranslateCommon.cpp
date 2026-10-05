#include "LauncherTranslateCommon.h"

#include "aicli/AbstractCli.h"
#include "website/AbstractEngine.h"
#include "website/HostTable.h"
#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/taxonomy/TaxonomyDb.h"
#include "website/taxonomy/TaxonomyTranslationFilter.h"
#include "website/taxonomy/TaxonomyTranslator.h"
#include "website/theme/AbstractTheme.h"
#include "website/translation/CategoryTranslator.h"
#include "website/translation/CommonBlocTranslator.h"
#include "website/translation/HubSeoTranslator.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QMap>
#include <QSet>
#include <QStringList>

const QString LauncherTranslateCommon::OPTION_NAME     = QStringLiteral("translateCommon");
const QString LauncherTranslateCommon::OPTION_LANGUAGE = QStringLiteral("language");

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
    // Collect target languages: --language <code> narrows to one; otherwise all
    // engine languages are used; each translator skips its own source language.
    // -------------------------------------------------------------------------
    const QStringList args = QCoreApplication::arguments();
    const int langIdx = args.indexOf(QStringLiteral("--") + OPTION_LANGUAGE);
    const QString languageFilter = (langIdx >= 0 && langIdx + 1 < args.size())
                                   ? args.at(langIdx + 1)
                                   : QString();

    // --cli <name>: select AI CLI; falls back to the first registered CLI.
    AbstractCli *cli = nullptr;
    const int cliIdx = args.indexOf(QStringLiteral("--") + AbstractLauncher::OPTION_CLI);
    if (cliIdx >= 0 && cliIdx + 1 < args.size()) {
        const QString cliName = args.at(cliIdx + 1);
        for (AbstractCli *c : AbstractCli::ALL_CLIS()) {
            if (c->getName() == cliName) {
                cli = c;
                break;
            }
        }
    }
    if (!cli) {
        cli = AbstractCli::ALL_CLIS().isEmpty() ? nullptr : AbstractCli::ALL_CLIS().first();
    }
    if (!cli) {
        qDebug() << "LauncherTranslateCommon: no AI CLI available.";
        holder->deleteLater();
        QCoreApplication::quit();
        return;
    }

    QStringList targetLangs;
    if (!languageFilter.isEmpty()) {
        targetLangs.append(languageFilter);
    } else {
        QSet<QString> seen;
        const int rowCount = engine->rowCount();
        for (int i = 0; i < rowCount; ++i) {
            const QString lang = engine->getLangCode(i);
            if (!lang.isEmpty() && !seen.contains(lang)) {
                seen.insert(lang);
                targetLangs.append(lang);
            }
        }
    }

    if (targetLangs.isEmpty()) {
        qDebug() << "LauncherTranslateCommon: no target languages to translate.";
        holder->deleteLater();
        QCoreApplication::quit();
        return;
    }

    // -------------------------------------------------------------------------
    // Build jobs and run — blocs → categories → taxonomy → hub SEO (sequential)
    // -------------------------------------------------------------------------
    const QList<AbstractCommonBloc *> blocs =
        theme->getTranslationBlocs();
    const QList<CommonBlocTranslator::TranslationJob> blocJobs =
        CommonBlocTranslator::buildJobs(blocs, sourceLang, targetLangs);

    auto *categoryTable = new CategoryTable(workingDir, holder);
    const QList<CategoryTranslator::TranslationJob> catJobs =
        CategoryTranslator::buildJobs(*categoryTable, sourceLang, targetLangs);

    auto *taxonomyDb = new TaxonomyDb(workingDir);
    // Only translate taxonomies the active engine declares as translatable —
    // e.g. a Fashion engine's Color/Season/Occasion/Material/Style, never a
    // Health engine's Symptoms, regardless of what's synced into taxonomy.db.
    const QStringList taxTypes = TaxonomyTranslationFilter::filterTranslatable(
        engine, taxonomyDb->allTypes());
    const QList<TaxonomyTranslator::TranslationJob> taxJobs =
        TaxonomyTranslator::buildJobs(*taxonomyDb, taxTypes, sourceLang, targetLangs);

    // Collect SEO templates from every registered page type.
    QMap<QString, QMap<QString, QString>> hubTemplates;
    for (const QString &typeId : AbstractPageType::allTypeIds()) {
        CategoryTable dummyTable(workingDir, nullptr);
        const auto pageType = AbstractPageType::createForTypeId(typeId, dummyTable);
        if (pageType) {
            const QMap<QString, QString> tmpls = pageType->seoTemplateStrings();
            if (!tmpls.isEmpty()) {
                hubTemplates.insert(typeId, tmpls);
            }
        }
    }

    auto *hubSeoTranslator = new HubSeoTranslator(workingDir, cli, holder);
    const QList<HubSeoTranslator::TranslationJob> hubJobs =
        hubSeoTranslator->buildJobs(hubTemplates, sourceLang, targetLangs);

    qDebug() << "[TranslateCommon] Bloc jobs:" << blocJobs.size()
             << " Category jobs:" << catJobs.size()
             << " Taxonomy jobs:" << taxJobs.size()
             << " Hub SEO jobs:" << hubJobs.size();

    auto *blocTranslator = new CommonBlocTranslator(*theme, workingDir, cli, holder);
    auto *catTranslator  = new CategoryTranslator(*categoryTable, workingDir, cli, holder);
    auto *taxTranslator  = new TaxonomyTranslator(*taxonomyDb, workingDir, cli, holder);

    QObject::connect(blocTranslator, &CommonBlocTranslator::logMessage, holder,
                     [](const QString &msg) {
                         qDebug() << "[TranslateCommon]" << qPrintable(msg);
                     });
    QObject::connect(catTranslator, &CategoryTranslator::logMessage, holder,
                     [](const QString &msg) {
                         qDebug() << "[TranslateCategories]" << qPrintable(msg);
                     });
    QObject::connect(taxTranslator, &TaxonomyTranslator::logMessage, holder,
                     [](const QString &msg) {
                         qDebug() << "[TranslateTaxonomy]" << qPrintable(msg);
                     });
    QObject::connect(hubSeoTranslator, &HubSeoTranslator::logMessage, holder,
                     [](const QString &msg) {
                         qDebug() << "[TranslateHubSeo]" << qPrintable(msg);
                     });

    QObject::connect(blocTranslator, &CommonBlocTranslator::finished, holder,
                     [catTranslator, catJobs](int translated, int errors) {
                         qDebug() << "[TranslateCommon] Blocs done. Translated:" << translated
                                  << " Errors:" << errors;
                         catTranslator->startWithJobs(catJobs);
                     });

    QObject::connect(catTranslator, &CategoryTranslator::finished, holder,
                     [taxTranslator, taxJobs](int translated, int errors) {
                         qDebug() << "[TranslateCategories] Done. Translated:" << translated
                                  << " Errors:" << errors;
                         taxTranslator->startWithJobs(taxJobs);
                     });

    QObject::connect(taxTranslator, &TaxonomyTranslator::finished, holder,
                     [hubSeoTranslator, hubJobs, taxonomyDb](int translated, int errors) {
                         qDebug() << "[TranslateTaxonomy] Done. Translated:" << translated
                                  << " Errors:" << errors;
                         delete taxonomyDb;
                         hubSeoTranslator->startWithJobs(hubJobs);
                     });

    QObject::connect(hubSeoTranslator, &HubSeoTranslator::finished, holder,
                     [holder](int translated, int errors) {
                         qDebug() << "[TranslateHubSeo] Done. Translated:" << translated
                                  << " Errors:" << errors;
                         holder->deleteLater();
                         QCoreApplication::quit();
                     });

    blocTranslator->startWithJobs(blocJobs);
}
