#ifndef PAGEEDITORDIALOG_H
#define PAGEEDITORDIALOG_H

#include <QDialog>
#include <QList>
#include <memory>

class AbstractPageBlockWidget;
class AbstractPageType;
class CategoryTable;
class IPageRepository;

namespace Ui { class PageEditorDialog; }

/**
 * Modal dialog for creating or editing a page.
 *
 * Create mode  (pageId == -1):
 *   The type combo is enabled.  Selecting a type populates the scroll area
 *   with that type's bloc editor widgets.  On accept, a new page row is
 *   created via IPageRepository::create() and the bloc data is saved.
 *
 * Edit mode  (pageId >= 0):
 *   The type combo is disabled (type cannot change after creation).
 *   The scroll area shows the type's widgets pre-populated with the stored
 *   bloc data.  On accept, the permalink may be updated (triggers
 *   IPageRepository::updatePermalink() if changed) and the data is saved.
 *
 * Bloc editor widgets are obtained via AbstractPageBloc::createEditWidget()
 * and parented to the scroll area's contents widget.  AbstractPageType is
 * reconstructed from the registry (createForTypeId) and owned by this dialog.
 *
 * The dialog holds non-owning references to IPageRepository and CategoryTable.
 */
class PageEditorDialog : public QDialog
{
    Q_OBJECT

public:
    // editingLangCode: BCP-47 code from WebsiteSettingsTable — used only in
    // create mode (pageId == -1) to stamp the new page's lang column.
    explicit PageEditorDialog(IPageRepository &repo,
                              CategoryTable   &categoryTable,
                              int              pageId,
                              const QString   &editingLangCode,
                              QWidget         *parent = nullptr);
    ~PageEditorDialog() override;

private slots:
    void _onTypeChanged(int index);
    void _onAccepted();

private:
    void _loadBlocs(const QString &typeId);
    void _clearBlocs();

    Ui::PageEditorDialog *ui;
    IPageRepository      &m_repo;
    CategoryTable        &m_categoryTable;
    int                   m_pageId;          // -1 = create mode
    QString               m_editingLangCode; // used in create mode only

    std::unique_ptr<AbstractPageType>   m_pageType;
    QList<AbstractPageBlockWidget *>    m_blocWidgets;
};

#endif // PAGEEDITORDIALOG_H
