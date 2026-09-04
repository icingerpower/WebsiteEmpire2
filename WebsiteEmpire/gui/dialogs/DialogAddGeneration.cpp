#include "DialogAddGeneration.h"
#include "ui_DialogAddGeneration.h"

#include "aspire/generator/AbstractGenerator.h"
#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/theme/AbstractTheme.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>

#include <algorithm>

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
    // Scope the picker to the engine's OWN generator when it declares one
    // (AbstractEngine::getGeneratorId()) — a Fashion site has no business
    // sourcing articles from Health/Language data, or vice versa. Only
    // engines that don't declare a generator (getGeneratorId().isEmpty(),
    // meaning "no known scoping") fall back to listing every registered
    // generator's tables, same as before this scoping existed.
    const QString scopedGeneratorId = engine ? engine->getGeneratorId() : QString{};

    ui->comboBoxPrimaryTable->addItem(tr("(None)"), QString{});
    for (auto it = AbstractGenerator::ALL_GENERATORS().constBegin();
         it != AbstractGenerator::ALL_GENERATORS().constEnd(); ++it) {
        if (!scopedGeneratorId.isEmpty() && it.key() != scopedGeneratorId) {
            continue;
        }
        const AbstractGenerator::GeneratorTables tables = it.value()->getTables();

        // Sort by id for a stable, deterministic combobox order across runs
        // (QHash iteration order is unspecified).
        QList<AbstractGenerator::TableDescriptor> descs = tables.primary.values();
        std::sort(descs.begin(), descs.end(),
                  [](const auto &a, const auto &b) { return a.id < b.id; });

        for (const AbstractGenerator::TableDescriptor &desc : std::as_const(descs)) {
            const QString displayName = it.value()->getName()
                                      + QStringLiteral(" — ")
                                      + desc.name;
            ui->comboBoxPrimaryTable->addItem(displayName, desc.id);
        }
    }

    // Pre-select based on the engine's linked generator. When that generator
    // exposes more than one primary table, picking one automatically would be
    // arbitrary — deterministically default to the alphabetically-first id
    // (matches the combobox's own sort order above) rather than leaving an
    // unexplained "(None)" selected.
    if (!scopedGeneratorId.isEmpty()) {
        const AbstractGenerator *proto =
            AbstractGenerator::ALL_GENERATORS().value(scopedGeneratorId, nullptr);
        if (proto) {
            const AbstractGenerator::GeneratorTables tables = proto->getTables();
            if (!tables.primary.isEmpty()) {
                QStringList ids = tables.primary.keys();
                std::sort(ids.begin(), ids.end());
                const int idx = ui->comboBoxPrimaryTable->findData(ids.first());
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

QString DialogAddGeneration::endPermalink() const
{
    return ui->lineEditEndPermalink->text().trimmed().toLower();
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
