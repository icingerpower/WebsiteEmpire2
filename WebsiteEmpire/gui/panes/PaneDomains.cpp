#include "PaneDomains.h"
#include "ui_PaneDomains.h"

#include "../dialogs/DialogEditHosts.h"
#include "../dialogs/DialogShowCommand.h"
#include "../dialogs/DialogTransferLog.h"
#include "website/perf/StatsDbMerger.h"
#include "website/AbstractEngine.h"
#include "website/HostTable.h"
#include "website/WebsiteSettingsTable.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/CategoryHubDirtySet.h"
#include "website/pages/CategoryHubSyncer.h"
#include "website/pages/SymptomHubSyncer.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRepositoryDb.h"
#include "website/translation/TranslationStatusTable.h"
#include "ExceptionWithTitleText.h"

#include <QAbstractItemView>
#include <QSet>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QSettings>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QUrl>

// ---- HostComboDelegate ------------------------------------------------------
// Displays a QComboBox populated with available host names for COL_HOST.

class HostComboDelegate : public QStyledItemDelegate
{
public:
    explicit HostComboDelegate(AbstractEngine *engine, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_engine(engine) {}

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *combo = new QComboBox(parent);
        combo->addItem(QString()); // empty = no host
        const QStringList names = m_engine->availableHostNames();
        for (const auto &name : std::as_const(names)) {
            combo->addItem(name);
        }
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = static_cast<QComboBox *>(editor);
        const QString current = index.data(Qt::DisplayRole).toString();
        const int idx = combo->findText(current);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    void setModelData(QWidget *editor,
                      QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        const auto *combo = static_cast<QComboBox *>(editor);
        model->setData(index, combo->currentText());
    }

private:
    AbstractEngine *m_engine;
};

// ---- PaneDomains ------------------------------------------------------------

PaneDomains::PaneDomains(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneDomains)
{
    ui->setupUi(this);
    _connectSlots();
}

PaneDomains::~PaneDomains()
{
    delete ui;
}

// ---- Public -----------------------------------------------------------------

void PaneDomains::setEngine(AbstractEngine *engine)
{
    m_engine = engine;
    ui->tableViewEngine->setModel(engine);
    ui->buttonApplyPerLang->setVisible(engine && engine->getVariations().size() > 1);
    if (engine) {
        ui->tableViewEngine->setItemDelegateForColumn(
            AbstractEngine::COL_HOST, new HostComboDelegate(engine, ui->tableViewEngine));
    }
}

void PaneDomains::setHostTable(HostTable *hostTable)
{
    m_hostTable = hostTable;
}

void PaneDomains::setWorkingDir(const QDir &workingDir)
{
    m_workingDir = workingDir;
    QSettings settings;
    const QString saved = settings.value(QLatin1String(SETTINGS_KEY_DEPLOY_PATH)).toString();
    if (!saved.isEmpty()) {
        ui->lineEditPathLocalDeploy->setText(saved);
    } else {
        ui->lineEditPathLocalDeploy->setText(workingDir.filePath(QStringLiteral("deploy")));
    }
}

// ---- Slots ------------------------------------------------------------------

void PaneDomains::apply()
{
    if (!m_engine) {
        return;
    }
    const QModelIndex current = ui->tableViewEngine->currentIndex();
    if (!current.isValid()) {
        QMessageBox::warning(this, tr("No selection"), tr("Please select a cell first."));
        return;
    }
    const int col = current.column();
    const QString tmpl = ui->lineEdit->text();
    const int rows = m_engine->rowCount();
    for (int row = 0; row < rows; ++row) {
        const QString lang  = m_engine->data(m_engine->index(row, AbstractEngine::COL_LANG_CODE)).toString();
        const QString theme = m_engine->data(m_engine->index(row, AbstractEngine::COL_THEME)).toString();
        m_engine->setData(m_engine->index(row, col), _applyTemplate(tmpl, lang, theme));
    }
}

void PaneDomains::applyPerLang()
{
    if (!m_engine) {
        return;
    }
    const QModelIndex current = ui->tableViewEngine->currentIndex();
    if (!current.isValid()) {
        QMessageBox::warning(this, tr("No selection"), tr("Please select a cell first."));
        return;
    }
    const int col = current.column();
    const QString selectedLang = m_engine->data(
        m_engine->index(current.row(), AbstractEngine::COL_LANG_CODE)).toString();
    const QString tmpl = ui->lineEdit->text();
    const int rows = m_engine->rowCount();
    for (int row = 0; row < rows; ++row) {
        const QString lang = m_engine->data(m_engine->index(row, AbstractEngine::COL_LANG_CODE)).toString();
        if (lang != selectedLang) {
            continue;
        }
        const QString theme = m_engine->data(m_engine->index(row, AbstractEngine::COL_THEME)).toString();
        m_engine->setData(m_engine->index(row, col), _applyTemplate(tmpl, lang, theme));
    }
}

void PaneDomains::editHosts()
{
    if (!m_hostTable) {
        return;
    }
    DialogEditHosts dialog(m_hostTable, this);
    dialog.exec();
}

void PaneDomains::upload()
{
    if (!m_engine || m_engine->rowCount() == 0) {
        QMessageBox::warning(this, tr("Upload"),
                             tr("No engine data available."));
        return;
    }

    const QList<HostInfo> hosts = _resolveHosts();
    if (hosts.isEmpty()) {
        QMessageBox::warning(this, tr("Upload"),
                             tr("No host configured in the engine table."));
        return;
    }

    const bool anyHostNeedsPassword = std::any_of(hosts.begin(), hosts.end(),
        [](const HostInfo &h) { return !h.password.isEmpty(); });
    if (anyHostNeedsPassword && QStandardPaths::findExecutable(QStringLiteral("sshpass")).isEmpty()) {
        QMessageBox::warning(this, tr("Upload"),
                             tr("sshpass is not installed.\n\n"
                                "Install it with:\n"
                                "  sudo apt install sshpass"));
        return;
    }

    // Auto-deploy the flat deployPath/content.db if needed — but only when a
    // resolved host actually has no per-language row (langCodes empty), since
    // that flat file is the only thing such a host reads further down. Every
    // multi-language engine (Health, Healybio, ...) has a language code on
    // every row, so this must never run for them: _deployLocallyImpl() requires
    // a flat workingDir/content.db that per-language engines never produce
    // (they use PageRepositoryDb/pages.db via "Deploy Locally" instead), so it
    // would always throw "content.db not found" for a freshly set up site.
    const bool hasFlatHost = std::any_of(hosts.begin(), hosts.end(),
        [](const HostInfo &h) { return h.langCodes.isEmpty(); });
    if (hasFlatHost && _deployNeeded()) {
        try {
            _deployLocallyImpl();
        } catch (const ExceptionWithTitleText &ex) {
            QMessageBox::critical(this, ex.errorTitle(), ex.errorText());
            return;
        }
    }

    const QString deployPath = _resolveDeployPath();
    const QString imagesDbLocal = m_workingDir.filePath(QStringLiteral("images.db"));
    const bool hasImagesDb = QFile::exists(imagesDbLocal);
    const QStringList qualifying = _qualifyingLangCodes();

    bool anyError = false;
    QStringList skippedLangs;

    // ── Phase 1: upload images.db BEFORE any service restarts ────────────────
    // images.db is shared across all languages on a host and lives ONE level
    // above each per-lang hostFolder (e.g. deploy/images.db, not deploy/en/images.db).
    // Uploading it after the restart (old code) meant the server restarted with
    // the stale file; uploading to hostFolder (old code) put it in the wrong place.
    // We deduplicate so multi-language hosts only upload once.
    if (hasImagesDb) {
        QSet<QString> uploadedImagesTo;
        for (const auto &host : std::as_const(hosts)) {
            const QString lang = host.langCodes.isEmpty() ? QString() : host.langCodes.first();
            if (!lang.isEmpty() && !qualifying.contains(lang)) {
                continue;
            }
            // Parent of the per-lang folder is the shared deploy root on the server.
            const QString remoteParent = QFileInfo(host.hostFolder).path();
            const QString dedupeKey   = host.url + QLatin1Char(':') + remoteParent;
            if (uploadedImagesTo.contains(dedupeKey)) {
                continue;
            }
            uploadedImagesTo.insert(dedupeKey);

            const QString remoteImages = host.username + QStringLiteral("@") + host.url
                                         + QStringLiteral(":") + remoteParent
                                         + QStringLiteral("/images.db");
            QString imgError;
            if (!_runRsync(host, imagesDbLocal, remoteImages, imgError)) {
                QMessageBox::critical(this, tr("Upload"),
                                      tr("Failed to upload images.db to %1:\n%2")
                                          .arg(host.name, imgError));
                anyError = true;
            }
        }
        if (anyError) {
            return;
        }
    }

    // ── Phase 2: upload content.db and restart each language service ──────────
    for (const auto &host : std::as_const(hosts)) {
        // Apply the same qualification gate as deployLocally(): skip languages
        // that don't have enough translated pages or weren't locally generated.
        const QString lang = host.langCodes.isEmpty() ? QString() : host.langCodes.first();
        if (!lang.isEmpty() && !qualifying.contains(lang)) {
            skippedLangs.append(lang);
            continue;
        }

        QString contentDbLocal;
        if (!lang.isEmpty()) {
            const QString perLang = QDir(deployPath).filePath(lang + QStringLiteral("/content.db"));
            if (QFile::exists(perLang)) {
                contentDbLocal = perLang;
            }
        }
        if (contentDbLocal.isEmpty() && lang.isEmpty()) {
            const QString flat = QDir(deployPath).filePath(QStringLiteral("content.db"));
            if (QFile::exists(flat)) {
                contentDbLocal = flat;
            }
        }
        if (contentDbLocal.isEmpty()) {
            skippedLangs.append(lang.isEmpty() ? host.name : lang);
            continue;
        }

        // Refuse to ship a database that is already broken locally.
        QString integrityError;
        if (!_verifyLocalDbIntegrity(contentDbLocal, integrityError)) {
            QMessageBox::critical(this, tr("Upload"),
                                  tr("Refusing to upload content.db for %1 — it failed a local "
                                     "integrity check:\n%2")
                                      .arg(lang.isEmpty() ? host.name : lang, integrityError));
            anyError = true;
            continue;
        }

        // Upload content.db
        const QString remoteContentPath = host.hostFolder + QStringLiteral("/content.db");
        const QString remoteContent = host.username + QStringLiteral("@") + host.url
                                      + QStringLiteral(":") + remoteContentPath;
        QString errorOutput;
        if (!_runRsync(host, contentDbLocal, remoteContent, errorOutput)) {
            QMessageBox::critical(this, tr("Upload"),
                                  tr("Failed to upload content.db to %1:\n%2")
                                      .arg(host.name, errorOutput));
            anyError = true;
            continue;
        }

        // Verify the file that landed on the server is actually intact before
        // restarting the service on top of it — a torn transfer would otherwise
        // go live silently with the old process still holding a stale (but valid)
        // copy open. Pass the plain remote path (no "user@host:" prefix) since
        // this runs inside an SSH session already connected to that host.
        QString remoteIntegrityError;
        if (!_verifyRemoteDbIntegrity(host, remoteContentPath, remoteIntegrityError)) {
            QMessageBox::critical(this, tr("Upload"),
                                  tr("Uploaded content.db to %1 but the remote copy failed "
                                     "integrity check — NOT restarting %2, the previous "
                                     "version keeps serving:\n%3")
                                      .arg(host.name, _siteServiceName(host, lang), remoteIntegrityError));
            anyError = true;
            continue;
        }

        // Restart the systemd service for the language just deployed.
        // Kill any manually-started StaticWebsiteServe first — a rogue process
        // holding the port causes systemctl restart to silently fail, leaving
        // the old binary (and its stale content.db) serving requests.
        if (!lang.isEmpty()) {
            // Anchor the kill to this site's deploy root (via its --images-db
            // argument) as well as the language, so a rogue process belonging
            // to a *different* site sharing the same VPS and language code
            // (e.g. two engines both deploying "fr") is never killed.
            const QString siteRoot = QFileInfo(host.hostFolder).path();
            const QString killCmd = QStringLiteral("pkill -f 'StaticWebsiteServe.*--lang ")
                                    + lang + QStringLiteral(".*") + siteRoot
                                    + QStringLiteral("' 2>/dev/null; true");
            QString killError;
            _runSshCommand(host, killCmd, killError); // best-effort; ignore failure

            const QString serviceName = _siteServiceName(host, lang);
            const QString restartCmd = QStringLiteral("systemctl restart ") + serviceName;
            QString restartError;
            if (!_runSshCommand(host, restartCmd, restartError)) {
                QMessageBox::warning(this, tr("Upload"),
                                     tr("Uploaded %1 to %2 but failed to restart %3:\n%4")
                                         .arg(lang, host.name, serviceName, restartError));
            }
        }
    }

    if (!anyError) {
        QString msg = tr("Upload completed.");
        if (!skippedLangs.isEmpty()) {
            msg += QStringLiteral("\n\n")
                   + tr("Skipped (no local content generated): %1")
                         .arg(skippedLangs.join(QStringLiteral(", ")));
        }
        QMessageBox::information(this, tr("Upload"), msg);
    }
}

void PaneDomains::uploadFull()
{
    if (!m_engine || m_engine->rowCount() == 0) {
        QMessageBox::warning(this, tr("Upload Full"),
                             tr("No engine data available."));
        return;
    }

    const QList<HostInfo> hosts = _resolveHosts();
    if (hosts.isEmpty()) {
        QMessageBox::warning(this, tr("Upload Full"),
                             tr("No host configured in the engine table."));
        return;
    }

    // Locate the local StaticWebsiteServe binary (Release build preferred).
    const QString appDir = QCoreApplication::applicationDirPath();
    QString localBinary;
    const QStringList candidates = {
        appDir + QStringLiteral("/StaticWebsiteServe"),
        appDir + QStringLiteral("/../StaticWebsiteServe/StaticWebsiteServe"),
        appDir + QStringLiteral("/../../build/StaticWebsiteServe/StaticWebsiteServe"),
        appDir + QStringLiteral("/../../StaticWebsiteServe/StaticWebsiteServe"),
    };
    for (const QString &c : std::as_const(candidates)) {
        if (QFile::exists(c)) {
            localBinary = QDir::cleanPath(c);
            break;
        }
    }
    if (localBinary.isEmpty()) {
        QMessageBox::critical(this, tr("Upload Full"),
                              tr("Could not find the StaticWebsiteServe binary.\n\n"
                                 "Build it first (Release configuration), then try again."));
        return;
    }

    const bool anyHostNeedsPassword = std::any_of(hosts.begin(), hosts.end(),
        [](const HostInfo &h) { return !h.password.isEmpty(); });
    if (anyHostNeedsPassword && QStandardPaths::findExecutable(QStringLiteral("sshpass")).isEmpty()) {
        QMessageBox::warning(this, tr("Upload Full"),
                             tr("sshpass is not installed.\n\n"
                                "Install it with:\n"
                                "  sudo apt install sshpass"));
        return;
    }

    // ── Phase 1: upload binary to each unique host ────────────────────────────
    // Binary lives at the grandparent of hostFolder:
    //   hostFolder   = /opt/websiteempire/deploy/en
    //   parent       = /opt/websiteempire/deploy      (images.db)
    //   grandparent  = /opt/websiteempire              (StaticWebsiteServe)
    QSet<QString> binaryUploadedTo;
    for (const auto &host : std::as_const(hosts)) {
        const QString remoteGrandparent =
            QFileInfo(QFileInfo(host.hostFolder).path()).path();
        const QString dedupeKey = host.url + QLatin1Char(':') + remoteGrandparent;
        if (binaryUploadedTo.contains(dedupeKey)) {
            continue;
        }
        binaryUploadedTo.insert(dedupeKey);

        const QString remoteBinaryPath = remoteGrandparent + QStringLiteral("/StaticWebsiteServe");
        const QString remoteBinary = host.username + QStringLiteral("@") + host.url
                                     + QStringLiteral(":") + remoteBinaryPath;
        QString binaryError;
        if (!_runRsync(host, localBinary, remoteBinary, binaryError)) {
            QMessageBox::critical(this, tr("Upload Full"),
                                  tr("Failed to upload StaticWebsiteServe to %1:\n%2")
                                      .arg(host.name, binaryError));
            return;
        }

        // Make executable and stop all running instances of THIS SITE'S binary
        // only — they must be down before the new binary starts so the ports
        // are free. Match the exact binary path (not just the bare executable
        // name) so a second site sharing this VPS but living at a different
        // deploy root (a different remoteGrandparent) is never killed.
        const QString prepCmd =
            QStringLiteral("chmod +x ") + remoteBinaryPath
            + QStringLiteral(" && pkill -f '") + remoteBinaryPath
            + QStringLiteral("' 2>/dev/null; true");
        QString prepError;
        _runSshCommand(host, prepCmd, prepError); // best-effort
    }

    // ── Phase 2: upload images.db (before any restarts) ──────────────────────
    const QString imagesDbLocal = m_workingDir.filePath(QStringLiteral("images.db"));
    if (QFile::exists(imagesDbLocal)) {
        QSet<QString> imagesUploadedTo;
        for (const auto &host : std::as_const(hosts)) {
            const QString remoteParent = QFileInfo(host.hostFolder).path();
            const QString dedupeKey   = host.url + QLatin1Char(':') + remoteParent;
            if (imagesUploadedTo.contains(dedupeKey)) {
                continue;
            }
            imagesUploadedTo.insert(dedupeKey);

            const QString remoteImages = host.username + QStringLiteral("@") + host.url
                                         + QStringLiteral(":") + remoteParent
                                         + QStringLiteral("/images.db");
            QString imgError;
            if (!_runRsync(host, imagesDbLocal, remoteImages, imgError)) {
                QMessageBox::critical(this, tr("Upload Full"),
                                      tr("Failed to upload images.db to %1:\n%2")
                                          .arg(host.name, imgError));
                return;
            }
        }
    }

    // ── Phase 3: upload content.db and restart each language service ──────────
    const QString deployPath     = _resolveDeployPath();
    const QStringList qualifying = _qualifyingLangCodes();
    bool anyError = false;

    for (const auto &host : std::as_const(hosts)) {
        const QString lang = host.langCodes.isEmpty() ? QString() : host.langCodes.first();
        if (!lang.isEmpty() && !qualifying.contains(lang)) {
            continue;
        }

        QString contentDbLocal;
        if (!lang.isEmpty()) {
            const QString perLang = QDir(deployPath).filePath(lang + QStringLiteral("/content.db"));
            if (QFile::exists(perLang)) {
                contentDbLocal = perLang;
            }
        }
        if (contentDbLocal.isEmpty()) {
            continue;
        }

        QString integrityError;
        if (!_verifyLocalDbIntegrity(contentDbLocal, integrityError)) {
            QMessageBox::critical(this, tr("Upload Full"),
                                  tr("Refusing to upload content.db for %1 — it failed a local "
                                     "integrity check:\n%2")
                                      .arg(lang.isEmpty() ? host.name : lang, integrityError));
            anyError = true;
            continue;
        }

        const QString remoteContentPath = host.hostFolder + QStringLiteral("/content.db");
        const QString remoteContent = host.username + QStringLiteral("@") + host.url
                                      + QStringLiteral(":") + remoteContentPath;
        QString contentError;
        if (!_runRsync(host, contentDbLocal, remoteContent, contentError)) {
            QMessageBox::critical(this, tr("Upload Full"),
                                  tr("Failed to upload content.db to %1:\n%2")
                                      .arg(host.name, contentError));
            anyError = true;
            continue;
        }

        QString remoteIntegrityError;
        if (!_verifyRemoteDbIntegrity(host, remoteContentPath, remoteIntegrityError)) {
            QMessageBox::critical(this, tr("Upload Full"),
                                  tr("Uploaded content.db to %1 but the remote copy failed "
                                     "integrity check — NOT restarting %2, the previous "
                                     "version keeps serving:\n%3")
                                      .arg(host.name, _siteServiceName(host, lang), remoteIntegrityError));
            anyError = true;
            continue;
        }

        if (!lang.isEmpty()) {
            const QString serviceName = _siteServiceName(host, lang);
            const QString restartCmd = QStringLiteral("systemctl restart ") + serviceName;
            QString restartError;
            if (!_runSshCommand(host, restartCmd, restartError)) {
                QMessageBox::warning(this, tr("Upload Full"),
                                     tr("Uploaded content to %1 but failed to restart %2:\n%3")
                                         .arg(host.name, serviceName, restartError));
            }
        }
    }

    if (!anyError) {
        QMessageBox::information(this, tr("Upload Full"),
                                 tr("Full upload completed: binary, images.db, and all content deployed."));
    }
}

void PaneDomains::download()
{
    if (!m_engine || m_engine->rowCount() == 0) {
        QMessageBox::warning(this, tr("Download"),
                             tr("No engine data available."));
        return;
    }

    const QList<HostInfo> hosts = _resolveHosts();
    if (hosts.isEmpty()) {
        QMessageBox::warning(this, tr("Download"),
                             tr("No host configured in the engine table."));
        return;
    }

    const bool anyHostNeedsPassword = std::any_of(hosts.begin(), hosts.end(),
        [](const HostInfo &h) { return !h.password.isEmpty(); });
    if (anyHostNeedsPassword && QStandardPaths::findExecutable(QStringLiteral("sshpass")).isEmpty()) {
        QMessageBox::warning(this, tr("Download"),
                             tr("sshpass is not installed.\n\n"
                                "Install it with:\n"
                                "  sudo apt install sshpass"));
        return;
    }

    // Each Drogon instance runs inside deploy/<lang>/ on the server and writes
    // its own stats.db there, so there is one remote file per host row (per
    // language).  Pull each into a scratch folder, then rebuild the single
    // local stats.db that all consumers read (PagesStatsWidget,
    // StatsDbDataSource, PageBlocHubGrid, CategoryHubSyncer) by merging them —
    // the old code rsynced every language onto the same local file, so only
    // the last language survived.
    DialogTransferLog dialog(tr("Download Statistics"), this);
    dialog.setTotalSteps(hosts.size() + 1); // +1 for the merge step
    dialog.show();

    const QString scratchName = QStringLiteral("stats_download");
    QDir scratchDir(m_workingDir.filePath(scratchName));
    scratchDir.removeRecursively();
    if (!m_workingDir.mkpath(scratchName)) {
        dialog.appendLine(tr("Cannot create scratch folder %1 — aborting.")
                              .arg(scratchDir.absolutePath()));
        dialog.markFinished(false);
        dialog.exec();
        return;
    }

    QStringList downloadedDbs;
    bool anyError = false;
    int  hostIndex = 0;

    for (const auto &host : std::as_const(hosts)) {
        ++hostIndex;
        const QString &lang  = QFileInfo(host.hostFolder).fileName();
        const QString  label = host.name + QStringLiteral(" [") + lang + QStringLiteral("]");
        const QString  remotePath = host.hostFolder + QStringLiteral("/stats.db");

        // Best effort: fold rows still sitting in stats.db-wal into stats.db
        // before pulling it — rsync only transfers the main file.  Fails
        // harmlessly (missing folder, missing sqlite3) for undeployed langs.
        dialog.appendLine(tr("%1: checkpointing WAL on the server…").arg(label));
        QString checkpointError;
        _runSshCommand(host,
                       QStringLiteral("sqlite3 '") + remotePath
                           + QStringLiteral("' 'PRAGMA wal_checkpoint(TRUNCATE);'"),
                       checkpointError);

        const QString localFile = scratchDir.filePath(
            QStringLiteral("stats-%1-%2.db").arg(QString::number(hostIndex), lang));
        const QString remoteStats = host.username + QStringLiteral("@") + host.url
                                    + QStringLiteral(":") + remotePath;

        dialog.appendLine(tr("%1: downloading %2…").arg(label, remotePath));
        QString authMode;
        QString output;
        int     exitCode = -1;
        if (!_execRsync(host, remoteStats, localFile, authMode, output, exitCode, true)) {
            dialog.appendLine(tr("%1: rsync timed out. Auth: %2").arg(label, authMode));
            anyError = true;
        } else if (exitCode == 0) {
            const QFileInfo downloadedInfo(localFile);
            dialog.appendLine(tr("%1: OK (%2 bytes)").arg(label).arg(downloadedInfo.size()));
            downloadedDbs.append(localFile);
        } else if (output.contains(QStringLiteral("No such file or directory"))) {
            dialog.appendLine(tr("%1: skipped — no stats.db on the server "
                                 "(language not deployed yet).").arg(label));
        } else {
            dialog.appendLine(tr("%1: FAILED (rsync exit code %2)").arg(label).arg(exitCode));
            dialog.appendLine(tr("Auth: %1\n%2").arg(authMode, output));
            anyError = true;
        }
        dialog.advanceStep();
    }

    if (downloadedDbs.isEmpty()) {
        dialog.appendLine(tr("Nothing was downloaded — the local stats.db was left untouched."));
        dialog.markFinished(!anyError);
        dialog.exec();
        return;
    }

    dialog.appendLine(tr("Merging %n downloaded database(s) into stats.db…",
                         nullptr, static_cast<int>(downloadedDbs.size())));
    try {
        const StatsDbMerger::MergeResult merged = StatsDbMerger::merge(
            downloadedDbs, m_workingDir.filePath(QStringLiteral("stats.db")));
        dialog.appendLine(tr("Merged %1 display/click rows and %2 session rows.")
                              .arg(merged.displaysClicks)
                              .arg(merged.pageSessions));
        scratchDir.removeRecursively();
    } catch (const ExceptionWithTitleText &ex) {
        dialog.appendLine(ex.errorTitle() + QStringLiteral(": ") + ex.errorText());
        anyError = true;
    }
    dialog.advanceStep();
    dialog.markFinished(!anyError);
    dialog.exec();
}

void PaneDomains::viewCommands()
{
    // TODO later
}

void PaneDomains::browseLocalDeployFolder()
{
    QSettings settings;
    const QString lastPath = settings.value(QLatin1String(SETTINGS_KEY_DEPLOY_PATH),
                                            _resolveDeployPath()).toString();
    const QString chosen = QFileDialog::getExistingDirectory(
        this, tr("Select local deploy folder"), lastPath);
    if (chosen.isEmpty()) {
        return;
    }
    ui->lineEditPathLocalDeploy->setText(chosen);
    settings.setValue(QLatin1String(SETTINGS_KEY_DEPLOY_PATH), chosen);
}

void PaneDomains::deployLocally()
{
    try {
        if (!m_engine || m_engine->rowCount() == 0) {
            ExceptionWithTitleText ex(tr("Generate & Publish"),
                                      tr("No engine configured. Set up at least one domain row first."));
            ex.raise();
            return;
        }

        // ── Determine qualifying languages ────────────────────────────────────
        const QStringList qualifying = _qualifyingLangCodes();

        struct LangTarget {
            int     engineIndex;
            QString lang;
            QString domain;
            int     port;
            QString hostFolder;
        };

        QList<LangTarget> targets;
        QSet<QString>     seenLangs;
        int portOffset = 0;
        const int rows = m_engine->rowCount();
        for (int i = 0; i < rows; ++i) {
            const QString lang = m_engine->getLangCode(i);
            if (lang.isEmpty() || seenLangs.contains(lang) || !qualifying.contains(lang)) {
                continue;
            }
            seenLangs.insert(lang);
            const QString domain = m_engine->data(
                m_engine->index(i, AbstractEngine::COL_DOMAIN)).toString().trimmed();
            if (domain.isEmpty()) {
                ExceptionWithTitleText ex(tr("Generate & Publish"),
                                          tr("Domain is empty for language '%1'.\n"
                                             "Set the domain (e.g. example.com) in the Domains tab before publishing.")
                                             .arg(lang));
                ex.raise();
                return;
            }
            const QString hostFolder = m_engine->data(
                m_engine->index(i, AbstractEngine::COL_HOST_FOLDER)).toString().trimmed();
            targets.append({i, lang, domain, 8080 + portOffset, hostFolder});
            ++portOffset;
        }

        if (targets.isEmpty()) {
            ExceptionWithTitleText ex(tr("Generate & Publish"),
                                      tr("No language has enough translated pages yet.\n"
                                         "Create or translate at least 10 pages before publishing."));
            ex.raise();
            return;
        }

        // ── Generate each qualifying language directly into its deploy dir ──────
        PageDb           pageDb(m_workingDir);
        PageRepositoryDb pageRepo(pageDb);
        CategoryTable    categoryTable(m_workingDir);
        PageGenerator       generator(pageRepo, categoryTable);
        {
            WebsiteSettingsTable siteSettings(m_workingDir);
            generator.setWebsiteContext(siteSettings.websiteName(), siteSettings.author());
        }
        CategoryHubDirtySet hubDirtySet(m_workingDir);
        CategoryHubSyncer   hubSyncer(pageRepo, categoryTable, hubDirtySet, generator);
        hubSyncer.syncStubs(m_engine->getLangCode(0));
        hubSyncer.markStaleByStats(m_workingDir);
        SymptomHubSyncer    symptomSyncer(pageRepo);
        symptomSyncer.syncStubs(m_workingDir, m_engine->getLangCode(0));

        // ── Locate StaticWebsiteServe binary ─────────────────────────────────
        const QString appDir = QCoreApplication::applicationDirPath();
        QString binaryPath = QStringLiteral("StaticWebsiteServe");
        const QStringList candidates = {
            appDir + QStringLiteral("/StaticWebsiteServe"),
            appDir + QStringLiteral("/../StaticWebsiteServe/StaticWebsiteServe"),
            appDir + QStringLiteral("/../../build/StaticWebsiteServe/StaticWebsiteServe"),
        };
        for (const QString &c : std::as_const(candidates)) {
            if (QFile::exists(c)) {
                binaryPath = QDir::cleanPath(c);
                break;
            }
        }

        // Kill all StaticWebsiteServe processes unconditionally — old deploys
        // from any directory (including stale servers) must release their ports.
        const QString deployBase = _resolveDeployPath();
        QProcess killer;
        killer.start(QStringLiteral("bash"),
                     {QStringLiteral("-c"),
                      QStringLiteral("pkill -f StaticWebsiteServe 2>/dev/null; true")});
        killer.waitForFinished(3000);
        QThread::msleep(500);

        // Copy images.db once to the deploy base — shared across all languages.
        const QString srcImages    = m_workingDir.filePath(QStringLiteral("images.db"));
        const QString sharedImages = QDir(deployBase).filePath(QStringLiteral("images.db"));
        if (!QFile::exists(srcImages)) {
            ExceptionWithTitleText ex(tr("Generate & Publish"),
                                      tr("images.db not found in the working directory:\n%1\n\n"
                                         "No images would be served — aborting before generating pages.")
                                         .arg(srcImages));
            ex.raise();
            return;
        }
        if (!QDir().mkpath(deployBase)) {
            ExceptionWithTitleText ex(tr("Generate & Publish"),
                                      tr("Could not create deploy folder:\n%1").arg(deployBase));
            ex.raise();
            return;
        }
        if (QFile::exists(sharedImages) && !QFile::remove(sharedImages)) {
            ExceptionWithTitleText ex(tr("Generate & Publish"),
                                      tr("Could not remove the previous images.db before copying "
                                         "the new one:\n%1").arg(sharedImages));
            ex.raise();
            return;
        }
        if (!QFile::copy(srcImages, sharedImages)) {
            ExceptionWithTitleText ex(tr("Generate & Publish"),
                                      tr("Failed to copy images.db to the deploy folder:\n%1\n\n"
                                         "Pages would generate but NO IMAGES would be served — "
                                         "aborting before generating pages.")
                                         .arg(sharedImages));
            ex.raise();
            return;
        }

        int totalPages = 0;

        // The language served at the domain root (no URL prefix) is whatever
        // WebsiteSettingsTable says is the editing/source language — NOT
        // m_engine->getLangCode(0). Row 0 is just whichever target language
        // happens to sort first in the table (AbstractEngine::_reconcileRows()
        // always appends the editing language rather than prepending it), so
        // comparing against row 0 silently prefixes the root language's own
        // sitemap/page URLs with its own lang code, breaking every URL in it.
        QString primaryLang = WebsiteSettingsTable(m_workingDir).editingLangCode();
        if (primaryLang.isEmpty()) {
            primaryLang = QStringLiteral("en");
        }

        QStringList urls;
        QStringList langSummaries;
        for (const LangTarget &t : std::as_const(targets)) {
            const QString destDir = QDir(deployBase).filePath(t.lang);

            if (!QDir().mkpath(destDir)) {
                qWarning() << "deployLocally: could not create" << destDir;
                continue;
            }

            // Remove stale content.db (and WAL/SHM) so generation starts fresh.
            const QDir ddir(destDir);
            QFile::remove(ddir.filePath(QStringLiteral("content.db-wal")));
            QFile::remove(ddir.filePath(QStringLiteral("content.db-shm")));
            QFile::remove(ddir.filePath(QStringLiteral("content.db")));

            const QString sitemapBase = QStringLiteral("https://") + t.domain
                + (t.lang == primaryLang ? QString{} : QStringLiteral("/") + t.lang);
            const int langPages = generator.generateAll(m_workingDir, ddir, t.domain, *m_engine, t.engineIndex, sitemapBase);
            totalPages += langPages;

            _restartLocalDrogon(destDir, binaryPath, t.port, sharedImages);

            if (langPages > 0) {
                urls.append(QStringLiteral("http://localhost:%1/index.html  [%2]")
                               .arg(t.port).arg(t.lang));
                langSummaries.append(QStringLiteral("[%1] %2 page(s)").arg(t.lang).arg(langPages));
            }
        }

        hubDirtySet.clear();
        pageRepo.markAllCompleteAsPublished();

        // ── Success dialog ────────────────────────────────────────────────────
        const QString urlList  = urls.join(QStringLiteral("\n"));
        const QString firstUrl = QStringLiteral("http://localhost:%1/index.html")
                                     .arg(targets.first().port);

        QStringList serviceNames;
        for (const auto &t : std::as_const(targets)) {
            const QString slug = t.hostFolder.isEmpty() ? QString() : _siteSlug(t.hostFolder);
            serviceNames.append(QStringLiteral("website-")
                                 + (slug.isEmpty() ? QString() : slug + QStringLiteral("-")) + t.lang);
        }
        const QString restartCmd = QStringLiteral("systemctl restart ") + serviceNames.join(QLatin1Char(' '));

        const QString msg = tr("Generated %1 page(s):\n%2\n\nServing:\n%3\n\nTo deploy on server:\n%4")
                               .arg(totalPages)
                               .arg(langSummaries.join(QStringLiteral("\n")))
                               .arg(urlList)
                               .arg(restartCmd);

        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Generate & Publish"));
        msgBox.setText(msg);
        QPushButton *openBtn       = msgBox.addButton(tr("Open"),            QMessageBox::ActionRole);
        QPushButton *copyUrlBtn    = msgBox.addButton(tr("Copy URL"),        QMessageBox::ActionRole);
        QPushButton *copyPathBtn   = msgBox.addButton(tr("Copy path"),       QMessageBox::ActionRole);
        QPushButton *copyCmdBtn    = msgBox.addButton(tr("Copy restart cmd"), QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.exec();

        if (msgBox.clickedButton() == openBtn) {
            QDesktopServices::openUrl(QUrl(firstUrl));
        } else if (msgBox.clickedButton() == copyUrlBtn) {
            QGuiApplication::clipboard()->setText(firstUrl);
        } else if (msgBox.clickedButton() == copyPathBtn) {
            QGuiApplication::clipboard()->setText(deployBase);
        } else if (msgBox.clickedButton() == copyCmdBtn) {
            QGuiApplication::clipboard()->setText(restartCmd);
        }

    } catch (const ExceptionWithTitleText &ex) {
        QMessageBox::critical(this, ex.errorTitle(), ex.errorText());
    }
}

void PaneDomains::viewSitemaps()
{
    if (!m_engine || m_engine->rowCount() == 0) {
        QMessageBox::information(this, tr("View sitemaps"),
                                 tr("No engine configured. Set up at least one domain row first."));
        return;
    }

    // Same qualification gate as deployLocally()/upload(): a language must
    // have enough translated pages AND already have a locally-generated
    // content.db — otherwise its sitemap.xml isn't actually live yet.
    const QStringList qualifying = _qualifyingLangCodes();
    const QString deployPath = _resolveDeployPath();

    QString primaryLang = WebsiteSettingsTable(m_workingDir).editingLangCode();
    if (primaryLang.isEmpty()) {
        primaryLang = QStringLiteral("en");
    }

    QStringList sitemapUrls;
    QSet<QString> seenLangs;
    const int rows = m_engine->rowCount();
    for (int i = 0; i < rows; ++i) {
        const QString lang = m_engine->getLangCode(i);
        if (lang.isEmpty() || seenLangs.contains(lang) || !qualifying.contains(lang)) {
            continue;
        }
        seenLangs.insert(lang);

        const QString contentDb = QDir(deployPath).filePath(lang + QStringLiteral("/content.db"));
        if (!QFile::exists(contentDb)) {
            continue; // qualifying but never locally generated — no live sitemap
        }

        const QString domain = m_engine->data(
            m_engine->index(i, AbstractEngine::COL_DOMAIN)).toString().trimmed();
        if (domain.isEmpty()) {
            continue;
        }

        const QString sitemapUrl = QStringLiteral("https://") + domain
            + (lang == primaryLang ? QString{} : QStringLiteral("/") + lang)
            + QStringLiteral("/sitemap.xml");
        sitemapUrls.append(sitemapUrl);
    }

    if (sitemapUrls.isEmpty()) {
        QMessageBox::information(this, tr("View sitemaps"),
                                 tr("No language has a locally-generated sitemap yet.\n"
                                    "Run \"Generate & Publish locally\" first."));
        return;
    }

    DialogShowCommand dlg(tr("View sitemaps"),
                          tr("Submit these sitemap URLs to Google Search Console "
                             "(one per language with published pages):"),
                          sitemapUrls.join(QLatin1Char('\n')), this);
    dlg.exec();
}

void PaneDomains::_deployLocallyImpl()
{
    const QString deployPath = _resolveDeployPath();
    if (deployPath.isEmpty()) {
        ExceptionWithTitleText ex(tr("Local deploy"),
                                  tr("No deploy path is set. Please select a folder first."));
        ex.raise();
        return;
    }

    const QDir deployDir(deployPath);
    if (!deployDir.exists()) {
        if (!QDir().mkpath(deployPath)) {
            ExceptionWithTitleText ex(tr("Local deploy"),
                                      tr("Could not create the deploy folder:\n%1").arg(deployPath));
            ex.raise();
            return;
        }
    }

    const QString srcDb  = m_workingDir.filePath(QStringLiteral("content.db"));
    const QString destDb = deployDir.filePath(QStringLiteral("content.db"));

    if (!QFile::exists(srcDb)) {
        ExceptionWithTitleText ex(tr("Local deploy"),
                                  tr("content.db not found in the working directory.\n"
                                     "Generate the pages first."));
        ex.raise();
        return;
    }

    if (QFile::exists(destDb)) {
        QFile::remove(destDb);
    }
    // Remove stale WAL/SHM so the new database opens cleanly without replaying
    // transactions that belong to a previous version of the file.
    QFile::remove(deployDir.filePath(QStringLiteral("content.db-wal")));
    QFile::remove(deployDir.filePath(QStringLiteral("content.db-shm")));

    if (!QFile::copy(srcDb, destDb)) {
        ExceptionWithTitleText ex(tr("Local deploy"),
                                  tr("Failed to copy content.db to:\n%1").arg(destDb));
        ex.raise();
        return;
    }

    // Copy images.db so the serve binary can find image blobs in its cwd.
    const QString srcImages  = m_workingDir.filePath(QStringLiteral("images.db"));
    const QString destImages = deployDir.filePath(QStringLiteral("images.db"));
    if (QFile::exists(srcImages)) {
        if (QFile::exists(destImages)) {
            QFile::remove(destImages);
        }
        QFile::copy(srcImages, destImages); // best-effort; failure won't break HTML pages
    }
}

// ---- Private ----------------------------------------------------------------

void PaneDomains::_connectSlots()
{
    connect(ui->buttonApply,        &QPushButton::clicked, this, &PaneDomains::apply);
    connect(ui->buttonApplyPerLang, &QPushButton::clicked, this, &PaneDomains::applyPerLang);
    connect(ui->buttonEdithosts,    &QPushButton::clicked, this, &PaneDomains::editHosts);
    connect(ui->buttonUpload,       &QPushButton::clicked, this, &PaneDomains::upload);
    connect(ui->buttonUploadFull,   &QPushButton::clicked, this, &PaneDomains::uploadFull);
    connect(ui->buttonDownload,     &QPushButton::clicked, this, &PaneDomains::download);
    connect(ui->buttonViewCommands, &QPushButton::clicked, this, &PaneDomains::viewCommands);
    connect(ui->buttonBrowseLocally, &QPushButton::clicked, this, &PaneDomains::browseLocalDeployFolder);
    connect(ui->buttonDeployLocally, &QPushButton::clicked, this, &PaneDomains::deployLocally);
    connect(ui->buttonViewSitemaps,  &QPushButton::clicked, this, &PaneDomains::viewSitemaps);
}

QString PaneDomains::_resolveDeployPath() const
{
    const QString fromUi = ui->lineEditPathLocalDeploy->text().trimmed();
    if (!fromUi.isEmpty()) {
        return fromUi;
    }
    return m_workingDir.filePath(QStringLiteral("deploy"));
}

QString PaneDomains::_applyTemplate(const QString &tmpl,
                                    const QString &lang,
                                    const QString &theme) const
{
    QString result = tmpl;
    result.replace(QStringLiteral("{LANG}"),  lang);
    result.replace(QStringLiteral("{THEME}"), theme);
    return result;
}

// ---- HostInfo ---------------------------------------------------------------

QString PaneDomains::HostInfo::uniqueKey() const
{
    return url + QStringLiteral("|") + port + QStringLiteral("|")
           + QString(hostFolder).replace(QLatin1Char('/'), QLatin1Char('|'));
}

// ---- Private helpers --------------------------------------------------------

QList<PaneDomains::HostInfo> PaneDomains::_resolveHosts() const
{
    QList<HostInfo> result;
    if (!m_engine || !m_hostTable) {
        return result;
    }

    QSet<QString> seenKeys;
    const int engineRows = m_engine->rowCount();

    for (int row = 0; row < engineRows; ++row) {
        const QString hostName = m_engine->data(
            m_engine->index(row, AbstractEngine::COL_HOST)).toString().trimmed();
        const QString hostFolder = m_engine->data(
            m_engine->index(row, AbstractEngine::COL_HOST_FOLDER)).toString().trimmed();
        const QString langCode = m_engine->data(
            m_engine->index(row, AbstractEngine::COL_LANG_CODE)).toString().trimmed();

        if (hostName.isEmpty() || hostFolder.isEmpty()) {
            continue;
        }

        // Look up the host in the host table
        int hostRow = -1;
        const int hostTableRows = m_hostTable->rowCount();
        for (int h = 0; h < hostTableRows; ++h) {
            const QString name = m_hostTable->data(
                m_hostTable->index(h, HostTable::COL_NAME)).toString();
            if (name == hostName) {
                hostRow = h;
                break;
            }
        }
        if (hostRow < 0) {
            continue;
        }

        HostInfo info;
        info.name       = hostName;
        info.url        = m_hostTable->data(m_hostTable->index(hostRow, HostTable::COL_URL)).toString();
        info.port       = m_hostTable->data(m_hostTable->index(hostRow, HostTable::COL_PORT)).toString();
        info.username   = m_hostTable->data(m_hostTable->index(hostRow, HostTable::COL_USERNAME)).toString();
        info.password   = m_hostTable->data(m_hostTable->index(hostRow, HostTable::COL_PASSWORD), Qt::EditRole).toString();
        info.hostFolder = hostFolder;

        const QString key = info.uniqueKey();
        if (!seenKeys.contains(key)) {
            seenKeys.insert(key);
            if (!langCode.isEmpty()) {
                info.langCodes.append(langCode);
            }
            result.append(info);
        } else if (!langCode.isEmpty()) {
            for (HostInfo &existing : result) {
                if (existing.uniqueKey() == key && !existing.langCodes.contains(langCode)) {
                    existing.langCodes.append(langCode);
                    break;
                }
            }
        }
    }

    return result;
}

QStringList PaneDomains::_qualifyingLangCodes() const
{
    if (!m_engine) {
        return {};
    }
    PageDb           pageDb(m_workingDir);
    PageRepositoryDb pageRepo(pageDb);
    CategoryTable    categoryTable(m_workingDir);

    QStringList engineLangs;
    const int rows = m_engine->rowCount();
    for (int i = 0; i < rows; ++i) {
        const QString lang = m_engine->getLangCode(i);
        if (!lang.isEmpty() && !engineLangs.contains(lang)) {
            engineLangs.append(lang);
        }
    }

    const QHash<QString, int> counts =
        TranslationStatusTable::countCompletedPerLang(pageRepo, categoryTable, engineLangs);

    QStringList result;
    for (const QString &lang : std::as_const(engineLangs)) {
        if (counts.value(lang, 0) >= 10) {
            result.append(lang);
        }
    }
    return result;
}

bool PaneDomains::_deployNeeded() const
{
    const QString src  = m_workingDir.filePath(QStringLiteral("content.db"));
    const QString dest = _resolveDeployPath() + QStringLiteral("/content.db");
    if (!QFile::exists(dest)) {
        return true;
    }
    const QFileInfo srcInfo(src);
    const QFileInfo destInfo(dest);
    return srcInfo.lastModified() > destInfo.lastModified();
}

void PaneDomains::_restartLocalDrogon(const QString &deployPath,
                                       const QString &binaryPath,
                                       int            port,
                                       const QString &imagesDbPath)
{
    // Kill any StaticWebsiteServe process whose cwd is exactly deployPath.
    // Use pgrep -f (match full command line) because the 18-char name exceeds
    // pgrep's 15-character limit for process-name matching.
    const QString killScript =
        QStringLiteral("for pid in $(pgrep -f StaticWebsiteServe 2>/dev/null); do "
                       "  cwd=$(readlink /proc/$pid/cwd 2>/dev/null); "
                       "  if [ \"$cwd\" = \"") + deployPath +
        QStringLiteral("\" ]; then kill \"$pid\"; fi; done");

    QProcess killer;
    killer.start(QStringLiteral("bash"), {QStringLiteral("-c"), killScript});
    killer.waitForFinished(3000);

    // Brief pause so the port is released before we rebind it.
    QThread::msleep(300);

    QStringList args = {QStringLiteral("--port"), QString::number(port)};
    if (!imagesDbPath.isEmpty()) {
        args << QStringLiteral("--images-db") << imagesDbPath;
    }
    QProcess::startDetached(binaryPath, args, deployPath);
}

bool PaneDomains::_runSshCommand(const HostInfo &host, const QString &command,
                                  QString &errorOutput, QString *output) const
{
    const QString remote = host.username + QStringLiteral("@") + host.url;
    QProcess process;

    if (host.password.isEmpty()) {
        process.start(QStringLiteral("ssh"), {
            QStringLiteral("-p"), host.port,
            QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=no"),
            remote,
            command
        });
    } else {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("SSHPASS"), host.password);
        process.setProcessEnvironment(env);
        process.start(QStringLiteral("sshpass"), {
            QStringLiteral("-e"),
            QStringLiteral("ssh"),
            QStringLiteral("-p"), host.port,
            QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=no"),
            remote,
            command
        });
    }

    if (!process.waitForFinished(30000)) {
        errorOutput = tr("SSH command timed out.");
        return false;
    }
    if (process.exitCode() != 0) {
        errorOutput = QString::fromUtf8(process.readAllStandardError());
        return false;
    }
    if (output) {
        *output = QString::fromUtf8(process.readAllStandardOutput());
    }
    return true;
}

bool PaneDomains::_verifyLocalDbIntegrity(const QString &dbPath, QString &errorOutput) const
{
    const QString connName = QStringLiteral("integrity_check_") + QFileInfo(dbPath).absoluteFilePath();
    QString result;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            errorOutput = tr("Could not open %1 for integrity check: %2")
                              .arg(dbPath, db.lastError().text());
            QSqlDatabase::removeDatabase(connName);
            return false;
        }

