#include "PaneAspire.h"
#include "ui_PaneAspire.h"

PaneAspire::PaneAspire(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneAspire)
{
    ui->setupUi(this);
}

PaneAspire::~PaneAspire()
{
    delete ui;
}
