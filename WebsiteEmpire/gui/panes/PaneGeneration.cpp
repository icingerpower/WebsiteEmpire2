#include "PaneGeneration.h"
#include "ui_PaneGeneration.h"

#include "GenStrategyTable.h"
#include "../dialogs/DialogAddGeneration.h"
#include "launcher/LauncherGeneration.h"
#include "website/AbstractEngine.h"
#include "website/WebsiteSettingsTable.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageRepositoryDb.h"
#include "workingdirectory/WorkingDirectoryManager.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableWidgetItem>

PaneGeneration::PaneGeneration(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneGeneration)
{
    ui->setupUi(this);
    _connectSlots();
}

PaneGeneration::~PaneGeneration()
{
    if (m_activeProcess) {
        m_activeProcess->disconnect();
    }
    delete ui;
}

void PaneGeneration::setup(const QDir           &workingDir,
                            AbstractEngine       *engine,
                            WebsiteSettingsTable *settingsTable)
{
    m_workingDir = workingDir;
    m_engine     = engine;
    m_strategies = new GenStrategyTable(workingDir, this);
    ui->tableViewStrategies->setModel(m_strategies);

    connect(ui->tableViewStrategies->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            &PaneGeneration::_onStrategySelectionChanged);

    m_domain      = _primaryDomain(engine, settingsTable);
    m_editingLang = (settingsTable && !settingsTable->editingLangCode().isEmpty())
                        ? settingsTable->editingLangCode()
                        : QStringLiteral("en");
    m_isSetup = true;
}

void PaneGeneration::setVisible(bool visible)
{
    QWidget::setVisible(visible);
}

// ---- Slots ------------------------------------------------------------------

void PaneGeneration::addGeneration()
{
    if (!m_isSetup) {
        return;
    }
    DialogAddGeneration dialog(m_engine, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_strategies->addRow(dialog.name(), dialog.pageTypeId(), dialog.themeId(),
                         dialog.customInstructions(), dialog.nonSvgImages(),
                         dialog.primaryAttrId());
}

void PaneGeneration::removeGeneration()
{
    if (!m_isSetup) {
        return;
    }
    const QModelIndex current = ui->tableViewStrategies->currentIndex();
    if (!current.isValid()) {
        return;
    }
    m_strategies->removeRows(current.row(), 1);
}

void PaneGeneration::generateOne()
{
    if (!m_isSetup) {
        return;
    }

    const QString exe = QCoreApplication::applicationFilePath();
    const QStringList args = {
        QStringLiteral("--") + AbstractLauncher::OPTION_WORKING_DIR,
        m_workingDir.absolutePath(),
        QStringLiteral("--") + LauncherGeneration::OPTION_NAME,
        QStringLiteral("--") + LauncherGeneration::OPTION_SESSIONS,
        QStringLiteral("1"),
        QStringLiteral("--") + LauncherGeneration::OPTION_LIMIT,
        QStringLiteral("1")
    };

    m_lastOkPermalink.clear();

    ui->textEditOutput->clear();
    ui->textEditOutput->append(tr("Running: %1 %2\n").arg(exe, args.join(QLatin1Char(' '))));

    auto *process = new QProcess(this);
    m_activeProcess = process;
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        const QString text = QString::fromUtf8(process->readAllStandardOutput());
        ui->textEditOutput->insertPlainText(text);
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const int pos = line.indexOf(QStringLiteral("] OK: "));
            if (pos != -1) {
                m_lastOkPermalink = line.mid(pos + 6).trimmed();
            }
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process]() {
        const QString text = QString::fromUtf8(process->readAllStandardError());
        ui->textEditOutput->insertPlainText(text);
    });
    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus) {
                m_activeProcess = nullptr;
                ui->textEditOutput->append(
                    tr("\nProcess finished (exit code %1).").arg(exitCode));
                computeRemainingToDo();

                if (exitCode == 0 && !m_lastOkPermalink.isEmpty()) {
                    const QString location = m_domain.isEmpty()
                        ? m_lastOkPermalink
                        : m_domain + m_lastOkPermalink;
                    QMessageBox::information(
                        this,
                        tr("Page Generated"),
                        tr("One page was generated successfully.\n\nURL:\n%1")
                            .arg(location));
                }
            });
    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            process,
            &QProcess::deleteLater);

    process->start(exe, args);
}