        // Fold the WAL into the main file first so the check (and the file that
        // gets rsynced) reflects a fully-settled snapshot, not one with pending
        // frames that a mid-checkpoint copy could tear. TRUNCATE mode only fully
        // merges the WAL when it isn't blocked by another connection's lock — if
        // "busy" comes back non-zero, some committed pages are still stranded in
        // content.db-wal, which _runRsync() never uploads. The subsequent
        // integrity_check below would still report "ok" in that case (a live
        // connection always reads main+WAL transparently), so that check alone
        // cannot catch this — the bare main file that actually gets rsynced would
        // still be missing pages, which is exactly what showed up as "Page N:
        // never used" on the remote copy after upload. So checkpoint success
        // must be verified explicitly here, before trusting the file is complete.
        bool checkpointStuck = false;
        QSqlQuery checkpoint(db);
        if (checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE);")) && checkpoint.next()) {
            const int busy = checkpoint.value(0).toInt();
            if (busy != 0) {
                checkpointStuck = true;
                errorOutput = tr("Could not fully checkpoint the WAL for %1 before upload — "
                                 "another connection is still holding it open (busy=%2). "
                                 "Close any other process using this database (a running local "
                                 "preview server, another WebsiteEmpire instance, etc.) and try again.")
                                  .arg(dbPath).arg(busy);
            }
        }

        if (!checkpointStuck) {
            QSqlQuery check(db);
            if (check.exec(QStringLiteral("PRAGMA integrity_check;")) && check.next()) {
                result = check.value(0).toString();
            } else {
                result = tr("(integrity_check query failed: %1)").arg(check.lastError().text());
            }
        }
    }
    QSqlDatabase::removeDatabase(connName);

    if (!errorOutput.isEmpty()) {
        return false;
    }

    if (result.compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
        errorOutput = tr("Local database %1 failed integrity check:\n%2").arg(dbPath, result);
        return false;
    }
    return true;
}

