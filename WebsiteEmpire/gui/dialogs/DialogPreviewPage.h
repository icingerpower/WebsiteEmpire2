#ifndef DIALOGPREVIEWPAGE_H
#define DIALOGPREVIEWPAGE_H

#include <QDialog>

class AbstractEngine;
class CategoryTable;
class IPageRepository;

namespace Ui { class DialogPreviewPage; }

/**
 * Read-only preview dialog for a single page.
 *
 * On construction the page type is reconstructed from the registry, bloc data is
 * loaded via IPageRepository, and addCode() is called to produce HTML which is
 * displayed in a QTextBrowser.  No database is written.
 *
 * websiteIndex is always 0 (first engine variation) for preview purposes.
 *
 * The dialog holds non-owning references to IPageRepository, CategoryTable, and
 * AbstractEngine; all three must outlive this dialog.
 */
class DialogPreviewPage : public QDialog
{
    Q_OBJECT

public:
    explicit DialogPreviewPage(IPageRepository &repo,
                               CategoryTable   &categoryTable,
                               AbstractEngine  &engine,
                               int              pageId,
                               QWidget         *parent = nullptr);
    ~DialogPreviewPage() override;

private:
    Ui::DialogPreviewPage *ui;
};

#endif // DIALOGPREVIEWPAGE_H
