#include "ShortCodeLinkDialog.h"
#include "ui_ShortCodeLinkDialog.h"

#include "website/shortcodes/AbstractShortCodeLink.h"

ShortCodeLinkDialog::ShortCodeLinkDialog(const QString &title, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ShortCodeLinkDialog)
{
    ui->setupUi(this);
    setWindowTitle(title);

    // Populate rel combo from the single canonical list in AbstractShortCodeLink.
    ui->comboRel->addItems(AbstractShortCodeLink::relValues());
    // DEFAULT_REL is always the first entry, so index 0 is already selected.
}

ShortCodeLinkDialog::~ShortCodeLinkDialog()
{
    delete ui;
}

QString ShortCodeLinkDialog::url() const
{
    return ui->lineEditUrl->text();
}

QString ShortCodeLinkDialog::rel() const
{
    return ui->comboRel->currentText();
}
