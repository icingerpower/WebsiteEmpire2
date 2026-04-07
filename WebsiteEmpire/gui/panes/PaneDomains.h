#ifndef PANEDOMAINS_H
#define PANEDOMAINS_H

#include <QDir>
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
    void download();
    void viewCommands();

    void browseLocalDeployFolder();
    void deployLocally();

private:
    struct HostInfo {
        QString name;
        QString url;
        QString port;
        QString username;
        QString password;
        QString hostFolder;
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
     * Runs sshpass with the given arguments. Returns true on success.
     * On failure, errorOutput is populated with the error message.
     */
    bool _runScp(const QStringList &args, QString &errorOutput) const;

    Ui::PaneDomains  *ui;
    QDir              m_workingDir;
    AbstractEngine   *m_engine    = nullptr;
    HostTable        *m_hostTable = nullptr;
};

#endif // PANEDOMAINS_H
