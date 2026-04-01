#include "ShortCodeImageDialog.h"
#include "ui_ShortCodeImageDialog.h"

ShortCodeImageDialog::ShortCodeImageDialog(const QString &title, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ShortCodeImageDialog)
{
    ui->setupUi(this);
    setWindowTitle(title);
}

ShortCodeImageDialog::~ShortCodeImageDialog()
{
    delete ui;
}

QString ShortCodeImageDialog::id() const
{
    return ui->lineEditId->text();
}

QString ShortCodeImageDialog::fileName() const
{
    return ui->lineEditFileName->text();
}

QString ShortCodeImageDialog::alt() const
{
    return ui->lineEditAlt->text();
}

int ShortCodeImageDialog::imageWidth() const
{
    return ui->spinBoxWidth->value();
}

int ShortCodeImageDialog::imageHeight() const
{
    return ui->spinBoxHeight->value();
}
