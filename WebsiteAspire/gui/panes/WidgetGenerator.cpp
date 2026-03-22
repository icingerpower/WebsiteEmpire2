#include "WidgetGenerator.h"
#include "ui_WidgetGenerator.h"

#include <QClipboard>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <QException>
#include <QFileDialog>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>

#include "generator/ParamDelegate.h"
#include "aspire/generator/ParamsModel.h"

#include "aspire/downloader/DownloadedPagesTable.h"
#include "aspire/generator/AbstractGenerator.h"
#include "launcher/AbstractLauncher.h"
#include "launcher/ClaudeRunner.h"
#include "launcher/LancherGenerator.h"
#include "launcher/LauncherRunJobs.h"
#include "workingdirectory/WorkingDirectoryManager.h"

WidgetGenerator::WidgetGenerator(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::WidgetGenerator)
{
    ui->setupUi(this);

    ui->buttonGetJobs->setEnabled(false);
    ui->buttonCopyJobs->setEnabled(false);
    ui->buttonRecordReply->setEnabled(false);
    ui->buttonCopyCommand->setEnabled(false);
    ui->buttonRun->setEnabled(false);
    ui->tableViewParams->hide();
    ui->labelParamError->hide();
    ui->splitterResults->hide();

    connect(ui->buttonGetJobs,     &QPushButton::clicked,  this, &WidgetGenerator::getJobs);
    connect(ui->buttonCopyJobs,    &QPushButton::clicked,  this, &WidgetGenerator::copyJobs);
    connect(ui->buttonRecordReply, &QPushButton::clicked,  this, &WidgetGenerator::recordReply);
    connect(ui->buttonCopyCommand, &QPushButton::clicked,  this, &WidgetGenerator::copyCommand);
    connect(ui->buttonRun,         &QPushButton::toggled,  this, &WidgetGenerator::_onRunToggled);
}

void WidgetGenerator::init(AbstractGenerator *generator)
{
    m_generator = generator;

    ui->buttonGetJobs->setEnabled(true);
    ui->buttonCopyJobs->setEnabled(true);
    ui->buttonRecordReply->setEnabled(true);
    ui->buttonCopyCommand->setEnabled(true);
    ui->buttonRun->setEnabled(true);

    // ---- Sessions spinbox: load persisted value ----------------------------
    QSettings settings;
    const int savedSessions = settings.value(_sessionsSettingsKey(), 1).toInt();
    ui->spinBoxSessions->setValue(qBound(1, savedSessions, 10));

    connect(ui->spinBoxSessions, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
                QSettings s;
                s.setValue(_sessionsSettingsKey(), v);
            });

    // ---- Parameter table ---------------------------------------------------
    const QList<AbstractGenerator::Param> params = m_generator->getParams();
    if (!params.isEmpty()) {
        auto *model    = new ParamsModel(m_generator, this);
        auto *delegate = new ParamDelegate(this);

        ui->tableViewParams->setModel(model);
        ui->tableViewParams->setItemDelegate(delegate);
        ui->tableViewParams->verticalHeader()->hide();
        ui->tableViewParams->horizontalHeader()->setStretchLastSection(true);
        ui->tableViewParams->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->tableViewParams->setEditTriggers(
            QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

        const int headerH = ui->tableViewParams->horizontalHeader()->sizeHint().height();
        const int rowH    = ui->tableViewParams->verticalHeader()->defaultSectionSize();
        ui->tableViewParams->setFixedHeight(headerH + rowH * params.size() + 4);
        ui->tableViewParams->show();

        connect(ui->tableViewParams, &QTableView::doubleClicked,
                this, &WidgetGenerator::_onParamDoubleClicked);
        connect(model, &ParamsModel::paramChanged,
                this, &WidgetGenerator::_onParamChanged);
    }

    // ---- Results tables ----------------------------------------------------
    const auto tablesList = m_generator->openResultsTables();
    if (!tablesList.isEmpty()) {
        for (const auto &pair : tablesList) {
            ui->listWidgetResultTables->addItem(pair.first);

            auto *page   = new QWidget(ui->stackedWidgetResults);
            auto *layout = new QVBoxLayout(page);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(2);

            auto *countLabel = new QLabel(page);
            layout->addWidget(countLabel);

            auto *tableView = new QTableView(page);
            tableView->setModel(pair.second);
            tableView->setEditTriggers(QAbstractItemView::DoubleClicked);
            tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
            tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
            tableView->horizontalHeader()->setStretchLastSection(true);
            tableView->verticalHeader()->hide();
            layout->addWidget(tableView);

            QAbstractItemModel *model = pair.second;
            auto updateCount = [countLabel, model]() {
                countLabel->setText(WidgetGenerator::tr("%1 entries").arg(model->rowCount()));
            };
            updateCount();
            connect(model, &QAbstractItemModel::rowsInserted,  countLabel, updateCount);
            connect(model, &QAbstractItemModel::rowsRemoved,   countLabel, updateCount);
            connect(model, &QAbstractItemModel::modelReset,    countLabel, updateCount);
            connect(model, &QAbstractItemModel::layoutChanged, countLabel, updateCount);

            ui->stackedWidgetResults->addWidget(page);
        }

        ui->listWidgetResultTables->setCurrentRow(0);
        ui->listWidgetResultTables->setVisible(tablesList.size() > 1);

        connect(ui->listWidgetResultTables, &QListWidget::currentRowChanged,
                ui->stackedWidgetResults,   &QStackedWidget::setCurrentIndex);

        ui->splitterResults->show();
    }

    _validateParams();
    _updateStats();
}

