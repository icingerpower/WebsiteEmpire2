#ifndef CATEGORYEDITORWIDGET_H
#define CATEGORYEDITORWIDGET_H

#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"

class CategoryTable;
class CategoryTreeModel;

namespace Ui { class CategoryEditorWidget; }

/**
 * Widget for displaying and editing the application's category hierarchy.
 *
 * The QTreeView is always backed by a CategoryTreeModel.  Two usage modes:
 *
 *   allowSelection = true  (default, used by PageBlocCategory):
 *     Items are user-checkable. load() restores checked ids from a
 *     comma-separated content string; save() serialises them back.
 *     The Add / Remove buttons let the user grow the vocabulary on the fly.
 *
 *   allowSelection = false (used by standalone "Manage Categories" screens):
 *     No checkboxes.  load() and save() are no-ops.
 *     Add / Remove buttons are the primary interaction.
 *
 * In both modes the user can rename a category by double-clicking its row.
 */
class CategoryEditorWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit CategoryEditorWidget(CategoryTable &table,
                                  bool           allowSelection = true,
                                  QWidget       *parent         = nullptr);
    ~CategoryEditorWidget() override;

    // AbstractPageBlockWidget interface.
    // load() parses a comma-separated list of integer category ids and checks
    // the corresponding items.  save() serialises the checked ids back to the
    // same format.  Both are no-ops when allowSelection is false.
    void load(const QString &origContent) override;
    void save(QString &contentToUpdate)   override;

private slots:
    void _onAddClicked();
    void _onRemoveClicked();
    void _onCategoryAdded(int id);

private:
    Ui::CategoryEditorWidget *ui;
    CategoryTable            &m_table;
    CategoryTreeModel        *m_model;
    bool                      m_allowSelection;
};

#endif // CATEGORYEDITORWIDGET_H
