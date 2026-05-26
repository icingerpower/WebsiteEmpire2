#include "PageBlocMetaWidget.h"
#include "ui_PageBlocMetaWidget.h"

#include "website/pages/blocs/PageBlocMeta.h"

PageBlocMetaWidget::PageBlocMetaWidget(QWidget *parent)
    : AbstractPageBlockWidget(parent)
    , ui(new Ui::PageBlocMetaWidget)
{
    ui->setupUi(this);
    connect(ui->editTitle, &QLineEdit::textChanged,
            this, &PageBlocMetaWidget::_onTitleChanged);
    connect(ui->editDescription, &QPlainTextEdit::textChanged,
            this, &PageBlocMetaWidget::_onDescChanged);
}

PageBlocMetaWidget::~PageBlocMetaWidget()
{
    delete ui;
}

void PageBlocMetaWidget::load(const QHash<QString, QString> &values)
{
    ui->editTitle->setText(values.value(QLatin1String(PageBlocMeta::KEY_SEO_TITLE)));
    ui->editDescription->setPlainText(values.value(QLatin1String(PageBlocMeta::KEY_SEO_DESC)));
}

void PageBlocMetaWidget::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(PageBlocMeta::KEY_SEO_TITLE), ui->editTitle->text());
    values.insert(QLatin1String(PageBlocMeta::KEY_SEO_DESC),  ui->editDescription->toPlainText());
}

void PageBlocMetaWidget::_onTitleChanged(const QString &text)
{
    ui->labelTitleCount->setText(QString::number(text.size()) + QStringLiteral(" / 60"));
}

void PageBlocMetaWidget::_onDescChanged()
{
    const int n = ui->editDescription->toPlainText().size();
    ui->labelDescCount->setText(QString::number(n) + QStringLiteral(" / 160"));
}
