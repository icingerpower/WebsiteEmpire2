#include "PaneUpdate.h"
#include "ui_PaneUpdate.h"

#include "UpdateStrategyTree.h"
#include "../dialogs/DialogAddUpdateStrategy.h"
#include "../dialogs/DialogAddUpdatePrompt.h"
#include "../dialogs/DialogPickArticlesToReset.h"
#include "../dialogs/DialogPickArticlesToUpdate.h"
#include "../dialogs/DialogViewUpdatedArticles.h"
#include "launcher/LauncherUpdate.h"
#include "launcher/AbstractLauncher.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"

#include <QCoreApplication>
#include "../dialogs/DialogShowCommand.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTimer>

PaneUpdate::PaneUpdate(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneUpdate)
{
    ui->setupUi(this);
    _connectSlots();
}

PaneUpdate::~PaneUpdate()
{
    if (m_activeProcess) {
        m_activeProcess->disconnect();
    }
    _saveCurrentPrompt();
    delete ui;
}

void PaneUpdate::setup(const QDir           &workingDir,
                        AbstractEngine       * /*engine*/,
                        WebsiteSettingsTable * /*settingsTable*/)
{
    m_workingDir = workingDir;
    m_strategies = new UpdateStrategyTree(workingDir, this);
    ui->treeViewStrategies->setModel(m_strategies);

    auto *header = ui->treeViewStrategies->header();
    header->setVisible(true);
    header->setSectionResizeMode(UpdateStrategyTree::COL_NAME,       QHeaderView::Stretch);
    header->setSectionResizeMode(UpdateStrategyTree::COL_UPDATE_SVG, QHeaderView::Fixed);
    header->setSectionResizeMode(UpdateStrategyTree::COL_UPDATE_IMG, QHeaderView::Fixed);
    header->resizeSection(UpdateStrategyTree::COL_UPDATE_SVG, 38);
    header->resizeSection(UpdateStrategyTree::COL_UPDATE_IMG, 38);

    ui->treeViewStrategies->expandAll();

    connect(ui->treeViewStrategies->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            &PaneUpdate::_onSelectionChanged);

    m_isSetup = true;
}

void PaneUpdate::setVisible(bool visible)
{
    if (!visible) {
        _saveCurrentPrompt();
    }
    QWidget::setVisible(visible);
}

// ---- Slots ------------------------------------------------------------------

void PaneUpdate::addStrategy()
{
    if (!m_isSetup) {
        return;
    }
    DialogAddUpdateStrategy dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_strategies->addStrategy(dialog.name(), dialog.pageTypeId(),
                               dialog.themeId(), false);
    ui->treeViewStrategies->expandAll();
}

