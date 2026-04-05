#ifndef PAGESLISTWIDGET_H
#define PAGESLISTWIDGET_H

#include <QDir>
#include <QWidget>

class AbstractEngine;
class CategoryTable;
class IPageRepository;
class PageGenerator;
class QSqlQueryModel;
class WebsiteSettingsTable;

namespace Ui { class PagesListWidget; }

/**
 * Widget that lists all pages from IPageRepository and exposes CRUD operations
 * and static-site generation.
 *
 * The table shows: ID, Type, Permalink, Lang, Updated.
 * Selecting a row enables Edit and Delete.
 * "New" opens a PageEditorDialog in create mode.
 * "Edit" opens a PageEditorDialog for the selected page.
 * "Delete" removes the selected page after confirmation.
 * "Generate" calls PageGenerator::generateAll() and reports the page count.
 *
 * The widget holds non-owning references to IPageRepository, CategoryTable,
 * and the working directory; the caller is responsible for their lifetime.
 */
class PagesListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PagesListWidget(IPageRepository      &repo,
                             CategoryTable        &categoryTable,
                             const QDir           &workingDir,
                             const QString        &domain,
                             AbstractEngine       &engine,
                             int                   websiteIndex,
                             WebsiteSettingsTable *settingsTable,
                             QWidget              *parent = nullptr);
    ~PagesListWidget() override;

signals:
    /** Emitted after generation with the number of pages written. */
    void generated(int pageCount);

private slots:
    void _onNewClicked();
    void _onEditClicked();
    void _onDeleteClicked();
    void _onGenerateClicked();
    void _onSelectionChanged();

private:
    void _refreshModel();
    int  _selectedPageId() const;

    Ui::PagesListWidget  *ui;
    IPageRepository      &m_repo;
    CategoryTable        &m_categoryTable;
    QDir                  m_workingDir;
    QString               m_domain;
    AbstractEngine       &m_engine;
    int                   m_websiteIndex;
    WebsiteSettingsTable *m_settingsTable = nullptr;
    QSqlQueryModel       *m_model         = nullptr;
};

#endif // PAGESLISTWIDGET_H
