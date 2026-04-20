#include "DialogAddGeneration.h"
#include "ui_DialogAddGeneration.h"

#include "aspire/generator/AbstractGenerator.h"
#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/theme/AbstractTheme.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>

DialogAddGeneration::DialogAddGeneration(AbstractEngine *engine, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogAddGeneration)
{
    ui->setupUi(this);

    // ---- Page types ---------------------------------------------------------
    for (const QString &typeId : AbstractPageType::allTypeIds()) {
        ui->comboBoxPageType->addItem(typeId, typeId);
    }

    // ---- Themes -------------------------------------------------------------
    const auto &themes = AbstractTheme::ALL_THEMES();
    if (themes.size() <= 1) {
        // No meaningful choice: hide the row entirely.
        ui->labelTheme->setVisible(false);
        ui->comboBoxTheme->setVisible(false);
    } else {
        ui->comboBoxTheme->addItem(tr("All themes"), QString{});
        for (auto it = themes.cbegin(); it != themes.cend(); ++it) {
            ui->comboBoxTheme->addItem(it.value()->getName(), it.key());
        }
    }

    // ---- Non-SVG images -----------------------------------------------------
    ui->comboBoxNonSvgImages->addItem(tr("No"),  false);
    ui->comboBoxNonSvgImages->addItem(tr("Yes"), true);

    // ---- Source table -------------------------------------------------------
    ui->comboBoxPrimaryTable->addItem(tr("(None)"), QString{});
    for (auto it = AbstractGenerator::ALL_GENERATORS().constBegin();
         it != AbstractGenerator::ALL_GENERATORS().constEnd(); ++it) {
        const AbstractGenerator::GeneratorTables tables = it.value()->getTables();
        if (tables.primary.isEmpty()) {
            continue;
        }
        const AbstractGenerator::TableDescriptor &desc = *tables.primary.constBegin();
        const QString displayName = it.value()->getName()
                                  + QStringLiteral(" — ")
                                  + desc.name;
        ui->comboBoxPrimaryTable->addItem(displayName, desc.id);
    }

    // Pre-select based on the engine's linked generator.
    if (engine && !engine->getGeneratorId().isEmpty()) {
        const AbstractGenerator *proto =
            AbstractGenerator::ALL_GENERATORS().value(engine->getGeneratorId(), nullptr);
        if (proto) {
            const AbstractGenerator::GeneratorTables tables = proto->getTables();
            if (!tables.primary.isEmpty()) {
                const QString attrId = tables.primary.constBegin()->id;
                const int idx = ui->comboBoxPrimaryTable->findData(attrId);
                if (idx >= 0) {
                    ui->comboBoxPrimaryTable->setCurrentIndex(idx);
                }
            }
        }
    }

    // ---- OK gating ----------------------------------------------------------
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    connect(ui->lineEditName,
            &QLineEdit::textChanged,
            this,
            &DialogAddGeneration::_onNameChanged);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &DialogAddGeneration::_onAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

DialogAddGeneration::~DialogAddGeneration()
{
    delete ui;
}

QString DialogAddGeneration::name() const
{
    return ui->lineEditName->text().trimmed();
}

QString DialogAddGeneration::pageTypeId() const
{
    return ui->comboBoxPageType->currentData().toString();
}

QString DialogAddGeneration::themeId() const
{
    // When the combo is hidden there is only one theme (or none); return empty
    // to mean "all themes / no filter".
    if (!ui->comboBoxTheme->isVisible()) {
        return {};
    }
    return ui->comboBoxTheme->currentData().toString();
}

QString DialogAddGeneration::primaryAttrId() const
{
    return ui->comboBoxPrimaryTable->currentData().toString();
}

QString DialogAddGeneration::customInstructions() const
{
    return ui->plainTextEditInstructions->toPlainText().trimmed();
}

bool DialogAddGeneration::nonSvgImages() const
{
    return ui->comboBoxNonSvgImages->currentData().toBool();
}

void DialogAddGeneration::_onNameChanged(const QString &text)
{
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
}

void DialogAddGeneration::_onAccepted()
{
    const QString instructions = ui->plainTextEditInstructions->toPlainText().trimmed();
    if (!instructions.isEmpty() && !instructions.contains(QStringLiteral("[TOPIC]"))) {
        QMessageBox::warning(this,
                             tr("Missing [TOPIC]"),
                             tr("Your custom instructions must contain [TOPIC] so the generator "
                                "knows where to insert the page topic.\n\n"
                                "Please add [TOPIC] to your instructions and try again."));
        return;
    }
    accept();
}
