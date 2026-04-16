#ifndef PANEGENERATION_H
#define PANEGENERATION_H

#include <QDir>
#include <QModelIndex>
#include <QWidget>

class AbstractEngine;
class GenStrategyTable;
class WebsiteSettingsTable;
namespace Ui { class PaneGeneration; }

/**
 * Pane for triggering and monitoring website generation.
 *
 * "Generate one" and "View gen. command" are disabled until a strategy row
 * is selected on the left.  After a successful "Generate one" run, a dialog
 * shows the URL of the page that was generated so the user can verify it.
 */
class PaneGeneration : public QWidget
{
    Q_OBJECT

public:
    explicit PaneGeneration(QWidget *parent = nullptr);
    ~PaneGeneration();

    /**
     * Binds the pane to a working directory and initialises the strategy table.
     * engine and settingsTable are used to derive the primary domain for the
     * post-generation dialog; both may be null (dialog will show permalink only).
     * Must be called once from MainWindow::_init() before the pane is first shown.
     */
    void setup(const QDir           &workingDir,
               AbstractEngine       *engine,
               WebsiteSettingsTable *settingsTable);

    void setVisible(bool visible) override;

public slots:
    void addGeneration();
    void removeGeneration();
    void generateOne();
    void viewGenCommand();
    void computeRemainingToDo();

private slots:
    void _onStrategySelectionChanged(const QModelIndex &current, const QModelIndex &previous);

private:
    void _connectSlots();

    // Returns "https://domain" for the editing language, or "" if not resolvable.
    QString _primaryDomain(AbstractEngine *engine, WebsiteSettingsTable *settingsTable) const;

    Ui::PaneGeneration *ui;
    QDir                m_workingDir;
    bool                m_isSetup          = false;
    AbstractEngine     *m_engine           = nullptr; // not owned; set by setup()
    GenStrategyTable   *m_strategies       = nullptr;
    QString             m_domain;          // cached from setup(); may be empty
    QString             m_lastOkPermalink; // set by generateOne() output parsing
};

#endif // PANEGENERATION_H
