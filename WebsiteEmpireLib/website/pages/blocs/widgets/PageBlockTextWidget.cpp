#include "PageBlockTextWidget.h"
#include "ui_PageBlockTextWidget.h"

PageBlockTextWidget::PageBlockTextWidget(QWidget *parent)
    : AbstractPageBlockWidget(parent)
    , ui(new Ui::PageBlockTextWidget)
{
    ui->setupUi(this);
}

PageBlockTextWidget::~PageBlockTextWidget()
{
    delete ui;
}

void PageBlockTextWidget::load(const QString &origContent)
{
    ui->textEdit->setPlainText(origContent);
}

void PageBlockTextWidget::save(QString &contentToUpdate)
{
    contentToUpdate = ui->textEdit->toPlainText();
}
