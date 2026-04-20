#ifndef PANEGENERATION_H
#define PANEGENERATION_H

#include <QDir>
#include <QModelIndex>
#include <QWidget>

class AbstractEngine;
class GenStrategyTable;
class QProcess;
class WebsiteSettingsTable;
namespace Ui { class PaneGeneration; }

/**
 * Pane for triggering and monitoring website generation.
 *
 * "Generate one" and "View gen. command" require both a selected strategy and
 * a valid linked aspire DB (when the strategy has a source table configured).
 * "Link to DB" opens a file picker and persists the path in strategies.json so
 * the launcher can find it without GUI interaction.
 * The DB path validity is re-checked on every strategy selection, so a file
 * deleted after linking is detected immediately.
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
    void linkDb();

private slots:
    void _onStrategySelectionChanged(const QModelIndex &current, const QModelIndex &previous);

private:
    void _connectSlots();

    /**
     * Returns the resolved path to the aspire DB for the given strategy row.
     * Checks the standard results_db/<primaryAttrId>.db path first, then the
     * stored path saved by linkDb().  Returns an empty string if neither exists.
     */
    QString _resolvedDbPath(int row) const;

    // Returns "https://domain" for the editing language, or "" if not resolvable.
    QString _primaryDomain(AbstractEngine *engine, WebsiteSettingsTable *settingsTable) const;

    Ui::PaneGeneration *ui;
    QDir                m_workingDir;
    bool                m_isSetup          = false;
    AbstractEngine     *m_engine           = nullptr; // not owned; set by setup()
    GenStrategyTable   *m_strategies       = nullptr;
    QString             m_domain;          // cached from setup(); may be empty
    QString             m_editingLang;     // cached from setup(); defaults to "en"
    QString             m_lastOkPermalink; // set by generateOne() output parsing
    // Tracks the running generation process so we can disconnect it before
    // destroying the UI (otherwise QProcess::~QProcess fires 'finished' after
    // ui->textEditOutput is already deleted, causing a segfault).
    QProcess           *m_activeProcess    = nullptr;
};

#endif // PANEGENERATION_H
