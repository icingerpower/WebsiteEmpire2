#include "DialogViewAttributes.h"
#include "ui_DialogViewAttributes.h"

#include <QHeaderView>

#include "aspire/attributes/AbstractPageAttributes.h"

DialogViewAttributes::DialogViewAttributes(const AbstractPageAttributes *pageAttributes,
                                           QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogViewAttributes)
{
    ui->setupUi(this);

    setWindowTitle(tr("Attributes — %1").arg(pageAttributes->getName()));

    // AbstractPageAttributes is a QAbstractTableModel; the cast is safe here
    // because the view is set to NoEditTriggers (read-only).
    ui->tableViewAttributes->setModel(
        const_cast<AbstractPageAttributes *>(pageAttributes));

    ui->tableViewAttributes->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    ui->tableViewAttributes->horizontalHeader()->setStretchLastSection(true);
    ui->tableViewAttributes->verticalHeader()->hide();
    ui->tableViewAttributes->resizeRowsToContents();
}

DialogViewAttributes::~DialogViewAttributes()
{
    delete ui;
}
