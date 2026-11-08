#include "PanePageTaxonomy.h"
#include "ui_PanePageTaxonomy.h"
#include "GenStrategyTable.h"
#include "../dialogs/DialogPreviewPage.h"
#include "../dialogs/DialogShowCommand.h"
#include "aicli/AbstractCli.h"
#include "aicli/AvailableCliList.h"
#include "aicli/AvailableCliTable.h"
#include "website/AbstractEngine.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/blocs/AbstractPageBloc.h"
#include "website/taxonomy/TaxonomyPageSettings.h"
#include "workingdirectory/WorkingDirectoryManager.h"
#include <algorithm>
#include <QCoreApplication>
#include <QMessageBox>
#include <QProcess>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>

PanePageTaxonomy::PanePageTaxonomy(QWidget *parent)
    : QWidget(parent), ui(new Ui::PanePageTaxonomy)
{
    ui->setupUi(this);
    m_generatedModel = new QStandardItemModel(this);
    ui->listViewGeneratedPages->setModel(m_generatedModel);
    connect(ui->comboBoxTaxonomy, &QComboBox::currentIndexChanged, this,
            [this](int) { loadTaxonomy(); });
    connect(ui->textEditPrompt, &QPlainTextEdit::textChanged, this, &PanePageTaxonomy::saveEdits);
    connect(ui->textEditClosing, &QPlainTextEdit::textChanged, this, &PanePageTaxonomy::saveEdits);
    connect(ui->buttonGenerateOne, &QPushButton::clicked, this, [this] { generate(true); });
    connect(ui->buttonGenerateAll, &QPushButton::clicked, this, [this] { generate(false); });
    connect(ui->listViewGeneratedPages, &QListView::doubleClicked, this,
            [this](const QModelIndex &index) {
                if (index.isValid()) {
                    previewPage(index.data(Qt::UserRole).toInt());
                }
            });
    connect(ui->buttonPreview, &QPushButton::clicked, this, [this] {
        const QModelIndex index = ui->listViewGeneratedPages->currentIndex();
        if (!index.isValid()) {
            QMessageBox::information(this, tr("No selection"),
                                     tr("Select a generated taxonomy page to preview."));
            return;
        }
        previewPage(index.data(Qt::UserRole).toInt());
    });
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
    m_engine = engine;
    m_strategies = new GenStrategyTable(workingDir, this, QStringLiteral("taxonomy_strategies.json"));
    auto *cliTable = new AvailableCliTable(this);
    m_cliList = new AvailableCliList(cliTable, this);
    ui->comboBoxCli->setModel(m_cliList);

    // Restore saved selection now and again each time a new CLI becomes available
    // (availability checks are async and may not have resolved yet).
    connect(m_cliList, &QAbstractListModel::rowsInserted, this,
            [this](const QModelIndex &, int, int) { _restoreTaxonomyCli(); });
    _restoreTaxonomyCli();

    connect(ui->comboBoxCli, &QComboBox::activated, this, [this](int) {
        AbstractCli *cli = m_cliList ? m_cliList->cliAt(ui->comboBoxCli->currentIndex()) : nullptr;
        WorkingDirectoryManager::instance()->settings()
            ->setValue(QStringLiteral("taxonomyCli"), cli ? cli->getName() : QString{});
    });

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
    refreshGeneratedPages();
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
        const QString text = QString::fromUtf8(m_process->readAllStandardOutput());
        ui->textEditOutput->appendPlainText(text);
        if (text.contains(QStringLiteral("] OK: "))) {
            refreshGeneratedPages();
        }
    });
    const auto finish = [this] {
        QProcess *process = m_process;
        m_process = nullptr;
        process->deleteLater();
        ui->comboBoxTaxonomy->setEnabled(true);
        ui->buttonGenerateOne->setEnabled(true);
        ui->buttonGenerateAll->setEnabled(true);
        refreshGeneratedPages();
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

void PanePageTaxonomy::refreshGeneratedPages()
{
    if (!m_generatedModel) {
        return;
    }
    m_generatedModel->clear();
    if (m_taxonomyId.isEmpty() || !m_workingDir.exists()) {
        ui->labelGeneratedPages->setText(tr("Generated taxonomy pages (0)"));
        return;
    }

    const bool symptoms = m_taxonomyId == QStringLiteral("symptoms");
    const QString typeId = symptoms ? QStringLiteral("symptom_hub")
                                    : QStringLiteral("fashion_tag_hub");

    PageDb pageDb(m_workingDir);
    PageRepositoryDb repo(pageDb);
    const QList<PageRecord> all = repo.findAll();

    QList<PageRecord> generated;
    for (const PageRecord &record : std::as_const(all)) {
        if (record.sourcePageId != 0 || record.typeId != typeId) {
            continue;
        }
        const auto data = repo.loadData(record.id);
        if (data.value(QStringLiteral("_taxonomyArticle")) != QStringLiteral("1")) {
            continue;
        }
        if (!symptoms && data.value(QStringLiteral("0_dimension")) != m_taxonomyId) {
            continue;
        }
        generated.append(record);
    }

    std::sort(generated.begin(), generated.end(),
              [](const PageRecord &a, const PageRecord &b) {
                  if (a.generatedAt != b.generatedAt) {
                      return a.generatedAt > b.generatedAt;
                  }
                  return a.permalink < b.permalink;
              });

    for (const PageRecord &record : std::as_const(generated)) {
        const QString label = record.generatedAt.isEmpty()
            ? record.permalink
            : QStringLiteral("%1 (%2)").arg(record.permalink, record.generatedAt);
        auto *item = new QStandardItem(label);
        item->setData(record.id, Qt::UserRole);
        item->setToolTip(tr("Double-click to preview %1").arg(record.permalink));
        m_generatedModel->appendRow(item);
    }

    ui->labelGeneratedPages->setText(
        tr("Generated taxonomy pages (%1)").arg(generated.size()));
    if (!generated.isEmpty() && !ui->listViewGeneratedPages->currentIndex().isValid()) {
        ui->listViewGeneratedPages->setCurrentIndex(m_generatedModel->index(0, 0));
    }
}

void PanePageTaxonomy::previewPage(int pageId)
{
    if (pageId <= 0 || !m_engine) {
        return;
    }
    PageDb pageDb(m_workingDir);
    PageRepositoryDb repo(pageDb);
    CategoryTable categoryTable(m_workingDir);
    DialogPreviewPage dlg(repo, categoryTable, *m_engine, m_workingDir, pageId, this);
    dlg.exec();
}

void PanePageTaxonomy::_restoreTaxonomyCli()
{
    if (!m_cliList) {
        return;
    }
    const QString saved = WorkingDirectoryManager::instance()->settings()
                              ->value(QStringLiteral("taxonomyCli")).toString();
    if (saved.isEmpty()) {
        return;
    }
    for (int i = 0; i < m_cliList->rowCount(); ++i) {
        const QString name =
            m_cliList->data(m_cliList->index(i, 0), Qt::DisplayRole).toString();
        if (name == saved) {
            const QSignalBlocker blocker(ui->comboBoxCli);
            ui->comboBoxCli->setCurrentIndex(i);
            return;
        }
    }
}
