#ifndef PANEPAGES_H
#define PANEPAGES_H

#include <QDir>
#include <QWidget>
#include <memory>

namespace Ui { class PanePages; }

class AbstractEngine;
class CategoryTable;
class PageDb;
class PageRepositoryDb;
class QSqlQueryModel;
class WebsiteSettingsTable;

/**
 * Pane for browsing and managing pages in the working directory's pages.db.
 *
 * DB initialisation is deferred: nothing is opened until setVisible(true) is
 * first called.  On subsequent visibility toggles the connection is checked and
 * re-established if it was lost (e.g. the working directory was remounted).
 *
 * Tear-down order matters: m_pageRepo holds a reference to *m_pageDb, so
 * m_pageRepo must be reset before m_pageDb in _initDb().
 */
class PanePages : public QWidget
{
    Q_OBJECT

public:
    explicit PanePages(QWidget *parent = nullptr);
    ~PanePages();

    /**
     * Binds the pane to a working directory and engine.
     * Must be called once from MainWindow::_init() before the pane is first shown.
     * DB initialisation is deferred to the first setVisible(true) call.
     * engine may be nullptr if no engine has been configured yet.
     */
    void setup(const QDir &workingDir, AbstractEngine *engine, WebsiteSettingsTable *settingsTable);

public slots:
    void addPage();
    void editPage();
    void removePage();
    void previewPage();

    void setVisible(bool visible) override;

private:
    void _connectSlots();

    /**
     * Creates (or recreates) CategoryTable, PageDb, and PageRepositoryDb from
     * m_workingDir.  Always tears down in reverse dependency order.
     */
    void _initDb();

    /**
     * Resets the QSqlQueryModel query so tableViewPages shows current data.
     * No-op when the DB is not open.
     */
    void _refreshModel();

    /**
     * Returns the id column value for the currently selected row, or -1.
     */
    int _selectedPageId() const;

    Ui::PanePages        *ui;
    QDir                  m_workingDir;
    AbstractEngine       *m_engine        = nullptr;
    WebsiteSettingsTable *m_settingsTable = nullptr;
    bool                  m_isSetup       = false;

    QSqlQueryModel *m_model = nullptr;

    std::unique_ptr<CategoryTable>    m_categoryTable;
    std::unique_ptr<PageDb>           m_pageDb;
    std::unique_ptr<PageRepositoryDb> m_pageRepo;
    bool m_resizedOnce = false;
};

#endif // PANEPAGES_H
