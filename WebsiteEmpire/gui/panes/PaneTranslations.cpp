#include "PaneTranslations.h"
#include "ui_PaneTranslations.h"

#include "launcher/AbstractLauncher.h"
#include "launcher/LauncherTranslate.h"
#include "launcher/LauncherTranslateCommon.h"
#include "website/AbstractEngine.h"
#include "website/commonblocs/AbstractCommonBloc.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/theme/AbstractTheme.h"
#include "website/translation/TranslationFieldTable.h"
#include "website/translation/TranslationStatusTable.h"

PaneTranslations::PaneTranslations(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneTranslations)
{
    ui->setupUi(this);
    ui->labelConfigWarning->setVisible(false);
    connect(ui->buttonReload,               &QPushButton::clicked, this, &PaneTranslations::_reload);
    connect(ui->buttonViewCommands,         &QPushButton::clicked, this, &PaneTranslations::_viewCommands);
    connect(ui->buttonRemoveOneTranslation, &QPushButton::clicked, this, &PaneTranslations::_removeOneTranslation);
    connect(ui->buttonRemoveAllTranslations,&QPushButton::clicked, this, &PaneTranslations::_removeAllTranslations);
}

PaneTranslations::~PaneTranslations()
{
    delete ui;
}

void PaneTranslations::setup(const QDir &workingDir, AbstractEngine *engine, AbstractTheme *theme)
{
    m_workingDir = workingDir;
    m_engine     = engine;
    m_theme      = theme;
    m_isSetup    = true;
}

void PaneTranslations::setTheme(AbstractTheme *theme)
{
    m_theme = theme;
    if (m_statusModel) {
        _reload();
    }
}

void PaneTranslations::setVisible(bool visible)
{
    QWidget::setVisible(visible);

    if (!visible || !m_isSetup || m_statusModel) {
        return;
    }

    _initModels();
}

void PaneTranslations::_reload()
{
    // Models hold references to repos — destroy models first.
    m_statusModel.reset();
    m_fieldModel.reset();
    // Repos are now unreferenced — safe to destroy.
    m_pageRepo.reset();
    m_pageDb.reset();
    m_categoryTable.reset();

    _initModels();
}

void PaneTranslations::_initModels()
{
    // Both tables share the same language columns: one per unique lang code in
    // the engine's domain rows, in the order they appear.
    QStringList langs;
    if (m_engine) {
        QSet<QString> seen;
        const int count = m_engine->rowCount();
        for (int i = 0; i < count; ++i) {
            const QString lang = m_engine->getLangCode(i);
            if (!lang.isEmpty() && !seen.contains(lang)) {
                seen.insert(lang);
                langs.append(lang);
            }
        }
    }

    m_categoryTable = std::make_unique<CategoryTable>(m_workingDir);
    m_pageDb        = std::make_unique<PageDb>(m_workingDir);
    m_pageRepo      = std::make_unique<PageRepositoryDb>(*m_pageDb);

    m_statusModel = std::make_unique<TranslationStatusTable>(
        *m_pageRepo, *m_categoryTable, langs);
    m_statusModel->reload();
    ui->tableViewStatus->setModel(m_statusModel.get());
    ui->tableViewStatus->horizontalHeader()->setSectionResizeMode(
        TranslationStatusTable::COL_PERMALINK, QHeaderView::Stretch);
    ui->tableViewStatus->resizeColumnsToContents();
    connect(ui->tableViewStatus->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, &PaneTranslations::_updateRemoveButtons);
    _updateRemoveButtons();

    QList<AbstractCommonBloc *> blocs;
    if (m_theme) {
        blocs += m_theme->getTopBlocs();
        blocs += m_theme->getBottomBlocs();
    }

    m_fieldModel = std::make_unique<TranslationFieldTable>(blocs, langs);
    ui->tableViewFields->setModel(m_fieldModel.get());
    ui->tableViewFields->horizontalHeader()->setSectionResizeMode(
        TranslationFieldTable::COL_SOURCE, QHeaderView::Stretch);
    ui->tableViewFields->resizeColumnsToContents();

    if (m_theme) {
        connect(m_fieldModel.get(), &QAbstractTableModel::dataChanged,
                this, [this]() {
                    m_theme->saveBlocsData();
                    _updateProgressLabels();
                });
    }

    // Diagnostic banner — tell the user exactly what still needs configuring.
    QStringList warnings;
    if (langs.isEmpty()) {
        warnings += tr("No languages in the engine — "
                       "add website languages in the Domains tab to enable translation columns.");
    }
    if (!m_theme) {
        warnings += tr("No theme selected — choose a theme in the Theme tab.");
    } else if (m_fieldModel->rowCount() == 0) {
        warnings += tr("Theme blocs have no source text yet — "
                       "fill in the Header, Footer, etc. in the Theme tab.");
    }
    const QString warningText = warnings.join(QStringLiteral("  |  "));
    ui->labelConfigWarning->setText(warningText);
    ui->labelConfigWarning->setVisible(!warningText.isEmpty());

    _updateProgressLabels();
}

