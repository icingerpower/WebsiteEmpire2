#include "ShortCodeLinkDialog.h"
#include "ui_ShortCodeLinkDialog.h"

ShortCodeLinkDialog::ShortCodeLinkDialog(const QString &title, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ShortCodeLinkDialog)
{
    ui->setupUi(this);
    setWindowTitle(title);
}

ShortCodeLinkDialog::~ShortCodeLinkDialog()
{
    delete ui;
}

QString ShortCodeLinkDialog::url() const
{
    return ui->lineEditUrl->text();
}

QString ShortCodeLinkDialog::rel() const
{
    return ui->lineEditRel->text();
}
