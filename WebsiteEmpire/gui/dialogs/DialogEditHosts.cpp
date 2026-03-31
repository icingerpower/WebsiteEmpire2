#include "DialogEditHosts.h"
#include "ui_DialogEditHosts.h"

#include "website/HostTable.h"

#include <QDialogButtonBox>
#include <QPushButton>

DialogEditHosts::DialogEditHosts(HostTable *hostTable, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogEditHosts)
    , m_hostTable(hostTable)
{
    ui->setupUi(this);
    ui->tableViewHosts->setModel(m_hostTable);

    connect(ui->buttonAddRow,    &QPushButton::clicked, this, &DialogEditHosts::addRow);
    connect(ui->buttonRemoveRow, &QPushButton::clicked, this, &DialogEditHosts::removeRow);
    connect(ui->buttonBox,       &QDialogButtonBox::rejected, this, &QDialog::accept);
}

DialogEditHosts::~DialogEditHosts()
{
    delete ui;
}

void DialogEditHosts::addRow()
{
    m_hostTable->addRow();
    const int lastRow = m_hostTable->rowCount() - 1;
    const QModelIndex nameIndex = m_hostTable->index(lastRow, HostTable::COL_NAME);
    ui->tableViewHosts->setCurrentIndex(nameIndex);
    ui->tableViewHosts->edit(nameIndex);
}

void DialogEditHosts::removeRow()
{
    const QModelIndex current = ui->tableViewHosts->currentIndex();
    if (!current.isValid()) {
        return;
    }
    m_hostTable->removeRows(current.row(), 1);
}