QString PaneDomains::_siteSlug(const QString &hostFolder) const
{
    // hostFolder = .../<siteRoot>/deploy/<lang> -- strip "<lang>" then "deploy"
    // to reach <siteRoot>, e.g.:
    //   /opt/websiteempire/deploy/fr          -> parent "/opt/websiteempire/deploy" -> grandparentDir "/opt/websiteempire" -> "websiteempire"
    //   /opt/websiteempire/healybio/deploy/fr -> parent ".../healybio/deploy"       -> grandparentDir ".../healybio"       -> "healybio"
    const QString &parent        = QFileInfo(hostFolder).path();
    const QString &grandparentDir = QFileInfo(parent).path();
    const QString &siteRoot      = QFileInfo(grandparentDir).fileName();
    if (siteRoot.compare(QStringLiteral("websiteempire"), Qt::CaseInsensitive) == 0) {
        return {};
    }
    QString slug = siteRoot.toLower();
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    return slug;
}

QString PaneDomains::_siteServiceName(const HostInfo &host, const QString &lang) const
{
    const QString slug = _siteSlug(host.hostFolder);
    return QStringLiteral("website-") + (slug.isEmpty() ? QString() : slug + QStringLiteral("-")) + lang;
}

bool PaneDomains::_verifyRemoteDbIntegrity(const HostInfo &host, const QString &remotePath,
                                            QString &errorOutput) const
{
    const QString cmd = QStringLiteral("sqlite3 '") + remotePath
                        + QStringLiteral("' 'PRAGMA integrity_check;'");
    QString output;
    if (!_runSshCommand(host, cmd, errorOutput, &output)) {
        return false;
    }

    const QString trimmed = output.trimmed();
    if (trimmed.compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
        errorOutput = tr("Remote database %1 failed integrity check:\n%2").arg(remotePath, trimmed);
        return false;
    }
    return true;
}

