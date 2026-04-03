#include "PaneSettings.h"
#include "ui_PaneSettings.h"

#include "website/WebsiteSettingsTable.h"

PaneSettings::PaneSettings(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneSettings)
{
    ui->setupUi(this);
}

PaneSettings::~PaneSettings()
{
    delete ui;
}

void PaneSettings::setSettingsTable(WebsiteSettingsTable *settingsTable)
{
    ui->tableViewSettings->setModel(settingsTable);
}
