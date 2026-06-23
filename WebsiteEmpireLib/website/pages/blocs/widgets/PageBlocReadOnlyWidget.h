#ifndef PAGEBLOCREADONLYWIDGET_H
#define PAGEBLOCREADONLYWIDGET_H

#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"

/**
 * A read-only informational widget for page blocs that have no editable content.
 *
 * Displays a description string explaining where the bloc's data comes from.
 * load() and save() are no-ops — this widget carries no state.
 */
class PageBlocReadOnlyWidget : public AbstractPageBlockWidget
{
public:
    explicit PageBlocReadOnlyWidget(const QString &description, QWidget *parent = nullptr);
    ~PageBlocReadOnlyWidget() override = default;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;
};

#endif // PAGEBLOCREADONLYWIDGET_H
