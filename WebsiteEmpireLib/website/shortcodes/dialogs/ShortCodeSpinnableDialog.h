#ifndef SHORTCODESPINNABLEDIALOG_H
#define SHORTCODESPINNABLEDIALOG_H

#include <QDialog>

namespace Ui {
class ShortCodeSpinnableDialog;
}

/**
 * Edit dialog for the [SPINNABLE] shortcode.
 *
 * Provides:
 *   - spinnableId() — non-negative integer seed; maps to the id argument.
 *                     Named spinnableId() to avoid ambiguity with QWidget::id().
 *   - random()      — when true the RNG is securely seeded; maps to random="true"
 */
class ShortCodeSpinnableDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShortCodeSpinnableDialog(QWidget *parent = nullptr);
    ~ShortCodeSpinnableDialog() override;

    int spinnableId() const;
    bool random() const;

private:
    Ui::ShortCodeSpinnableDialog *ui;
};

#endif // SHORTCODESPINNABLEDIALOG_H
