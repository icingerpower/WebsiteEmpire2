#ifndef PAGEEDITORWIDGET_H
#define PAGEEDITORWIDGET_H

#include <QList>
#include <QWidget>
#include <memory>

class AbstractPageBlockWidget;
class AbstractPageType;
class CategoryTable;
class IPageRepository;

namespace Ui { class PageEditorWidget; }

/**
 * Embeddable widget for creating or editing a page.
 *
 * Create mode  (pageId == -1):
 *   The type combo is enabled.  Selecting a type populates the vertical
 *   splitter with that type's bloc editor widgets.  On save, a new page row is
 *   created via IPageRepository::create() and the bloc data is saved.  After
 *   the first save, the widget transitions into edit mode: the type combo is
 *   disabled and subsequent saves update the existing row.
 *
 * Edit mode  (pageId >= 0):
 *   The type combo is disabled (type cannot change after creation).
 *   The splitter shows the type's widgets pre-populated with the stored bloc
 *   data.  On save, the permalink may be updated (triggers
 *   IPageRepository::updatePermalink() if changed) and the data is saved.
 *
 * Emits saved(int pageId) after every successful save.
 *
 * Bloc editor widgets are obtained via AbstractPageBloc::createEditWidget()
 * and added to a vertical QSplitter so each section can be resized
 * independently.  AbstractPageType is reconstructed from the registry
 * (createForTypeId) and owned by this widget.
 *
 * Holds non-owning references to IPageRepository and CategoryTable.
 */
class PageEditorWidget : public QWidget
{
    Q_OBJECT

public:
    // editingLangCode: BCP-47 code from WebsiteSettingsTable — used only in
    // create mode (pageId == -1) to stamp the new page's lang column.
    explicit PageEditorWidget(IPageRepository &repo,
                              CategoryTable   &categoryTable,
                              int              pageId,           // -1 = create
                              const QString   &editingLangCode,
                              QWidget         *parent = nullptr);
    ~PageEditorWidget() override;

signals:
    void saved(int pageId);

private slots:
    void _onTypeChanged(int index);
    void _onSaveClicked();

private:
    void _loadBlocs(const QString &typeId);
    void _clearBlocs();

    Ui::PageEditorWidget               *ui;
    IPageRepository                    &m_repo;
    CategoryTable                      &m_categoryTable;
    int                                 m_pageId;          // -1 = create, >=0 = edit
    QString                             m_editingLangCode; // used in create mode only

    std::unique_ptr<AbstractPageType>   m_pageType;
    QList<AbstractPageBlockWidget *>    m_blocWidgets;
};

#endif // PAGEEDITORWIDGET_H
