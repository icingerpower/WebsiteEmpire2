#ifndef PANEPAGESTATSH
#define PANEPAGESTATSH

#include <QWidget>

class QDir;
class PagesStatsWidget;

namespace Ui {
class PanePageStats;
}

/**
 * "Page Stats" main-window tab.  Hosts a PagesStatsWidget bound to
 * workingDir/stats.db (the merged file produced by PaneDomains::download()).
 * Empty until setWorkingDir() is called by MainWindow.
 */
class PanePageStats : public QWidget
{
    Q_OBJECT

public:
    explicit PanePageStats(QWidget *parent = nullptr);
    ~PanePageStats();

    /**
     * Creates the PagesStatsWidget for this working directory and mounts it
     * in the pane.  Replaces any previously mounted widget (working-dir
     * switch), releasing its stats.db connection first.
     */
    void setWorkingDir(const QDir &workingDir);

private:
    Ui::PanePageStats *ui;
    PagesStatsWidget  *m_statsWidget = nullptr;
};

#endif // PANEPAGESTATSH
