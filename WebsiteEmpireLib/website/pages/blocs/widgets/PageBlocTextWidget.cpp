#include "PageBlocTextWidget.h"
#include "ui_PageBlocTextWidget.h"

PageBlocTextWidget::PageBlocTextWidget(QWidget *parent)
    : AbstractPageBlockWidget(parent)
    , ui(new Ui::PageBlocTextWidget)
{
    ui->setupUi(this);
}

PageBlocTextWidget::~PageBlocTextWidget()
{
    delete ui;
}

void PageBlocTextWidget::load(const QString &origContent)
{
    ui->textEdit->setPlainText(origContent);
}

void PageBlocTextWidget::save(QString &contentToUpdate)
{
    contentToUpdate = ui->textEdit->toPlainText();
}
