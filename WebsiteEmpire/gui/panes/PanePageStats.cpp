#include "PanePageStats.h"
#include "ui_PanePageStats.h"

#include "website/pages/widgets/PagesStatsWidget.h"

#include <QDir>

PanePageStats::PanePageStats(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PanePageStats)
{
    ui->setupUi(this);
}

PanePageStats::~PanePageStats()
{
    delete ui;
}

void PanePageStats::setWorkingDir(const QDir &workingDir)
{
    if (m_statsWidget) {
        ui->verticalLayout->removeWidget(m_statsWidget);
        delete m_statsWidget; // closes its stats.db connection
        m_statsWidget = nullptr;
    }
    m_statsWidget = new PagesStatsWidget(workingDir, this);
    ui->verticalLayout->addWidget(m_statsWidget);
}