void PaneUpdate::addPrompt()
{
    if (!m_isSetup) {
        return;
    }
    const QString stratId = _currentStrategyId();
    if (stratId.isEmpty()) {
        return;
    }

    // Build available targets from the strategy's page type.
    const QModelIndex current = ui->treeViewStrategies->currentIndex();
    const QString pageTypeId = m_strategies->pageTypeIdFor(current);
    QList<AbstractPageType::AiUpdateTarget> targets;
    if (!pageTypeId.isEmpty()) {
        CategoryTable categoryTable(m_workingDir);
        auto pageType = AbstractPageType::createForTypeId(pageTypeId, categoryTable);
        if (pageType) {
            targets = pageType->aiUpdateTargets();
        }
    }

    DialogAddUpdatePrompt dialog(targets, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString promptId = m_strategies->addPrompt(
        stratId, dialog.name(), QString(),
        dialog.saveKey(), dialog.skipIfKeyNonEmpty());
    if (promptId.isEmpty()) {
        return;
    }
    ui->treeViewStrategies->expandAll();

    // Select the new prompt so the user can immediately type instructions.
    for (int i = 0; i < m_strategies->rowCount(); ++i) {
        const auto *stratItem = m_strategies->item(i);
        if (!stratItem || stratItem->data(UpdateStrategyTree::ROLE_ID).toString() != stratId) {
            continue;
        }
        for (int j = 0; j < stratItem->rowCount(); ++j) {
            const auto *promptItem = stratItem->child(j);
            if (promptItem && promptItem->data(UpdateStrategyTree::ROLE_ID).toString() == promptId) {
                const QModelIndex idx = m_strategies->indexFromItem(
                    const_cast<QStandardItem *>(promptItem));
                ui->treeViewStrategies->setCurrentIndex(idx);
                break;
            }
        }
        break;
    }
}

void PaneUpdate::removeSelected()
{
    if (!m_isSetup) {
        return;
    }
    const QModelIndex current = ui->treeViewStrategies->currentIndex();
    if (!current.isValid()) {
        return;
    }
    m_strategies->removeItem(current);
}

void PaneUpdate::updateAll()
{
    _runUpdate(-1);
}

void PaneUpdate::updateOne()
{
    _runUpdate(1);
}

void PaneUpdate::updateN()
{
    _runUpdate(ui->spinBoxUpdateCount->value());
}

void PaneUpdate::updateSelection()
{
    if (!m_isSetup) {
        return;
    }
    const QString promptId = _currentPromptId();
    if (promptId.isEmpty()) {
        return;
    }
    const QModelIndex current = ui->treeViewStrategies->currentIndex();
    const QString typeId = m_strategies->pageTypeIdFor(current);

    PageDb           pageDb(m_workingDir);
    PageRepositoryDb pageRepo(pageDb);

    const QList<PageRecord> pages = pageRepo.findPagesForUpdate(typeId, promptId, -1, {});
    if (pages.isEmpty()) {
        QMessageBox::information(this,
            tr("Update selection"),
            tr("No pages found for this prompt."));
        return;
    }

    DialogPickArticlesToUpdate dialog(pages, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QList<int> selected = dialog.selectedPageIds();
    if (selected.isEmpty()) {
        return;
    }
    _runUpdate(-1, selected);
}

void PaneUpdate::stop()
{
    if (m_activeProcess) {
        m_activeProcess->kill();
    }
}

void PaneUpdate::_runUpdate(int limit, const QList<int> &pageIds)
{
    if (!m_isSetup) {
        return;
    }

    const QString stratId  = _currentStrategyId();
    const QString promptId = _currentPromptId();
    if (stratId.isEmpty()) {
        return;
    }

    const QString exe = QCoreApplication::applicationFilePath();
    QStringList args = {
        QStringLiteral("--") + AbstractLauncher::OPTION_WORKING_DIR,
        m_workingDir.absolutePath(),
        QStringLiteral("--") + LauncherUpdate::OPTION_NAME,
        QStringLiteral("--") + LauncherUpdate::OPTION_STRATEGY,
        stratId
    };
    if (limit >= 1) {
        args << QStringLiteral("--") + LauncherUpdate::OPTION_LIMIT
             << QString::number(limit);
    }
    if (!promptId.isEmpty()) {
        args << QStringLiteral("--") + LauncherUpdate::OPTION_PROMPT << promptId;
    }
    if (!pageIds.isEmpty()) {
        QStringList idStrs;
        idStrs.reserve(pageIds.size());
        for (int id : std::as_const(pageIds)) {
            idStrs << QString::number(id);
        }
        args << QStringLiteral("--") + LauncherUpdate::OPTION_PAGES
             << idStrs.join(QLatin1Char(','));
    }

    ui->textEditOutput->clear();
    m_outputBuffer.clear();
    ui->textEditOutput->appendPlainText(
        tr("Running: %1 %2\n").arg(exe, args.join(QLatin1Char(' '))));

    ui->progressBarUpdate->setVisible(true);
    ui->buttonUpdateAll->setEnabled(false);
    ui->buttonUpdateOne->setEnabled(false);
    ui->buttonUpdateN->setEnabled(false);
    ui->buttonUpdateSelection->setEnabled(false);
    ui->buttonStop->setEnabled(true);

    auto *process = new QProcess(this);
    m_activeProcess = process;

    // Inactivity timer: if the subprocess produces no output for 20 minutes,
    // it is assumed stuck (e.g. a silent claude hang). Any output resets it.
    static constexpr int kInactivityMs = 25 * 60 * 1000;
    auto *inactivityTimer = new QTimer(this);
    inactivityTimer->setSingleShot(true);
    inactivityTimer->setInterval(kInactivityMs);
    m_inactivityTimer = inactivityTimer;
    connect(inactivityTimer, &QTimer::timeout, this, [this]() {
        ui->textEditOutput->appendPlainText(
            tr("\n[Inactivity timeout — no output for 25 min. Killing process.]"));
        if (m_activeProcess) {
            m_activeProcess->kill();
        }
    });

    auto appendOutput = [this, inactivityTimer](const QByteArray &bytes) {
        inactivityTimer->start(); // reset on every chunk of output
        m_outputBuffer += QString::fromUtf8(bytes);
        // Flush complete lines to the text edit so they never interleave.
        int newline = m_outputBuffer.indexOf(QLatin1Char('\n'));
        while (newline >= 0) {
            ui->textEditOutput->appendPlainText(m_outputBuffer.left(newline));
            m_outputBuffer = m_outputBuffer.mid(newline + 1);
            newline = m_outputBuffer.indexOf(QLatin1Char('\n'));
        }
    };

    connect(process, &QProcess::readyReadStandardOutput, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });
    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, process, inactivityTimer, appendOutput](int exitCode, QProcess::ExitStatus exitStatus) {
                inactivityTimer->stop();
                inactivityTimer->deleteLater();
                m_inactivityTimer = nullptr;
                // Drain any bytes the process wrote after the last readyRead signal.
                const QByteArray tail = process->readAllStandardOutput();
                if (!tail.isEmpty()) {
                    appendOutput(tail);
                }
                const QByteArray tailErr = process->readAllStandardError();
                if (!tailErr.isEmpty()) {
                    appendOutput(tailErr);
                }
                // Flush any remaining partial line (no trailing \n).
                if (!m_outputBuffer.isEmpty()) {
                    ui->textEditOutput->appendPlainText(m_outputBuffer);
                    m_outputBuffer.clear();
                }
                m_activeProcess = nullptr;
                ui->progressBarUpdate->setVisible(false);
                ui->buttonUpdateAll->setEnabled(true);
                ui->buttonUpdateOne->setEnabled(true);
                ui->buttonUpdateN->setEnabled(true);
                ui->buttonUpdateSelection->setEnabled(!_currentPromptId().isEmpty());
                ui->buttonStop->setEnabled(false);
                const QString statusMsg = (exitStatus == QProcess::CrashExit)
                    ? tr("\nProcess CRASHED (signal %1).").arg(exitCode)
                    : tr("\nProcess finished (exit code %1).").arg(exitCode);
                ui->textEditOutput->appendPlainText(statusMsg);
                process->deleteLater();
            });
    inactivityTimer->start();
    process->start(exe, args);
}

