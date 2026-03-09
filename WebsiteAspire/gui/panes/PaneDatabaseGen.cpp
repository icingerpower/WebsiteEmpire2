#include "PaneDatabaseGen.h"
#include "ui_PaneDatabaseGen.h"

PaneDatabaseGen::PaneDatabaseGen(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneDatabaseGen)
{
    ui->setupUi(this);
}

PaneDatabaseGen::~PaneDatabaseGen()
{
    delete ui;
}
