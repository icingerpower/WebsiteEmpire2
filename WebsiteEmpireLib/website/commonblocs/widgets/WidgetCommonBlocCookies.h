#ifndef WIDGETCOMMONBLOCCOOKIES_H
#define WIDGETCOMMONBLOCCOOKIES_H

#include "website/commonblocs/AbstractCommonBlocWidget.h"

namespace Ui { class WidgetCommonBlocCookies; }

/**
 * Editor widget for CommonBlocCookies.
 *
 * Presents two text fields:
 *   - Message    — the consent notice shown to visitors
 *   - Button text — the label on the Accept button
 *
 * loadFrom() and saveTo() downcast the AbstractCommonBloc reference to
 * CommonBlocCookies to access the typed setters and getters.
 */
class WidgetCommonBlocCookies : public AbstractCommonBlocWidget
{
    Q_OBJECT

public:
    explicit WidgetCommonBlocCookies(QWidget *parent = nullptr);
    ~WidgetCommonBlocCookies() override;

    void loadFrom(const AbstractCommonBloc &bloc) override;
    void saveTo(AbstractCommonBloc &bloc) const override;

private:
    Ui::WidgetCommonBlocCookies *ui;
};

#endif // WIDGETCOMMONBLOCCOOKIES_H
