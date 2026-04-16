#ifndef PAGEBLOC_AUTOLINK_H
#define PAGEBLOC_AUTOLINK_H

#include "website/pages/blocs/AbstractPageBloc.h"

#include <QStringList>

/**
 * Stores a list of keywords for automatic in-text linking across the site.
 *
 * When the page generator encounters any of these keywords while rendering
 * another article's text, it wraps the keyword in an anchor tag that links
 * to this page.
 *
 * On every save() the keyword list is pushed into LinksManager::instance()
 * so the central link registry stays up-to-date without a full site scan.
 *
 * The page URL is persisted under KEY_PAGE_URL and loaded/saved with the
 * rest of the bloc data.  It must be initialised before the first save via
 * setPageUrl() — typically called by PageTypeArticle when the page record's
 * URL is known or changes.
 *
 * addCode() is a no-op: auto-link substitution is performed by the page
 * generator across all pages; this bloc contributes no direct body HTML.
 *
 * Key naming:
 *   KEY_PAGE_URL  — canonical URL of the page these keywords link to
 *   KEY_KW_COUNT  — number of keywords stored
 *   "kw_0", "kw_1", … — individual keywords (indices are stable)
 */
class PageBlocAutoLink : public AbstractPageBloc
{
public:
    static constexpr const char *KEY_PAGE_URL = "page_url";
    static constexpr const char *KEY_KW_COUNT = "kw_count";

    PageBlocAutoLink() = default;
    ~PageBlocAutoLink() override = default;

    /**
     * Sets the page URL these keywords link to.
     * Must be called before the first save() whenever the parent page's URL
     * is known (e.g. after PageTypeArticle is created for an existing record,
     * or after the user changes a page's URL in the editor).
     */
    void setPageUrl(const QString &url);

    /** Returns the currently stored page URL. */
    QString pageUrl() const;

    /** Returns the keyword list. */
    const QStringList &keywords() const;

    QString getName() const override;
    void load(const QHash<QString, QString> &values) override;

    /**
     * Writes all keys to values, then calls
     * LinksManager::instance().setKeywords() to keep the central registry
     * in sync.
     */
    void save(QHash<QString, QString> &values) const override;

    /** No body HTML output — auto-linking is handled by the page generator. */
    void addCode(QStringView origContent, AbstractEngine &engine, int websiteIndex,
                 QString &html, QString &css, QString &js,
                 QSet<QString> &cssDoneIds, QSet<QString> &jsDoneIds) const override;

    AbstractPageBlockWidget *createEditWidget() override;

private:
    QString     m_pageUrl;
    QStringList m_keywords;
};

#endif // PAGEBLOC_AUTOLINK_H
