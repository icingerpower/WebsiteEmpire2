#ifndef PAGETYPEARTICLE_H
#define PAGETYPEARTICLE_H

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocAutoLink.h"
#include "website/pages/blocs/PageBlocCategoryLinks.h"
#include "website/pages/blocs/PageBlocMeta.h"
#include "website/pages/blocs/PageBlocSocial.h"
#include "website/pages/blocs/PageBlocSocialMedia.h"
#include "website/pages/blocs/PageBlocText.h"

#include <QScopedPointer>

class CategoryTable;
class PageBlocCategory;

/**
 * A page type composed of seven blocs (in order):
 *   0 — PageBlocCategory      : primary breadcrumb category
 *   1 — PageBlocText           : main article body
 *   2 — PageBlocSocial         : social-media text metadata (title + desc, first pass)
 *   3 — PageBlocAutoLink       : keywords that auto-link to this page
 *   4 — PageBlocCategoryLinks  : cross-reference category links (body parts, etc.)
 *   5 — PageBlocSocialMedia    : social-media image variants (second pass, opt-in)
 *   6 — PageBlocMeta           : SEO title + meta description (translatable)
 *
 * Registered in the AbstractPageType registry under TYPE_ID = "article".
 *
 * The category bloc is first so getAttributes() returns the page's selected
 * categories before any text-bloc attributes.  PageBlocMeta is last so it can
 * be appended without re-indexing the existing 0–5 keys in stored page data.
 *
 * PageBlocCategory is a QObject and is therefore heap-allocated; the
 * QScopedPointer owns it.  The destructor is declared here and defined in the
 * .cpp so that QScopedPointer can see the full PageBlocCategory type at the
 * point of deletion without pulling it into this header.
 *
 * Call setPageUrl() whenever the parent page's URL is known or changes so
 * that PageBlocAutoLink can write the correct key into LinksManager on save.
 *
 * Call setGenerationContext() (AbstractPageType) before addCode() so that
 * buildHeadMetaTags() can emit correct canonical, og:url and hreflang tags.
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

    /** Returns the social text bloc (first-pass titles/descs) for the page generator. */
    const PageBlocSocial &socialTextBloc() const;

    /** Returns the social image bloc (second-pass WebP variants) for the page generator. */
    const PageBlocSocialMedia &socialBloc() const;

    /** Returns the auto-link bloc for direct access by the page generator. */
    const PageBlocAutoLink &autoLinkBloc() const;

    /** Returns the SEO meta bloc for direct access by tests and the page generator. */
    const PageBlocMeta &metaBloc() const;

    /**
     * Emits into the page <head>:
     *   <title>, <meta name="description">  — from PageBlocMeta (translated)
     *   <link rel="canonical">              — baseUrl + m_permalink
     *   og:url, og:type, og:locale         — derived automatically
     *   <link rel="alternate" hreflang>     — source + each target lang
     *   per-platform social <meta> tags     — from PageBlocSocial + AbstractSocialMedia
     *
     * Requires setGenerationContext() to have been called for canonical /
     * hreflang output; tags that depend on missing data are silently omitted.
     */
    QString buildHeadMetaTags(const QString &baseUrl,
                               const QString &langCode) const override;

    /**
     * Returns true: PageTypeArticle always requires an SVG image in the first
     * pass.  LauncherGeneration will not mark the page Complete if SVG
     * generation failed — it stays ContentReady for retry on the next run.
     */
    bool hasSvg() const override;

private:
    QScopedPointer<PageBlocCategory> m_categoryBloc;
    PageBlocText                     m_textBloc;
    PageBlocSocial                   m_socialTextBloc;
    PageBlocAutoLink                 m_autoLinkBloc;
    PageBlocCategoryLinks            m_categoryLinksBloc;
    PageBlocSocialMedia              m_socialBloc;
    PageBlocMeta                     m_metaBloc;
    QList<const AbstractPageBloc *>  m_blocs;
};

#endif // PAGETYPEARTICLE_H
