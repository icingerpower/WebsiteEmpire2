#ifndef PANEPAGES_H
#define PANEPAGES_H

#include <QDir>
#include <QSet>
#include <QString>
#include <QWidget>
#include <memory>

namespace Ui { class PanePages; }

class AbstractEngine;
class CategoryTable;
class PageDb;
class PageRepositoryDb;
class QSortFilterProxyModel;
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
    void generateHomePage();
    void generateLegalPages();
    void translate();
    void viewCommandTranslate();
    void copyUrl();

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

    /**
     * Hides/shows rows in tableViewPages based on the current comboBoxPageType
     * selection.  An empty typeId (index 0, "All page types") shows every row.
     */
    void _applyTypeFilter();

    /**
     * Returns the AbstractLegalPageDef IDs for legal source pages that already
     * exist in the repository for the current editing language.
     * Loads page_data for every legal source page to read the __legal_def_id stamp.
     */
    QSet<QString> _existingLegalDefIds() const;

    /**
     * Returns the page ID of the legal source page stamped with defId,
     * or -1 if not found.
     */
    int _legalPageId(const QString &defId) const;

    /**
     * Throws ExceptionWithTitleText if any mandatory legal setting
     * (company name, address, registration number, contact email) is empty.
     */
    void _validateLegalSettings() const;

    /**
     * Returns true if a source page of type PageTypeHome::TYPE_ID already
     * exists for the current editing language.
     */
    bool _homePageExists() const;

    /**
     * Greys out buttonGenerateHomePage when a home page already exists,
     * resets to default style when one is missing.
     */
    void _updateHomeButton();

    /**
     * Updates buttonGenerateLegalPages style to grey when all registered
     * AbstractLegalPageDef instances have a corresponding source page,
     * and back to the default style when any is missing.
     */
    void _updateLegalButton();

    Ui::PanePages        *ui;
    QDir                  m_workingDir;
    AbstractEngine       *m_engine        = nullptr;
    WebsiteSettingsTable *m_settingsTable = nullptr;
    bool                  m_isSetup       = false;

    QSqlQueryModel          *m_model      = nullptr;
    QSortFilterProxyModel   *m_proxyModel = nullptr;

    std::unique_ptr<CategoryTable>    m_categoryTable;
    std::unique_ptr<PageDb>           m_pageDb;
    std::unique_ptr<PageRepositoryDb> m_pageRepo;
    bool m_resizedOnce = false;
};

#endif // PANEPAGES_H
