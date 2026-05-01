#ifndef DIALOGPREVIEWPAGE_H
#define DIALOGPREVIEWPAGE_H

#include <QDialog>
#include <QDir>
#include <QList>
#include <QString>

class AbstractEngine;
class CategoryTable;
class IPageRepository;

namespace Ui { class DialogPreviewPage; }

/**
 * Read-only preview dialog for a page and all its language versions.
 *
 * Left panel (QListWidget, 150 px default):
 *   Shows the source language plus every language for which a translation
 *   exists in the page data (inline system: keys like "0_tr:fr:text") or as
 *   a legacy separate translation page record.
 *
 * Right panel (QTextBrowser):
 *   Displays the generated HTML produced by AbstractPageType::addCode().
 *   The engine row (websiteIndex) is resolved per language so the correct
 *   translation text is selected during rendering.
 *
 * Language discovery:
 *   - Source language: always the first entry.
 *   - Inline translations: detected by scanning the source page's data map for
 *     keys matching "_tr:<lang>:" and adding each unique lang found.
 *   - Legacy translation page records (sourcePageId != 0): added for
 *     backward compatibility with the pre-inline-translation scheme.
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
    struct PreviewEntry {
        int     pageIdToLoad; ///< legacy: translation page id; inline: source page id
        QString lang;         ///< language code to render
    };

    void _renderPage(const PreviewEntry &entry);
    // Replaces <img src="/file.svg"> tags in html with PNG data URIs rendered
    // from the SVG blobs in images.db.  No-op if images.db does not exist.
    void _inlineSvgs(QString &html);

    // Returns the engine row index whose lang code matches lang, or 0 as fallback.
    int _engineIndexForLang(const QString &lang) const;

    Ui::DialogPreviewPage *ui;
    IPageRepository       &m_repo;
    CategoryTable         &m_categoryTable;
    AbstractEngine        &m_engine;
    QDir                   m_workingDir;

    QList<PreviewEntry> m_entries; // parallel to listLanguages rows
};

#endif // DIALOGPREVIEWPAGE_H
