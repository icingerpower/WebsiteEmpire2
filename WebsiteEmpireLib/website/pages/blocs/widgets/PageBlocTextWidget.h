#ifndef PAGEBLOCTEXTWIDGET_H
#define PAGEBLOCTEXTWIDGET_H

#include "AbstractPageBlockWidget.h"

namespace Ui { class PageBlocTextWidget; }

/**
 * Edit widget for a PageBlocText bloc.
 * Presents a QTextEdit in which the user enters the bloc's plain-text content
 * (paragraphs separated by blank lines, optional shortcodes).
 */
class PageBlocTextWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocTextWidget(QWidget *parent = nullptr);
    ~PageBlocTextWidget() override;

    void load(const QString &origContent) override;
    void save(QString &contentToUpdate) override;

private:
    Ui::PageBlocTextWidget *ui;
};

#endif // PAGEBLOCTEXTWIDGET_H
