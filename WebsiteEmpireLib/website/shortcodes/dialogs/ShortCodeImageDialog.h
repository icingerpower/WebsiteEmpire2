#ifndef SHORTCODEIMAGEDIALOG_H
#define SHORTCODEIMAGEDIALOG_H

#include <QDialog>

namespace Ui {
class ShortCodeImageDialog;
}

/**
 * Edit dialog shared by [IMGFIX] and [IMGTR] shortcodes.
 *
 * The window title is set by the caller (ShortCodeImageFix or ShortCodeImageTr)
 * to distinguish the two uses:
 *   - "Insert Fixed Image"        (IMGFIX)
 *   - "Insert Translatable Image" (IMGTR)
 *
 * imageWidth() and imageHeight() return 0 when the spinbox is left at its
 * default; AbstractShortCodeImage::getTextBegin() treats 0 as "omit attribute".
 *
 * The methods are named imageWidth()/imageHeight() instead of width()/height()
 * to avoid shadowing QWidget::width() and QWidget::height().
 */
class ShortCodeImageDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShortCodeImageDialog(const QString &title, QWidget *parent = nullptr);
    ~ShortCodeImageDialog() override;

    QString id() const;
    QString fileName() const;
    QString alt() const;
    int imageWidth() const;   ///< 0 means not set — width attribute will be omitted
    int imageHeight() const;  ///< 0 means not set — height attribute will be omitted

private:
    Ui::ShortCodeImageDialog *ui;
};

#endif // SHORTCODEIMAGEDIALOG_H
