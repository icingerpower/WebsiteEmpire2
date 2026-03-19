#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "../../common/workingdirectory/DialogOpenConfig.h"
#include "../../common/types/types.h"

#include "gui/MainWindow.h"
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
    QCoreApplication::setApplicationName("WebsiteAspire");

    QApplication a(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::tr("WebsiteAspire - web scraping tool"));
    parser.addHelpOption();

    const QCommandLineOption workingDirOption(
        AbstractLauncher::OPTION_WORKING_DIR,
        QCoreApplication::tr("Run headlessly using <dir> as working directory."),
        QStringLiteral("dir"));
    parser.addOption(workingDirOption);

    const QCommandLineOption urlsOption(
        QStringLiteral("urls"),
        QCoreApplication::tr("Path to a text file with one URL per line (used by downloaders that support file URL download)."),
        QStringLiteral("filepath"));
    parser.addOption(urlsOption);

    for (AbstractLauncher *launcher : AbstractLauncher::ALL_LAUNCHERS()) {
        parser.addOption(QCommandLineOption(
            launcher->getOptionName(),
            QCoreApplication::tr("Run launcher '%1' with <value>.").arg(launcher->getOptionName()),
            QStringLiteral("value")));
    }

    parser.process(a);

    if (parser.isSet(workingDirOption)) {
        WorkingDirectoryManager::instance()->open(parser.value(workingDirOption));

        bool launcherFound = false;
        for (AbstractLauncher *launcher : AbstractLauncher::ALL_LAUNCHERS()) {
            if (parser.isSet(launcher->getOptionName())) {
                launcher->run(parser.value(launcher->getOptionName()));
                launcherFound = true;
            }
        }

        if (!launcherFound) {
            return 0;
        }
        return a.exec();
    }

    WorkingDirectoryManager::instance()->installDarkOrangePalette();
    DialogOpenConfig dialog;
    dialog.exec();
    if (dialog.wasRejected()) {
        return 0;
    }

    MainWindow w;
    w.show();
    return a.exec();
}
