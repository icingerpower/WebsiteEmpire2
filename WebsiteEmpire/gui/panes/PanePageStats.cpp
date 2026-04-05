#include "PanePageStats.h"
#include "ui_PanePageStats.h"

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
