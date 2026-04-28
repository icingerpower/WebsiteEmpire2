#ifndef DIALOGPREVIEWPAGE_H
#define DIALOGPREVIEWPAGE_H

#include <QDialog>
#include <QDir>
#include <QList>

class AbstractEngine;
class CategoryTable;
class IPageRepository;

namespace Ui { class DialogPreviewPage; }

/**
 * Read-only preview dialog for a page and all its language versions.
 *
 * Left panel (QListWidget, 150 px default):
 *   Shows language codes for the source page and every AI-translated variant.
 *   Selecting a language re-renders the preview in the QTextBrowser.
 *
 * Right panel (QTextBrowser):
 *   Displays the generated HTML produced by AbstractPageType::addCode().
 *   websiteIndex is always 0 (first engine variation) for preview purposes.
 *
 * Language discovery:
 *   The "root" page is determined from the passed pageId:
 *   - If sourcePageId == 0: this is the root; find its translations.
 *   - If sourcePageId > 0: use sourcePageId as the root; find root + siblings.
 *
 * The dialog holds non-owning references; all three must outlive the dialog.
 */
class DialogPreviewPage : public QDialog
{
    Q_OBJECT

public:
    explicit DialogPreviewPage(IPageRepository &repo,
                               CategoryTable   &categoryTable,
                               AbstractEngine  &engine,
                               const QDir      &workingDir,
                               int              pageId,
                               QWidget         *parent = nullptr);
    ~DialogPreviewPage() override;

private slots:
    void _onLanguageSelected(int row);

private:
    void _renderPage(int pageId);
    // Replaces <img src="/file.svg"> tags in html with PNG data URIs rendered
    // from the SVG blobs in images.db.  No-op if images.db does not exist.
    void _inlineSvgs(QString &html);

    Ui::DialogPreviewPage *ui;
    IPageRepository       &m_repo;
    CategoryTable         &m_categoryTable;
    AbstractEngine        &m_engine;
    QDir                   m_workingDir;

    QList<int> m_pageIds; // parallel to listLanguages rows
};

#endif // DIALOGPREVIEWPAGE_H
