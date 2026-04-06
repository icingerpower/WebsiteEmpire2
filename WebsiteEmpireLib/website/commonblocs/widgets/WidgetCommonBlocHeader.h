#ifndef WIDGETCOMMONBLOCHEADER_H
#define WIDGETCOMMONBLOCHEADER_H

#include "website/commonblocs/AbstractCommonBlocWidget.h"

namespace Ui { class WidgetCommonBlocHeader; }

/**
 * Editor widget for CommonBlocHeader.
 *
 * Presents two fields:
 *   - Title (required) — rendered as the site's <h1>
 *   - Subtitle (optional) — rendered as a <p> below the title
 *
 * loadFrom() and saveTo() downcast the AbstractCommonBloc reference to
 * CommonBlocHeader to access the typed setters and getters.
 */
class WidgetCommonBlocHeader : public AbstractCommonBlocWidget
{
    Q_OBJECT

public:
    explicit WidgetCommonBlocHeader(QWidget *parent = nullptr);
    ~WidgetCommonBlocHeader() override;

    void loadFrom(const AbstractCommonBloc &bloc) override;
    void saveTo(AbstractCommonBloc &bloc) const override;

private:
    Ui::WidgetCommonBlocHeader *ui;
};

#endif // WIDGETCOMMONBLOCHEADER_H
