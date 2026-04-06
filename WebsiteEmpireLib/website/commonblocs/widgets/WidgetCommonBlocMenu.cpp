#include "WidgetCommonBlocMenu.h"
#include "ui_WidgetCommonBlocMenu.h"
#include "DialogMenuItemEdit.h"

#include "website/commonblocs/AbstractCommonBlocMenu.h"
#include "website/pages/AbstractLegalPageDef.h"

#include <QHeaderView>
#include <QMenu>
#include <QPushButton>
#include <QTreeWidgetItem>

WidgetCommonBlocMenu::WidgetCommonBlocMenu(QWidget *parent)
    : AbstractCommonBlocWidget(parent)
    , ui(new Ui::WidgetCommonBlocMenu)
{
    ui->setupUi(this);

    ui->treeWidget->header()->setSectionResizeMode(COL_LABEL,  QHeaderView::Stretch);
    ui->treeWidget->header()->setSectionResizeMode(COL_URL,    QHeaderView::Stretch);
    ui->treeWidget->header()->setSectionResizeMode(COL_NEWTAB, QHeaderView::ResizeToContents);
    ui->treeWidget->header()->setSectionResizeMode(COL_REL,    QHeaderView::ResizeToContents);

    connect(ui->btnAdd,      &QPushButton::clicked, this, &WidgetCommonBlocMenu::onAddItem);
    connect(ui->btnAddSub,   &QPushButton::clicked, this, &WidgetCommonBlocMenu::onAddSubItem);
    connect(ui->btnEdit,     &QPushButton::clicked, this, &WidgetCommonBlocMenu::onEditItem);
    connect(ui->btnDelete,   &QPushButton::clicked, this, &WidgetCommonBlocMenu::onDeleteItem);
    connect(ui->btnMoveUp,   &QPushButton::clicked, this, &WidgetCommonBlocMenu::onMoveUp);
    connect(ui->btnMoveDown, &QPushButton::clicked, this, &WidgetCommonBlocMenu::onMoveDown);
    connect(ui->btnLegalPages, &QPushButton::clicked, this, [this]() {
        if (m_legalMenu) {
            m_legalMenu->exec(ui->btnLegalPages->mapToGlobal(
                ui->btnLegalPages->rect().bottomLeft()));
        }
    });
    connect(ui->treeWidget, &QTreeWidget::itemSelectionChanged,
            this, &WidgetCommonBlocMenu::updateButtonStates);
    connect(ui->treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &WidgetCommonBlocMenu::onItemDoubleClicked);

    setupLegalPagesMenu();
    updateButtonStates();
}

WidgetCommonBlocMenu::~WidgetCommonBlocMenu()
{
    delete ui;
}

void WidgetCommonBlocMenu::loadFrom(const AbstractCommonBloc &bloc)
{
    const auto &menu = static_cast<const AbstractCommonBlocMenu &>(bloc);
    ui->treeWidget->clear();
    for (const auto &item : std::as_const(menu.items())) {
        auto *topItem = new QTreeWidgetItem(ui->treeWidget);
        populateTreeItem(topItem, item.label, item.url, item.newTab, item.rel);
        for (const auto &sub : std::as_const(item.children)) {
            auto *subItem = new QTreeWidgetItem(topItem);
            populateTreeItem(subItem, sub.label, sub.url, sub.newTab, sub.rel);
        }
        topItem->setExpanded(true);
    }
}

void WidgetCommonBlocMenu::saveTo(AbstractCommonBloc &bloc) const
{
    auto &menu = static_cast<AbstractCommonBlocMenu &>(bloc);
    QList<MenuItem> items;
    const int topCount = ui->treeWidget->topLevelItemCount();
    for (int i = 0; i < topCount; ++i) {
        QTreeWidgetItem *topItem = ui->treeWidget->topLevelItem(i);
        MenuItem item = extractMenuItem(topItem);
        const int childCount = topItem->childCount();
        for (int j = 0; j < childCount; ++j) {
            QTreeWidgetItem *childItem = topItem->child(j);
            MenuSubItem sub;
            sub.label  = childItem->text(COL_LABEL);
            sub.url    = childItem->text(COL_URL);
            sub.newTab = childItem->data(COL_LABEL, Qt::UserRole).toBool();
            sub.rel    = childItem->text(COL_REL);
            item.children.append(sub);
        }
        items.append(item);
    }
    menu.setItems(items);
}

void WidgetCommonBlocMenu::onAddItem()
{
    DialogMenuItemEdit dlg(this);
    dlg.setWindowTitle(tr("Add Item"));
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const MenuItem item = dlg.item();
    auto *topItem = new QTreeWidgetItem(ui->treeWidget);
    populateTreeItem(topItem, item.label, item.url, item.newTab, item.rel);
    ui->treeWidget->setCurrentItem(topItem);
    updateButtonStates();
}

