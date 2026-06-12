#ifndef PANESETTINGS_H
#define PANESETTINGS_H

#include <QDir>
#include <QWidget>

class AbstractTheme;
class AvailableCliList;
class AvailableCliTable;
class WebsiteSettingsTable;

namespace Ui {
class PaneSettings;
}

class PaneSettings : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettings(QWidget *parent = nullptr);
    ~PaneSettings();

    // Binds the pane to an already-constructed settings table.
    void setSettingsTable(WebsiteSettingsTable *settingsTable);

    /**
     * Supply the working directory so the favicon picker can write to images.db.
     * Must be called once from MainWindow::_init() before the pane is first shown.
     */
    void setWorkingDir(const QDir &dir);

    /**
     * Bind the pane to the active theme so favicon changes are reflected live.
     * May be called again after a theme switch.
     * theme may be nullptr to disable the favicon controls.
     */
    void setTheme(AbstractTheme *theme);

private slots:
    void onThemeChanged(int index);
    void onDefaultCliChanged(int index);
    void onPickFaviconSvg();
    void onClearFaviconSvg();
    void onPickFaviconIco();
    void onClearFaviconIco();
    void onPickFaviconApple();
    void onClearFaviconApple();

private:
    // Selects the comboDefaultCli row whose name matches the "defaultCli" setting.
    // Called on construction and each time a new CLI becomes available.
    void _restoreDefaultCli();

    void _updateFaviconLabels();

    Ui::PaneSettings  *ui;
    AvailableCliTable *m_cliTable    = nullptr;
    AvailableCliList  *m_cliList     = nullptr;
    AbstractTheme     *m_theme       = nullptr;
    QDir               m_workingDir;
};

#endif // PANESETTINGS_H
