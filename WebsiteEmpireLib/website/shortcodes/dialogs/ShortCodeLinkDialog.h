#ifndef SHORTCODELINKDIALOG_H
#define SHORTCODELINKDIALOG_H

#include <QDialog>

namespace Ui {
class ShortCodeLinkDialog;
}

/**
 * Edit dialog shared by [LINKFIX] and [LINKTR] shortcodes.
 *
 * The window title is set by the caller (ShortCodeLinkFix or ShortCodeLinkTr)
 * to distinguish the two uses:
 *   - "Insert Fixed Link"        (LINKFIX)
 *   - "Insert Translatable Link" (LINKTR)
 *
 * The rel combo is populated from AbstractShortCodeLink::relValues(); the first
 * entry (DEFAULT_REL = "dofollow") is pre-selected.
 */
class ShortCodeLinkDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShortCodeLinkDialog(const QString &title, QWidget *parent = nullptr);
    ~ShortCodeLinkDialog() override;

    QString url() const;
    QString rel() const;

private:
    Ui::ShortCodeLinkDialog *ui;
};

#endif // SHORTCODELINKDIALOG_H
