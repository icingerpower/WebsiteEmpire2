#include "DialogPickArticlesToReset.h"
#include "ui_DialogPickArticlesToReset.h"

#include "website/pages/PageRecord.h"

#include <QPushButton>
#include <QListWidgetItem>

DialogPickArticlesToReset::DialogPickArticlesToReset(const QList<PageRecord> &pages,
                                                     QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogPickArticlesToReset)
{
    ui->setupUi(this);

    m_pageIds.reserve(pages.size());
    for (const PageRecord &page : std::as_const(pages)) {
        m_pageIds.append(page.id);
        auto *item = new QListWidgetItem(page.permalink, ui->listWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }

    connect(ui->buttonCheckAll,   &QPushButton::clicked,
            this, &DialogPickArticlesToReset::_checkAll);
    connect(ui->buttonUncheckAll, &QPushButton::clicked,
            this, &DialogPickArticlesToReset::_uncheckAll);
}

DialogPickArticlesToReset::~DialogPickArticlesToReset()
{
    delete ui;
}

QList<int> DialogPickArticlesToReset::selectedPageIds() const
{
    QList<int> result;
    const int count = ui->listWidget->count();
    for (int i = 0; i < count; ++i) {
        if (ui->listWidget->item(i)->checkState() == Qt::Checked) {
            result.append(m_pageIds.at(i));
        }
    }
    return result;
}

void DialogPickArticlesToReset::_checkAll()
{
    const int count = ui->listWidget->count();
    for (int i = 0; i < count; ++i) {
        ui->listWidget->item(i)->setCheckState(Qt::Checked);
    }
}

void DialogPickArticlesToReset::_uncheckAll()
{
    const int count = ui->listWidget->count();
    for (int i = 0; i < count; ++i) {
        ui->listWidget->item(i)->setCheckState(Qt::Unchecked);
    }
}
