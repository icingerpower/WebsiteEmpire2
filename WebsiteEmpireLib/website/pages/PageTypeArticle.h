#ifndef PAGETYPEARTICLE_H
#define PAGETYPEARTICLE_H

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocAutoLink.h"
#include "website/pages/blocs/PageBlocSocial.h"
#include "website/pages/blocs/PageBlocText.h"

#include <QScopedPointer>

class CategoryTable;
class PageBlocCategory;

/**
 * A page type composed of four blocs (in order):
 *   0 — PageBlocCategory   : category selection
 *   1 — PageBlocText        : main article body
 *   2 — PageBlocSocial      : Open Graph / social-media meta tags
 *   3 — PageBlocAutoLink    : keywords that auto-link to this page
 *
 * Registered in the AbstractPageType registry under TYPE_ID = "article".
 *
 * The category bloc is first so getAttributes() returns the page's selected
 * categories before any text-bloc attributes.
 *
 * PageBlocCategory is a QObject and is therefore heap-allocated; the
 * QScopedPointer owns it.  The destructor is declared here and defined in the
 * .cpp so that QScopedPointer can see the full PageBlocCategory type at the
 * point of deletion without pulling it into this header.
 *
 * Call setPageUrl() whenever the parent page's URL is known or changes so
 * that PageBlocAutoLink can write the correct key into LinksManager on save.
 */
class PageTypeArticle : public AbstractPageType
{
public:
    static constexpr const char *TYPE_ID      = "article";
    static constexpr const char *DISPLAY_NAME = "Article";

    explicit PageTypeArticle(CategoryTable &categoryTable);
    ~PageTypeArticle() override;

    QString getTypeId()      const override;
    QString getDisplayName() const override;

    const QList<const AbstractPageBloc *> &getPageBlocs() const override;

    /**
     * Sets the canonical URL of the article page so PageBlocAutoLink can
     * register its keywords under the correct key in LinksManager.
     * Must be called before the first save() for a given page record.
     */
    void setPageUrl(const QString &url);

    /** Returns the social-media bloc for direct access by the page generator. */
    const PageBlocSocial &socialBloc() const { return m_socialBloc; }

    /** Returns the auto-link bloc for direct access by the page generator. */
    const PageBlocAutoLink &autoLinkBloc() const { return m_autoLinkBloc; }

private:
    QScopedPointer<PageBlocCategory> m_categoryBloc;
    PageBlocText                     m_textBloc;
    PageBlocSocial                   m_socialBloc;
    PageBlocAutoLink                 m_autoLinkBloc;
    QList<const AbstractPageBloc *>  m_blocs;
};

#endif // PAGETYPEARTICLE_H
