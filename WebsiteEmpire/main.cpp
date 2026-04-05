#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "../../common/workingdirectory/DialogOpenConfig.h"
#include "../../common/types/types.h"

#include "gui/MainWindow.h"
#include "gui/dialogs/DialogPickEngine.h"
#include "launcher/AbstractLauncher.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QSet>

int main(int argc, char *argv[])
{
    qRegisterMetaType<QSet<QString>>();
    qRegisterMetaType<QHash<QString, QSet<QString>>>();
    qRegisterMetaType<QMap<QString, bool>>();

    QCoreApplication::setOrganizationName("Icinger Power");
    QCoreApplication::setOrganizationDomain("ecomelitepro.com");
    QCoreApplication::setApplicationName("WebsiteEmpire");

    QApplication a(argc, argv);

    // -------------------------------------------------------------------------
    // CLI mode: --workingDir <dir> [--<launcher>]
    // -------------------------------------------------------------------------
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::tr("WebsiteEmpire - website building tool"));
    parser.addHelpOption();

    const QCommandLineOption workingDirOption(
        AbstractLauncher::OPTION_WORKING_DIR,
        QCoreApplication::tr("Run headlessly using <dir> as working directory."),
        QStringLiteral("dir"));
    parser.addOption(workingDirOption);

    for (AbstractLauncher *launcher : AbstractLauncher::ALL_LAUNCHERS()) {
        if (launcher->isFlag()) {
            parser.addOption(QCommandLineOption(
                launcher->getOptionName(),
                QCoreApplication::tr("Run launcher '%1'.")
                    .arg(launcher->getOptionName())));
        } else {
            parser.addOption(QCommandLineOption(
                launcher->getOptionName(),
                QCoreApplication::tr("Run launcher '%1' with <value>.")
                    .arg(launcher->getOptionName()),
                QStringLiteral("value")));
        }
    }

    parser.parse(QCoreApplication::arguments());

    if (parser.isSet(workingDirOption)) {
        WorkingDirectoryManager::instance()->open(parser.value(workingDirOption));

        bool launcherFound = false;
        for (AbstractLauncher *launcher : AbstractLauncher::ALL_LAUNCHERS()) {
            if (parser.isSet(launcher->getOptionName())) {
                const QString value = launcher->isFlag()
                                          ? QString()
                                          : parser.value(launcher->getOptionName());
                launcher->run(value);
                launcherFound = true;
            }
        }

        if (!launcherFound) {
            qDebug() << "WebsiteEmpire: --workingDir given but no launcher option found.";
            return 0;
        }
        return a.exec();
    }

    // -------------------------------------------------------------------------
    // GUI mode
    // -------------------------------------------------------------------------
    WorkingDirectoryManager::instance()->installDarkOrangePalette();
    DialogOpenConfig dialog;
    dialog.exec();
    if (dialog.wasRejected()) {
        return 0;
    }

    const auto settings = WorkingDirectoryManager::instance()->settings();
    if (settings->value(DialogPickEngine::settingsKey()).toString().isEmpty()) {
        DialogPickEngine pickDialog;
        if (pickDialog.exec() != QDialog::Accepted) {
            return 0;
        }
    }

    MainWindow w;
    w.show();
    return a.exec();
}
