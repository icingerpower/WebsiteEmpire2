#ifndef PAGEEDITORDIALOG_H
#define PAGEEDITORDIALOG_H

#include <QDialog>
#include <QList>
#include <QStringList>
#include <memory>

class AbstractPageBlockWidget;
class AbstractPageType;
class CategoryTable;
class ImageWriter;
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
 *   The type combo is disabled.  The scroll area shows the type's widgets
 *   pre-populated with stored data.  On accept, the permalink may be updated
 *   and the data is saved.
 *
 * Translation lock:
 *   If the page is a translation (sourcePageId > 0) and has not yet been
 *   AI-translated (translatedAt is empty), the scroll area and OK button are
 *   disabled and a warning label is shown.  Once translatedAt is set by
 *   PageTranslator, the dialog opens fully editable.
 *   A staleness warning is shown when the source page was updated after the
 *   last AI translation pass, but editing is still allowed.
 *
 * Permalink history:
 *   A "History…" tool button next to the permalink field opens
 *   DialogPermalinkHistory to manage the redirect type (301/302/410) for
 *   each old permalink recorded by updatePermalink().
 *
 * editingLangCode is used only in create mode (pageId == -1) to stamp the
 * new page's lang column.
 *
 * The dialog holds non-owning references to IPageRepository and CategoryTable.
 */
class PageEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PageEditorDialog(IPageRepository &repo,
                              CategoryTable   &categoryTable,
                              int              pageId,
                              const QString   &editingLangCode,
                              QWidget         *parent = nullptr);
    ~PageEditorDialog() override;

    /**
     * Supplies image upload context to bloc widgets that support it.
     * Must be called before exec() — widgets are created in the constructor.
     * imageWriter must remain valid for the lifetime of this dialog.
     * No-op when imageWriter is nullptr or domains is empty.
     */
    void setImageContext(ImageWriter *imageWriter, const QStringList &domains);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void _onTypeChanged(int index);
    void _onAccepted();
    void _onHistoryClicked();

private:
    void _loadBlocs(const QString &typeId);
    void _clearBlocs();

    Ui::PageEditorDialog *ui;
    IPageRepository      &m_repo;
    CategoryTable        &m_categoryTable;
    int                   m_pageId;          // -1 = create mode
    QString               m_editingLangCode; // used in create mode only
    bool                  m_locked = false;  // translation not yet done

    ImageWriter *m_imageWriter = nullptr;
    QStringList  m_imageDomains;

    std::unique_ptr<AbstractPageType>   m_pageType;
    QList<AbstractPageBlockWidget *>    m_blocWidgets;
};

#endif // PAGEEDITORDIALOG_H
