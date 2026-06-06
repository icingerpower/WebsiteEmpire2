#include "DialogPickArticlesToUpdate.h"
#include "ui_DialogPickArticlesToUpdate.h"

#include "website/pages/PageRecord.h"

#include <algorithm>

#include <QHeaderView>
#include <QPushButton>
#include <QTreeWidgetItem>

DialogPickArticlesToUpdate::DialogPickArticlesToUpdate(const QList<PageRecord> &pages,
                                                       QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogPickArticlesToUpdate)
{
    ui->setupUi(this);

    // ID column narrow, Permalink column stretches.
    ui->treeWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->treeWidget->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->treeWidget->setRootIsDecorated(false);

    // Sort pages by ID ascending (same order as PanePages) before inserting.
    QList<PageRecord> sorted = pages;
    std::sort(sorted.begin(), sorted.end(),
              [](const PageRecord &a, const PageRecord &b) { return a.id < b.id; });

    for (const PageRecord &page : std::as_const(sorted)) {
        auto *item = new QTreeWidgetItem(ui->treeWidget);
        item->setText(0, QString::number(page.id));
        item->setText(1, page.permalink);
        item->setData(0, Qt::UserRole, page.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Unchecked);
    }

    connect(ui->buttonCheckAll,   &QPushButton::clicked,
            this, &DialogPickArticlesToUpdate::_checkAll);
    connect(ui->buttonUncheckAll, &QPushButton::clicked,
            this, &DialogPickArticlesToUpdate::_uncheckAll);
    connect(ui->lineEditFilter,   &QLineEdit::textChanged,
            this, &DialogPickArticlesToUpdate::_applyFilter);
}

DialogPickArticlesToUpdate::~DialogPickArticlesToUpdate()
{
    delete ui;
}

QList<int> DialogPickArticlesToUpdate::selectedPageIds() const
{
    QList<int> result;
    const int count = ui->treeWidget->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        const auto *item = ui->treeWidget->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked) {
            result.append(item->data(0, Qt::UserRole).toInt());
        }
    }
    return result;
}

void DialogPickArticlesToUpdate::_checkAll()
{
    const int count = ui->treeWidget->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        auto *item = ui->treeWidget->topLevelItem(i);
        if (!item->isHidden()) {
            item->setCheckState(0, Qt::Checked);
        }
    }
}

void DialogPickArticlesToUpdate::_uncheckAll()
{
    const int count = ui->treeWidget->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        auto *item = ui->treeWidget->topLevelItem(i);
        if (!item->isHidden()) {
            item->setCheckState(0, Qt::Unchecked);
        }
    }
}

void DialogPickArticlesToUpdate::_applyFilter(const QString &text)
{
    const int count = ui->treeWidget->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        auto *item = ui->treeWidget->topLevelItem(i);
        const bool hide = !text.isEmpty()
                          && !item->text(1).contains(text, Qt::CaseInsensitive);
        item->setHidden(hide);
    }
}
