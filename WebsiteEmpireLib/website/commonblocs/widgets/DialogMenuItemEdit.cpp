#include "DialogMenuItemEdit.h"
#include "ui_DialogMenuItemEdit.h"

#include "DialogPickPage.h"

#include <QToolButton>

DialogMenuItemEdit::DialogMenuItemEdit(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogMenuItemEdit)
{
    ui->setupUi(this);
    setupRelCombo();
    connect(ui->btnPickPage, &QToolButton::clicked, this, &DialogMenuItemEdit::onPickPage);
}

DialogMenuItemEdit::~DialogMenuItemEdit()
{
    delete ui;
}

void DialogMenuItemEdit::setItem(const MenuItem &item)
{
    ui->lineEditLabel->setText(item.label);
    ui->lineEditUrl->setText(item.url);
    ui->checkNewTab->setChecked(item.newTab);
    ui->checkImportant->setChecked(item.important);

    const int idx = ui->comboRel->findText(item.rel);
    if (idx >= 0) {
        ui->comboRel->setCurrentIndex(idx);
    } else {
        ui->comboRel->setCurrentText(item.rel);
    }
}

MenuItem DialogMenuItemEdit::item() const
{
    MenuItem result;
    result.label     = ui->lineEditLabel->text().trimmed();
    result.url       = ui->lineEditUrl->text().trimmed();
    result.rel       = ui->comboRel->currentText().trimmed();
    result.newTab    = ui->checkNewTab->isChecked();
    result.important = ui->checkImportant->isChecked();
    return result;
}

void DialogMenuItemEdit::onPickPage()
{
    DialogPickPage dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QString &permalink = dlg.selectedPermalink();
    if (!permalink.isEmpty()) {
        ui->lineEditUrl->setText(permalink);
    }
}

void DialogMenuItemEdit::setupRelCombo()
{
    ui->comboRel->addItem(QString());                                      // (none)
    ui->comboRel->addItem(QStringLiteral("nofollow"));
    ui->comboRel->addItem(QStringLiteral("noopener"));
    ui->comboRel->addItem(QStringLiteral("noreferrer"));
    ui->comboRel->addItem(QStringLiteral("noopener noreferrer"));
    ui->comboRel->addItem(QStringLiteral("nofollow noopener noreferrer"));
    ui->comboRel->setCurrentIndex(0);
}