void PaneUpdate::viewUpdateCommand()
{
    if (!m_isSetup) {
        return;
    }
    const QString stratId  = _currentStrategyId();
    const QString promptId = _currentPromptId();
    if (stratId.isEmpty()) {
        return;
    }

    const QString exe     = QCoreApplication::applicationFilePath();
    const QString workDir = m_workingDir.absolutePath();

    QString cmd = QStringLiteral("%1 --%2 \"%3\" --%4 --%5 %6")
                      .arg(exe,
                           AbstractLauncher::OPTION_WORKING_DIR,
                           workDir,
                           LauncherUpdate::OPTION_NAME,
                           LauncherUpdate::OPTION_STRATEGY,
                           stratId);
    if (!promptId.isEmpty()) {
        cmd += QStringLiteral(" --") + LauncherUpdate::OPTION_PROMPT
               + QStringLiteral(" ") + promptId;
    }

    DialogShowCommand dlg(tr("Update command"),
                          tr("Run this command in a terminal to update pages:"),
                          cmd, this);
    dlg.exec();
}

void PaneUpdate::viewUpdated()
{
    if (!m_isSetup) {
        return;
    }
    const QString promptId = _currentPromptId();
    if (promptId.isEmpty()) {
        return;
    }
    const QModelIndex current = ui->treeViewStrategies->currentIndex();
    const QString typeId = m_strategies->pageTypeIdFor(current);

    PageDb           pageDb(m_workingDir);
    PageRepositoryDb pageRepo(pageDb);

    const QList<PageRecord> updated    = pageRepo.findPagesWithUpdateAttempt(promptId);
    const QList<PageRecord> notUpdated = pageRepo.findPagesWithoutUpdateAttempt(typeId, promptId);

    DialogViewUpdatedArticles dialog(updated, notUpdated, this);
    dialog.exec();
}

