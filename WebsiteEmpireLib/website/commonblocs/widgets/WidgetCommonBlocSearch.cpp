#include "WidgetCommonBlocSearch.h"
#include "ui_WidgetCommonBlocSearch.h"

#include "website/commonblocs/CommonBlocSearch.h"

WidgetCommonBlocSearch::WidgetCommonBlocSearch(QWidget *parent)
    : AbstractCommonBlocWidget(parent)
    , ui(new Ui::WidgetCommonBlocSearch)
{
    ui->setupUi(this);
    connect(ui->lineEditAction,      &QLineEdit::textEdited, this, &WidgetCommonBlocSearch::changed);
    connect(ui->lineEditPlaceholder, &QLineEdit::textEdited, this, &WidgetCommonBlocSearch::changed);
}

WidgetCommonBlocSearch::~WidgetCommonBlocSearch()
{
    delete ui;
}

void WidgetCommonBlocSearch::loadFrom(const AbstractCommonBloc &bloc)
{
    const auto &search = static_cast<const CommonBlocSearch &>(bloc);
    ui->lineEditAction->setText(search.action());
    ui->lineEditPlaceholder->setText(search.placeholder());
}

void WidgetCommonBlocSearch::saveTo(AbstractCommonBloc &bloc) const
{
    auto &search = static_cast<CommonBlocSearch &>(bloc);
    search.setAction(ui->lineEditAction->text().trimmed());
    search.setPlaceholder(ui->lineEditPlaceholder->text().trimmed());
}