WidgetGenerator::~WidgetGenerator()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// getJobs
// ---------------------------------------------------------------------------

void WidgetGenerator::getJobs()
{
    if (!m_generator) {
        return;
    }

    const QString paramError = m_generator->checkParams(m_generator->currentParams());
    if (!paramError.isEmpty()) {
        ui->labelLog->setText(tr("Cannot get jobs: %1").arg(paramError));
        return;
    }

    const int count = ui->spinBoxJobCount->value();
    QJsonArray jobs;
    for (int i = 0; i < count; ++i) {
        const QString jobStr = m_generator->getNextJob();
        if (jobStr.isEmpty()) {
            break;
        }
        jobs << QJsonDocument::fromJson(jobStr.toUtf8()).object();
    }

    if (jobs.isEmpty()) {
        ui->labelLog->setText(tr("No more pending jobs."));
        _updateStats();
        return;
    }

    ui->plainTextEditJobs->setPlainText(
        QString::fromUtf8(QJsonDocument(jobs).toJson(QJsonDocument::Indented)));
    ui->labelLog->setText(
        tr("Retrieved %1 job(s). Paste into Claude, then paste the reply below.")
            .arg(jobs.size()));
    _updateStats();
}

// ---------------------------------------------------------------------------
// copyJobs
// ---------------------------------------------------------------------------

void WidgetGenerator::copyJobs()
{
    const QString text = ui->plainTextEditJobs->toPlainText();
    if (text.isEmpty()) {
        return;
    }
    QGuiApplication::clipboard()->setText(text);
    ui->labelLog->setText(tr("Jobs copied to clipboard."));
}

// ---------------------------------------------------------------------------
// recordReply
// ---------------------------------------------------------------------------

void WidgetGenerator::recordReply()
{
    if (!m_generator) {
        return;
    }

    const QString replyText = ui->plainTextEditReply->toPlainText().trimmed();
    if (replyText.isEmpty()) {
        QMessageBox::warning(this,
                             tr("No Reply"),
                             tr("Please paste Claude's reply before recording."));
        return;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(replyText.toUtf8(), &parseErr);

    QList<QJsonObject> replies;
    if (doc.isArray()) {
        for (const QJsonValue &val : doc.array()) {
            if (val.isObject()) {
                replies << val.toObject();
            }
        }
    } else if (doc.isObject()) {
        replies << doc.object();
    } else {
        ui->labelLog->setText(tr("FAILURE: %1").arg(parseErr.errorString()));
        return;
    }

    int successCount = 0;
    int failCount    = 0;
    QStringList failReasons;

    for (const QJsonObject &reply : std::as_const(replies)) {
        const QString replyStr =
            QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact));
        try {
            if (m_generator->recordReply(replyStr)) {
                ++successCount;
            } else {
                ++failCount;
                failReasons << tr("Unknown jobId or malformed reply.");
            }
        } catch (const QException &ex) {
            ++failCount;
            failReasons << QString::fromUtf8(ex.what());
        }
    }

    if (failCount == 0) {
        ui->plainTextEditReply->clear();
        ui->labelLog->setText(tr("SUCCESS: %1 reply(ies) recorded.").arg(successCount));
    } else {
        ui->labelLog->setText(
            tr("Recorded %1, failed %2: %3")
                .arg(successCount)
                .arg(failCount)
                .arg(failReasons.join(QStringLiteral("; "))));
    }

    _updateStats();
}

