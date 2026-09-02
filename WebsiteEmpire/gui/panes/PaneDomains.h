#ifndef PANEDOMAINS_H
#define PANEDOMAINS_H

#include <QDir>
#include <QStringList>
#include <QWidget>

class AbstractEngine;
class HostTable;

namespace Ui {
class PaneDomains;
}

class PaneDomains : public QWidget
{
    Q_OBJECT

    static constexpr const char *SETTINGS_KEY_DEPLOY_PATH = "PaneDomains/localDeployPath";

public:
    explicit PaneDomains(QWidget *parent = nullptr);
    ~PaneDomains();

    // Binds the pane to an already-init()'d engine.
    // Hides "Apply per lang" when the engine has only one variation.
    // Installs a combo-box delegate on the Host column.
    void setEngine(AbstractEngine *engine);

    // Provides the HostTable needed by the "Edit hosts" dialog.
    void setHostTable(HostTable *hostTable);

    /**
     * Sets the working directory used to derive the default deploy path
     * (workingDir/deploy) and to locate content.db for local deployment.
     * Populates lineEditPathLocalDeploy with the last persisted QSettings path,
     * or the default workingDir/deploy if no path was previously saved.
     * Must be called before the pane is first shown.
     */
    void setWorkingDir(const QDir &workingDir);

public slots:
    void apply();
    void applyPerLang();
    void editHosts();
    void upload();
    void uploadFull();
    void download();
    void viewCommands();

    void browseLocalDeployFolder();
    void deployLocally();
    void viewSitemaps();

private:
    struct HostInfo {
        QString     name;
        QString     url;
        QString     port;
        QString     username;
        QString     password;
        QString     hostFolder;
        QStringList langCodes; // language codes from engine rows mapped to this host
        /// Returns a QSettings-safe key (no raw slashes -- uses | as separator).
        QString uniqueKey() const;
    };

    void _connectSlots();
    QString _applyTemplate(const QString &tmpl,
                           const QString &lang,
                           const QString &theme) const;

    /**
     * Returns the resolved deploy path: lineEditPathLocalDeploy text if non-empty,
     * otherwise workingDir/deploy.
     */
    QString _resolveDeployPath() const;

    /**
     * Performs the actual local deploy — raises ExceptionWithTitleText on any
     * error.  deployLocally() (the slot) wraps this in a try/catch and shows
     * QMessageBox::critical.  upload() calls this directly and handles the
     * exception itself so that the overall upload flow is not interrupted by
     * a stale-deploy error shown twice.
     */
    void _deployLocallyImpl();

    /**
     * Resolves unique hosts from the engine table by looking up credentials
     * in the host table. Rows with empty host name or folder are skipped.
     */
    QList<HostInfo> _resolveHosts() const;

    /**
     * Returns true if content.db in the working directory is newer than
     * the deployed copy, or if the deployed copy does not exist.
     */
    bool _deployNeeded() const;

    /**
     * Returns the language codes from the engine table that have at least 10
     * fully-translated pages. Used by both deployLocally() and upload() so both
     * apply the same qualification gate.
     */
    QStringList _qualifyingLangCodes() const;

    /**
     * Kills any StaticWebsiteServe process whose working directory is
     * deployPath, then starts a fresh instance of binaryPath from deployPath
     * listening on port.
     */
    void _restartLocalDrogon(const QString &deployPath,
                              const QString &binaryPath,
                              int            port = 8080,
                              const QString &imagesDbPath = {});

    /**
     * Runs rsync via sshpass to transfer a single file. src and dst may each be
     * a local path or a "user@host:path" remote path — pass them in whichever
     * order matches the direction (upload vs download).
     * Returns true on success; on failure errorOutput is populated.
     */
    bool _runRsync(const HostInfo &host, const QString &src, const QString &dst,
                   QString &errorOutput) const;

    /**
     * Lower-level rsync runner shared by _runRsync() and download().
     * Returns false only on timeout (120 s); otherwise exitCode and the merged
     * stdout/stderr output tell the caller what happened — rsync exit code 23
     * with "No such file or directory" in the output means the remote file
     * does not exist (e.g. a language that was never deployed), which
     * download() treats as a skip rather than an error.
     *
     * When pumpEvents is true the wait runs through a QEventLoop instead of
     * QProcess::waitForFinished(), so an application-modal progress dialog
     * stays responsive; callers must guard against re-entrancy (the download
     * flow does so via the dialog's application modality).
     */
    bool _execRsync(const HostInfo &host, const QString &src, const QString &dst,
                    QString &authMode, QString &output, int &exitCode,
                    bool pumpEvents) const;

    /**
     * Runs a single command on the remote host over SSH.
     * Uses sshpass when host.password is set, direct SSH otherwise.
     * Returns true on success; on failure errorOutput is populated.
     * If output is non-null, it receives the command's stdout on success.
     */
    bool _runSshCommand(const HostInfo &host, const QString &command,
                        QString &errorOutput, QString *output = nullptr) const;

    /**
     * Runs "PRAGMA integrity_check" on a local SQLite file, after truncating
     * its WAL so the check sees a fully-settled single-file snapshot (the same
     * state that will be rsynced). Returns false with errorOutput populated
     * if the result is anything other than "ok" — callers must not upload a
     * database that fails this check.
     */
    bool _verifyLocalDbIntegrity(const QString &dbPath, QString &errorOutput) const;

    /**
     * Runs "PRAGMA integrity_check" on a remote SQLite file via the sqlite3
     * CLI over SSH. Returns false with errorOutput populated if the result
     * is anything other than "ok" — callers must not restart the remote
     * service on top of a database that fails this check, since a torn
     * rsync transfer would otherwise go live silently.
     */
    bool _verifyRemoteDbIntegrity(const HostInfo &host, const QString &remotePath,
                                  QString &errorOutput) const;

    /**
     * Derives a site slug from a per-language HostFolder so that two engines
     * deployed to the same VPS under different deploy roots don't collide on
     * systemd unit names or process matching. The slug is the folder name two
     * levels above the language folder, e.g.:
     *   /opt/websiteempire/deploy/en            -> "websiteempire" -> "" (legacy, no slug)
     *   /opt/websiteempire/healybio/deploy/en   -> "healybio"      -> "healybio"
     * The legacy grandparent name "websiteempire" maps to an empty slug so
     * existing single-site deployments keep their exact current unit names
     * with no VPS-side migration required.
     */
    QString _siteSlug(const QString &hostFolder) const;

    /**
     * Returns the systemd unit name to restart for this host/language, e.g.
     * "website-en" (legacy layout) or "website-healybio-en" (a second site
     * sharing the VPS). See _siteSlug() for how the slug is derived.
     */
    QString _siteServiceName(const HostInfo &host, const QString &lang) const;

    Ui::PaneDomains  *ui;
    QDir              m_workingDir;
    AbstractEngine   *m_engine    = nullptr;
    HostTable        *m_hostTable = nullptr;
};

#endif // PANEDOMAINS_H