void PaneGeneration::viewGenCommand()
{
    if (!m_isSetup) {
        return;
    }
    const QString exe     = QCoreApplication::applicationFilePath();
    const QString workDir = m_workingDir.absolutePath();
    const QString cmd = QStringLiteral("%1 --%2 \"%3\" --%4 --%5 3")
                            .arg(exe,
                                 AbstractLauncher::OPTION_WORKING_DIR,
                                 workDir,
                                 LauncherGeneration::OPTION_NAME,
                                 LauncherGeneration::OPTION_SESSIONS);
    ui->textEditOutput->setPlainText(cmd);
}

void PaneGeneration::computeRemainingToDo()
{
    if (!m_isSetup) {
        return;
    }

    PageDb           pageDb(m_workingDir);
    PageRepositoryDb pageRepo(pageDb);

    const int rowCount = m_strategies->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        const QString typeId = m_strategies->data(
            m_strategies->index(row, GenStrategyTable::COL_PAGE_TYPE)).toString();

        const int total   = pageRepo.countByTypeId(typeId);
        const int pending = pageRepo.findPendingByTypeId(typeId).size();
        const int done    = total - pending;

        // N Total: count from aspire DB when linked; falls back to pages.db count.
        int nTotal = total;
        const QString primaryAttrId = m_strategies->primaryAttrIdForRow(row);
        if (!primaryAttrId.isEmpty()) {
            const QString dbPath = _resolvedDbPath(row);
            if (!dbPath.isEmpty()) {
                const QString conn = QStringLiteral("pane_count_")
                                   + primaryAttrId
                                   + QStringLiteral("_")
                                   + QString::number(row);
                int dbCount = 0;
                {
                    QSqlDatabase db =
                        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
                    db.setDatabaseName(dbPath);
                    if (db.open()) {
                        QSqlQuery q(db);
                        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM records"))
                            && q.next()) {
                            dbCount = q.value(0).toInt();
                        }
                    }
                }
                QSqlDatabase::removeDatabase(conn);
                nTotal = dbCount;
            }
        }

        m_strategies->setNDone(row, done);
        m_strategies->setNTotal(row, nTotal);
    }
}

void PaneGeneration::linkDb()
{
    if (!m_isSetup) {
        return;
    }
    const QModelIndex current = ui->tableViewStrategies->currentIndex();
    if (!current.isValid()) {
        return;
    }
    const int row = current.row();
    const QString primaryAttrId = m_strategies->primaryAttrIdForRow(row);
    if (primaryAttrId.isEmpty()) {
        return;
    }

    const QString dbPath = QFileDialog::getOpenFileName(
        this,
        tr("Link source database for '%1'").arg(primaryAttrId),
        QString{},
        tr("SQLite databases (*.db)"));
    if (dbPath.isEmpty()) {
        return;
    }

    m_strategies->setPrimaryDbPath(row, dbPath);
    computeRemainingToDo();
    _onStrategySelectionChanged(current, QModelIndex{});
}

// ---- Private slots ----------------------------------------------------------

