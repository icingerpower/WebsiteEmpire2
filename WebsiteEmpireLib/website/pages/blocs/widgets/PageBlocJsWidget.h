#ifndef PAGEBLOCJSWIDGET_H
#define PAGEBLOCJSWIDGET_H

#include "AbstractPageBlockWidget.h"

namespace Ui { class PageBlocJsWidget; }

/**
 * Edit widget for a PageBlocJs bloc.
 * Presents three QPlainTextEdit editors in a QTabWidget (HTML / CSS / JS)
 * so the user can author each raw fragment independently.
 *
 * load() reads KEY_HTML, KEY_CSS, KEY_JS from the hash.
 * save() writes them back.
 */
class PageBlocJsWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocJsWidget(QWidget *parent = nullptr);
    ~PageBlocJsWidget() override;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

private:
    Ui::PageBlocJsWidget *ui;
};

#endif // PAGEBLOCJSWIDGET_H
