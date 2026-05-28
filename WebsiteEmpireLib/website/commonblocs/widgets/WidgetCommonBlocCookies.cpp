#include "WidgetCommonBlocCookies.h"
#include "ui_WidgetCommonBlocCookies.h"

#include "website/commonblocs/CommonBlocCookies.h"

WidgetCommonBlocCookies::WidgetCommonBlocCookies(QWidget *parent)
    : AbstractCommonBlocWidget(parent)
    , ui(new Ui::WidgetCommonBlocCookies)
{
    ui->setupUi(this);
    connect(ui->lineEditMessage,    &QLineEdit::textEdited, this, &WidgetCommonBlocCookies::changed);
    connect(ui->lineEditButtonText, &QLineEdit::textEdited, this, &WidgetCommonBlocCookies::changed);
    connect(ui->lineEditRejectText, &QLineEdit::textEdited, this, &WidgetCommonBlocCookies::changed);
    connect(ui->lineEditRejectUrl,  &QLineEdit::textEdited, this, &WidgetCommonBlocCookies::changed);
}

WidgetCommonBlocCookies::~WidgetCommonBlocCookies()
{
    delete ui;
}

void WidgetCommonBlocCookies::loadFrom(const AbstractCommonBloc &bloc)
{
    const auto &cookies = static_cast<const CommonBlocCookies &>(bloc);
    ui->lineEditMessage->setText(cookies.message());
    ui->lineEditButtonText->setText(cookies.buttonText());
    ui->lineEditRejectText->setText(cookies.rejectText());
    ui->lineEditRejectUrl->setText(cookies.rejectUrl());
}

void WidgetCommonBlocCookies::saveTo(AbstractCommonBloc &bloc) const
{
    auto &cookies = static_cast<CommonBlocCookies &>(bloc);
    cookies.setMessage(ui->lineEditMessage->text().trimmed());
    cookies.setButtonText(ui->lineEditButtonText->text().trimmed());
    cookies.setRejectText(ui->lineEditRejectText->text().trimmed());
    cookies.setRejectUrl(ui->lineEditRejectUrl->text().trimmed());
}
