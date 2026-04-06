#ifndef WIDGETCOMMONBLOCMENU_H
#define WIDGETCOMMONBLOCMENU_H

#include "website/commonblocs/AbstractCommonBlocWidget.h"
#include "website/commonblocs/MenuItem.h"

class QMenu;
class QTreeWidgetItem;

namespace Ui { class WidgetCommonBlocMenu; }

/**
 * Editor widget shared by CommonBlocMenuTop and CommonBlocMenuBottom.
 *
 * Displays a QTreeWidget with one level of nesting (top-level items and their
 * optional sub-items).  The "Legal Pages ▾" button quickly adds any registered
 * AbstractLegalPageDef as a top-level link so the user does not have to look up
 * permalinks manually.
 *
 * Column layout:
 *   0 — Label    (also stores the newTab bool in Qt::UserRole)
 *   1 — URL
 *   2 — New Tab  ("Yes" or empty)
 *   3 — Rel
 *
 * loadFrom() / saveTo() cast the AbstractCommonBloc reference to
 * AbstractCommonBlocMenu to access setItems() / items().
 */
class WidgetCommonBlocMenu : public AbstractCommonBlocWidget
{
    Q_OBJECT

    enum Column { COL_LABEL = 0, COL_URL = 1, COL_NEWTAB = 2, COL_REL = 3 };

public:
    explicit WidgetCommonBlocMenu(QWidget *parent = nullptr);
    ~WidgetCommonBlocMenu() override;

    void loadFrom(const AbstractCommonBloc &bloc) override;
    void saveTo(AbstractCommonBloc &bloc) const override;

private slots:
    void onAddItem();
    void onAddSubItem();
    void onEditItem();
    void onDeleteItem();
    void onMoveUp();
    void onMoveDown();
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void updateButtonStates();

private:
    void setupLegalPagesMenu();
    void addLegalPageItem(const QString &label, const QString &url);
    void populateTreeItem(QTreeWidgetItem *treeItem, const QString &label,
                          const QString &url, bool newTab, const QString &rel);
    MenuItem extractMenuItem(QTreeWidgetItem *treeItem) const;

    Ui::WidgetCommonBlocMenu *ui;
    QMenu *m_legalMenu = nullptr;
};

#endif // WIDGETCOMMONBLOCMENU_H
