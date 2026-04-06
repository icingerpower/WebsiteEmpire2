#include "PaneMenus.h"
#include "ui_PaneMenus.h"

PaneMenus::PaneMenus(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneMenus)
{
    ui->setupUi(this);
}

PaneMenus::~PaneMenus()
{
    delete ui;
}
