#include "PageBlocJsWidget.h"
#include "ui_PageBlocJsWidget.h"

#include "website/pages/blocs/PageBlocJs.h"

PageBlocJsWidget::PageBlocJsWidget(QWidget *parent)
    : AbstractPageBlockWidget(parent)
    , ui(new Ui::PageBlocJsWidget)
{
    ui->setupUi(this);
}

PageBlocJsWidget::~PageBlocJsWidget()
{
    delete ui;
}

void PageBlocJsWidget::load(const QHash<QString, QString> &values)
{
    ui->htmlEdit->setPlainText(values.value(QLatin1String(PageBlocJs::KEY_HTML)));
    ui->cssEdit->setPlainText(values.value(QLatin1String(PageBlocJs::KEY_CSS)));
    ui->jsEdit->setPlainText(values.value(QLatin1String(PageBlocJs::KEY_JS)));
}

void PageBlocJsWidget::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(PageBlocJs::KEY_HTML), ui->htmlEdit->toPlainText());
    values.insert(QLatin1String(PageBlocJs::KEY_CSS),  ui->cssEdit->toPlainText());
    values.insert(QLatin1String(PageBlocJs::KEY_JS),   ui->jsEdit->toPlainText());
}
