#include "ShortCodeSpinnableDialog.h"
#include "ui_ShortCodeSpinnableDialog.h"

ShortCodeSpinnableDialog::ShortCodeSpinnableDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ShortCodeSpinnableDialog)
{
    ui->setupUi(this);
}

ShortCodeSpinnableDialog::~ShortCodeSpinnableDialog()
{
    delete ui;
}

int ShortCodeSpinnableDialog::spinnableId() const
{
    return ui->spinBoxId->value();
}

bool ShortCodeSpinnableDialog::random() const
{
    return ui->checkBoxRandom->isChecked();
}
