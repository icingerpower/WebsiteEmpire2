#ifndef SHORTCODETITLEDIALOG_H
#define SHORTCODETITLEDIALOG_H

#include <QDialog>

namespace Ui {
class ShortCodeTitleDialog;
}

/**
 * Edit dialog for the [TITLE] shortcode.
 *
 * Provides a level() field (1–6) from which ShortCodeTitle::getTextBegin()
 * builds the opening shortcode tag.
 */
class ShortCodeTitleDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShortCodeTitleDialog(QWidget *parent = nullptr);
    ~ShortCodeTitleDialog() override;

    int level() const;  ///< returns 1–6 matching the selected combobox entry

private:
    Ui::ShortCodeTitleDialog *ui;
};

#endif // SHORTCODETITLEDIALOG_H
