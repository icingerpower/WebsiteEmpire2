#include "ShortCodeInlineDialog.h"
#include "ui_ShortCodeInlineDialog.h"

ShortCodeInlineDialog::ShortCodeInlineDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ShortCodeInlineDialog)
{
    ui->setupUi(this);
}

ShortCodeInlineDialog::~ShortCodeInlineDialog()
{
    delete ui;
}