void PaneGeneration::_onStrategySelectionChanged(const QModelIndex &current,
                                                  const QModelIndex & /*previous*/)
{
    const bool hasSelection = current.isValid();

    if (!hasSelection) {
        ui->buttonGenOne->setEnabled(false);
        ui->buttonCommandGen->setEnabled(false);
        ui->buttonLinkDb->setEnabled(false);
        ui->textEditPrompt->clear();
        ui->tableWidgetStrategyParams->setRowCount(0);
        return;
    }

    const int row = current.row();
    const QString primaryAttrId = m_strategies->primaryAttrIdForRow(row);
    const bool needsDb       = !primaryAttrId.isEmpty();
    const QString resolvedDb = needsDb ? _resolvedDbPath(row) : QString{};
    const bool dbReady       = !needsDb || !resolvedDb.isEmpty();

    ui->buttonGenOne->setEnabled(dbReady);
    ui->buttonCommandGen->setEnabled(dbReady);
    ui->buttonLinkDb->setEnabled(needsDb);

    ui->textEditPrompt->setPlainText(m_strategies->customInstructionsForRow(row));

    // Build parameter rows dynamically so we can add the DB path when needed.
    struct ParamRow { QString label; QString value; bool warn = false; };
    QList<ParamRow> paramRows;

    const auto colVal = [&](int c) {
        return m_strategies->data(m_strategies->index(row, c)).toString();
    };

    paramRows.append({ tr("Name"),         colVal(GenStrategyTable::COL_NAME) });
    paramRows.append({ tr("Page type"),    colVal(GenStrategyTable::COL_PAGE_TYPE) });
    paramRows.append({ tr("Theme"),        colVal(GenStrategyTable::COL_THEME) });
    paramRows.append({ tr("Images"),       colVal(GenStrategyTable::COL_NON_SVG_IMAGES) });
    paramRows.append({ tr("Source table"), colVal(GenStrategyTable::COL_PRIMARY_ATTR_ID) });
    paramRows.append({ tr("Priority"),     colVal(GenStrategyTable::COL_PRIORITY) });
    {
        const QString done  = colVal(GenStrategyTable::COL_N_DONE);
        const QString total = colVal(GenStrategyTable::COL_N_TOTAL);
        paramRows.append({ tr("Done / Total"), done + QStringLiteral(" / ") + total });
    }
    if (needsDb) {
        if (resolvedDb.isEmpty()) {
            paramRows.append({ tr("DB path"),
                               tr("Not linked — click \"Link to DB\""),
                               true });
        } else {
            paramRows.append({ tr("DB path"), resolvedDb });
        }
    }

    auto *tw = ui->tableWidgetStrategyParams;
    tw->setRowCount(paramRows.size());

    for (int i = 0; i < paramRows.size(); ++i) {
        const auto &p = paramRows.at(i);
        tw->setItem(i, 0, new QTableWidgetItem(p.label));
        auto *valItem = new QTableWidgetItem(p.value);
        if (p.warn) {
            valItem->setForeground(Qt::red);
        }
        tw->setItem(i, 1, valItem);
    }
    tw->resizeColumnsToContents();
    tw->horizontalHeader()->setStretchLastSection(true);
}

// ---- Private ----------------------------------------------------------------

void PaneGeneration::_connectSlots()
{
    connect(ui->buttonAdd,
            &QPushButton::clicked,
            this,
            &PaneGeneration::addGeneration);
    connect(ui->buttonRemove,
            &QPushButton::clicked,
            this,
            &PaneGeneration::removeGeneration);
    connect(ui->buttonGenOne,
            &QPushButton::clicked,
            this,
            &PaneGeneration::generateOne);
    connect(ui->buttonCommandGen,
            &QPushButton::clicked,
            this,
            &PaneGeneration::viewGenCommand);
    connect(ui->buttonComputeRemaining,
            &QPushButton::clicked,
            this,
            &PaneGeneration::computeRemainingToDo);
    connect(ui->buttonLinkDb,
            &QPushButton::clicked,
            this,
            &PaneGeneration::linkDb);
}

QString PaneGeneration::_resolvedDbPath(int row) const
{
    const QString primaryAttrId = m_strategies->primaryAttrIdForRow(row);
    if (primaryAttrId.isEmpty()) {
        return {};
    }
    const QString stdPath = m_workingDir.filePath(
        QStringLiteral("results_db/") + primaryAttrId + QStringLiteral(".db"));
    if (QFile::exists(stdPath)) {
        return stdPath;
    }
    const QString stored = m_strategies->primaryDbPathForRow(row);
    if (!stored.isEmpty() && QFile::exists(stored)) {
        return stored;
    }
    return {};
}

QString PaneGeneration::_primaryDomain(AbstractEngine       *engine,
                                        WebsiteSettingsTable *settingsTable) const
{
    if (!engine || !settingsTable || engine->rowCount() == 0) {
        return {};
    }

    const QString editingLang = settingsTable->editingLangCode();

    QString domain;
    for (int i = 0; i < engine->rowCount(); ++i) {
        const QString lang = engine->data(
            engine->index(i, AbstractEngine::COL_LANG_CODE)).toString();
        if (lang == editingLang) {
            domain = engine->data(
                engine->index(i, AbstractEngine::COL_DOMAIN)).toString();
            break;
        }
    }
    if (domain.isEmpty()) {
        domain = engine->data(
            engine->index(0, AbstractEngine::COL_DOMAIN)).toString();
    }

    if (domain.endsWith(QLatin1Char('/'))) {
        domain.chop(1);
    }
    return domain;
}
