#ifndef PANESETTINGS_H
#define PANESETTINGS_H

#include <QWidget>

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

private slots:
    void onThemeChanged(int index);
    void onDefaultCliChanged(int index);

private:
    // Selects the comboDefaultCli row whose name matches the "defaultCli" setting.
    // Called on construction and each time a new CLI becomes available.
    void _restoreDefaultCli();

    Ui::PaneSettings  *ui;
    AvailableCliTable *m_cliTable = nullptr;
    AvailableCliList  *m_cliList  = nullptr;
};

#endif // PANESETTINGS_H
