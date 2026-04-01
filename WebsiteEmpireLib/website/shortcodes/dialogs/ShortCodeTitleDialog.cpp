#include "ShortCodeTitleDialog.h"
#include "ui_ShortCodeTitleDialog.h"

ShortCodeTitleDialog::ShortCodeTitleDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ShortCodeTitleDialog)
{
    ui->setupUi(this);
}

ShortCodeTitleDialog::~ShortCodeTitleDialog()
{
    delete ui;
}

int ShortCodeTitleDialog::level() const
{
    // comboBox items are "1" through "6"; currentIndex() is 0-based.
    return ui->comboBoxLevel->currentIndex() + 1;
}
