#include "DialogAddUpdateStrategy.h"
#include "ui_DialogAddUpdateStrategy.h"

#include "website/pages/AbstractPageType.h"
#include "website/theme/AbstractTheme.h"

#include <QDialogButtonBox>
#include <QPushButton>

DialogAddUpdateStrategy::DialogAddUpdateStrategy(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogAddUpdateStrategy)
{
    ui->setupUi(this);

    // ---- Page types ---------------------------------------------------------
    for (const QString &typeId : AbstractPageType::allTypeIds()) {
        ui->comboBoxPageType->addItem(typeId, typeId);
    }

    // ---- Themes -------------------------------------------------------------
    const auto &themes = AbstractTheme::ALL_THEMES();
    if (themes.size() <= 1) {
        ui->labelTheme->setVisible(false);
        ui->comboBoxTheme->setVisible(false);
    } else {
        ui->comboBoxTheme->addItem(tr("All themes"), QString{});
        for (auto it = themes.cbegin(); it != themes.cend(); ++it) {
            ui->comboBoxTheme->addItem(it.value()->getName(), it.key());
        }
    }

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    connect(ui->lineEditName, &QLineEdit::textChanged,
            this, &DialogAddUpdateStrategy::_onNameChanged);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

DialogAddUpdateStrategy::~DialogAddUpdateStrategy()
{
    delete ui;
}

QString DialogAddUpdateStrategy::name() const
{
    return ui->lineEditName->text().trimmed();
}

QString DialogAddUpdateStrategy::pageTypeId() const
{
    return ui->comboBoxPageType->currentData().toString();
}

QString DialogAddUpdateStrategy::themeId() const
{
    if (!ui->comboBoxTheme->isVisible()) {
        return {};
    }
    return ui->comboBoxTheme->currentData().toString();
}

void DialogAddUpdateStrategy::_onNameChanged(const QString &text)
{
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
}
