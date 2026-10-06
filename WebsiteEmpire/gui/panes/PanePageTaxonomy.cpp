#include "PanePageTaxonomy.h"
#include "ui_PanePageTaxonomy.h"
#include "GenStrategyTable.h"
#include "../dialogs/DialogShowCommand.h"
#include "aicli/AbstractCli.h"
#include "aicli/AvailableCliList.h"
#include "aicli/AvailableCliTable.h"
#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/AbstractPageBloc.h"
#include "website/taxonomy/TaxonomyPageSettings.h"
#include <QCoreApplication>
#include <QMessageBox>
#include <QProcess>
#include <QSet>

PanePageTaxonomy::PanePageTaxonomy(QWidget *parent)
    : QWidget(parent), ui(new Ui::PanePageTaxonomy)
{
    ui->setupUi(this);
    connect(ui->comboBoxTaxonomy, &QComboBox::currentIndexChanged, this,
            [this](int) { loadTaxonomy(); });
    connect(ui->textEditPrompt, &QPlainTextEdit::textChanged, this, &PanePageTaxonomy::saveEdits);
    connect(ui->textEditClosing, &QPlainTextEdit::textChanged, this, &PanePageTaxonomy::saveEdits);
    connect(ui->buttonGenerateOne, &QPushButton::clicked, this, [this] { generate(true); });
    connect(ui->buttonGenerateAll, &QPushButton::clicked, this, [this] { generate(false); });
    connect(ui->buttonCommand, &QPushButton::clicked, this, [this] {
        saveEdits();
        QStringList quoted;
        QStringList args = generationArgs(false);
        args.prepend(QCoreApplication::applicationFilePath());
        for (QString arg : std::as_const(args)) {
            arg.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            quoted.append(QLatin1Char('\'') + arg + QLatin1Char('\''));
        }
        DialogShowCommand dialog(tr("Taxonomy article generation"),
            tr("Generate missing articles for the selected taxonomy."),
            quoted.join(QLatin1Char(' ')), this);
        dialog.exec();
    });
}

PanePageTaxonomy::~PanePageTaxonomy()
{
    if (m_process) {
        m_process->disconnect(this);
    }
    delete ui;
}

void PanePageTaxonomy::setup(const QDir &workingDir, AbstractEngine *engine,
                             WebsiteSettingsTable * /*settings*/)
{
    m_workingDir = workingDir;
    m_strategies = new GenStrategyTable(workingDir, this, QStringLiteral("taxonomy_strategies.json"));
    auto *cliTable = new AvailableCliTable(this);
    auto *cliList = new AvailableCliList(cliTable, this);
    ui->comboBoxCli->setModel(cliList);
    QSet<QString> seen;
    if (engine) {
        for (const AbstractPageType *type : engine->getPageTypes()) {
            for (const AbstractPageBloc *bloc : type->getPageBlocs()) {
                const auto descriptors = bloc->taxonomies();
                for (const auto &desc : descriptors) {
                    if (!seen.contains(desc.id)) {
                        seen.insert(desc.id);
                        ui->comboBoxTaxonomy->addItem(desc.displayName, desc.id);
                    }
                }
            }
        }
    }
    loadTaxonomy();
}

void PanePageTaxonomy::loadTaxonomy()
{
    if (!m_strategies) {
        return;
    }
    m_loading = true;
    m_taxonomyId = ui->comboBoxTaxonomy->currentData().toString();
    TaxonomyPageSettings settings(m_workingDir);
    m_row = m_strategies->rowForId(settings.strategyId(m_taxonomyId));
    if (!m_taxonomyId.isEmpty() && m_row < 0) {
        const QString type = m_taxonomyId == QStringLiteral("symptoms")
            ? QStringLiteral("symptom_hub") : QStringLiteral("fashion_tag_hub");
        const QString id = m_strategies->addRow(ui->comboBoxTaxonomy->currentText(), type, {}, {}, false);
        settings.setStrategyId(m_taxonomyId, id);
        m_row = m_strategies->rowForId(id);
    }
    ui->textEditPrompt->setPlainText(m_strategies->customInstructionsForRow(m_row));
    ui->textEditClosing->setPlainText(settings.closingText(m_taxonomyId));
    const bool enabled = m_row >= 0;
    ui->textEditPrompt->setEnabled(enabled);
    ui->textEditClosing->setEnabled(enabled);
    ui->buttonGenerateOne->setEnabled(enabled);
    ui->buttonGenerateAll->setEnabled(enabled);
    ui->buttonCommand->setEnabled(enabled);
    m_loading = false;
}

void PanePageTaxonomy::saveEdits()
{
    if (m_loading || !m_strategies || m_row < 0) {
        return;
    }
    m_strategies->setCustomInstructions(m_row, ui->textEditPrompt->toPlainText());
    TaxonomyPageSettings(m_workingDir).setClosingText(m_taxonomyId, ui->textEditClosing->toPlainText());
}

QStringList PanePageTaxonomy::generationArgs(bool one) const
{
    QStringList args = {QStringLiteral("--workingDir"), m_workingDir.absolutePath(),
        QStringLiteral("--generation"), QStringLiteral("--page-taxonomy"), m_taxonomyId,
        QStringLiteral("--sessions"), QStringLiteral("1"), QStringLiteral("--new-only")};
    if (one) {
        args << QStringLiteral("--limit") << QStringLiteral("1");
    }
    auto *list = qobject_cast<AvailableCliList *>(ui->comboBoxCli->model());
    AbstractCli *cli = list ? list->cliAt(ui->comboBoxCli->currentIndex()) : nullptr;
    if (cli) {
        args << QStringLiteral("--cli") << cli->getName();
    }
    return args;
}

void PanePageTaxonomy::generate(bool one)
{
    if (m_process || m_row < 0) {
        return;
    }
    if (ui->comboBoxCli->currentIndex() < 0) {
        QMessageBox::warning(this, tr("No AI CLI available"), tr("Install an AI CLI before generating articles."));
        return;
    }
    saveEdits();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    ui->textEditOutput->clear();
    ui->comboBoxTaxonomy->setEnabled(false);
    ui->buttonGenerateOne->setEnabled(false);
    ui->buttonGenerateAll->setEnabled(false);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        ui->textEditOutput->appendPlainText(QString::fromUtf8(m_process->readAllStandardOutput()));
    });
    const auto finish = [this] {
        QProcess *process = m_process;
        m_process = nullptr;
        process->deleteLater();
        ui->comboBoxTaxonomy->setEnabled(true);
        ui->buttonGenerateOne->setEnabled(true);
        ui->buttonGenerateAll->setEnabled(true);
    };
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, finish](int exitCode, QProcess::ExitStatus status) {
            ui->textEditOutput->appendPlainText(status == QProcess::NormalExit && exitCode == 0
                ? tr("Generation finished.") : tr("Generation failed (exit code %1).").arg(exitCode));
            finish();
        });
    connect(m_process, &QProcess::errorOccurred, this, [this, finish](QProcess::ProcessError error) {
        ui->textEditOutput->appendPlainText(m_process->errorString());
        if (error == QProcess::FailedToStart) {
            finish();
        }
    });
    m_process->start(QCoreApplication::applicationFilePath(), generationArgs(one));
}
