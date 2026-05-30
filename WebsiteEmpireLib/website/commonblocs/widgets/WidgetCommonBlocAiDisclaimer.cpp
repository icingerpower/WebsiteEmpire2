#include "WidgetCommonBlocAiDisclaimer.h"
#include "ui_WidgetCommonBlocAiDisclaimer.h"

#include "website/commonblocs/CommonBlocAiDisclaimer.h"

WidgetCommonBlocAiDisclaimer::WidgetCommonBlocAiDisclaimer(QWidget *parent)
    : AbstractCommonBlocWidget(parent)
    , ui(new Ui::WidgetCommonBlocAiDisclaimer)
{
    ui->setupUi(this);
    connect(ui->lineEditText, &QLineEdit::textEdited, this, &WidgetCommonBlocAiDisclaimer::changed);
}

WidgetCommonBlocAiDisclaimer::~WidgetCommonBlocAiDisclaimer()
{
    delete ui;
}

void WidgetCommonBlocAiDisclaimer::loadFrom(const AbstractCommonBloc &bloc)
{
    const auto &disclaimer = static_cast<const CommonBlocAiDisclaimer &>(bloc);
    ui->lineEditText->setText(disclaimer.text());
}

void WidgetCommonBlocAiDisclaimer::saveTo(AbstractCommonBloc &bloc) const
{
    auto &disclaimer = static_cast<CommonBlocAiDisclaimer &>(bloc);
    disclaimer.setText(ui->lineEditText->text().trimmed());
}