// ---------------------------------------------------------------------------
// copyCommand
// ---------------------------------------------------------------------------

void WidgetGenerator::copyCommand()
{
    if (!m_generator) {
        return;
    }

    const QString workingDir  = WorkingDirectoryManager::instance()->workingDir().path();
    const QString generatorId = m_generator->getId();

    const QString base =
        QStringLiteral("WebsiteAspire --%1 \"%2\" --%3 %4")
            .arg(AbstractLauncher::OPTION_WORKING_DIR,
                 workingDir,
                 LancherGenerator::OPTION_NAME,
                 generatorId);

    const QString runBase =
        QStringLiteral("WebsiteAspire --%1 \"%2\" --%3 %4")
            .arg(AbstractLauncher::OPTION_WORKING_DIR,
                 workingDir,
                 LauncherRunJobs::OPTION_NAME,
                 generatorId);

    QMessageBox::information(
        this,
        tr("Command Line Usage"),
        tr("Get the next pending job(s):\n\n%1\n\n"
           "Record Claude's filled reply:\n\n%2\n\n"
           "Run jobs automatically (N sessions, Ctrl+C to stop gracefully):\n\n%3")
            .arg(base + QStringLiteral(" --getjob 10"),
                 base + QStringLiteral(" --recordjob '<filled-json>'"),
                 runBase + QStringLiteral(" --%1 %2")
                     .arg(LauncherRunJobs::OPTION_SESSIONS)
                     .arg(ui->spinBoxSessions->value())));
}

// ---------------------------------------------------------------------------
// Params helpers
// ---------------------------------------------------------------------------

void WidgetGenerator::_onParamDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid() || index.column() != 1) {
        return;
    }

    const bool isFile   = index.data(ParamsModel::IsFileRole).toBool();
    const bool isFolder = index.data(ParamsModel::IsFolderRole).toBool();

    if (!isFile && !isFolder) {
        return;
    }

    const QString current = index.data(Qt::EditRole).toString();
    QString path;
    if (isFile) {
        path = QFileDialog::getOpenFileName(this, tr("Select File"), current);
    } else {
        path = QFileDialog::getExistingDirectory(this, tr("Select Folder"), current);
    }

    if (!path.isEmpty()) {
        ui->tableViewParams->model()->setData(index, path, Qt::EditRole);
    }
}

void WidgetGenerator::_onParamChanged()
{
    _validateParams();
    _updateStats();
}

void WidgetGenerator::_validateParams()
{
    if (!m_generator) {
        return;
    }
    const QString error = m_generator->checkParams(m_generator->currentParams());
    ui->labelParamError->setText(error);
    ui->labelParamError->setVisible(!error.isEmpty());
}

void WidgetGenerator::_updateStats()
{
    if (!m_generator) {
        return;
    }
    const int total   = m_generator->getAllJobIds().size();
    const int pending = m_generator->pendingCount();
    ui->labelStats->setText(tr("%1 pending / %2 initial").arg(pending).arg(total));
}

// ---------------------------------------------------------------------------
// Auto-run
// ---------------------------------------------------------------------------

void WidgetGenerator::_onRunToggled(bool checked)
{
    if (checked) {
        const QString paramError = m_generator->checkParams(m_generator->currentParams());
        if (!paramError.isEmpty()) {
            ui->labelLog->setText(tr("Cannot run: %1").arg(paramError));
            // Uncheck without re-triggering this slot.
            QSignalBlocker blocker(ui->buttonRun);
            ui->buttonRun->setChecked(false);
            return;
        }

        m_stopRequested      = false;
        m_activeSessionCount = ui->spinBoxSessions->value();
        m_jobsDone           = 0;
        m_jobsFailed         = 0;
        _setRunningUi(true);

        for (int i = 0; i < m_activeSessionCount; ++i) {
            // Fire-and-forget: QCoro keeps the coroutine alive via the awaitable
            // (qCoro(process).waitForFinished) registered with the Qt event loop.
            _runSession(i);
        }
    } else {
        // User clicked Run again → request stop after current job(s).
        m_stopRequested = true;
        ui->labelLog->setText(tr("Stopping after current job(s) finish..."));
    }
}