void PaneUpdate::resetAttempt()
{
    if (!m_isSetup) {
        return;
    }
    const QString promptId = _currentPromptId();
    if (promptId.isEmpty()) {
        return;
    }

    PageDb           pageDb(m_workingDir);
    PageRepositoryDb pageRepo(pageDb);

    const QList<PageRecord> pages = pageRepo.findPagesWithUpdateAttempt(promptId);
    if (pages.isEmpty()) {
        QMessageBox::information(this,
            tr("Reset attempt"),
            tr("No update attempts found for this prompt."));
        return;
    }

    DialogPickArticlesToReset dialog(pages, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QList<int> selected = dialog.selectedPageIds();
    if (selected.isEmpty()) {
        return;
    }
    pageRepo.clearUpdateAttempts(selected, promptId);
}

// ---- Private slots ----------------------------------------------------------

void PaneUpdate::_onSelectionChanged(const QModelIndex &current,
                                      const QModelIndex &previous)
{
    // Always work with column 0 — clicking a checkbox column must not discard edits.
    const QModelIndex prev0 = previous.isValid() ? previous.sibling(previous.row(), 0) : QModelIndex{};
    const QModelIndex cur0  = current.isValid()  ? current.sibling(current.row(),   0) : QModelIndex{};

    if (prev0.isValid() && m_isSetup && !m_strategies->isStrategy(prev0)) {
        m_strategies->setInstructions(prev0, ui->textEditPrompt->toPlainText());
    }

    if (!cur0.isValid()) {
        ui->buttonUpdateAll->setEnabled(false);
        ui->buttonUpdateOne->setEnabled(false);
        ui->buttonUpdateN->setEnabled(false);
        ui->buttonUpdateSelection->setEnabled(false);
        ui->buttonCommandUpdate->setEnabled(false);
        ui->buttonViewUpdated->setEnabled(false);
        ui->buttonResetAttempt->setEnabled(false);
        ui->buttonAddPrompt->setEnabled(false);
        ui->textEditPrompt->clear();
        ui->textEditPrompt->setEnabled(false);
        return;
    }

    const bool isStrat = m_strategies->isStrategy(cur0);

    // "Add Prompt" enabled when a strategy is selected (or a prompt under one).
    ui->buttonAddPrompt->setEnabled(true);
    ui->buttonUpdateAll->setEnabled(true);
    ui->buttonUpdateOne->setEnabled(true);
    ui->buttonUpdateN->setEnabled(true);
    ui->buttonCommandUpdate->setEnabled(true);
    // These three only make sense when a specific prompt is selected.
    ui->buttonUpdateSelection->setEnabled(!isStrat);
    ui->buttonViewUpdated->setEnabled(!isStrat);
    ui->buttonResetAttempt->setEnabled(!isStrat);

    m_updatingPrompt = true;
    if (isStrat) {
        ui->textEditPrompt->clear();
        ui->textEditPrompt->setEnabled(false);
    } else {
        ui->textEditPrompt->setEnabled(true);
        ui->textEditPrompt->setPlainText(m_strategies->instructionsFor(cur0));
    }
    m_updatingPrompt = false;
}

// ---- Private ----------------------------------------------------------------

void PaneUpdate::_connectSlots()
{
    connect(ui->buttonAddStrategy, &QPushButton::clicked,
            this, &PaneUpdate::addStrategy);
    connect(ui->buttonAddPrompt, &QPushButton::clicked,
            this, &PaneUpdate::addPrompt);
    connect(ui->buttonRemove, &QPushButton::clicked,
            this, &PaneUpdate::removeSelected);
    connect(ui->buttonUpdateAll, &QPushButton::clicked,
            this, &PaneUpdate::updateAll);
    connect(ui->buttonUpdateOne, &QPushButton::clicked,
            this, &PaneUpdate::updateOne);
    connect(ui->buttonUpdateN, &QPushButton::clicked,
            this, &PaneUpdate::updateN);
    connect(ui->buttonUpdateSelection, &QPushButton::clicked,
            this, &PaneUpdate::updateSelection);
    connect(ui->buttonStop, &QPushButton::clicked,
            this, &PaneUpdate::stop);
    connect(ui->buttonCommandUpdate, &QPushButton::clicked,
            this, &PaneUpdate::viewUpdateCommand);
    connect(ui->buttonViewUpdated, &QPushButton::clicked,
            this, &PaneUpdate::viewUpdated);
    connect(ui->buttonResetAttempt, &QPushButton::clicked,
            this, &PaneUpdate::resetAttempt);
    connect(ui->textEditPrompt, &QTextEdit::textChanged,
            this, &PaneUpdate::_onPromptEdited);
}

void PaneUpdate::_onPromptEdited()
{
    if (m_updatingPrompt || !m_isSetup) {
        return;
    }
    const QModelIndex current = ui->treeViewStrategies->currentIndex();
    if (!current.isValid() || m_strategies->isStrategy(current)) {
        return;
    }
    m_strategies->setInstructions(current, ui->textEditPrompt->toPlainText());
}

void PaneUpdate::_saveCurrentPrompt()
{
    if (!m_isSetup) {
        return;
    }
    const QModelIndex current = ui->treeViewStrategies->currentIndex();
    if (!current.isValid() || m_strategies->isStrategy(current)) {
        return;
    }
    m_strategies->setInstructions(current, ui->textEditPrompt->toPlainText());
}

QString PaneUpdate::_currentStrategyId() const
{
    const QModelIndex current = ui->treeViewStrategies->currentIndex();
    if (!current.isValid()) {
        return {};
    }
    if (m_strategies->isStrategy(current)) {
        return m_strategies->nodeId(current);
    }
    // Prompt node: return its parent strategy id.
    const QModelIndex parent = current.parent();
    return parent.isValid() ? m_strategies->nodeId(parent) : QString{};
}

QString PaneUpdate::_currentPromptId() const
{
    const QModelIndex current = ui->treeViewStrategies->currentIndex();
    if (!current.isValid() || m_strategies->isStrategy(current)) {
        return {};
    }
    return m_strategies->nodeId(current);
}
