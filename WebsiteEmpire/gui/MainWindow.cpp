#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "dialogs/DialogPickEngine.h"
#include "panes/PaneDomains.h"
#include "panes/PaneSettings.h"
#include "website/AbstractEngine.h"
#include "website/WebsiteSettingsTable.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include <QSettings>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    _init();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::_init()
{
    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();

    m_hostTable.reset(new HostTable(workingDir));
    m_settingsTable.reset(new WebsiteSettingsTable(workingDir));
    ui->tabSettings->setSettingsTable(m_settingsTable.data());

    const auto settings = WorkingDirectoryManager::instance()->settings();
    const QString engineId = settings->value(DialogPickEngine::settingsKey()).toString();
    const AbstractEngine *proto = AbstractEngine::ALL_ENGINES().value(engineId, nullptr);
    ui->tabDomains->setHostTable(m_hostTable.data());

    if (proto) {
        AbstractEngine *engine = proto->create(this);
        engine->init(workingDir, *m_hostTable);
        m_engine.reset(engine);
        ui->tabDomains->setEngine(m_engine.data());
    }
}
