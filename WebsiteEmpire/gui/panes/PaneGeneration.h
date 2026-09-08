#ifndef PANEGENERATION_H
#define PANEGENERATION_H

#include <QDir>
#include <QModelIndex>
#include <QSet>       // QSet<QString> parameter of _unresolvedRasterCount
#include <QWidget>

class AbstractEngine;
class AbstractCli;
class AvailableCliList;
class AvailableCliTable;
class GenStrategyTable;
class IPageRepository;
class QProcess;
class WebsiteSettingsTable;
namespace Ui { class PaneGeneration; }

/**
 * Pane for triggering and monitoring website generation.
 *
 * "Generate one" and "View gen. command" require both a selected strategy and
 * a valid linked aspire DB (when the strategy has a source table configured).
 * "Custom topic" bypasses the source DB: the user types any topic name, the
 * launcher slugifies it (applying the strategy's endPermalink suffix) and
 * generates exactly one page.  Requires a selected strategy but not a linked DB.
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
    void generateCustomTopic();

    /**
     * Runs the launcher with --retry-only so it processes ONLY the retry queue:
     * every article left in ContentReady gets its missing or permanently-failed
     * raster images regenerated until the page is complete. Generates no new
     * articles.
     *
     * This is the counterpart to generateOne(), which passes --new-only and so
     * deliberately never touches the backlog — without this action a page whose
     * images failed had no reachable way to be finished from the UI.
     * Unlike generatePhase2() it sets no SocialMedia flag: that is a separate,
     * later pass and must not be triggered as a side effect of image repair.
     */
    void finishIncomplete();

    void generatePhase2();
    void viewGenCommand();
    void computeRemainingToDo();
    void linkDb();

private slots:
    void _onStrategySelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void _onPromptEdited();
    void _onSvgEdited();
    void _onImageInstructionsEdited();
    void _onImageCountMinEdited(int value);
    void _onImageCountMaxEdited(int value);

private:
    void _connectSlots();

    /**
     * Shared subprocess launcher used by generateOne() and generateCustomTopic().
     * Disables the generation buttons, streams output to textEditOutput, and
     * re-enables them on completion.  Calls computeRemainingToDo() when done.
     */
    void _startProcess(QStringList args);

    /**
     * Persists textEditPrompt, textEditSvgInstructions, and textEditImageInstructions
     * to the currently selected strategy row.  No-op when no strategy is selected or
     * the pane has not been set up.  Called from the destructor, setVisible(false),
     * and before switching rows.
     */
    void _saveCurrentPrompts();

    /**
     * Returns the resolved path to the aspire DB for the given strategy row.
     * Checks the standard results_db/<primaryAttrId>.db path first, then the
     * stored path saved by linkDb().  Returns an empty string if neither exists.
     */
    QString _resolvedDbPath(int row) const;

    // Formats a millisecond duration as a human-readable string (e.g. "45 s",
    // "3 min 12 s") for reporting how long a generation run took.
    static QString _formatElapsed(qint64 ms);

    // Returns "https://domain" for the editing language, or "" if not resolvable.
    QString _primaryDomain(AbstractEngine *engine, WebsiteSettingsTable *settingsTable) const;

    /** Restores comboBoxCli's selection from the "generationCli" setting, if available. */
    void _restoreGenerationCli();

    /** Returns the AbstractCli currently selected in comboBoxCli, or nullptr if none. */
    AbstractCli *_selectedCli() const;

    /**
     * Counts AI-generated pages of typeId that must NOT be reported as done
     * because at least one of their raster images is still Pending or went
     * FailedFinal — an article with a broken image is not a finished article,
     * and such a page cannot publish (PageGenerator's gate rejects it) while
     * the retry queue still has repair work to do on it.
     *
     * Only pages with a generatedAt stamp are counted, matching what the "done"
     * figure includes, so the subtraction can never make the count negative.
     * When expectedPermalinks is non-empty the page's permalink must also be in
     * it, mirroring countGeneratedMatchingPermalinks()'s own filter.
     */
    static int _unresolvedRasterCount(const IPageRepository &pageRepo,
                                      const QString         &typeId,
                                      const QSet<QString>   &expectedPermalinks);

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
    // Guard: true while _onStrategySelectionChanged is loading fields programmatically
    // so _onPromptEdited() / _onSvgEdited() do not write back a false save.
    bool                m_updatingFields   = false;
    AvailableCliTable  *m_cliTable         = nullptr;
    AvailableCliList   *m_cliList          = nullptr;
};

#endif // PANEGENERATION_H
