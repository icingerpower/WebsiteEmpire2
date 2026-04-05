#include "DialogGenerateLegalPages.h"
#include "ui_DialogGenerateLegalPages.h"

#include "website/pages/AbstractLegalPageDef.h"

#include <QTableWidgetItem>

DialogGenerateLegalPages::DialogGenerateLegalPages(const QSet<QString> &existingDefIds,
                                                    QWidget             *parent)
    : QDialog(parent)
    , ui(new Ui::DialogGenerateLegalPages)
{
    ui->setupUi(this);

    const QList<AbstractLegalPageDef *> &defs = AbstractLegalPageDef::allDefs();
    ui->tableWidget->setRowCount(defs.size());
    ui->tableWidget->setColumnCount(3);
    ui->tableWidget->setHorizontalHeaderLabels(
        {tr("Generate"), tr("Page"), tr("Status")});

    for (int row = 0; row < defs.size(); ++row) {
        const AbstractLegalPageDef *def = defs.at(row);
        const bool exists = existingDefIds.contains(def->getId());

        // Column 0: checkbox — pre-checked for missing pages, unchecked for existing.
        auto *checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkItem->setCheckState(exists ? Qt::Unchecked : Qt::Checked);
        ui->tableWidget->setItem(row, 0, checkItem);

        // Column 1: page display name; stable def ID stored in UserRole for retrieval.
        auto *nameItem = new QTableWidgetItem(def->getDisplayName());
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        nameItem->setData(Qt::UserRole, def->getId());
        ui->tableWidget->setItem(row, 1, nameItem);

        // Column 2: existence status.
        auto *statusItem = new QTableWidgetItem(exists ? tr("Exists") : tr("Missing"));
        statusItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        ui->tableWidget->setItem(row, 2, statusItem);
    }

    ui->tableWidget->resizeColumnsToContents();

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

DialogGenerateLegalPages::~DialogGenerateLegalPages()
{
    delete ui;
}

QList<QString> DialogGenerateLegalPages::selectedDefIds() const
{
    QList<QString> ids;
    const int rows = ui->tableWidget->rowCount();
    for (int row = 0; row < rows; ++row) {
        const QTableWidgetItem *checkItem = ui->tableWidget->item(row, 0);
        if (checkItem && checkItem->checkState() == Qt::Checked) {
            const QTableWidgetItem *nameItem = ui->tableWidget->item(row, 1);
            if (nameItem) {
                ids.append(nameItem->data(Qt::UserRole).toString());
            }
        }
    }
    return ids;
}
