#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "dialogs/DialogPickEngine.h"
#include "panes/PaneDomains.h"
#include "panes/PanePages.h"
#include "panes/PaneSettings.h"
#include "panes/PaneTheme.h"
#include "website/AbstractEngine.h"
#include "website/WebsiteSettingsTable.h"
#include "website/theme/AbstractTheme.h"
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
    ui->tabDomains->setWorkingDir(workingDir);

    const QString themeId = settings->value(AbstractTheme::settingsKey()).toString();
    const AbstractTheme *themeProto = AbstractTheme::ALL_THEMES().value(themeId, nullptr);
    if (themeProto) {
        m_theme.reset(themeProto->create(workingDir, this));
    }

    if (proto) {
        AbstractEngine *engine = proto->create(this);
        engine->init(workingDir, *m_hostTable);
        engine->setTheme(m_theme.data());
        if (m_theme && engine->rowCount() > 0) {
            m_theme->setSourceLangCode(engine->getLangCode(0));
        }
        m_engine.reset(engine);
        ui->tabDomains->setEngine(m_engine.data());
    }

    ui->tabPages->setup(workingDir, m_engine.data(), m_settingsTable.data());
    ui->tabTheme->setTheme(m_theme.data());

    connect(ui->tabTheme, &PaneTheme::themeIdSelected,
            this, &MainWindow::_reloadTheme);
}

void MainWindow::_reloadTheme(const QString &themeId)
{
    const QDir workingDir = WorkingDirectoryManager::instance()->workingDir();
    const auto settings = WorkingDirectoryManager::instance()->settings();

    const AbstractTheme *proto = AbstractTheme::ALL_THEMES().value(themeId, nullptr);
    if (!proto) {
        return;
    }

    settings->setValue(AbstractTheme::settingsKey(), themeId);

    m_theme.reset(proto->create(workingDir, this));

    if (m_engine) {
        m_engine->setTheme(m_theme.data());
        if (m_engine->rowCount() > 0) {
            m_theme->setSourceLangCode(m_engine->getLangCode(0));
        }
    }

    ui->tabTheme->setTheme(m_theme.data());
}
