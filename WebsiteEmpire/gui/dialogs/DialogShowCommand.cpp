#include "DialogShowCommand.h"
#include "ui_DialogShowCommand.h"

#include <QFont>

DialogShowCommand::DialogShowCommand(const QString &title,
                                     const QString &description,
                                     const QString &command,
                                     QWidget       *parent)
    : QDialog(parent)
    , ui(new Ui::DialogShowCommand)
{
    ui->setupUi(this);
    setWindowTitle(title);
    ui->labelDescription->setText(description);

    QFont mono = ui->plainTextCommand->font();
    mono.setFamily(QStringLiteral("monospace"));
    ui->plainTextCommand->setFont(mono);
    ui->plainTextCommand->setPlainText(command);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
}

DialogShowCommand::~DialogShowCommand()
{
    delete ui;
}
