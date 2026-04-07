#include "PaneDomains.h"
#include "ui_PaneDomains.h"

#include "../dialogs/DialogEditHosts.h"
#include "website/AbstractEngine.h"
#include "website/HostTable.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/pages/PageDb.h"
#include "website/pages/PageGenerator.h"
#include "website/pages/PageRepositoryDb.h"
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

        // Generate all pages into workingDir/content.db
        CategoryTable   categoryTable(m_workingDir);
        PageDb          pageDb(m_workingDir);
        PageRepositoryDb pageRepo(pageDb);
        PageGenerator   generator(pageRepo, categoryTable);

        // One generateAll() call per unique domain — multiple engine rows can
        // share the same domain (e.g. different themes); generating twice would
        // only overwrite the same content.db rows and inflate the page count.
        int totalPages = 0;
        QSet<QString> seenDomains;
        const int rows = m_engine->rowCount();
        for (int i = 0; i < rows; ++i) {
            const QString domain = m_engine->data(
                m_engine->index(i, AbstractEngine::COL_DOMAIN)).toString().trimmed();
            if (domain.isEmpty() || seenDomains.contains(domain)) {
                continue;
            }
            seenDomains.insert(domain);
            totalPages += generator.generateAll(m_workingDir, domain, *m_engine, i);
        }

        // Copy content.db to the deploy folder
        _deployLocallyImpl();

        // Locate the StaticWebsiteServe binary (check sibling build dirs first)
        const QString appDir = QCoreApplication::applicationDirPath();
        QString binaryPath = QStringLiteral("StaticWebsiteServe"); // fallback: rely on PATH
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

        const QString deployPath = _resolveDeployPath();
        const QString homeUrl    = QStringLiteral("http://localhost:8080/index.html");

        const QString msg = tr(
            "Generated %1 page(s) and published to:\n"
            "%2\n\n"
            "To serve locally with Drogon:\n"
            "  1. Open a terminal\n"
            "  2. cd %2\n"
            "  3. %3\n\n"
            "Then open: %4")
            .arg(totalPages)
            .arg(deployPath, binaryPath, homeUrl);

        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Generate & Publish"));
        msgBox.setText(msg);
        QPushButton *copyBtn = msgBox.addButton(tr("Copy path"),      QMessageBox::ActionRole);
        QPushButton *openBtn = msgBox.addButton(tr("Open home page"), QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.exec();

        if (msgBox.clickedButton() == copyBtn) {
            QGuiApplication::clipboard()->setText(deployPath);
        } else if (msgBox.clickedButton() == openBtn) {
            QDesktopServices::openUrl(QUrl(homeUrl));
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

    if (!QFile::copy(srcDb, destDb)) {
        ExceptionWithTitleText ex(tr("Local deploy"),
                                  tr("Failed to copy content.db to:\n%1").arg(destDb));
        ex.raise();
        return;
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