QCoro::Task<void> WidgetGenerator::_runSession(int sessionIndex)
{
    const QDir logDir(m_generator->workingDir().filePath(QStringLiteral("claude_logs")));
    const int  sNum = sessionIndex + 1; // 1-based for display

    while (!m_stopRequested) {
        const QString jobJson = m_generator->getNextJob();
        if (jobJson.isEmpty()) {
            break;
        }

        // Extract jobId for display and logging.
        const QJsonDocument jobDoc = QJsonDocument::fromJson(jobJson.toUtf8());
        const QString jobId = jobDoc.object().value(QStringLiteral("jobId")).toString();

        ui->labelLog->setText(
            tr("[S%1] Running: %2 (%3 pending)")
                .arg(QString::number(sNum), jobId, QString::number(m_generator->pendingCount())));

        const ClaudeJobResult result = co_await runClaudeJob(jobJson);
        writeClaudeJobLog(logDir, jobId, result);

        if (!result.processStarted) {
            // 'claude' executable not found — no point retrying any job.
            ui->labelLog->setText(
                tr("[S%1] FATAL: 'claude' not found in PATH. Logs: claude_logs/").arg(sNum));
            ++m_jobsFailed;
            break;
        }

        if (result.json.isEmpty()) {
            // Claude ran but produced no usable JSON. Log and try the next job.
            // The failed job remains in pending and will be retried on the next run.
            ui->labelLog->setText(
                tr("[S%1] FAIL: %2 — exit %3, no JSON (%4 ms). See claude_logs/")
                    .arg(QString::number(sNum),
                         jobId,
                         QString::number(result.exitCode),
                         QString::number(result.durationMs)));
            ++m_jobsFailed;
            continue;
        }

        try {
            if (m_generator->recordReply(result.json)) {
                ++m_jobsDone;
                ui->labelLog->setText(
                    tr("[S%1] OK: %2 (%3 ms) | done: %4, failed: %5")
                        .arg(QString::number(sNum),
                             jobId,
                             QString::number(result.durationMs),
                             QString::number(m_jobsDone),
                             QString::number(m_jobsFailed)));
                _updateStats();
            } else {
                ++m_jobsFailed;
                ui->labelLog->setText(
                    tr("[S%1] FAIL: %2 — unknown jobId or malformed reply (%3 ms)")
                        .arg(QString::number(sNum), jobId, QString::number(result.durationMs)));
            }
        } catch (const QException &ex) {
            ++m_jobsFailed;
            ui->labelLog->setText(
                tr("[S%1] FAIL: %2 — %3")
                    .arg(QString::number(sNum), jobId, QString::fromUtf8(ex.what())));
        }
    }

    // Schedule UI reset after this coroutine frame exits — using a queued
    // call avoids touching the task list while still inside the coroutine.
    if (--m_activeSessionCount == 0) {
        QTimer::singleShot(0, this, &WidgetGenerator::_onAllSessionsDone);
    }
}

void WidgetGenerator::_onAllSessionsDone()
{
    _setRunningUi(false);
    _updateStats();

    const QString summary =
        tr("done: %1, failed: %2").arg(QString::number(m_jobsDone), QString::number(m_jobsFailed));

    if (m_stopRequested) {
        ui->labelLog->setText(tr("Auto-run stopped. %1").arg(summary));
    } else {
        ui->labelLog->setText(tr("Auto-run finished — no more pending jobs. %1").arg(summary));
    }
    m_stopRequested = false;
}

void WidgetGenerator::_setRunningUi(bool running)
{
    ui->buttonGetJobs->setEnabled(!running);
    ui->buttonCopyJobs->setEnabled(!running);
    ui->buttonRecordReply->setEnabled(!running);
    ui->buttonCopyCommand->setEnabled(!running);
    ui->spinBoxJobCount->setEnabled(!running);
    ui->spinBoxSessions->setEnabled(!running);
    ui->tableViewParams->setEnabled(!running);

    if (!running) {
        // Uncheck the button without re-triggering _onRunToggled.
        QSignalBlocker blocker(ui->buttonRun);
        ui->buttonRun->setChecked(false);
    }
}

QString WidgetGenerator::_sessionsSettingsKey() const
{
    return QStringLiteral("WidgetGenerator/%1/sessions")
        .arg(m_generator ? m_generator->getId() : QStringLiteral("default"));
}
