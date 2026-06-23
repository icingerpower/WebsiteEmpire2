#ifndef PANEGENERATEDPAGES_H
#define PANEGENERATEDPAGES_H

#include <QDir>
#include <QWidget>
#include <memory>

namespace Ui { class PaneGeneratedPages; }

class AbstractEngine;
class CategoryHubDirtySet;
class CategoryHubSyncer;
class CategoryTable;
class GeneratedPagesModel;
class PageDb;
class PageGenerator;
class PageRepositoryDb;
class SymptomHubSyncer;
class TaxonomyIndexSyncer;
class WebsiteSettingsTable;

/**
 * Read-only pane listing pages that are generated automatically from triggers
 * rather than authored manually — currently category hub pages only, but
 * designed to accommodate other auto-generated page types in the future.
 *
 * DB initialisation is deferred to the first setVisible(true) call, matching
 * the pattern used by PanePages.  Stub synchronisation runs automatically on
 * each show so the list stays current without a manual action.
 *
 * Tear-down order: syncer → dirtySet → generator → repo → db → categoryTable.
 */
class PaneGeneratedPages : public QWidget
{
    Q_OBJECT

public:
    explicit PaneGeneratedPages(QWidget *parent = nullptr);
    ~PaneGeneratedPages();

    /**
     * Binds the pane to a working directory, engine, and settings table.
     * Must be called once from MainWindow::_init() before the pane is first shown.
     */
    void setup(const QDir &workingDir, AbstractEngine *engine, WebsiteSettingsTable *settingsTable);

    void setVisible(bool visible) override;

public slots:
    /** Creates stub hub pages for any categories not yet covered, then refreshes. */
    void syncStubs();

    /** Renders all hub pages currently in the dirty set, then refreshes. */
    void reRenderStale();

    /** Marks the selected pages dirty and re-renders them, then refreshes. */
    void reRenderSelected();

    /** Opens the preview dialog for the selected page. */
    void previewPage();

private:
    void _connectSlots();

    /**
     * Creates (or recreates) all DB objects from m_workingDir.
     * Always tears down in reverse dependency order.
     */
    void _initDb();

    /**
     * Rebuilds the model rows from the repository and dirty set.
     * No-op when the DB is not open.
     */
    void _refreshModel();

    /**
     * Returns the page ID of the currently selected row, or -1.
     */
    int _selectedPageId() const;

    /**
     * Returns the page IDs for all currently selected rows.
     */
    QList<int> _selectedPageIds() const;

    /**
     * Returns the domain for website index 0, falling back to "localhost"
     * when the engine has no rows or the domain cell is empty.
     */
    QString _firstDomain() const;

    Ui::PaneGeneratedPages  *ui;
    QDir                     m_workingDir;
    AbstractEngine          *m_engine       = nullptr;
    WebsiteSettingsTable    *m_settingsTable = nullptr;
    bool                     m_isSetup       = false;

    GeneratedPagesModel *m_pagesModel = nullptr;

    std::unique_ptr<CategoryTable>       m_categoryTable;
    std::unique_ptr<PageDb>              m_pageDb;
    std::unique_ptr<PageRepositoryDb>    m_pageRepo;
    std::unique_ptr<PageGenerator>       m_pageGenerator;
    std::unique_ptr<CategoryHubDirtySet> m_dirtySet;
    std::unique_ptr<CategoryHubSyncer>    m_syncer;
    std::unique_ptr<SymptomHubSyncer>     m_symptomSyncer;
    std::unique_ptr<TaxonomyIndexSyncer>  m_taxonomyIndexSyncer;
};

#endif // PANEGENERATEDPAGES_H
