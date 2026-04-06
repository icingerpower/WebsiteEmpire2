#ifndef WIDGETCOMMONBLOCFOOTER_H
#define WIDGETCOMMONBLOCFOOTER_H

#include "website/commonblocs/AbstractCommonBlocWidget.h"

namespace Ui { class WidgetCommonBlocFooter; }

/**
 * Editor widget for CommonBlocFooter.
 *
 * Presents a single text field for the footer line (e.g. a copyright notice).
 *
 * loadFrom() and saveTo() downcast the AbstractCommonBloc reference to
 * CommonBlocFooter to access the typed setter and getter.
 */
class WidgetCommonBlocFooter : public AbstractCommonBlocWidget
{
    Q_OBJECT

public:
    explicit WidgetCommonBlocFooter(QWidget *parent = nullptr);
    ~WidgetCommonBlocFooter() override;

    void loadFrom(const AbstractCommonBloc &bloc) override;
    void saveTo(AbstractCommonBloc &bloc) const override;

private:
    Ui::WidgetCommonBlocFooter *ui;
};

#endif // WIDGETCOMMONBLOCFOOTER_H
