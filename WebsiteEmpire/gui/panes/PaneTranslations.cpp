#include "PaneTranslations.h"
#include "ui_PaneTranslations.h"

#include "launcher/AbstractLauncher.h"
#include "launcher/LauncherTranslate.h"
#include "launcher/LauncherTranslateCommon.h"
#include "website/AbstractEngine.h"
#include "website/commonblocs/AbstractCommonBloc.h"

#include <QDialog>
#include <QHeaderView>
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
    connect(ui->buttonReload,        &QPushButton::clicked, this, &PaneTranslations::_reload);
    connect(ui->buttonViewCommands,  &QPushButton::clicked, this, &PaneTranslations::_viewCommands);
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

    const QString cmd1 = QStringLiteral("WebsiteEmpire --%1 \"%2\" --%3")
        .arg(AbstractLauncher::OPTION_WORKING_DIR,
             workingPath,
             LauncherTranslate::OPTION_NAME);

    const QString cmd2 = QStringLiteral("WebsiteEmpire --%1 \"%2\" --%3")
        .arg(AbstractLauncher::OPTION_WORKING_DIR,
             workingPath,
             LauncherTranslateCommon::OPTION_NAME);

    const QString text = tr("# Translate pages:\n%1\n\n# Translate common blocs (header, footer, …):\n%2")
        .arg(cmd1, cmd2);

    auto *dlg  = new QDialog(this);
    dlg->setWindowTitle(tr("Translation Commands"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->resize(700, 260);

    auto *edit = new QPlainTextEdit(text, dlg);
    edit->setReadOnly(true);

    auto *btn = new QPushButton(tr("Close"), dlg);
    connect(btn, &QPushButton::clicked, dlg, &QDialog::accept);

    auto *layout = new QVBoxLayout(dlg);
    layout->addWidget(edit);
    layout->addWidget(btn);

    dlg->exec();
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
