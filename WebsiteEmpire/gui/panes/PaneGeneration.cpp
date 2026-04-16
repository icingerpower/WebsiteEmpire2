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
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

PaneGeneration::PaneGeneration(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneGeneration)
{
    ui->setupUi(this);
    _connectSlots();
}

PaneGeneration::~PaneGeneration()
{
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

    m_domain  = _primaryDomain(engine, settingsTable);
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

    ui->textEditPrompt->clear();
    ui->textEditPrompt->append(tr("Running: %1 %2\n").arg(exe, args.join(QLatin1Char(' '))));

    auto *process = new QProcess(this);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        const QString text = QString::fromUtf8(process->readAllStandardOutput());
        ui->textEditPrompt->insertPlainText(text);
        // Parse "[S…] OK: <permalink>" lines to remember what was generated.
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
        ui->textEditPrompt->insertPlainText(text);
    });
    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus) {
                ui->textEditPrompt->append(
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
    ui->textEditPrompt->setPlainText(cmd);
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

        // N Done: pages already generated in pages.db
        const int total   = pageRepo.countByTypeId(typeId);
        const int pending = pageRepo.findPendingByTypeId(typeId).size();
        const int done    = total - pending;

        // N Total: rows in the aspire primary table (source data available).
        // Falls back to the pages.db total when no source table is linked.
        const QString primaryAttrId = m_strategies->primaryAttrIdForRow(row);
        int nTotal = total;
        if (!primaryAttrId.isEmpty()) {
            const QString dbPath = m_workingDir.filePath(
                QStringLiteral("results_db/") + primaryAttrId + QStringLiteral(".db"));
            if (QFile::exists(dbPath)) {
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
                } // db and q destroyed here — SQLite lock released
                QSqlDatabase::removeDatabase(conn);
                nTotal = dbCount;
            }
        }

        m_strategies->setNDone(row, done);
        m_strategies->setNTotal(row, nTotal);
    }
}

// ---- Private slots ----------------------------------------------------------

void PaneGeneration::_onStrategySelectionChanged(const QModelIndex &current,
                                                  const QModelIndex & /*previous*/)
{
    const bool hasSelection = current.isValid();
    ui->buttonGenOne->setEnabled(hasSelection);
    ui->buttonCommandGen->setEnabled(hasSelection);
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
}

QString PaneGeneration::_primaryDomain(AbstractEngine       *engine,
                                        WebsiteSettingsTable *settingsTable) const
{
    if (!engine || !settingsTable || engine->rowCount() == 0) {
        return {};
    }

    const QString editingLang = settingsTable->editingLangCode();

    // Prefer the domain matching the editing language; fall back to the first row.
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

    // Strip trailing slash so we can safely prepend the permalink's leading slash.
    if (domain.endsWith(QLatin1Char('/'))) {
        domain.chop(1);
    }
    return domain;
}