void PaneTranslations::_viewCommands()
{
    const QString workingPath = m_workingDir.absolutePath();
    const QString prefix = QStringLiteral("WebsiteEmpire --%1 \"%2\"")
        .arg(AbstractLauncher::OPTION_WORKING_DIR, workingPath);

    // Pick a sensible example language for the --language option: second unique
    // lang code in the engine (first is the editing/source language).
    QString exampleLang = QStringLiteral("fr");
    if (m_engine) {
        QSet<QString> seen;
        QStringList langs;
        const int count = m_engine->rowCount();
        for (int i = 0; i < count; ++i) {
            const QString lang = m_engine->getLangCode(i);
            if (!lang.isEmpty() && !seen.contains(lang)) {
                seen.insert(lang);
                langs.append(lang);
            }
        }
        if (langs.size() >= 2) {
            exampleLang = langs.at(1);
        }
    }

    const QString cmdTranslateMin = prefix
        + QStringLiteral(" --") + LauncherTranslate::OPTION_NAME;

    const QString cmdTranslateFull = prefix
        + QStringLiteral(" --") + LauncherTranslate::OPTION_NAME
        + QStringLiteral(" --") + LauncherTranslate::OPTION_LANGUAGE + QStringLiteral(" ") + exampleLang
        + QStringLiteral(" --") + LauncherTranslate::OPTION_LIMIT + QStringLiteral(" 1");

    const QString cmdCommon = prefix
        + QStringLiteral(" --") + LauncherTranslateCommon::OPTION_NAME;

    const QString text = tr(
        "# Translate pages (all languages):\n"
        "%1\n\n"
        "# Translate pages (all optional args — replace '%2' and '1' as needed):\n"
        "%3\n\n"
        "# Translate common blocs (header, footer, …):\n"
        "%4"
    ).arg(cmdTranslateMin, exampleLang, cmdTranslateFull, cmdCommon);

    auto *dlg  = new QDialog(this);
    dlg->setWindowTitle(tr("Translation Commands"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->resize(800, 320);

    auto *edit = new QPlainTextEdit(text, dlg);
    edit->setReadOnly(true);

    auto *btn = new QPushButton(tr("Close"), dlg);
    connect(btn, &QPushButton::clicked, dlg, &QDialog::accept);

    auto *layout = new QVBoxLayout(dlg);
    layout->addWidget(edit);
    layout->addWidget(btn);

    dlg->exec();
}

void PaneTranslations::_removeOneTranslation()
{
    if (!m_statusModel || !m_pageRepo) {
        return;
    }

    const QModelIndexList sel = ui->tableViewStatus->selectionModel()->selectedRows();
    if (sel.isEmpty()) {
        return;
    }

    const int pageId = m_statusModel->pageIdAt(sel.first().row());
    if (pageId < 0) {
        return;
    }

    const QStringList &langs = m_statusModel->targetLangs();
    if (langs.isEmpty()) {
        return;
    }

    auto *dlg    = new QDialog(this);
    dlg->setWindowTitle(tr("Remove one translation"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto *combo  = new QComboBox(dlg);
    for (const QString &lang : std::as_const(langs)) {
        combo->addItem(lang);
    }

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    auto *layout = new QFormLayout(dlg);
    layout->addRow(tr("Language:"), combo);
    layout->addRow(buttons);

    if (dlg->exec() != QDialog::Accepted) {
        return;
    }

    const QString lang = combo->currentText();
    m_pageRepo->clearTranslationData(pageId, lang);
    m_statusModel->reload();
    _updateProgressLabels();
}

void PaneTranslations::_removeAllTranslations()
{
    if (!m_statusModel || !m_pageRepo) {
        return;
    }

    const QModelIndexList sel = ui->tableViewStatus->selectionModel()->selectedRows();
    if (sel.isEmpty()) {
        return;
    }

    const int pageId = m_statusModel->pageIdAt(sel.first().row());
    if (pageId < 0) {
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        tr("Remove all translations"),
        tr("Remove all translations for page %1?\n\nThis cannot be undone.").arg(pageId),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (answer != QMessageBox::Yes) {
        return;
    }

    m_pageRepo->clearAllTranslationData(pageId);
    m_statusModel->reload();
    _updateProgressLabels();
}

void PaneTranslations::_updateRemoveButtons()
{
    const bool hasSelection =
        ui->tableViewStatus->selectionModel() &&
        ui->tableViewStatus->selectionModel()->hasSelection();
    ui->buttonRemoveOneTranslation->setEnabled(hasSelection);
    ui->buttonRemoveAllTranslations->setEnabled(hasSelection);
}

void PaneTranslations::_updateProgressLabels()
{
    if (m_statusModel) {
        const auto &[done, total] = m_statusModel->progress();
        ui->labelStatusProgress->setText(
            tr("Pages: %1 / %2 translations done").arg(done).arg(total));
    }
    if (m_fieldModel) {
        const auto &[done, total] = m_fieldModel->progress();
        ui->labelFieldsProgress->setText(
            tr("Common blocs: %1 / %2 fields translated").arg(done).arg(total));
    }
}