void WidgetCommonBlocMenu::onAddSubItem()
{
    QTreeWidgetItem *selected = ui->treeWidget->currentItem();
    if (!selected) {
        return;
    }
    // If the selected item is already a sub-item, add to its parent instead
    QTreeWidgetItem *parentItem = selected->parent() ? selected->parent() : selected;
    DialogMenuItemEdit dlg(this);
    dlg.setWindowTitle(tr("Add Sub-item"));
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const MenuItem item = dlg.item();
    auto *subItem = new QTreeWidgetItem(parentItem);
    populateTreeItem(subItem, item.label, item.url, item.newTab, item.rel);
    parentItem->setExpanded(true);
    ui->treeWidget->setCurrentItem(subItem);
}

void WidgetCommonBlocMenu::onEditItem()
{
    QTreeWidgetItem *selected = ui->treeWidget->currentItem();
    if (!selected) {
        return;
    }
    DialogMenuItemEdit dlg(this);
    dlg.setWindowTitle(tr("Edit Item"));
    dlg.setItem(extractMenuItem(selected));
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const MenuItem item = dlg.item();
    populateTreeItem(selected, item.label, item.url, item.newTab, item.rel);
}

void WidgetCommonBlocMenu::onDeleteItem()
{
    QTreeWidgetItem *selected = ui->treeWidget->currentItem();
    if (!selected) {
        return;
    }
    delete selected;
    updateButtonStates();
}

void WidgetCommonBlocMenu::onMoveUp()
{
    QTreeWidgetItem *item = ui->treeWidget->currentItem();
    if (!item) {
        return;
    }
    QTreeWidgetItem *parent = item->parent();
    if (parent) {
        const int idx = parent->indexOfChild(item);
        if (idx <= 0) {
            return;
        }
        parent->takeChild(idx);
        parent->insertChild(idx - 1, item);
    } else {
        const int idx = ui->treeWidget->indexOfTopLevelItem(item);
        if (idx <= 0) {
            return;
        }
        ui->treeWidget->takeTopLevelItem(idx);
        ui->treeWidget->insertTopLevelItem(idx - 1, item);
    }
    ui->treeWidget->setCurrentItem(item);
}

void WidgetCommonBlocMenu::onMoveDown()
{
    QTreeWidgetItem *item = ui->treeWidget->currentItem();
    if (!item) {
        return;
    }
    QTreeWidgetItem *parent = item->parent();
    if (parent) {
        const int idx = parent->indexOfChild(item);
        if (idx >= parent->childCount() - 1) {
            return;
        }
        parent->takeChild(idx);
        parent->insertChild(idx + 1, item);
    } else {
        const int idx = ui->treeWidget->indexOfTopLevelItem(item);
        if (idx >= ui->treeWidget->topLevelItemCount() - 1) {
            return;
        }
        ui->treeWidget->takeTopLevelItem(idx);
        ui->treeWidget->insertTopLevelItem(idx + 1, item);
    }
    ui->treeWidget->setCurrentItem(item);
}

void WidgetCommonBlocMenu::onItemDoubleClicked(QTreeWidgetItem * /*item*/, int /*column*/)
{
    onEditItem();
}

void WidgetCommonBlocMenu::updateButtonStates()
{
    const QTreeWidgetItem *item = ui->treeWidget->currentItem();
    const bool hasItem = item != nullptr;
    ui->btnAddSub->setEnabled(hasItem);
    ui->btnEdit->setEnabled(hasItem);
    ui->btnDelete->setEnabled(hasItem);
    ui->btnMoveUp->setEnabled(hasItem);
    ui->btnMoveDown->setEnabled(hasItem);
}

void WidgetCommonBlocMenu::setupLegalPagesMenu()
{
    const auto &defs = AbstractLegalPageDef::allDefs();
    if (defs.isEmpty()) {
        ui->btnLegalPages->setVisible(false);
        return;
    }
    m_legalMenu = new QMenu(this);
    for (const auto *def : std::as_const(defs)) {
        QAction *action = m_legalMenu->addAction(def->getDisplayName());
        const QString label = def->getDisplayName();
        const QString url   = def->getDefaultPermalink();
        connect(action, &QAction::triggered, this, [this, label, url]() {
            addLegalPageItem(label, url);
        });
    }
}

void WidgetCommonBlocMenu::addLegalPageItem(const QString &label, const QString &url)
{
    auto *topItem = new QTreeWidgetItem(ui->treeWidget);
    populateTreeItem(topItem, label, url, false, QString());
    ui->treeWidget->setCurrentItem(topItem);
    updateButtonStates();
}

void WidgetCommonBlocMenu::populateTreeItem(QTreeWidgetItem *treeItem,
                                            const QString   &label,
                                            const QString   &url,
                                            bool             newTab,
                                            const QString   &rel)
{
    treeItem->setText(COL_LABEL,  label);
    treeItem->setText(COL_URL,    url);
    treeItem->setText(COL_NEWTAB, newTab ? tr("Yes") : QString());
    treeItem->setText(COL_REL,    rel);
    treeItem->setData(COL_LABEL, Qt::UserRole, newTab);
}

MenuItem WidgetCommonBlocMenu::extractMenuItem(QTreeWidgetItem *treeItem) const
{
    MenuItem item;
    item.label  = treeItem->text(COL_LABEL);
    item.url    = treeItem->text(COL_URL);
    item.newTab = treeItem->data(COL_LABEL, Qt::UserRole).toBool();
    item.rel    = treeItem->text(COL_REL);
    return item;
}
