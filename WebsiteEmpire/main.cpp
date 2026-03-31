#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "../../common/workingdirectory/DialogOpenConfig.h"
#include "../../common/types/types.h"

#include "gui/MainWindow.h"
#include "gui/dialogs/DialogPickEngine.h"

#include <QApplication>
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
