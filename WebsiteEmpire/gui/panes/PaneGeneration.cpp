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
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableWidgetItem>
#include <QTextEdit>

// Mirrors the slug logic in LauncherGeneration: lower-case, collapse non-alnum to '-',
// strip leading/trailing dashes.  Must stay in sync with the launcher.
static QString makeTopicSlug(const QString &name)
{
    static const QRegularExpression s_reNonAlnum(QStringLiteral("[^a-z0-9]+"));
    QString slug = name.toLower();
    slug.replace(s_reNonAlnum, QStringLiteral("-"));
    while (slug.startsWith(QLatin1Char('-'))) { slug.remove(0, 1); }
    while (slug.endsWith(QLatin1Char('-')))   { slug.chop(1); }
    return slug;
}

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
    _saveCurrentPrompt(); // persist any unsaved edits before ui is torn down
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
    if (!visible) {
        _saveCurrentPrompt();
    }
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

    ui->progressBarGeneration->setVisible(true);
    ui->buttonGenOne->setEnabled(false);

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
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
                m_activeProcess = nullptr;
                ui->progressBarGeneration->setVisible(false);
                ui->buttonGenOne->setEnabled(true);
                const QString statusMsg = (exitStatus == QProcess::CrashExit)
                    ? tr("\nProcess CRASHED (signal %1) — run the generation command in a "
                         "terminal for a backtrace.").arg(exitCode)
                    : tr("\nProcess finished (exit code %1).").arg(exitCode);
                ui->textEditOutput->append(statusMsg);
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
                process->deleteLater();
            });
    process->start(exe, args);
}

