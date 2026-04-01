#include "ShortCodeVideoDialog.h"
#include "ui_ShortCodeVideoDialog.h"

ShortCodeVideoDialog::ShortCodeVideoDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ShortCodeVideoDialog)
{
    ui->setupUi(this);
}

ShortCodeVideoDialog::~ShortCodeVideoDialog()
{
    delete ui;
}

QString ShortCodeVideoDialog::url() const
{
    return ui->lineEditUrl->text();
}
