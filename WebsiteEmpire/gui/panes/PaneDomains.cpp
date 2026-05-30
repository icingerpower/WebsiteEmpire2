#include "PaneDomains.h"
#include "ui_PaneDomains.h"

#include "../dialogs/DialogEditHosts.h"
#include "website/AbstractEngine.h"
#include "website/HostTable.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/CategoryHubDirtySet.h"
#include "website/pages/CategoryHubSyncer.h"
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
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QThread>
#include <QSettings>
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

    // Auto-deploy if needed
    if (_deployNeeded()) {
        try {
            _deployLocallyImpl();
        } catch (const ExceptionWithTitleText &ex) {
            QMessageBox::critical(this, ex.errorTitle(), ex.errorText());
            return;
        }
    }

    const QString deployPath = _resolveDeployPath();
    const QString contentDbLocal = QDir(deployPath).filePath(QStringLiteral("content.db"));
    const QString imagesDbLocal = m_workingDir.filePath(QStringLiteral("images.db"));
    const bool hasImagesDb = QFile::exists(imagesDbLocal);

    QSettings settings;
    bool anyError = false;

    for (const auto &host : std::as_const(hosts)) {
        // First-time server setup check
        const QString setupKey = QStringLiteral("PaneDomains/serverSetup/") + host.uniqueKey();
        if (!settings.value(setupKey).toBool()) {
            QString setupText;
            setupText += QStringLiteral("# 1. Create the remote folder (run on server):\n");
            setupText += QStringLiteral("mkdir -p ") + host.hostFolder + QStringLiteral("\n\n");
            setupText += QStringLiteral("# 2. Upload the server binary (run locally, replace path as needed):\n");
            setupText += QStringLiteral("sshpass -p '") + host.password
                         + QStringLiteral("' scp -P ") + host.port
                         + QStringLiteral(" /path/to/StaticWebsiteServe ")
                         + host.username + QStringLiteral("@") + host.url
                         + QStringLiteral(":") + host.hostFolder
                         + QStringLiteral("/StaticWebsiteServe\n\n");
            setupText += QStringLiteral("# 3. Make it executable and start it (run on server):\n");
            setupText += QStringLiteral("chmod +x ") + host.hostFolder
                         + QStringLiteral("/StaticWebsiteServe\n");
            setupText += QStringLiteral("cd ") + host.hostFolder
                         + QStringLiteral(" && nohup ./StaticWebsiteServe > server.log 2>&1 &\n");

            QMessageBox msgBox(this);
            msgBox.setWindowTitle(tr("First-time server setup"));
            msgBox.setText(tr("Have you completed the server setup for %1 (%2)?")
                               .arg(host.name, host.url));
            msgBox.setDetailedText(setupText);
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            msgBox.setDefaultButton(QMessageBox::No);
            if (msgBox.exec() != QMessageBox::Yes) {
                return;
            }
            settings.setValue(setupKey, true);
        }

        // Upload content.db
        const QString remoteContent = host.username + QStringLiteral("@") + host.url
                                      + QStringLiteral(":") + host.hostFolder
                                      + QStringLiteral("/content.db");
        const QStringList contentArgs = {
            QStringLiteral("-p"), host.password,
            QStringLiteral("scp"),
            QStringLiteral("-P"), host.port,
            QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=no"),
            contentDbLocal,
            remoteContent
        };
        QString errorOutput;
        if (!_runScp(contentArgs, errorOutput)) {
            QMessageBox::critical(this, tr("Upload"),
                                  tr("Failed to upload content.db to %1:\n%2")
                                      .arg(host.name, errorOutput));
            anyError = true;
            continue;
        }

        // Upload images.db if present
        if (hasImagesDb) {
            const QString remoteImages = host.username + QStringLiteral("@") + host.url
                                         + QStringLiteral(":") + host.hostFolder
                                         + QStringLiteral("/images.db");
            const QStringList imagesArgs = {
                QStringLiteral("-p"), host.password,
                QStringLiteral("scp"),
                QStringLiteral("-P"), host.port,
                QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=no"),
                imagesDbLocal,
                remoteImages
            };
            QString imgErrorOutput;
            if (!_runScp(imagesArgs, imgErrorOutput)) {
                QMessageBox::critical(this, tr("Upload"),
                                      tr("Failed to upload images.db to %1:\n%2")
                                          .arg(host.name, imgErrorOutput));
                anyError = true;
                continue;
            }
        }
    }

    if (!anyError) {
        QMessageBox::information(this, tr("Upload"), tr("Upload completed."));
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

    const QString localStatsDb = m_workingDir.filePath(QStringLiteral("stats.db"));
    bool anyError = false;

    for (const auto &host : std::as_const(hosts)) {
        const QString remoteStats = host.username + QStringLiteral("@") + host.url
                                    + QStringLiteral(":") + host.hostFolder
                                    + QStringLiteral("/stats.db");
        const QStringList args = {
            QStringLiteral("-p"), host.password,
            QStringLiteral("scp"),
            QStringLiteral("-P"), host.port,
            QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=no"),
            remoteStats,
            localStatsDb
        };
        QString errorOutput;
        if (!_runScp(args, errorOutput)) {
            QMessageBox::critical(this, tr("Download"),
                                  tr("Failed to download stats.db from %1:\n%2")
                                      .arg(host.name, errorOutput));
            anyError = true;
        }
    }

    if (!anyError) {
        QMessageBox::information(this, tr("Download"), tr("Download completed."));
    }
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

        // ── Determine qualifying languages (≥ 5 fully-translated pages) ────────
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
        const QHash<QString, int> translatedCounts =
            TranslationStatusTable::countCompletedPerLang(pageRepo, categoryTable, engineLangs);

        struct LangTarget {
            int     engineIndex;
            QString lang;
            QString domain;
            int     port;
        };

        QList<LangTarget> targets;
        QSet<QString>     seenLangs;
        int portOffset = 0;
        for (int i = 0; i < rows; ++i) {
            const QString lang = m_engine->getLangCode(i);
            if (lang.isEmpty() || seenLangs.contains(lang)) {
                continue;
            }
            const int count = translatedCounts.value(lang, 0);
            if (count < 10) {
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
            targets.append({i, lang, domain, 8080 + portOffset});
            ++portOffset;
        }

        if (targets.isEmpty()) {
            ExceptionWithTitleText ex(tr("Generate & Publish"),
                                      tr("No language has 5 or more pages yet.\n"
                                         "Create or translate at least 5 pages before publishing."));
            ex.raise();
            return;
        }

        // ── Generate each qualifying language directly into its deploy dir ──────
        PageGenerator       generator(pageRepo, categoryTable);
        CategoryHubDirtySet hubDirtySet(m_workingDir);
        CategoryHubSyncer   hubSyncer(pageRepo, categoryTable, hubDirtySet, generator);
        hubSyncer.syncStubs(m_engine->getLangCode(0));
        hubSyncer.markStaleByStats(m_workingDir);

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
        if (QFile::exists(srcImages)) {
            if (QFile::exists(sharedImages)) {
                QFile::remove(sharedImages);
            }
            QFile::copy(srcImages, sharedImages);
        }

        int totalPages = 0;

        QStringList urls;
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

            totalPages += generator.generateAll(m_workingDir, ddir, t.domain, *m_engine, t.engineIndex);

            _restartLocalDrogon(destDir, binaryPath, t.port, sharedImages);

            urls.append(QStringLiteral("http://localhost:%1/index.html  [%2]")
                           .arg(t.port).arg(t.lang));
        }

        hubDirtySet.clear();
        pageRepo.markAllCompleteAsPublished();

        // ── Success dialog ────────────────────────────────────────────────────
        const QString urlList  = urls.join(QStringLiteral("\n"));
        const QString firstUrl = QStringLiteral("http://localhost:%1/index.html")
                                     .arg(targets.first().port);
        const QString msg = tr("Generated %1 page(s).\n\nServing:\n%2")
                               .arg(totalPages).arg(urlList);

        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Generate & Publish"));
        msgBox.setText(msg);
        QPushButton *openBtn    = msgBox.addButton(tr("Open"),       QMessageBox::ActionRole);
        QPushButton *copyUrlBtn = msgBox.addButton(tr("Copy URL"),   QMessageBox::ActionRole);
        QPushButton *copyPathBtn = msgBox.addButton(tr("Copy path"), QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.exec();

        if (msgBox.clickedButton() == openBtn) {
            QDesktopServices::openUrl(QUrl(firstUrl));
        } else if (msgBox.clickedButton() == copyUrlBtn) {
            QGuiApplication::clipboard()->setText(firstUrl);
        } else if (msgBox.clickedButton() == copyPathBtn) {
            QGuiApplication::clipboard()->setText(deployBase);
        }

    } catch (const ExceptionWithTitleText &ex) {
        QMessageBox::critical(this, ex.errorTitle(), ex.errorText());
    }
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
    connect(ui->buttonDownload,     &QPushButton::clicked, this, &PaneDomains::download);
    connect(ui->buttonViewCommands, &QPushButton::clicked, this, &PaneDomains::viewCommands);
    connect(ui->buttonBrowseLocally, &QPushButton::clicked, this, &PaneDomains::browseLocalDeployFolder);
    connect(ui->buttonDeployLocally, &QPushButton::clicked, this, &PaneDomains::deployLocally);
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
        info.password   = m_hostTable->data(m_hostTable->index(hostRow, HostTable::COL_PASSWORD)).toString();
        info.hostFolder = hostFolder;

        const QString key = info.uniqueKey();
        if (!seenKeys.contains(key)) {
            seenKeys.insert(key);
            result.append(info);
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

bool PaneDomains::_runScp(const QStringList &args, QString &errorOutput) const
{
    QProcess process;
    process.start(QStringLiteral("sshpass"), args);
    if (!process.waitForFinished(60000)) {
        if (process.error() == QProcess::FailedToStart) {
            errorOutput = tr("sshpass is not installed. Install it with: sudo apt install sshpass");
        } else {
            errorOutput = tr("SCP process timed out.");
        }
        return false;
    }
    if (process.error() == QProcess::FailedToStart) {
        errorOutput = tr("sshpass is not installed. Install it with: sudo apt install sshpass");
        return false;
    }
    if (process.exitCode() != 0) {
        errorOutput = QString::fromUtf8(process.readAllStandardError());
        return false;
    }
    return true;
}
