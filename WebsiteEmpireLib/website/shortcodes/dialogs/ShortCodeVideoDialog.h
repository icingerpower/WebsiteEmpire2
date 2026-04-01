#ifndef SHORTCODEVIDEODIALOG_H
#define SHORTCODEVIDEODIALOG_H

#include <QDialog>

namespace Ui {
class ShortCodeVideoDialog;
}

/**
 * Edit dialog for the [VIDEO] shortcode.
 *
 * Provides a single url() field from which ShortCodeVideo::getTextBegin()
 * builds the opening shortcode tag.
 */
class ShortCodeVideoDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShortCodeVideoDialog(QWidget *parent = nullptr);
    ~ShortCodeVideoDialog() override;

    QString url() const;

private:
    Ui::ShortCodeVideoDialog *ui;
};

#endif // SHORTCODEVIDEODIALOG_H
