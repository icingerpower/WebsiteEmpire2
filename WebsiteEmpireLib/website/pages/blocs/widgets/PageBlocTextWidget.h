#ifndef PAGEBLOCTEXTWIDGET_H
#define PAGEBLOCTEXTWIDGET_H

#include "AbstractPageBlockWidget.h"

namespace Ui { class PageBlocTextWidget; }

/**
 * Edit widget for a PageBlocText bloc.
 * Presents a QTextEdit in which the user enters the bloc's plain-text content
 * (paragraphs separated by blank lines, optional shortcodes).
 *
 * load() reads KEY_TEXT from the hash; save() writes it back.
 */
class PageBlocTextWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocTextWidget(QWidget *parent = nullptr);
    ~PageBlocTextWidget() override;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

private:
    Ui::PageBlocTextWidget *ui;
};

#endif // PAGEBLOCTEXTWIDGET_H
