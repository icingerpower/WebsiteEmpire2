#include "DialogPickEngine.h"
#include "ui_DialogPickEngine.h"

#include <QPushButton>

#include "website/AbstractEngine.h"
#include "workingdirectory/WorkingDirectoryManager.h"

DialogPickEngine::DialogPickEngine(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogPickEngine)
{
    ui->setupUi(this);

    for (const AbstractEngine *engine : AbstractEngine::ALL_ENGINES()) {
        QListWidgetItem *item = new QListWidgetItem(engine->getName(), ui->listWidget);
        item->setData(Qt::UserRole, engine->getId());
    }

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    connect(ui->listWidget, &QListWidget::itemSelectionChanged,
            this, &DialogPickEngine::onItemSelectionChanged);
    connect(ui->listWidget, &QListWidget::itemDoubleClicked,
            this, &DialogPickEngine::accept);
    connect(this, &QDialog::accepted,
            this, &DialogPickEngine::onAccepted);
}

DialogPickEngine::~DialogPickEngine()
{
    delete ui;
}

QString DialogPickEngine::settingsKey()
{
    return QStringLiteral("engineId");
}

void DialogPickEngine::onItemSelectionChanged()
{
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(
        !ui->listWidget->selectedItems().isEmpty());
}

void DialogPickEngine::onAccepted()
{
    const QListWidgetItem *item = ui->listWidget->currentItem();
    if (!item) {
        return;
    }
    const auto settings = WorkingDirectoryManager::instance()->settings();
    settings->setValue(settingsKey(), item->data(Qt::UserRole).toString());
}
