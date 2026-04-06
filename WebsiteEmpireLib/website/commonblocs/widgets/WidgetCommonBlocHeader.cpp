#include "WidgetCommonBlocHeader.h"
#include "ui_WidgetCommonBlocHeader.h"

#include "website/commonblocs/CommonBlocHeader.h"

WidgetCommonBlocHeader::WidgetCommonBlocHeader(QWidget *parent)
    : AbstractCommonBlocWidget(parent)
    , ui(new Ui::WidgetCommonBlocHeader)
{
    ui->setupUi(this);
}

WidgetCommonBlocHeader::~WidgetCommonBlocHeader()
{
    delete ui;
}

void WidgetCommonBlocHeader::loadFrom(const AbstractCommonBloc &bloc)
{
    const auto &header = static_cast<const CommonBlocHeader &>(bloc);
    ui->lineEditTitle->setText(header.title());
    ui->lineEditSubtitle->setText(header.subtitle());
}

void WidgetCommonBlocHeader::saveTo(AbstractCommonBloc &bloc) const
{
    auto &header = static_cast<CommonBlocHeader &>(bloc);
    header.setTitle(ui->lineEditTitle->text().trimmed());
    header.setSubtitle(ui->lineEditSubtitle->text().trimmed());
}
