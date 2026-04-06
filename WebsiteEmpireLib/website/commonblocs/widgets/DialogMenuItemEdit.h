#ifndef DIALOGMENUITEMEDIT_H
#define DIALOGMENUITEMEDIT_H

#include <QDialog>

#include "website/commonblocs/MenuItem.h"

namespace Ui { class DialogMenuItemEdit; }

/**
 * Modal dialog for adding or editing a single menu item (top-level or sub-item).
 *
 * Both MenuItem and MenuSubItem share the same fields (label, url, rel, newTab),
 * so one dialog covers both cases.  The caller sets the window title to
 * distinguish "Add Item", "Add Sub-item", and "Edit Item".
 *
 * Usage:
 *   DialogMenuItemEdit dlg(this);
 *   dlg.setWindowTitle(tr("Add Item"));
 *   dlg.setItem(existing);             // optional — pre-fill for editing
 *   if (dlg.exec() == QDialog::Accepted) {
 *       const MenuItem item = dlg.item();
 *   }
 *
 * The rel combo is editable — common values are pre-populated but the user can
 * type any rel token string (e.g. "nofollow sponsored").
 */
class DialogMenuItemEdit : public QDialog
{
    Q_OBJECT

public:
    explicit DialogMenuItemEdit(QWidget *parent = nullptr);
    ~DialogMenuItemEdit() override;

    /** Pre-fills all fields from an existing item (use when editing). */
    void setItem(const MenuItem &item);

    /**
     * Returns the current field values as a MenuItem.
     * The children list is always empty — sub-item children are not editable here.
     */
    MenuItem item() const;

private:
    void setupRelCombo();

    Ui::DialogMenuItemEdit *ui;
};

#endif // DIALOGMENUITEMEDIT_H
