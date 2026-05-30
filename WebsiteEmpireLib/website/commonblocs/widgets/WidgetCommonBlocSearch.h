#ifndef WIDGETCOMMONBLOCSEARCH_H
#define WIDGETCOMMONBLOCSEARCH_H

#include "website/commonblocs/AbstractCommonBlocWidget.h"

namespace Ui {
class WidgetCommonBlocSearch;
}

class WidgetCommonBlocSearch : public AbstractCommonBlocWidget
{
    Q_OBJECT
public:
    explicit WidgetCommonBlocSearch(QWidget *parent = nullptr);
    ~WidgetCommonBlocSearch() override;

    void loadFrom(const AbstractCommonBloc &bloc) override;
    void saveTo(AbstractCommonBloc &bloc) const override;

private:
    Ui::WidgetCommonBlocSearch *ui;
};

#endif // WIDGETCOMMONBLOCSEARCH_H
