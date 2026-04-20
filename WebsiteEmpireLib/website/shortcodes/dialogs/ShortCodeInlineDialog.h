#ifndef SHORTCODEINLINEDIALOG_H
#define SHORTCODEINLINEDIALOG_H

#include <QDialog>

namespace Ui { class ShortCodeInlineDialog; }

/**
 * Minimal confirmation dialog for argument-less inline shortcodes (BOLD, ITALIC).
 *
 * Shows a description label (set via the window title or setDescription()) and
 * an OK / Cancel button pair.  No arguments are collected because these shortcodes
 * wrap selected text with no configurable parameters.
 */
class ShortCodeInlineDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShortCodeInlineDialog(QWidget *parent = nullptr);
    ~ShortCodeInlineDialog() override;

private:
    Ui::ShortCodeInlineDialog *ui;
};

#endif // SHORTCODEINLINEDIALOG_H