void PaneGeneration::viewGenCommand()
{
    if (!m_isSetup) {
        return;
    }

    const QModelIndex idx = ui->tableViewStrategies->currentIndex();
    if (!idx.isValid()) {
        return;
    }
    const QString strategyId = m_strategies->idForRow(idx.row());

    const QString exe     = QCoreApplication::applicationFilePath();
    const QString workDir = m_workingDir.absolutePath();
    const QString cmd = QStringLiteral("%1 --%2 \"%3\" --%4 --%5 %6 --%7 1")
                            .arg(exe,
                                 AbstractLauncher::OPTION_WORKING_DIR,
                                 workDir,
                                 LauncherGeneration::OPTION_NAME,
                                 LauncherGeneration::OPTION_STRATEGY,
                                 strategyId,
                                 LauncherGeneration::OPTION_LIMIT);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Generation command"));
    msgBox.setText(tr("Run this command in a terminal to generate one page for the selected strategy:"));
    msgBox.setDetailedText(cmd);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
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

        int done   = 0;
        int nTotal = pageRepo.countByTypeId(typeId);

        const QString dbPath = _resolvedDbPath(row);
        if (!dbPath.isEmpty()) {
            // DB is linked: read all topic names, build expected permalink set,
            // then count only generated pages whose permalink matches a topic.
            // This ensures manually-added pages that have no corresponding DB topic
            // are never counted as "done".
            const QString conn = QStringLiteral("pane_count_row_") + QString::number(row);
            QSet<QString> expectedPermalinks;
            int dbCount = 0;
            {
                QSqlDatabase db =
                    QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
                db.setDatabaseName(dbPath);
                if (db.open()) {
                    QSqlQuery countQ(db);
                    if (countQ.exec(QStringLiteral("SELECT COUNT(*) FROM records"))
                        && countQ.next()) {
                        dbCount = countQ.value(0).toInt();
                    }

                    // Detect the first non-id column (the topic name field).
                    QString nameColumn;
                    QSqlQuery pragmaQ(db);
                    if (pragmaQ.exec(QStringLiteral("PRAGMA table_info(records)"))) {
                        while (pragmaQ.next()) {
                            const QString col = pragmaQ.value(1).toString();
                            if (col != QStringLiteral("id") && nameColumn.isEmpty()) {
                                nameColumn = col;
                            }
                        }
                    }
                    if (!nameColumn.isEmpty()) {
                        QSqlQuery topicQ(db);
                        topicQ.exec(QStringLiteral("SELECT ") + nameColumn
                                    + QStringLiteral(" FROM records"));
                        while (topicQ.next()) {
                            const QString name = topicQ.value(0).toString().trimmed();
                            if (!name.isEmpty()) {
                                const QString slug = makeTopicSlug(name);
                                if (!slug.isEmpty()) {
                                    expectedPermalinks.insert(
                                        QLatin1Char('/') + slug);
                                }
                            }
                        }
                    }
                }
            }
            QSqlDatabase::removeDatabase(conn);

            nTotal = dbCount;
            done   = pageRepo.countGeneratedMatchingPermalinks(typeId,
                                                                expectedPermalinks);
        } else {
            // No DB linked: count all AI-generated pages of this type.
            done = pageRepo.findGeneratedByTypeId(typeId).size();
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

    const QString label = primaryAttrId.isEmpty()
                            ? tr("Link source database")
                            : tr("Link source database for '%1'").arg(primaryAttrId);
    const QString dbPath = QFileDialog::getOpenFileName(
        this,
        label,
        m_workingDir.absolutePath(),
        tr("SQLite databases (*.db)"));
    if (dbPath.isEmpty()) {
        return;
    }

    // Store as a path relative to the working directory so it survives Dropbox
    // sync to a machine with a different absolute path prefix.
    const QString storedPath = m_workingDir.relativeFilePath(dbPath);
    m_strategies->setPrimaryDbPath(row, storedPath);
    computeRemainingToDo();
    _onStrategySelectionChanged(current, QModelIndex{});
}

// ---- Private slots ----------------------------------------------------------

void PaneGeneration::_onStrategySelectionChanged(const QModelIndex &current,
                                                  const QModelIndex &previous)
{
    // Persist any edits the user made to the prompt before switching rows.
    if (previous.isValid() && m_isSetup) {
        m_strategies->setCustomInstructions(previous.row(),
                                             ui->textEditPrompt->toPlainText());
    }

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
    const QString resolvedDb = _resolvedDbPath(row);
    // Generation buttons require a linked DB only when a source table is configured.
    const bool dbReady = !needsDb || !resolvedDb.isEmpty();

    ui->buttonGenOne->setEnabled(dbReady);
    ui->buttonCommandGen->setEnabled(dbReady);
    ui->buttonLinkDb->setEnabled(true); // always allow picking/changing the linked DB

    m_updatingPrompt = true;
    ui->textEditPrompt->setPlainText(m_strategies->customInstructionsForRow(row));
    m_updatingPrompt = false;

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
    {
        // Always show the DB path row so the user knows whether one is linked.
        const QString storedRaw = m_strategies->primaryDbPathForRow(row);
        if (resolvedDb.isEmpty()) {
            const bool mustLink = needsDb; // generation blocked until linked
            paramRows.append({ tr("DB path"),
                               storedRaw.isEmpty()
                                   ? tr("Not linked — click \"Link to DB\"")
                                   : tr("Not found: %1").arg(storedRaw),
                               mustLink });
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
    connect(ui->textEditPrompt,
            &QTextEdit::textChanged,
            this,
            &PaneGeneration::_onPromptEdited);
}

void PaneGeneration::_onPromptEdited()
{
    if (m_updatingPrompt || !m_isSetup) {
        return;
    }
    const QModelIndex current = ui->tableViewStrategies->currentIndex();
    if (!current.isValid()) {
        return;
    }
    m_strategies->setCustomInstructions(current.row(),
                                         ui->textEditPrompt->toPlainText());
}

void PaneGeneration::_saveCurrentPrompt()
{
    if (!m_isSetup) {
        return;
    }
    const QModelIndex current = ui->tableViewStrategies->currentIndex();
    if (!current.isValid()) {
        return;
    }
    m_strategies->setCustomInstructions(current.row(),
                                         ui->textEditPrompt->toPlainText());
}

QString PaneGeneration::_resolvedDbPath(int row) const
{
    // 1. Standard convention: results_db/<primaryAttrId>.db inside working directory.
    //    Only applicable when a source table is configured.
    const QString primaryAttrId = m_strategies->primaryAttrIdForRow(row);
    if (!primaryAttrId.isEmpty()) {
        const QString stdPath = m_workingDir.filePath(
            QStringLiteral("results_db/") + primaryAttrId + QStringLiteral(".db"));
        if (QFile::exists(stdPath)) {
            return stdPath;
        }
    }

    // 2. Stored path — may be relative (new, portable format) or absolute (legacy).
    //    Relative paths are resolved against the working directory so that a
    //    Dropbox-synced working directory opens correctly on any machine.
    const QString stored = m_strategies->primaryDbPathForRow(row);
    if (!stored.isEmpty()) {
        if (QFile::exists(stored)) {
            return QDir::cleanPath(stored); // absolute path that still resolves
        }
        const QString resolved = m_workingDir.absoluteFilePath(stored);
        if (QFile::exists(resolved)) {
            return QDir::cleanPath(resolved); // relative path resolved, .. removed
        }
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