bool PaneDomains::_runRsync(const HostInfo &host, const QString &src, const QString &dst,
                             QString &errorOutput) const
{
    QString authMode;
    QString output;
    int     exitCode = -1;
    if (!_execRsync(host, src, dst, authMode, output, exitCode, false)) {
        errorOutput = tr("rsync timed out. Auth: %1").arg(authMode);
        return false;
    }
    if (exitCode != 0) {
        errorOutput = tr("Auth: %1\n%2").arg(authMode, output);
        return false;
    }
    return true;
}

bool PaneDomains::_execRsync(const HostInfo &host, const QString &src, const QString &dst,
                              QString &authMode, QString &output, int &exitCode,
                              bool pumpEvents) const
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

    if (host.password.isEmpty()) {
        authMode = QStringLiteral("SSH key (no password in host table)");
        const QString sshCmd = QStringLiteral("ssh -p ") + host.port
                               + QStringLiteral(" -o StrictHostKeyChecking=no");
        process.start(QStringLiteral("rsync"), {
            QStringLiteral("-az"),
            QStringLiteral("--checksum"),
            QStringLiteral("--mkpath"),
            QStringLiteral("-e"), sshCmd,
            src,
            dst
        });
    } else {
        authMode = QStringLiteral("password via sshpass (port %1, password length %2)")
                       .arg(host.port).arg(host.password.length());
        const QString rsh = QStringLiteral("sshpass -e ssh -p ") + host.port
                            + QStringLiteral(" -o StrictHostKeyChecking=no");
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("SSHPASS"), host.password);
        process.setProcessEnvironment(env);
        process.start(QStringLiteral("rsync"), {
            QStringLiteral("-az"),
            QStringLiteral("--checksum"),
            QStringLiteral("--mkpath"),
            QStringLiteral("--rsh"), rsh,
            src,
            dst
        });
    }

    bool finished = false;
    if (pumpEvents) {
        // Wait via the event loop so the caller's progress dialog stays
        // responsive (repaints, log scrolling) instead of freezing the GUI.
        QEventLoop loop;
        QTimer     timeout;
        timeout.setSingleShot(true);
        connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                &loop, &QEventLoop::quit);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(120000);
        if (process.state() != QProcess::NotRunning) {
            loop.exec();
        }
        finished = (process.state() == QProcess::NotRunning);
        if (!finished) {
            process.kill();
            process.waitForFinished(5000);
        }
    } else {
        finished = process.waitForFinished(120000);
    }

    if (!finished) {
        return false;
    }
    exitCode = process.exitCode();
    output   = QString::fromUtf8(process.readAll());
    return true;
}
