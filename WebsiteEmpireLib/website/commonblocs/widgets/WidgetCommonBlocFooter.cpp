#include "WidgetCommonBlocFooter.h"
#include "ui_WidgetCommonBlocFooter.h"

#include "website/commonblocs/CommonBlocFooter.h"

WidgetCommonBlocFooter::WidgetCommonBlocFooter(QWidget *parent)
    : AbstractCommonBlocWidget(parent)
    , ui(new Ui::WidgetCommonBlocFooter)
{
    ui->setupUi(this);
    connect(ui->lineEditText, &QLineEdit::textEdited, this, &WidgetCommonBlocFooter::changed);
}

WidgetCommonBlocFooter::~WidgetCommonBlocFooter()
{
    delete ui;
}

void WidgetCommonBlocFooter::loadFrom(const AbstractCommonBloc &bloc)
{
    const auto &footer = static_cast<const CommonBlocFooter &>(bloc);
    ui->lineEditText->setText(footer.text());
}

void WidgetCommonBlocFooter::saveTo(AbstractCommonBloc &bloc) const
{
    auto &footer = static_cast<CommonBlocFooter &>(bloc);
    footer.setText(ui->lineEditText->text().trimmed());
}
