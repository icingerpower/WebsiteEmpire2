#ifndef PANEUPDATE_H
#define PANEUPDATE_H

#include <QDir>
#include <QModelIndex>
#include <QWidget>

class AbstractEngine;
class UpdateStrategyTree;
class WebsiteSettingsTable;
class QProcess;
class QTimer;
namespace Ui { class PaneUpdate; }

/**
 * Pane for triggering and monitoring AI-driven page updates.
 *
 * The tree on the left shows update strategies (top-level) and their prompt
 * steps (children).  Selecting a prompt step enables "Update one", which runs
 * one page update using that prompt.  The prompt instructions are edited
 * inline in the text area on the right and saved automatically.
 */
class PaneUpdate : public QWidget
{
    Q_OBJECT

public:
    explicit PaneUpdate(QWidget *parent = nullptr);
    ~PaneUpdate();

    /**
     * Binds the pane to a working directory and initialises the strategy tree.
     * Must be called once from MainWindow::_init() before the pane is first shown.
     */
    void setup(const QDir           &workingDir,
               AbstractEngine       *engine,
               WebsiteSettingsTable *settingsTable);

    void setVisible(bool visible) override;

public slots:
    void addStrategy();
    void addPrompt();
    void removeSelected();
    void updateAll();
    void updateOne();
    void updateN();
    void stop();
    void viewUpdateCommand();
    void resetAttempt();

private slots:
    void _onSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void _onPromptEdited();

private:
    void _connectSlots();

    /** Launches an update process; limit < 0 means no limit (update all). */
    void _runUpdate(int limit);

    /**
     * Persists the current textEditPrompt content to the selected prompt node.
     * No-op when the selected item is a strategy node or nothing is selected.
     */
    void _saveCurrentPrompt();

    /** Returns the strategy UUID for the current selection (prompt or strategy). */
    QString _currentStrategyId() const;
    /** Returns the prompt UUID for the current selection (empty if strategy selected). */
    QString _currentPromptId() const;

    Ui::PaneUpdate     *ui;
    QDir                m_workingDir;
    bool                m_isSetup         = false;
    UpdateStrategyTree *m_strategies      = nullptr;
    bool                m_updatingPrompt  = false;
    QProcess           *m_activeProcess     = nullptr;
    QTimer             *m_inactivityTimer  = nullptr;
    QString             m_outputBuffer;   // accumulates partial lines from the subprocess
};

#endif // PANEUPDATE_H
