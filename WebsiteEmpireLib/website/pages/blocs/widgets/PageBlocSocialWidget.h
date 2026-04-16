#ifndef PAGEBLOC_SOCIAL_WIDGET_H
#define PAGEBLOC_SOCIAL_WIDGET_H

#include "AbstractPageBlockWidget.h"

namespace Ui { class PageBlocSocialWidget; }

/**
 * Edit widget for a PageBlocSocial bloc.
 *
 * Displays title and description fields for four social platforms:
 * Facebook / Open Graph, Twitter / X, Pinterest, and LinkedIn.
 *
 * "Apply All" copies the Facebook title and description to every other
 * platform, letting the user start from a single baseline and refine
 * per-platform copy as needed.
 */
class PageBlocSocialWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocSocialWidget(QWidget *parent = nullptr);
    ~PageBlocSocialWidget() override;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

private:
    Ui::PageBlocSocialWidget *ui;
};

#endif // PAGEBLOC_SOCIAL_WIDGET_H
