#ifndef PANESETTINGS_H
#define PANESETTINGS_H

#include <QWidget>

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

private:
    Ui::PaneSettings *ui;
};

#endif // PANESETTINGS_H
