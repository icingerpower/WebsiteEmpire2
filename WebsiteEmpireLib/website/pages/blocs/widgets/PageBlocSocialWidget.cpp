#include "PageBlocSocialWidget.h"
#include "ui_PageBlocSocialWidget.h"

#include "website/pages/blocs/PageBlocSocial.h"

#include <QPushButton>

PageBlocSocialWidget::PageBlocSocialWidget(QWidget *parent)
    : AbstractPageBlockWidget(parent)
    , ui(new Ui::PageBlocSocialWidget)
{
    ui->setupUi(this);

    // "Apply All": copy the Facebook title and description to every other platform.
    // The user fills in one set of copy, then refines per-platform as needed.
    connect(ui->btnApplyAll, &QPushButton::clicked, this, [this]() {
        const QString title = ui->editFacebookTitle->text();
        const QString desc  = ui->editFacebookDesc->toPlainText();
        ui->editTwitterTitle->setText(title);
        ui->editTwitterDesc->setPlainText(desc);
        ui->editPinterestTitle->setText(title);
        ui->editPinterestDesc->setPlainText(desc);
        ui->editLinkedInTitle->setText(title);
        ui->editLinkedInDesc->setPlainText(desc);
    });
}

PageBlocSocialWidget::~PageBlocSocialWidget()
{
    delete ui;
}

void PageBlocSocialWidget::load(const QHash<QString, QString> &values)
{
    ui->editFacebookTitle->setText(values.value(QLatin1String(PageBlocSocial::KEY_FB_TITLE)));
    ui->editFacebookDesc->setPlainText(values.value(QLatin1String(PageBlocSocial::KEY_FB_DESC)));
    ui->editTwitterTitle->setText(values.value(QLatin1String(PageBlocSocial::KEY_TW_TITLE)));
    ui->editTwitterDesc->setPlainText(values.value(QLatin1String(PageBlocSocial::KEY_TW_DESC)));
    ui->editPinterestTitle->setText(values.value(QLatin1String(PageBlocSocial::KEY_PT_TITLE)));
    ui->editPinterestDesc->setPlainText(values.value(QLatin1String(PageBlocSocial::KEY_PT_DESC)));
    ui->editLinkedInTitle->setText(values.value(QLatin1String(PageBlocSocial::KEY_LI_TITLE)));
    ui->editLinkedInDesc->setPlainText(values.value(QLatin1String(PageBlocSocial::KEY_LI_DESC)));
}

void PageBlocSocialWidget::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(PageBlocSocial::KEY_FB_TITLE), ui->editFacebookTitle->text());
    values.insert(QLatin1String(PageBlocSocial::KEY_FB_DESC),  ui->editFacebookDesc->toPlainText());
    values.insert(QLatin1String(PageBlocSocial::KEY_TW_TITLE), ui->editTwitterTitle->text());
    values.insert(QLatin1String(PageBlocSocial::KEY_TW_DESC),  ui->editTwitterDesc->toPlainText());
    values.insert(QLatin1String(PageBlocSocial::KEY_PT_TITLE), ui->editPinterestTitle->text());
    values.insert(QLatin1String(PageBlocSocial::KEY_PT_DESC),  ui->editPinterestDesc->toPlainText());
    values.insert(QLatin1String(PageBlocSocial::KEY_LI_TITLE), ui->editLinkedInTitle->text());
    values.insert(QLatin1String(PageBlocSocial::KEY_LI_DESC),  ui->editLinkedInDesc->toPlainText());
}
