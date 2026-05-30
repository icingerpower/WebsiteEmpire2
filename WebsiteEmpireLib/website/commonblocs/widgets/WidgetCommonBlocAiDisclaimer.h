#ifndef WIDGETCOMMONBLOCAIDISCLAIMER_H
#define WIDGETCOMMONBLOCAIDISCLAIMER_H

#include "website/commonblocs/AbstractCommonBlocWidget.h"

namespace Ui {
class WidgetCommonBlocAiDisclaimer;
}

class WidgetCommonBlocAiDisclaimer : public AbstractCommonBlocWidget
{
    Q_OBJECT
public:
    explicit WidgetCommonBlocAiDisclaimer(QWidget *parent = nullptr);
    ~WidgetCommonBlocAiDisclaimer() override;

    void loadFrom(const AbstractCommonBloc &bloc) override;
    void saveTo(AbstractCommonBloc &bloc) const override;

private:
    Ui::WidgetCommonBlocAiDisclaimer *ui;
};

#endif // WIDGETCOMMONBLOCAIDISCLAIMER_H
