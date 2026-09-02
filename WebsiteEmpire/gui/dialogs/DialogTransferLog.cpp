#include "DialogTransferLog.h"
#include "ui_DialogTransferLog.h"

#include <QCloseEvent>
#include <QPushButton>

DialogTransferLog::DialogTransferLog(const QString &title, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogTransferLog)
{
    ui->setupUi(this);
    setWindowTitle(title);
    ui->buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &DialogTransferLog::reject);
}

DialogTransferLog::~DialogTransferLog()
{
    delete ui;
}

void DialogTransferLog::setTotalSteps(int total)
{
    ui->progressBar->setRange(0, total);
    ui->progressBar->setValue(0);
}

void DialogTransferLog::advanceStep()
{
    ui->progressBar->setValue(ui->progressBar->value() + 1);
}

void DialogTransferLog::appendLine(const QString &line)
{
    ui->plainTextEditLog->appendPlainText(line);
}

void DialogTransferLog::markFinished(bool success)
{
    m_finished = true;
    ui->progressBar->setValue(ui->progressBar->maximum());
    appendLine(success ? tr("— Finished —") : tr("— Finished with errors —"));
    ui->buttonBox->button(QDialogButtonBox::Close)->setEnabled(true);
}

void DialogTransferLog::reject()
{
    if (!m_finished) {
        return;
    }
    QDialog::reject();
}

void DialogTransferLog::closeEvent(QCloseEvent *event)
{
    if (!m_finished) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}
