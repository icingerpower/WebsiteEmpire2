#ifndef PAGEBLOCKTEXTWIDGET_H
#define PAGEBLOCKTEXTWIDGET_H

#include "website/pages/AbstractPageBlockWidget.h"

namespace Ui { class PageBlockTextWidget; }

/**
 * Edit widget for a PageBlockText bloc.
 * Presents a QTextEdit in which the user enters the block's plain-text content
 * (paragraphs separated by blank lines, optional shortcodes).
 */
class PageBlockTextWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlockTextWidget(QWidget *parent = nullptr);
    ~PageBlockTextWidget() override;

    void load(const QString &origContent) override;
    void save(QString &contentToUpdate) override;

private:
    Ui::PageBlockTextWidget *ui;
};

#endif // PAGEBLOCKTEXTWIDGET_H
